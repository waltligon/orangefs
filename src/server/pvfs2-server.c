/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *
 * July 2002
 * 
 * The server is now configured to answer requests
 * Operations are set forth from state_machine
 * and check_dependancies.
 * There should be little modification to this code
 * from now on!
 * 
 * May 2002
 * 
 * Modularized Code.  
 * Removed Status Queue. (see job_check_consistency)
 * Added Signal Handlers
 * 
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <signal.h>
#include <rpc/rpc.h>

#include <bmi.h>
#include <gossip.h>
#include <job.h>
#include <trove.h>
#include <pvfs2-debug.h>
#include <pvfs2-storage.h>
#include <PINT-reqproto-encode.h>
#include <request-scheduler.h>

#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>

/* Internal Globals */

/* For the switch statement to know what interfaces to shutdown */
static PINT_server_status_code server_level_init;

/* All parameters read in from the configuration file */
static struct server_configuration_s *user_opts;

/* A flag to stop the main loop from processing and handle the signal 
	after all threads complete and are no longer blocking */
static int signal_recvd_flag;

extern PINT_server_trove_keys_s Trove_Common_Keys[];

/* Prototypes */
static int initialize_interfaces(PINT_server_status_code *server_level_init);
static int initialize_signal_handlers();
static int initialize_server_state(PINT_server_status_code *server_level_init,
                                   PINT_server_op *s_op, job_status_s
				   *job_status_structs[]);
static int server_init(void);
static int server_shutdown(PINT_server_status_code level,
			   int ret,
			   int sig);
static void *sig_handler(int sig);
int PINT_server_cp_bmi_unexp(PINT_server_op * s_op,
			     job_status_s * ret);
void PINT_server_get_bmi_unexp_err(int ret);


/*
  Initializes the bmi, flow, trove, and job interfaces.
  returns < 0 on error and sets the passed in server status code
  to the appropriate value for proper server shutdown.
*/
static int initialize_interfaces(PINT_server_status_code *server_level_init)
{
    int ret = 0, i = 0;
    char *method_name = NULL;

    /* initialize BMI Interface (bmi.c) */
    ret = BMI_initialize("bmi_tcp", user_opts->host_id, BMI_INIT_SERVER);
    if (ret < 0)
    {
	gossip_err("BMI_initialize Failed: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_GOSSIP_INTERFACE;
	goto interface_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "BMI Init Complete\n");

    /* initialize the flow interface */
    ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
    if (ret < 0)
    {
	gossip_err("Flow_initialize Failed: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_BMI_INTERFACE;
	goto interface_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "Flow Init Complete\n");

    /* initialize Trove Interface */
    ret = trove_initialize(user_opts->storage_path, 0, &method_name, 0);
    if (ret < 0)
    {
	gossip_err("Trove Init Failed: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_FLOW_INTERFACE;
	goto interface_init_failed;
    }

    /* Uses filesystems in config file. */
    for (i = 0; i < user_opts->number_filesystems; i++)
    {
       ret=trove_collection_lookup(user_opts->file_systems[i]->file_system_name,
			      &(user_opts->file_systems[i]->coll_id),NULL,NULL);
	if (ret < 0)
	{
	    gossip_lerr("Error initializing filesystem %s\n",
                        user_opts->file_systems[i]->file_system_name);
	    goto interface_init_failed;
	}
    }
    gossip_debug(SERVER_DEBUG, "Storage Init Complete\n");
    gossip_debug(SERVER_DEBUG, "%d filesystems initialized\n", i);

    /* initialize Job Interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
	gossip_err("Error initializing job interface: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_STORAGE_INTERFACE;
	goto interface_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "Job Init Complete\n");
    
  interface_init_failed:
    return ret;
}


/* Registers signal handlers. returns < 0 on error. */
static int initialize_signal_handlers()
{
    int ret = 1;

    /* Register Signals */
    signal(SIGHUP, (void *) sig_handler);
    signal(SIGILL, (void *) sig_handler);
#if 0
/* #ifdef USE_SIGACTION */
    act.sa_handler = (void *) sig_handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    if (sigaction(SIGHUP, &act, NULL) < 0)
    {
	gossip_err("Error Registering Signal SIGHUP.\nProgram Terminating.\n");
	goto sh_init_failed;
    }
    if (sigaction(SIGSEGV, &act, NULL) < 0)
    {
	gossip_err("Error Registering Signal SIGSEGV.\nProgram Terminating.\n");
	goto sh_init_failed;
    }
    if (sigaction(SIGPIPE, &act, NULL) < 0)
    {
	gossip_err("Error Registering Signal SIGPIPE.\nProgram Terminating.\n");
	goto sh_init_failed;
    }
/* #else */
    signal(SIGSEGV, (void *) sig_handler);
    signal(SIGPIPE, (void *) sig_handler);
#endif
    ret = 0;

  sh_init_failed:
    return ret;
}


/*
  Initializes the server interfaces, state machine, req scheduler,
  posts the bmi unexpected buffers, and registers signal handlers.
  returns < 0 on error and sets the passed in server status code
  to the appropriate value for proper server shutdown.
*/
static int initialize_server_state(PINT_server_status_code *server_level_init,
                                   PINT_server_op *s_op,
				   job_status_s *job_status_structs[])
{
    int ret = 0, i = 0;

    /* initialize the bmi, flow, trove and job interfaces */
    ret = initialize_interfaces(server_level_init);
    if (ret < 0)
    {
	gossip_err("Error: Could not initialize server interfaces; aborting.\n");
	goto state_init_failed;
    }

    /* initialize Server State Machine */
    ret = PINT_state_machine_init();	/* state_machine.c:68 */
    if (ret < 0)
    {
	gossip_err("Error initializing state_machine interface: %s\n",
		strerror(-ret));
	*server_level_init = SHUTDOWN_HIGH_LEVEL_INTERFACE;
	goto state_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "State Machine Init Complete\n");

    /* initialize Server Request Scheduler */
    ret = PINT_req_sched_initialize();
    if (ret < 0)
    {
	gossip_err("Error initializing Request Scheduler interface: %s\n",
		strerror(-ret));
	*server_level_init = STATE_MACHINE_HALT;
	goto state_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "Request Scheduler Init Complete\n");

    /* Below, we initially post BMI unexpected msg buffers =) */
    for (i = 0; i < user_opts->initial_unexpected_requests; i++)
    {
        /* ARE THESE SUPPOSED TO BE UNINITIALIZED??? -N.M. */
	ret = PINT_server_cp_bmi_unexp(s_op, &((*job_status_structs)[0]));
	if (ret < 0)
	{
	    PINT_server_get_bmi_unexp_err(ret);
	    *server_level_init = CHECK_DEPS_QUEUE;
	    goto state_init_failed;
	}
    }	/* End of BMI Unexpected Requests rof */
    gossip_debug(SERVER_DEBUG, "All BMI_unexp Posted\n");

    *server_level_init = UNEXPECTED_BMI_FAILURE;

    ret = initialize_signal_handlers();

  state_init_failed:
    return ret;
}


int main(int argc,
	 char **argv)
{

    /* Used to check completion of interface initializations */
    int ret = 1;

    /* Inside for loop variable */
    int i = 0;

    /* Flag used to post new BMI Unexpected messages */
    int postBMIFlag = 0;

    /* Total number of jobs from job_wait_world */
    int out_count = 0;

    /* Used for user_pointers that come out of job_wait_world */
    PINT_server_op *s_op = NULL;

    /* Job ID Array used in job_wait_world */
    job_id_t job_id_array[MAX_JOBS];

    /* User Pointer Array used in job_wait_world */
    void *completed_job_pointers[MAX_JOBS];

    /* Status Structures used in job_wait_world */
    job_status_s job_status_structs[MAX_JOBS];

    /* Insert Temp Trove Stuff Here */

#ifdef DEBUG
    int Temp_Check_Out_Debug = 10;
    int Temp_Jobs_Complete_Debug = 0;
#endif

    /* Begin Main */

    /* Passed to server shutdown function */
    server_level_init = STATUS_UNKNOWN;

    /* When we get a signal, we are thread based, so we need to make
       sure that one, the parent has the signal, and two, none of the 
       threads are blocking on a semaphore.  Yet another cool error
       found and resolved.  dw
     */
    signal_recvd_flag = 0;

    /* Enable the gossip interface. */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, SERVER_DEBUG);

    /* Sanity Check 1. Make sure we are root */
    if (getuid() != 0 && geteuid() != 0)
    {
	gossip_err("WARNING: Server should be run as root\n");
    }

    /* 
     *      Read configuration options...
     * function located in server_config.c
     */
    user_opts = PINT_server_config(argc, argv);	/* server_config.c:53 */
    if (!user_opts)
    {
	gossip_err("Error: Could not read configuration; aborting.\n");
	goto server_shutdown;
    }

    /* perform initial steps to run as a server 
     * This function is located at the bottom of this file
     * TODO: Should it be here? dw
     */
    ret = server_init();	/* server_daemon.c:286 */
    if (ret < 0)
    {
	gossip_err("Error: Could not start server; aborting.\n");
	server_level_init = DEALLOC_INIT_MEMORY;
	goto server_shutdown;
    }

    ret = initialize_server_state(&server_level_init,s_op,&job_status_structs);
    if (ret < 0)
    {
	gossip_err("Error: Could not initialize server; aborting.\n");
	goto server_shutdown;
    }
    server_level_init = UNEXPECTED_POSTINIT_FAILURE;

    /* The do work forever loop. */
    while (1)
    {
	out_count = MAX_JOBS;
	if (signal_recvd_flag != 0)
	{
	    ret = signal_recvd_flag;
	    goto server_shutdown;
	}
	/* TODO: use a named default value for the timeout eventually */
	ret = job_testworld(job_id_array, &out_count,
		completed_job_pointers, job_status_structs, 100);
	if (ret < 0)
	{
	    gossip_lerr("FREAK OUT.\n");
	    exit(-1);
	}

	for (i = 0; i < out_count; i++)
	{
	    s_op = (PINT_server_op *) completed_job_pointers[i];
	    if (s_op->op == BMI_UNEXP)
	    {
		postBMIFlag = 1;
#ifdef DEBUG
		if (Temp_Jobs_Complete_Debug++ == Temp_Check_Out_Debug)
		{
		    ret = -1;
		    goto server_shutdown;
		}
#endif
		ret = PINT_state_machine_initialize_unexpected(s_op,
			&job_status_structs[i]);
            do
            {
                ret = PINT_state_machine_next(s_op, &job_status_structs[i]);
            } while (ret == 1);

	    if (ret < 0)
	    {
		gossip_lerr("Error on job %d, Return Code: %d\n", i, ret);
		server_level_init = UNEXPECTED_LOOP_END;
		ret = 1;
		goto server_shutdown;
		/* if ret < 0 oh no... job mechanism died */
		/* TODO: fix this */
	    }
	    if (postBMIFlag)
	    {
		postBMIFlag = 0;
		ret = PINT_server_cp_bmi_unexp(s_op, &job_status_structs[i]);
		if (ret < 0)
		{
		    /* TODO: do something here, the return value was
		     * not being checked for failure before.  I just
		     * put something here to make it exit for the
		     * moment.  -Phil
		     */
		    gossip_lerr("Error: NOT HANDLED.\n");
		    exit(-1);
		}
	    }
	}
    }
    server_level_init = UNEXPECTED_LOOP_END;

  server_shutdown:
    server_shutdown(server_level_init, ret, 0);
    return -1;	/* Should never get here */
}


/* server_init()
 *
 * performs initialization steps for server program
 *
 * returns 0 on success, -errno on failure
 */

static int server_init(void)
{
#ifdef DEBUG
    char logname[] = "/tmp/pvfs_server.log";
    int logfd;
#endif

    /* create a child process to continue execution */
#if 0
    ret = fork();
    if (ret < 0)
    {
	return (-errno);
    }
    if (ret > 0)
    {
	exit(0);	/* kill the parent */
    }

    setsid();	/* Become the parent */
    umask(0);
#endif
    chdir("/");	/* avoid busy filesystems */

    /* Now we need a log file...          
     * 
     * NOTE: Will not run if DEBUG is set 
     */
#ifdef DEBUG
    gossip_ldebug(SERVER_DEBUG, "Log Init\n");
    gossip_enable_file("/tmp/pvfsServer.log", "a");
#else
    gossip_debug(SERVER_DEBUG, "Logging was skipped b/c DEBUG Flag set\n"
                 "Output will be displayed onscreen\n");
#endif

    return (0);
}

/* For fatal errors, and SIG */

static int server_shutdown(PINT_server_status_code level,
			   int ret,
			   int siglevel)
{
    int i;
    switch (level)
    {
    case UNEXPECTED_LOOP_END:
    case UNEXPECTED_POSTINIT_FAILURE:
	/* TODO:  Should we do anything else here?
	 *        Shutting down here means we were running
	 *        perfectly  and failed somewhere inside the
	 *        while loop.*/
    case UNEXPECTED_BMI_FAILURE:
    case CHECK_DEPS_QUEUE:
    case STATE_MACHINE_HALT:
	/* State Machine */
	PINT_state_machine_halt();
    case SHUTDOWN_HIGH_LEVEL_INTERFACE:
	/* Turn off High Level Interface */
	job_finalize();
    case SHUTDOWN_STORAGE_INTERFACE:
	/* Turn off Storage IFace */
	trove_finalize();
	for(i=0;i<user_opts->number_filesystems;i++)
	{
	    free(user_opts->file_systems[i]);
	    if (user_opts->file_systems[i]->file_system_name)
		free(user_opts->file_systems[i]->file_system_name);
	    if (user_opts->file_systems[i]->meta_server_list)
		free(user_opts->file_systems[i]->meta_server_list);
	    if (user_opts->file_systems[i]->io_server_list)
		free(user_opts->file_systems[i]->io_server_list);
	}
	if(user_opts->host_id)
	    free(user_opts->host_id);
	if(user_opts->storage_path)
	    free(user_opts->storage_path);
	if(user_opts->default_meta_server_list)
	    free(user_opts->default_meta_server_list);
	if(user_opts->default_io_server_list)
	    free(user_opts->default_io_server_list);
	if(user_opts->file_system_names)
	    free(user_opts->file_system_names);
	free(user_opts);
    case SHUTDOWN_FLOW_INTERFACE:
	/* Turn off Flows */
	PINT_flow_finalize();
    case SHUTDOWN_BMI_INTERFACE:
	/* Turn off BMI */
	BMI_finalize();
    case SHUTDOWN_GOSSIP_INTERFACE:
	gossip_disable();
    case DEALLOC_INIT_MEMORY:
	/* Unalloc any memory we have */
	//free(user_opts->host_id);
	//free(user_opts->tcp_path_bmi_library);
	//free(user_opts);
    case STATUS_UNKNOWN:
    default:
	if (siglevel == 0)
	{
	    exit(-ret);
	}
	else
	    exit(0);
    }
    exit(-1);

}

/* TODO: Fix signal handler */
/* Problem is how to handle user_opts and server_init_level */
/* Update!!! 31/01/2002  I made both static... Try that out for now */

/* Signal Handling */

static void *sig_handler(int sig)
{
    gossip_debug(SERVER_DEBUG, "Got Signal %d... Level...%d\n",
	    sig, (int)server_level_init);
    signal_recvd_flag = sig;
#if 0
    if (sig == SIGSEGV)
    {
	exit(-1);
    }
    else if (sig == 1)
    {
	exit(server_shutdown(server_level_init, 0, 0));
	/* if we get a restart, what to do now? */
    }
#ifndef USE_SIGACTION
    signal(sig, (void *) sig_handler);
#endif
    if (sig == SIGPIPE)
    {
	return (0);
    }
    exit(-1);
#endif
    return NULL;

}

struct server_configuration_s *get_server_config_struct(void)
{
    return user_opts;
}

int PINT_server_cp_bmi_unexp(PINT_server_op * serv_op,
			     job_status_s * temp_stat)
{
    job_id_t jid;
    int ret;
    char *mem_calc_ptr;

    serv_op = (PINT_server_op *) malloc(sizeof(PINT_server_op)+sizeof(void *));
    if (!serv_op)
    {
	return (-1);
    }

    serv_op->op = BMI_UNEXP;


    mem_calc_ptr = (char *) serv_op;
    serv_op->encoded.buffer_list = (void *)mem_calc_ptr+sizeof(PINT_server_op);

    serv_op->unexp_bmi_buff = (struct BMI_unexpected_info *)
	    malloc(sizeof(struct BMI_unexpected_info));
    if (!serv_op->unexp_bmi_buff)
    {
	return (-2);
    }

    /* TODO:
     * Consider optimizations later, so that we don't have to
     * disable immediate completion.  See the mailing list thread
     * started here:
     *
     * http://www.beowulf-underground.org/pipermail/pvfs2-internal/2003-February/000305.html
     * 
     * At the moment, the server cannot handle immediate completion
     * in this part of the code.
     * -Phil
     */
    ret = job_bmi_unexp(serv_op->unexp_bmi_buff, serv_op,
	    temp_stat, &jid, JOB_NO_IMMED_COMPLETE);
    if (ret < 0)
    {
	return (-4);
    }

    return (0);
}

void PINT_server_get_bmi_unexp_err(int ret)
{
    switch (ret)
    {
    case -1:
	gossip_err("Error Initializing Initial Server Struct\n");
	break;
    case -2:
	/* We are out of memory... call shutdown and error */
	gossip_err("Error Initializing Unexpected BMI Struct\n");
	break;
    case -3:
	gossip_err("Error Initializing Job Struct\n");
	break;
    case -4:
	gossip_err("Error Posting Job\n");
	break;
    }

}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/* End pvfs_server.c */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <signal.h>
#include <rpc/rpc.h>
#include <assert.h>
#include <getopt.h>

#include "bmi.h"
#include "gossip.h"
#include "job.h"
#include "trove.h"
#include "pvfs2-debug.h"
#include "pvfs2-storage.h"
#include "PINT-reqproto-encode.h"
#include "request-scheduler.h"

#include "pvfs2-server.h"
#include "state-machine.h"
#include "mkspace.h"
#include "server-config.h"
#include "quicklist.h"

/* Internal Globals */
job_context_id PINT_server_job_context = -1;

static int server_create_storage_space = 0;

/* For the switch statement to know what interfaces to shutdown */
static PINT_server_status_code server_level_init;

/* All parameters read in from the configuration file */
static struct server_configuration_s user_opts;

/* A flag to stop the main loop from processing and handle the signal 
   after all threads complete and are no longer blocking */
static int signal_recvd_flag;

extern PINT_server_trove_keys_s Trove_Common_Keys[];

/* Prototypes */
static int initialize_interfaces(
    PINT_server_status_code *server_level_init);

static int initialize_signal_handlers(void);

static int initialize_server_state(
    PINT_server_status_code *server_level_init,
    PINT_server_op *s_op, 
    job_status_s *job_status_structs);

static int server_init(void);

static int server_shutdown(
    PINT_server_status_code level,
    int ret,
    int sig);

static void *sig_handler(int sig);

static int initialize_new_server_op(job_status_s * ret);
static int server_parse_cmd_line_args(int argc, char **argv);


/*
  Initializes the bmi, flow, trove, and job interfaces.
  returns < 0 on error and sets the passed in server status code
  to the appropriate value for proper server shutdown.
*/
static int initialize_interfaces(PINT_server_status_code *server_level_init)
{
    int ret = 0;
    char *method_name = NULL;
    char *cur_handle_range = NULL;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs;

    /* initialize BMI Interface (bmi.c) */
    printf("Passing in %s\n",user_opts.host_id);
    ret = BMI_initialize("bmi_tcp", user_opts.host_id, BMI_INIT_SERVER);
    if (ret < 0)
    {
	gossip_err("BMI_initialize Failed: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_GOSSIP_INTERFACE;
	goto interface_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "BMI Init Complete\n");

    /* initialize the flow interface */
    ret =
	PINT_flow_initialize("flowproto_bmi_trove,flowproto_dump_offsets", 0);
    if (ret < 0)
    {
	gossip_err("Flow_initialize Failed: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_BMI_INTERFACE;
	goto interface_init_failed;
    }
    gossip_debug(SERVER_DEBUG, "Flow Init Complete\n");

    /* initialize Trove Interface */
    ret = trove_initialize(user_opts.storage_path, 0, &method_name, 0);
    if (ret < 0)
    {
	gossip_err("Trove Init Failed: %s\n", strerror(-ret));

        if (ret == -1)
        {
            gossip_err("\n*****************************\n");
            gossip_err("Invalid Storage Space: %s\n\n",
		       user_opts.storage_path);
            gossip_err("Storage initialization failed.  The most "
		       "common reason\nfor this is that the storage space "
		       "has not yet been\ncreated or is located on a "
		       "partition that has not yet\nbeen mounted.  "
		       "If you'd like to create the storage space,\n"
		       "re-run this program with a -f option.\n");
            gossip_err("\n*****************************\n");
        }
	*server_level_init = SHUTDOWN_FLOW_INTERFACE;
	goto interface_init_failed;
    }

    /* Uses filesystems in config file. */
    cur = user_opts.file_systems;
    while(cur)
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        ret = trove_collection_lookup(cur_fs->file_system_name,
                                      &(cur_fs->coll_id),NULL,NULL);
	if (ret < 0)
	{
	    gossip_lerr("Error initializing filesystem %s\n",
                        cur_fs->file_system_name);
	    goto interface_init_failed;
	}
        cur_handle_range =
            PINT_server_config_get_handle_range_str(&user_opts,cur_fs);
        if (!cur_handle_range)
        {
	    gossip_lerr("Error: Invalid handle range for host %s "
                        "(alias %s) specified in file system %s\n",
                        user_opts.host_id,
                        PINT_server_config_get_host_alias_ptr(
                            &user_opts,user_opts.host_id),
                        cur_fs->file_system_name);
	    goto interface_init_failed;
        }

        ret = trove_collection_setinfo(
            cur_fs->coll_id,TROVE_COLLECTION_HANDLE_RANGES,
            (void *)cur_handle_range);
        if (ret < 0)
        {
	    gossip_lerr("Error adding handle range %s to filesystem %s\n",
                        cur_handle_range,cur_fs->file_system_name);
	    goto interface_init_failed;
        }
        cur = llist_next(cur);
    }
    gossip_debug(SERVER_DEBUG, "Storage Init Complete\n");
    gossip_debug(SERVER_DEBUG, "%d filesystems initialized\n",
                 llist_count(user_opts.file_systems));

    /* initialize Job Interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
	gossip_err("Error initializing job interface: %s\n", strerror(-ret));
	*server_level_init = SHUTDOWN_STORAGE_INTERFACE;
	goto interface_init_failed;
    }
    
    ret = job_open_context(&PINT_server_job_context);
    if(ret < 0)
    {
	gossip_err("Error opening job context.\n");
	*server_level_init = SHUTDOWN_JOB_INTERFACE;
	goto interface_init_failed;
    }

    gossip_debug(SERVER_DEBUG, "Job Init Complete\n");

  interface_init_failed:
    return ret;
}


/* Registers signal handlers. returns < 0 on error. */
static int initialize_signal_handlers(void)
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

  sh_init_failed:
#endif
    ret = 0;
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
				   job_status_s *job_status_structs)
{
    int ret = 0, i = 0;

    /* initialize the bmi, flow, trove and job interfaces */
    ret = initialize_interfaces(server_level_init);
    if (ret < 0)
    {
	gossip_err("Error: Could not initialize server interfaces; "
                   "aborting.\n");
	goto state_init_failed;
    }

    /* initialize Server State Machine */
    ret = PINT_state_machine_init();
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
    for (i = 0; i < user_opts.initial_unexpected_requests; i++)
    {
	ret = initialize_new_server_op(&job_status_structs[i]);
	if (ret < 0)
	{
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


int main(int argc, char **argv)
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

#ifdef DEBUG
    int Temp_Check_Out_Debug = 10;
    int Temp_Jobs_Complete_Debug = 0;
#endif

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
    if (server_parse_cmd_line_args(argc, argv) != 0) {
	goto server_shutdown;
    }
    if (PINT_server_config(&user_opts, argv[optind], argv[optind + 1]))
    {
	gossip_err("Error: Could not read configuration; aborting.\n");
	goto server_shutdown;
    }

    /* make sure the configuration file is valid */
    if (!PINT_server_config_is_valid_configuration(&user_opts))
    {
	gossip_err("Error: Invalid configuration; aborting.\n");
	goto server_shutdown;
    }

    /* check if we need to create a storage space and exit */
    if (server_create_storage_space)
    {
        return PINT_server_config_pvfs2_mkspace(&user_opts);
    }

    /* perform initial steps to run as a server  */
    ret = server_init();	
    if (ret < 0)
    {
	gossip_err("Error: Could not start server; aborting.\n");
	server_level_init = DEALLOC_INIT_MEMORY;
	goto server_shutdown;
    }

    if (MAX_JOBS < user_opts.initial_unexpected_requests)
    {
	gossip_err("Error: initial_unexpected request value is too "
                   "large; aborting.\n");
	server_level_init = DEALLOC_INIT_MEMORY;
	goto server_shutdown;
    }

    ret = initialize_server_state(&server_level_init,s_op, job_status_structs);
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
	ret = job_testcontext(job_id_array, &out_count,
		completed_job_pointers, job_status_structs, 100,
		PINT_server_job_context);
	if (ret < 0)
	{
	    gossip_lerr("FREAK OUT.\n");
	    exit(-1);
	}

	for (i = 0; i < out_count; i++)
	{
	    s_op = (PINT_server_op *) completed_job_pointers[i];

	    /* 
	       There are two possibilities here.
	       1.  The operation is a new request (Unexpected).  We will
	           fall into the if() loop.

	       2.  The operation was previously posted to the job
	           interface.  We need to continue operation on this.
		   We do not need to perform the additional unexpected 
		   message overhead.
	     */
	    ret = 1;

	    /* Case 1.  Unexpected message overhead.*/
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
	    }
	    
	    /* 
	       Right here, both case 1 and case 2 merge.  
	       
	       If the operation was originally case 1, then ret reflects 
	       the completion of the first state for the respective operation.
	       NOTE: that ret can now be <= 0.

	       Otherwise, ret has not been altered from the original 
	       assignment of 1.  Therefore will will enter this loop and perform
	       work on this request.
	    */
	       
            while (ret == 1)
            {
                ret = PINT_state_machine_next(s_op, &job_status_structs[i]);
            }

	    if (ret < 0)
	    {
		gossip_lerr("Error on job %d, Return Code: %d\n", i, ret);
		server_level_init = UNEXPECTED_LOOP_END;
		ret = 1;
		goto server_shutdown;
		/* if ret < 0 oh no... job mechanism died */
		/* TODO: fix this */
	    }

	    if (postBMIFlag) /* unexpected message */
	    {
		postBMIFlag = 0;
		ret = initialize_new_server_op(&job_status_structs[i]);
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
	} /* ... for i < out_count */
    } /* ... while (1) */
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
	job_close_context(PINT_server_job_context);
    case SHUTDOWN_JOB_INTERFACE:
	job_finalize();
    case SHUTDOWN_STORAGE_INTERFACE:
	/* Turn off Storage IFace */
	trove_finalize();
    case SHUTDOWN_FLOW_INTERFACE:
	/* Turn off Flows */
	PINT_flow_finalize();
    case SHUTDOWN_BMI_INTERFACE:
	/* Turn off BMI */
	BMI_finalize();
    case SHUTDOWN_GOSSIP_INTERFACE:
	gossip_disable();
    case DEALLOC_INIT_MEMORY:
	/* De-alloc any memory we have */
        PINT_server_config_release(&user_opts);
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
    return &user_opts;
}

static int server_parse_cmd_line_args(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv,"fh")) != EOF) {
	switch (opt) {
	    case 'f':
		server_create_storage_space = 1;
		break;
	    case '?':
	    case 'h':
	    default:
		gossip_err("pvfs2-server: [-fh] <global_config_file> <server_config_file>\n\n"
			   "\t-h will show this message\n"
			   "\t-f will cause server to create file system storage and exit\n"
			   );
		return 1;
		break;
	}
    }
    return 0;
}

static int initialize_new_server_op(job_status_s *temp_stat)
{
    int ret;
    job_id_t j_id;
    PINT_server_op *s_op;

    /* allocate space for server operation structure, and memset() it to zero */
    s_op = (PINT_server_op *) malloc(sizeof(PINT_server_op) + ENCODED_HEADER_SIZE);
    if (s_op == NULL)
    {
	return (-1);
    }
    memset(s_op, 0, sizeof(PINT_server_op));

    s_op->op = BMI_UNEXP;

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
     *
     * note: unexp_bmi_buff is really a struct that describes an unexpected
     * message (it is an output parameter).
     */
    ret = job_bmi_unexp(&(s_op->unexp_bmi_buff),
			s_op, /* user ptr */
			temp_stat,
			&j_id,
			JOB_NO_IMMED_COMPLETE,
			PINT_server_job_context);
    if (ret < 0) {
	free(s_op);
	return -1; /* TODO: ????????? */
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

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
 * 
 * Jan 2002
 *
 * This is a basic server.  It will not do any work at this stage.
 * This is meant to be a basic framework.
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

#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <server-queue.h>

/* Internal Globals */

/* TODO:  I am making the server_level_init global for signal handling...
 *        Do we want to do the same for the user_opts? */

/****************************************/
/* TODO:  SUPPORT GM AND TCP for BMI!!! */
/****************************************/

static int server_level_init;
static struct server_configuration_s *user_opts;
static int SIGNAL_RECVD;


/* Prototypes */

static int server_init(void);
static int server_shutdown(int level,int ret,int sig);
static void *sig_handler(int sig);
int PINT_server_cp_bmi_unexp(PINT_server_op *s_op, job_status_s *ret);
void PINT_server_get_bmi_unexp_err(int ret);

/* Functions  */

int main(int argc, char **argv) {

	int ret = -1;
	int i = 0;
	int tempWhile = 0;
	int postBMIFlag = 0;
	int TEMP_CHECK_OUT = 10;
	int TEMP_JOBS_COMPLETE = 0;

	job_id_t job_id_array[MAX_JOBS];
	void *my_user_ptrs[MAX_JOBS];
	job_status_s status_jobs[MAX_JOBS];
	int out_count;
	PINT_server_op *s_op;
	char *method_name;
	
	struct unexpected_info* temp_buff;

	/* Insert Temp Trove Stuff Here */

	TROVE_coll_id coll_id;

	SIGNAL_RECVD = 0;
	server_level_init = 0; /* Used for shutdown function */

	gossip_enable_stderr();
	gossip_set_debug_mask(1,SERVER_DEBUG);

	/* Sanity Check 1. Make sure we are root */
	if (getuid() != 0 && geteuid() != 0){
		gossip_err("WARNING: Server should be run as root\n");
	}

	/* Read configuration options...
	 * function located in server_config.c
	 */
	user_opts = server_config(argc,argv);  /* server_config.c:53 */

	if(!user_opts)
	{
		gossip_err("Error: Could not read configuration; aborting.\n");
		server_shutdown(server_level_init,1,0);
	}
	server_level_init++;
	
	/* perform initial steps to run as a server 
	 * This function is located at the bottom of this file
	 * TODO: Should it be here? dw
	 */ 
	ret = server_init();	 /* server_daemon.c:286 */
	if(ret < 0)
	{
		gossip_err("Error: Could not start server; aborting.\n");
		server_shutdown(server_level_init,1,0);
	}
	server_level_init++;
	
	/* initialize BMI Interface */
	ret = BMI_initialize("bmi_tcp", user_opts->host_id, BMI_INIT_SERVER); /* bmi.c */
	if(ret < 0){
		gossip_err("BMI_initialize Failed: %s\n",strerror(-ret));
		server_shutdown(server_level_init,ret,0);
	}
	server_level_init++;

	gossip_debug(SERVER_DEBUG,"BMI Init Complete\n");

#if 0
	ret = PINT_flow_initialize(NULL,0);
	if(ret < 0){
		gossip_err("Flow_initialize Failed: %s\n",strerror(-ret));
		server_shutdown(server_level_init,user_opts,ret,0);
	}

	server_level_init++;

	gossip_debug(SERVER_DEBUG,"Flow Init Complete\n");

#endif

	ret = trove_initialize(user_opts->storage_path,0,&method_name,0);
	if(ret < 0){
		gossip_err("Trove Init Failed: %s\n",strerror(-ret));
		server_shutdown(server_level_init,ret,0);
	}

	ret = trove_collection_lookup("fs-foo",&coll_id,NULL,NULL);

	server_level_init++;
		
	gossip_debug(SERVER_DEBUG,"Storage Init Complete\n");

	/* Initialize the job interface 
	 * see dir ../io/job 
	 */

   ret = job_initialize(0); /* job.c */
	if(ret < 0)
	{
		gossip_err("Error initializing job interface: %s\n", strerror(-ret));
		server_shutdown(server_level_init,ret,0);
	}

	server_level_init++;

	gossip_debug(SERVER_DEBUG,"Job Init Complete\n");

	/* Now init the Server State Machine */
	ret = PINT_state_machine_init(); /* state_machine.c:68 */
	if(ret < 0)
	{
		gossip_err("Error initializing state_machine interface: %s\n", strerror(-ret));
		server_shutdown(server_level_init,ret,0);
	}

	server_level_init++;

	gossip_debug(SERVER_DEBUG,"State Machine Init Complete\n");

#if 0
	/* Yah... so seriously... this most likely is not needed anymore...
	 * see the consistency doc for more details
	 */
	ret = PINT_server_queue_init();
	if(ret < 0)
	{
		gossip_err("Error initializing Server Queue/Dependancy interface: %s\n", strerror(-ret));
		server_shutdown(server_level_init,ret,0);
	}

	server_level_init++;

	gossip_debug(SERVER_DEBUG,"Server Queue Init Complete\n");
#endif

	
	/* Below, we would init post BMI unexpected msg buffers =) */
	/* TODO:  You need to malloc unexpected_info, keep track of it
	 * by user_ptr in job_post.
	 * also malloc job_status_s
	 * the only time job_status is used is when a job completes immediately¸
	 * so why alloc so many?  why not use what we have already in our waitworld array?
	 */

	for(i=0; i<user_opts->initial_unexpected_requests; i++)
	{
		ret = PINT_server_cp_bmi_unexp(s_op,&status_jobs[0]);
		if (ret < 0) 
		{
			PINT_server_get_bmi_unexp_err(ret);
			if(ret == -4)
				free(temp_buff);
			server_shutdown(server_level_init,ret,0);
		}
		if (ret == 1)
		{
			ret = PINT_state_machine_initialize_unexpected(s_op,&status_jobs[i]); 
			while(ret == 1)
			{
				ret = PINT_state_machine_next(my_user_ptrs[i],&status_jobs[i]);
			}
			gossip_debug(SERVER_DEBUG,"BMI_unexp Completed\n");
		}
	} /* End of BMI Unexpected Requests rof */
	server_level_init++;
	gossip_debug(SERVER_DEBUG,"All BMI_unexp Posted\n");

	/* Register Signals */
	signal(SIGHUP, (void *)sig_handler);
	signal(SIGILL, (void *)sig_handler);
#if 0
//#ifdef USE_SIGACTION
	act.sa_handler = (void *)sig_handler;
	act.sa_mask = 0;
	act.sa_flags = 0;
	if (sigaction(SIGHUP,&act,NULL) < 0)
	{
		gossip_err("Error Registering Signal SIGHUP.\nProgram Terminating.\n");
		server_shutdown(server_level_init,ret,0);
	}
	if (sigaction(SIGSEGV,&act,NULL) < 0)
	{
		gossip_err("Error Registering Signal SIGSEGV.\nProgram Terminating.\n");
		server_shutdown(server_level_init,ret,0);
	}
	if (sigaction(SIGPIPE,&act,NULL) < 0)
	{
		gossip_err("Error Registering Signal SIGPIPE.\nProgram Terminating.\n");
		server_shutdown(server_level_init,ret,0);
	}
//#else
	signal(SIGSEGV, (void *)sig_handler);
	signal(SIGPIPE, (void *)sig_handler);
#endif

	while(1)   /* The do work forever loop. */
	{
		out_count = MAX_JOBS;
		if (SIGNAL_RECVD != 0)
		{
			server_shutdown(server_level_init,SIGNAL_RECVD,0);
		}
      ret = job_waitworld(job_id_array,&out_count,my_user_ptrs,status_jobs);
		if(ret < 0)
		{
			gossip_lerr("FREAK OUT.\n");
			exit(-1);
		}
		if(out_count !=0)
			gossip_debug(SERVER_DEBUG,"Outcount: %d\n",out_count);
		
		for(i=0;i<out_count;i++) 
		{
			s_op = (PINT_server_op *) my_user_ptrs[i];
			if(s_op->op == BMI_UNEXP)
			{

doWorkUnexp:
#ifdef DEBUG
				if(TEMP_JOBS_COMPLETE++ == TEMP_CHECK_OUT)
					server_shutdown(server_level_init,-1,0);
#endif

				ret = PINT_state_machine_initialize_unexpected(s_op,&status_jobs[i]); 
				postBMIFlag = 1;

			}
			else 
			{
				ret = PINT_state_machine_next(s_op,&status_jobs[i]);
			}
			while(ret == 1)
			{
				ret = PINT_state_machine_next(s_op,&status_jobs[i]);
			}

			if(ret < 0)
			{
				gossip_lerr("Error on job %d, Return Code: %d\n",i,ret);
				exit(-1);
			/* if ret < 0 oh no... job mechanism died */
			/* TODO: fix taht */
			}
			if(postBMIFlag)
			{
				gossip_debug(SERVER_DEBUG,"Posting Another BMI Job\n");
				postBMIFlag = 0;
				ret = PINT_server_cp_bmi_unexp(s_op,&status_jobs[i]);
				if(ret == 1)
				{
					goto doWorkUnexp;
				}
			}

		}

	}
	gossip_debug(SERVER_DEBUG,"Bailing out of While loop: tempWhile = %d\n",tempWhile);
	return(server_shutdown(server_level_init+1,1,0));
	
}


/* server_init()
 *
 * performs initialization steps for server program
 *
 * returns 0 on success, -errno on failure
 */

static int server_init(void) {

	/*int ret = -1;*/
#ifdef DEBUG
	char logname[] = "/tmp/pvfs_server.log";
	int logfd;
#endif

	/* create a child process to continue execution */
#if 0
	ret = fork();
	if(ret < 0)
	{
		return(-errno);
	}
	if (ret > 0) 
	{
		exit(0); /* kill the parent */
	}

	setsid();   /* Become the parent */ 
	umask(0);
#endif
	chdir("/"); /* avoid busy filesystems */

   /* Now we need a log file...          
    * 
    * NOTE: Will not run if DEBUG is set 
	 */
#ifdef DEBUG  
	gossip_ldebug(SERVER_DEBUG,"Log Init\n");
	gossip_enable_file("/tmp/pvfsServer.log","a");
#else
	gossip_debug(SERVER_DEBUG,"Logging was skipped b/c DEBUG Flag set\nOutput will be displayed onscreen\n");
#endif

	return(0);
}

/* For fatal errors, and SIG */

static int server_shutdown(int level,int ret,int siglevel) {
	switch(level) {
		case 11:   /* Outside of While Loop in Main() 
					  * This is really bad */
		case 10:
			/* TODO:  Should we do anything else here?
			 *        Shutting down here means we were running
			 *        perfectly  and failed somewhere inside the
			 *        while loop.*/
		case 9: 
			/* BMI Unexpected Failure */
		case 8:
			/* Check Deps/ Queue */
		case 7:
			/* State Machine */
			PINT_state_machine_halt();
		case 6:
			/* Turn off High Level Interface */
			job_finalize();
		case 5:
			/* Turn off Storage IFace */
			trove_finalize();
		case 4:
			/* Turn off Flows */
         /* PINT_flow_finalize(); */
		case 3:
			/* Turn off BMI */
			BMI_finalize();
		case 2:
			gossip_disable();
		case 1:
			/* Unalloc any memory we have */
			//free(user_opts->host_id);
			//free(user_opts->tcp_path_bmi_library);
			//free(user_opts);
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
	gossip_debug(SERVER_DEBUG,"Got Signal %d... Level...%d\n",sig,server_level_init);
	SIGNAL_RECVD=sig;
#if 0
	if(sig == SIGSEGV)
	{
		exit(-1);
	}
	else if(sig == 1)
	{
		exit(server_shutdown(server_level_init,0,0));
		/* if we get a restart, what to do now? */
	}
#ifndef USE_SIGACTION
	signal(sig, (void *)sig_handler);
#endif
	if(sig == SIGPIPE)
	{
		return(0);
	}
	exit(-1);
#endif
	return NULL;

}

struct server_configuration_s *get_server_config_struct(void)
{
	return user_opts;
}

int PINT_server_cp_bmi_unexp(PINT_server_op *serv_op, job_status_s *temp_stat)
{
	job_id_t jid;
	int ret;

	serv_op = (PINT_server_op *) malloc(sizeof(PINT_server_op));
	if(!serv_op)
	{
		return(-1);
	}
	serv_op->op = BMI_UNEXP;

	serv_op->unexp_bmi_buff = (struct unexpected_info *) malloc(sizeof(struct unexpected_info));
	if(!serv_op->unexp_bmi_buff)
	{
		return(-2);
	}

	ret = job_bmi_unexp(serv_op->unexp_bmi_buff,serv_op,temp_stat,&jid);
	if(ret < 0)
	{
		return(-4);
	}
	if(ret == 1)
	{
		// WE NEED TO submit to check dep;
	}
	return(0);
}

void PINT_server_get_bmi_unexp_err(int ret)
{
	switch(ret)
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

/* End pvfs_server.c */

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
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
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
#include "state-machine-fns.h"
#include "mkspace.h"
#include "server-config.h"
#include "quicklist.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* Lookup table for determining what state machine to use when a
 * new request is received.
 */
static struct PINT_state_machine_s *PINT_server_op_table[PVFS_MAX_SERVER_OP+1] =
{NULL};

/* For the switch statement to know what interfaces to shutdown */
static PINT_server_status_flag server_status_flag;

/* All parameters read in from the configuration file */
static struct server_configuration_s server_config;

/* A flag to stop the main loop from processing and handle the signal 
 * after all threads complete and are no longer blocking.
 */
static int signal_recvd_flag = 0;

/* this is used externally by some server state machines */
job_context_id server_job_context = -1;

static int server_create_storage_space = 0;
static int server_background = 1;

PINT_server_trove_keys_s Trove_Common_Keys[] = {
    {"root_handle", 12},
    {"metadata", 9},
    {"dir_ent", 8},
    {"datafile_handles", 17},
    {"metafile_dist", 14},
    {"symlink_target", 15}
};

/* These three are used continuously in our wait loop.  They could
 * be relatively large, so rather than allocate them on the stack,
 * we'll make them dynamically allocated globals.
 */
static job_id_t *server_job_id_array = NULL;
static void **server_completed_job_p_array = NULL;
static job_status_s *server_job_status_array = NULL;

/* Prototypes for internal functions */
static int server_initialize(
    PINT_server_status_flag *server_status_flag,
    job_status_s *job_status_structs);
static int server_initialize_subsystems(
    PINT_server_status_flag *server_status_flag);
static int server_setup_signal_handlers(void);
static int server_setup_process_environment(int background);
static int server_shutdown(
    PINT_server_status_flag status,
    int ret, int sig);
static void *server_sig_handler(int sig);
static int server_post_unexpected_recv(job_status_s * ret);
static int server_parse_cmd_line_args(int argc, char **argv);

static void server_state_table_initialize(void);
static int server_state_machine_start(
    PINT_server_op *s_op, job_status_s *ret);

/* main()
 */
int main(int argc, char **argv)
{
    int ret = -1, debug_mask = 0;

#ifdef WITH_MTRACE
    mtrace();
#endif

    /* Passed to server shutdown function */
    server_status_flag = SERVER_DEFAULT_INIT;

    /* Enable the gossip interface to send out stderr and set an initial
     * debug mask so that we can output errors at startup.
     */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, SERVER_DEBUG);

    server_status_flag |= SERVER_GOSSIP_INIT;

    gossip_debug(SERVER_DEBUG, "PVFS2 Server version %s starting.\n",
		 PVFS2_VERSION);

    /* Determine initial server configuration, looking at both command line
     * arguments and the configuration file.
     */
    if (server_parse_cmd_line_args(argc, argv) != 0)
    {
	goto server_shutdown;
    }
    if (PINT_parse_config(&server_config, argv[optind], argv[optind + 1]))
    {
	gossip_err("Fatal Error: This server requires a valid "
                   "configuration for operation.\nPlease check your "
                   "configuration setting.  Server aborting.\n");
	goto server_shutdown;
    }

    server_status_flag |= SERVER_CONFIG_INIT;

    /* Verify that our configuration makes sense. */
    if (!PINT_config_is_valid_configuration(&server_config))
    {
	gossip_err("Error: Invalid configuration; aborting.\n");
	goto server_shutdown;
    }

    /* Reset the gossip debug mask based on configuration
     * settings that we now have access to.
     */
    debug_mask = PVFS_debug_eventlog_to_mask(server_config.event_logging);
    gossip_set_debug_mask(1, debug_mask);
    gossip_debug(SERVER_DEBUG,"Logging %s (mask %d)\n",
                 server_config.event_logging, debug_mask);

    /* If we were directed to create a storage space, do so and then
     * exit.
     */
    if (server_create_storage_space)
    {
        ret = PINT_config_pvfs2_mkspace(&server_config);
	exit(ret);
    }

    if (server_config.initial_unexpected_requests > PVFS_SERVER_MAX_JOBS)
    {
	gossip_err("Error: initial_unexpected request value is too "
                   "large; aborting.\n");
	goto server_shutdown;
    }
    else
    {
        int num_jobs = server_config.initial_unexpected_requests;

        server_job_id_array = (job_id_t *)
            malloc(num_jobs * sizeof(job_id_t));
        server_completed_job_p_array = (void **)
            malloc(num_jobs * sizeof(void *));
        server_job_status_array = (job_status_s *)
            malloc(num_jobs * sizeof(job_status_s));

        if (!server_job_id_array ||
            !server_completed_job_p_array ||
            !server_job_status_array)
        {
            if (server_job_id_array)
            {
                free(server_job_id_array);
            }
            if (server_completed_job_p_array)
            {
                free(server_completed_job_p_array);
            }
            if (server_job_status_array)
            {
                free(server_job_status_array);
            }
            gossip_err("Error: initial_unexpected request allocation "
                       "failure; aborting.\n");
            goto server_shutdown;
        }
        server_status_flag |= SERVER_JOB_OBJS_ALLOCATED;
    }

    /* Initialize the server (many many steps) */
    ret = server_initialize(&server_status_flag, server_job_status_array);
    if (ret < 0)
    {
	gossip_err("Error: Could not initialize server; aborting.\n");
	goto server_shutdown;
    }

    /* Initialization complete; process server requests indefinitely. */
    for ( ;; )  
    {
	int i, comp_ct = PVFS_SERVER_MAX_JOBS;

	if (signal_recvd_flag != 0)
	{
	    ret = signal_recvd_flag;
	    goto server_shutdown;
	}

	ret = job_testcontext(server_job_id_array,
			      &comp_ct,
			      server_completed_job_p_array,
			      server_job_status_array,
                              PVFS2_SERVER_DEFAULT_TIMEOUT_MS,
			      server_job_context);
	if (ret < 0)
	{
	    gossip_lerr("pvfs2-server panic; main loop aborting\n");
	    exit(-1);
	}

	/* Loop through the completed jobs and handle whatever comes next. */
	for (i = 0; i < comp_ct; i++)
	{
	    int unexpected_msg = 0;
	    PINT_server_op *s_op = server_completed_job_p_array[i];;

	    /* Completed jobs might be ongoing, or might be new (unexpected)
	     * ones.  We handle the first step of either type here.
	     */
	    if (s_op->op == BMI_UNEXPECTED_OP)
	    {
		unexpected_msg = 1;
		ret = server_state_machine_start(
                    s_op, &server_job_status_array[i]);
		if(ret < 0)
		{
		    gossip_err("Error: server unable to handle request, "
                               "error code: %d.\n", (int)ret);
		    free(s_op->unexp_bmi_buff.buffer);
		    /* TODO: tell BMI to drop this address? */
		    /* set return code to zero to allow server to continue 
		     * processing 
		     */
		    ret = 0;
		}
	    }
	    else {
		/* NOTE: PINT_state_machine_next() is a function that is
		 * shared with the client-side state machine processing,
		 * so it is defined in the src/common directory.
		 */
		ret = PINT_state_machine_next(s_op, &server_job_status_array[i]);
	    }

	    /* Either of the above might have completed immediately (ret == 1).
	     * While the job continues to complete immediately, we continue to
	     * service it.
	     */
            while (ret == 1)
            {
                ret = PINT_state_machine_next(s_op, &server_job_status_array[i]);
            }

	    if (ret < 0)
	    {
		gossip_lerr("Error: unhandled state machine processing "
                            "error (most likely an unhandled job error).\n");
		/* TODO: handle this properly */
		assert(0);
	    }

	    if (unexpected_msg)
	    {
		/* If this was a new (unexpected) job, we need to post a
		 * replacement unexpected job so that we can continue to 
		 * receive incoming requests.
		 */

		ret = server_post_unexpected_recv(&server_job_status_array[i]);
		if (ret < 0)
		{
		    /* TODO: do something here, the return value was
		     * not being checked for failure before.  I just
		     * put something here to make it exit for the
		     * moment.  -Phil
		     */
		    gossip_lerr("Error: NOT HANDLED.\n");
		    exit(1);
		}
	    }
	} /* ... for i < comp_ct */
    }

  server_shutdown:
    server_shutdown(server_status_flag, ret, 0);
    return -1;	/* Should never get here */
} /* end of main() */

/* server_initialize()
 *
 * Handles:
 * - backgrounding, redirecting logging
 * - initializing all the subsystems (BMI, Trove, etc.)
 * - setting up the state table used to map new requests to
 *   state machines
 * - allocating and posting the initial unexpected message jobs
 * - setting up signal handlers
 */
static int server_initialize(
    PINT_server_status_flag *server_status_flag,
    job_status_s *job_status_structs)
{
    int ret = 0, i = 0;

    /* Handle backgrounding, setting up working directory, and so on. */
    ret = server_setup_process_environment(server_background);
    if (ret < 0)
    {
	gossip_err("Error: Could not start server; aborting.\n");
        return ret;
    }

    /* Initialize the bmi, flow, trove and job interfaces */
    ret = server_initialize_subsystems(server_status_flag);
    if (ret < 0)
    {
	gossip_err("Error: Could not initialize server interfaces; "
                   "aborting.\n");
        return ret;
    }

    /* Initialize table of state machines (no return value) */
    server_state_table_initialize();

    *server_status_flag |= SERVER_STATE_MACHINE_INIT;

    /* Post starting set of BMI unexpected msg buffers */
    for (i = 0; i < server_config.initial_unexpected_requests; i++)
    {
	ret = server_post_unexpected_recv(&job_status_structs[i]);
	if (ret < 0)
	{
            gossip_err("Error posting unexpected recv\n");
            return ret;
	}
    }

    *server_status_flag |= SERVER_BMI_UNEXP_POST_INIT;

    ret = server_setup_signal_handlers();

    *server_status_flag |= SERVER_SIGNAL_HANDLER_INIT;

    gossip_debug(SERVER_DEBUG, "Initialization completed successfully.\n");

    return ret;
}

/* server_setup_process_environment()
 *
 * Handles:
 * - chdir() to / to prevent busy file systems
 * - setting known umask
 * - possibly backgrounding
 *   - fork(), setsid(), etc.
 *   - redirecting gossip output to file
 *
 * Returns 0 on success.
 */
static int server_setup_process_environment(int background)
{

    chdir("/");  /* avoid busy file systems */
    umask(0077); /* let's have a deterministic umask too */

    if (background) {
	int ret;

	/* become a daemon, redirect log to file */
	ret = fork();
	if (ret < 0) {
	    exit(1); /* couldn't fork?!? */
	}

	if (ret > 0) exit(0); /* parent goes away */

	ret = setsid();
	if (ret < 0) {
	    exit(2);
	}

	/* NOTE: THIS IS NECESSARY UNTIL ALL LOGGING IN SERVER IS THROUGH
	 * GOSSIP; OTHERWISE PRINTFS CAN END UP DUMPING DATA IN A SOCKET!
	 */
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);

        assert(server_config.logfile != NULL);
	ret = gossip_enable_file(server_config.logfile, "a");
	if (ret < 0) {
	    gossip_lerr("error opening log file %s\n",
                        server_config.logfile);
	    exit(3); /* couldn't open log! */
	}
    }
    else
    {
	/* stay in the foreground, direct log to stderr */

	/* note: gossip has already been directed to stderr
	 *       at this point; nothing to do?
	 */
    }
    return 0;
}

/* server_initialize_subsystems()
 *
 * This:
 * - initializes encoding/decoding subsystem
 * - initializes BMI
 * - initializes Trove
 *   - finds the collection IDs for all file systems
 *   - gets a context from Trove
 *   - tells Trove what handles are to be used
 *     for each file system (collection)
 * - initializes the flow subsystem
 * - initializes the job subsystem
 *   - gets a job context
 * - initialize the request scheduler
 */
static int server_initialize_subsystems(
    PINT_server_status_flag *server_status_flag)
{
    int ret = 0;
    char *method_name = NULL;
    char *cur_merged_handle_range = NULL;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs;
    TROVE_context_id trove_context = -1;

    /* intialize protocol encoder */
    ret = PINT_encode_initialize();
    if (ret < 0)
    {
	gossip_err("PINT_encode_initialize() failed.\n");
        return ret;
    }

    *server_status_flag |= SERVER_ENCODER_INIT;

    gossip_debug(SERVER_DEBUG,
		 "Passing %s to BMI as listen address.\n",
		 server_config.host_id);

    /* initialize BMI */
    ret = BMI_initialize(server_config.bmi_modules, 
	server_config.host_id, BMI_INIT_SERVER);
    if (ret < 0)
    {
	gossip_err("BMI_initialize Failed: %s\n", strerror(-ret));
        return ret;
    }

    *server_status_flag |= SERVER_BMI_INIT;

    /* initialize Trove */
    ret = trove_initialize(server_config.storage_path, 0, &method_name, 0);
    if (ret < 0)
    {
	gossip_err("Trove Init Failed: %s\n", strerror(-ret));

        if (ret == -1)
        {
            gossip_err("\n*****************************\n");
            gossip_err("Invalid Storage Space: %s\n\n",
		       server_config.storage_path);
            gossip_err("Storage initialization failed.  The most "
		       "common reason\nfor this is that the storage space "
		       "has not yet been\ncreated or is located on a "
		       "partition that has not yet\nbeen mounted.  "
		       "If you'd like to create the storage space,\n"
		       "re-run this program with a -f option.\n");
            gossip_err("\n*****************************\n");
        }
        return ret;
    }

    *server_status_flag |= SERVER_TROVE_INIT;

    cur = server_config.file_systems;
    while(cur)
    {
        cur_fs = PINT_llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        ret = trove_collection_lookup(cur_fs->file_system_name,
                                      &(cur_fs->coll_id),
				      NULL,
				      NULL);
	if (ret < 0)
	{
	    gossip_lerr("Error initializing filesystem %s\n",
                        cur_fs->file_system_name);
            return ret;
	}

        /*
         * get a range string that combines all handles for both meta
         * and data ranges specified in the config file.
         *
         * the server isn't concerned with what allocation of handles
         * are meta and which are data at this level, so we lump them
         * all together and hand them to trove-handle-mgmt.
         */
        cur_merged_handle_range =
            PINT_config_get_merged_handle_range_str(
                &server_config, cur_fs);

        /*
         * error out if we're not configured to house either a
         * meta or data handle range at all.
	 */
        if (!cur_merged_handle_range)
        {
	    gossip_lerr("Error: Invalid handle range for host %s "
                        "(alias %s) specified in file system %s\n",
                        server_config.host_id,
                        PINT_config_get_host_alias_ptr(
                            &server_config, server_config.host_id),
                        cur_fs->file_system_name);
            return -1;
        }
        else
        {
            /* open a trove context for this collection id */
            ret = trove_open_context(cur_fs->coll_id, &trove_context);
            if (ret < 0)
            {
                gossip_lerr("Error initializing trove context\n");
                return ret;
            }

            /* add configured merged handle range for this host/fs */
            ret = trove_collection_setinfo(cur_fs->coll_id, trove_context,
					   TROVE_COLLECTION_HANDLE_RANGES,
					   (void *)cur_merged_handle_range);
            if (ret < 0)
            {
                gossip_lerr("Error adding handle range %s to "
                            "filesystem %s\n",
			    cur_merged_handle_range,
                            cur_fs->file_system_name);
                return ret;
            }

	    gossip_debug(SERVER_DEBUG, "File system %s using handles: %s\n",
			 cur_fs->file_system_name, cur_merged_handle_range);

            trove_close_context(cur_fs->coll_id, trove_context);
            free(cur_merged_handle_range);
        }
	ret = trove_collection_setinfo(cur_fs->coll_id, trove_context, 
				    TROVE_COLLECTION_HANDLE_TIMEOUT,
				    &(server_config.handle_purgatory));
	if (ret < 0)
	{
	    gossip_lerr("Error setting handle timeout\n");
	    return ret;
	}

        cur = PINT_llist_next(cur);
    }
#ifdef __PVFS2_TROVE_THREADED__
#ifdef __PVFS2_TROVE_AIO_THREADED__
    gossip_debug(SERVER_DEBUG, "Storage Init Complete (aio-threaded)\n");
#else
    gossip_debug(SERVER_DEBUG, "Storage Init Complete (threaded)\n");
#endif
#else
    gossip_debug(SERVER_DEBUG, "Storage Init Complete (non-threaded)\n");
#endif
    gossip_debug(SERVER_DEBUG, "%d filesystem(s) initialized\n",
                 PINT_llist_count(server_config.file_systems));

    /* initialize the flow interface */
    ret = PINT_flow_initialize(server_config.flow_modules, 0);
    if (ret < 0)
    {
	gossip_err("Flow_initialize Failed: %s\n", strerror(-ret));
        return ret;
    }

    *server_status_flag |= SERVER_FLOW_INIT;

    /* initialize Job Interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
	gossip_err("Error initializing job interface: %s\n", strerror(-ret));
        return ret;
    }

    *server_status_flag |= SERVER_JOB_INIT;
    
    ret = job_open_context(&server_job_context);
    if (ret < 0)
    {
	gossip_err("Error opening job context.\n");
        return ret;
    }

    *server_status_flag |= SERVER_JOB_CTX_INIT;

    /* initialize Server Request Scheduler */
    ret = PINT_req_sched_initialize();
    if (ret < 0)
    {
	gossip_err("Error initializing Request Scheduler interface: %s\n",
		strerror(-ret));
        return ret;
    }

    *server_status_flag |= SERVER_REQ_SCHED_INIT;

    return ret;
}

/* server_setup_signal_handlers()
 */
static int server_setup_signal_handlers(void)
{
    signal(SIGHUP, (void *) server_sig_handler);
    signal(SIGILL, (void *) server_sig_handler);
    signal(SIGPIPE, (void *) server_sig_handler);
    return 0;
}

/* server_shutdown()
 *
 * Tries to figure out what has been done so far and clean things up.
 */
static int server_shutdown(
    PINT_server_status_flag status,
    int ret, int siglevel)
{
    if (status & SERVER_STATE_MACHINE_INIT)
	PINT_state_machine_halt();

    if (status & SERVER_JOB_CTX_INIT)
	job_close_context(server_job_context);

    if (status & SERVER_JOB_INIT)
	job_finalize();

    if (status & SERVER_TROVE_INIT)
	trove_finalize();

    if (status & SERVER_FLOW_INIT)
	PINT_flow_finalize();

    if (status & SERVER_BMI_INIT)
	BMI_finalize();

    if (status & SERVER_ENCODER_INIT)
	PINT_encode_finalize();

    if (status & SERVER_GOSSIP_INIT)
	gossip_disable();

    if (status & SERVER_CONFIG_INIT)
        PINT_config_release(&server_config);

    if (status & SERVER_JOB_OBJS_ALLOCATED)
    {
        free(server_job_id_array);
        free(server_completed_job_p_array);
        free(server_job_status_array);
    }

    exit((siglevel == 0) ? -ret : 0);
}

/* server_sig_handler()
 */
static void *server_sig_handler(int sig)
{
    gossip_err("PVFS2 server: got signal: %d, server_status_flag: %d\n", 
	sig, (int)server_status_flag);

    /* short circuit non critical signals here */
    if(sig == SIGPIPE)
    {
	/* reset handler and continue processing */
	signal(sig, (void*) server_sig_handler);
	return(NULL);
    }

    /* set the signal_recvd_flag on critical errors to cause the server to 
     * exit gracefully on the next work cycle
     */
    signal_recvd_flag = sig;
    return NULL;
}

/* server_parse_cmd_line_args()
 */
static int server_parse_cmd_line_args(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv,"fhd")) != EOF) {
	switch (opt) {
	    case 'f':
		server_create_storage_space = 1;
		break;
	    case 'd':
		server_background = 0;
		break;
	    case '?':
	    case 'h':
	    default:
		gossip_err("pvfs2-server: [-hdf] <global_config_file> <server_config_file>\n\n"
			   "\t-h will show this message\n"
			   "\t-d will keep the server in the foreground\n"
			   "\t-f will cause server to create file system storage and exit\n"
			   );
		return 1;
		break;
	}
    }
    return 0;
}

/* server_post_unexpected_recv()
 *
 * Allocates space for an unexpected BMI message and posts this.
 *
 * Returns 0 on success, < 0 on error.
 *
 * TODO:
 * - FIX RETURN VALUE TO BE MORE HELPFUL
 * - INVESTIGATE KEEPING A FIXED NUMBER OF PINT_SERVER_OPS AROUND TO AVOID
 *   MALLOC/FREE
 */
static int server_post_unexpected_recv(job_status_s *temp_stat)
{
    int ret;
    job_id_t j_id;
    PINT_server_op *s_op;

    /* allocate space for server operation structure, and memset() it to zero */
    s_op = (PINT_server_op *) malloc(sizeof(PINT_server_op));
    if (s_op == NULL)
    {
	return (-1);
    }
    memset(s_op, 0, sizeof(PINT_server_op));

    s_op->op = BMI_UNEXPECTED_OP;

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
			0,
			temp_stat,
			&j_id,
			JOB_NO_IMMED_COMPLETE,
			server_job_context);
    if (ret < 0)
    {
	free(s_op);
	return -1;
    }

    return 0;
}

/* server_state_machine_start()
 *
 * initializes fields in the s_op structure and begins execution of
 * the appropriate state machine
 *
 * returns 0 on success, -PVFS_errno on failure
 */
static int server_state_machine_start(
    PINT_server_op *s_op,
    job_status_s *ret)
{
    int retval = -1;

    retval = PINT_decode(s_op->unexp_bmi_buff.buffer,
		PINT_ENCODE_REQ,
		&s_op->decoded,
		s_op->unexp_bmi_buff.addr,
		s_op->unexp_bmi_buff.size);
    if(retval < 0)
    {
	gossip_err("Error: server received a corrupt or unsupported "
                   "request message.\n");
	return(retval);
    }

    s_op->req  = (struct PVFS_server_req *) s_op->decoded.buffer;
    assert(s_op->req != NULL);

    s_op->addr = s_op->unexp_bmi_buff.addr;
    s_op->tag  = s_op->unexp_bmi_buff.tag;
    s_op->op   = s_op->req->op;
    s_op->current_state = PINT_state_machine_locate(s_op);

    if(!s_op->current_state)
    {
	gossip_err("Error: server does not implement request type: %d\n",
                   (int)s_op->req->op);
	PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);
	return(-PVFS_ENOSYS);
    }

    s_op->resp.op = s_op->req->op;

    return ((s_op->current_state->state_action))(s_op,ret);
}

/* server_state_table_initialize()
 *
 * sets up a table of state machines that can be located with
 * PINT_state_machine_locate()
 *
 */
static void server_state_table_initialize(void)
{

    /* fill in indexes for each supported request type */
    PINT_server_op_table[PVFS_SERV_INVALID]       = NULL;
    PINT_server_op_table[PVFS_SERV_CREATE]        = &pvfs2_create_sm;
    PINT_server_op_table[PVFS_SERV_REMOVE]        = &pvfs2_remove_sm;
    PINT_server_op_table[PVFS_SERV_IO]            = &pvfs2_io_sm;
    PINT_server_op_table[PVFS_SERV_GETATTR]       = &pvfs2_get_attr_sm;
    PINT_server_op_table[PVFS_SERV_SETATTR]       = &pvfs2_set_attr_sm;
    PINT_server_op_table[PVFS_SERV_LOOKUP_PATH]   = &pvfs2_lookup_sm;
    PINT_server_op_table[PVFS_SERV_CREATEDIRENT]  = &pvfs2_crdirent_sm;
    PINT_server_op_table[PVFS_SERV_RMDIRENT]      = &pvfs2_rmdirent_sm;
    PINT_server_op_table[PVFS_SERV_MKDIR]         = &pvfs2_mkdir_sm;
    PINT_server_op_table[PVFS_SERV_READDIR]       = &pvfs2_readdir_sm;
    PINT_server_op_table[PVFS_SERV_GETCONFIG]     = &pvfs2_get_config_sm;
    PINT_server_op_table[PVFS_SERV_FLUSH]	  = &pvfs2_flush_sm;
    PINT_server_op_table[PVFS_SERV_TRUNCATE]	  = &pvfs2_truncate_sm;
    PINT_server_op_table[PVFS_SERV_MGMT_SETPARAM] = &pvfs2_setparam_sm;
    PINT_server_op_table[PVFS_SERV_MGMT_NOOP]     = &pvfs2_noop_sm;
    PINT_server_op_table[PVFS_SERV_STATFS]	  = &pvfs2_statfs_sm;
}

/* server_state_machine_complete()
 *
 * function to be called at the completion of state machine execution;
 * it frees up any resources associated with the state machine that were
 * allocated before the state machine started executing.  Also returns
 * appropriate return value to make the state machine stop transitioning
 *
 * returns 0
 *
 * TODO: keep a pool of state structures, and just return this one to the
 *       pool here.
 */
int server_state_machine_complete(PINT_server_op *s_op)
{
    /* release the decoding of the unexpected request */
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);

    /* free the buffer that the unexpected request came in on */
    free(s_op->unexp_bmi_buff.buffer);

    /* free the operation structure itself */
    free(s_op);

    return 0;
}

/* get_server_config_struct()
 *
 * Note: used externally
 */
struct server_configuration_s *get_server_config_struct(void)
{
    return &server_config;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

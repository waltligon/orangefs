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

#ifdef __PVFS2_SEGV_BACKTRACE__
#include <execinfo.h>
#define __USE_GNU
#include <ucontext.h>
#endif

#define __PINT_REQPROTO_ENCODE_FUNCS_C

#include "bmi.h"
#include "gossip.h"
#include "job.h"
#include "trove.h"
#include "pvfs2-debug.h"
#include "pvfs2-storage.h"
#include "PINT-reqproto-encode.h"
#include "pvfs2-server.h"
#include "state-machine.h"
#include "mkspace.h"
#include "server-config.h"
#include "quicklist.h"
#include "pint-dist-utils.h"
#include "pint-perf-counter.h"
#include "id-generator.h"
#include "job-time-mgr.h"
#include "pint-cached-config.h"
#include "pvfs2-internal.h"
#include "src/server/request-scheduler/request-scheduler.h"
#include "pint-event.h"
#include "pint-util.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#ifdef __PVFS2_TROVE_THREADED__
#ifdef __PVFS2_TROVE_AIO_THREADED__
#define SERVER_STORAGE_MODE "aio-threaded"
#else
#define SERVER_STORAGE_MODE "threaded"
#endif
#else
#define SERVER_STORAGE_MODE "non-threaded"
#endif

#define PVFS2_VERSION_REQUEST 0xFF

/* this controls how many jobs we will test for per job_testcontext()
 * call. NOTE: this is currently independent of the config file
 * parameter that governs how many unexpected BMI jobs are kept posted
 * at any given time
 */
#define PVFS_SERVER_TEST_COUNT 64

/* track performance counters for the server */
static struct PINT_perf_key server_keys[] =
{
    {"bytes read", PINT_PERF_READ, 0},
    {"bytes written", PINT_PERF_WRITE, 0},
    {"metadata reads", PINT_PERF_METADATA_READ, PINT_PERF_PRESERVE},
    {"metadata writes", PINT_PERF_METADATA_WRITE, PINT_PERF_PRESERVE},
    {"metadata dspace ops", PINT_PERF_METADATA_DSPACE_OPS, PINT_PERF_PRESERVE},
    {"metadata keyval ops", PINT_PERF_METADATA_KEYVAL_OPS, PINT_PERF_PRESERVE},
    {"request scheduler", PINT_PERF_REQSCHED, PINT_PERF_PRESERVE},
    {NULL, 0, 0},
};

/* For the switch statement to know what interfaces to shutdown */
static PINT_server_status_flag server_status_flag;

/* All parameters read in from the configuration file */
static struct server_configuration_s server_config;

/* A flag to stop the main loop from processing and handle the signal
 * after all threads complete and are no longer blocking.
 */
static int signal_recvd_flag = 0;
static pid_t server_controlling_pid = 0;

static PINT_event_id PINT_sm_event_id;

/* A list of all serv_op's posted for unexpected message alone */
QLIST_HEAD(posted_sop_list);
/* A list of all serv_op's posted for expected messages alone */
QLIST_HEAD(inprogress_sop_list);
/* A list of all serv_op's that are started automatically without requests */
static QLIST_HEAD(noreq_sop_list);

/* this is used externally by some server state machines */
job_context_id server_job_context = -1;

typedef struct
{
    int server_remove_storage_space;
    int server_create_storage_space;
    int server_background;
    char *pidfile;
    char *server_alias;
} options_t;

static options_t s_server_options = { 0, 0, 1, NULL, NULL};
static char *fs_conf = NULL;

/* each of the elements in this array consists of a string and its length.
 * we're able to use sizeof here because sizeof an inlined string ("") gives
 * the length of the string with the null terminator
 */
PINT_server_trove_keys_s Trove_Common_Keys[] =
{
    {ROOT_HANDLE_KEYSTR, ROOT_HANDLE_KEYLEN},
    {DIRECTORY_ENTRY_KEYSTR, DIRECTORY_ENTRY_KEYLEN},
    {DATAFILE_HANDLES_KEYSTR, DATAFILE_HANDLES_KEYLEN},
    {METAFILE_DIST_KEYSTR, METAFILE_DIST_KEYLEN},
    {SYMLINK_TARGET_KEYSTR, SYMLINK_TARGET_KEYLEN},
    {METAFILE_LAYOUT_KEYSTR, METAFILE_LAYOUT_KEYLEN},
    {NUM_DFILES_REQ_KEYSTR, NUM_DFILES_REQ_KEYLEN}
};

/* These three are used continuously in our wait loop.  They could be
 * relatively large, so rather than allocate them on the stack, we'll
 * make them dynamically allocated globals.
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
static int server_purge_unexpected_recv_machines(void);
static int server_setup_process_environment(int background);
static int server_shutdown(
    PINT_server_status_flag status,
    int ret, int sig);
static void reload_config(void);
static void server_sig_handler(int sig);
static void hup_sighandler(int sig, siginfo_t *info, void *secret);
static int server_parse_cmd_line_args(int argc, char **argv);
#ifdef __PVFS2_SEGV_BACKTRACE__
static void bt_sighandler(int sig, siginfo_t *info, void *secret);
#endif
static int create_pidfile(char *pidfile);
static void write_pidfile(int fd);
static void remove_pidfile(void);
static int generate_shm_key_hint(int* server_index);

static void precreate_pool_finalize(void);
static int precreate_pool_initialize(int server_index);
static int precreate_pool_setup_server(const char* host, PVFS_fs_id fsid,
    PVFS_handle* pool_handle);
static int precreate_pool_launch_refiller(const char* host, 
    PVFS_BMI_addr_t addr, PVFS_fs_id fsid, PVFS_handle pool_handle);
static int precreate_pool_count(
    PVFS_fs_id fsid, PVFS_handle pool_handle, int* count);

static TROVE_method_id trove_coll_to_method_callback(TROVE_coll_id);


struct server_configuration_s *PINT_get_server_config(void)
{
    return &server_config;
}

int main(int argc, char **argv)
{
    int ret = -1, siglevel = 0;
    struct PINT_smcb *tmp_op = NULL;
    uint64_t debug_mask = 0;

#ifdef WITH_MTRACE
    mtrace();
#endif

    /* Passed to server shutdown function */
    server_status_flag = SERVER_DEFAULT_INIT;

    /* Enable the gossip interface to send out stderr and set an
     * initial debug mask so that we can output errors at startup.
     */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);

    server_status_flag |= SERVER_GOSSIP_INIT;

    /* Determine initial server configuration, looking at both command
     * line arguments and the configuration file.
     */
    ret = server_parse_cmd_line_args(argc, argv);
    if (ret == PVFS2_VERSION_REQUEST)
    {
        return 0;
    }
    else if (ret != 0)
    {
        goto server_shutdown;
    }

    gossip_debug_fp(stderr, 'S', GOSSIP_LOGSTAMP_DATETIME,
                    "PVFS2 Server on node %s version %s starting...\n",
                    s_server_options.server_alias, PVFS2_VERSION);

    /* code to handle older two config file format */

    ret = PINT_parse_config(&server_config, fs_conf, s_server_options.server_alias);
    if (ret)
    {
        gossip_err("Error: Please check your config files.\n");
        gossip_err("Error: Server aborting.\n");
        free(s_server_options.server_alias);
        ret = -PVFS_EINVAL;
        goto server_shutdown;
    }

    free(s_server_options.server_alias);

    server_status_flag |= SERVER_CONFIG_INIT;

    if (!PINT_config_is_valid_configuration(&server_config))
    {
        gossip_err("Error: Invalid configuration; aborting.\n");
        ret = -PVFS_EINVAL;
        goto server_shutdown;
    }

    /* reset gossip debug mask based on configuration settings */
    debug_mask = PVFS_debug_eventlog_to_mask(server_config.event_logging);
    gossip_set_debug_mask(1, debug_mask);
    gossip_set_logstamp(server_config.logstamp_type);
    gossip_debug(GOSSIP_SERVER_DEBUG,"Logging %s (mask %llu)\n",
                 server_config.event_logging, llu(debug_mask));

    /* remove storage space and exit if requested */
    if (s_server_options.server_remove_storage_space)
    {
        ret = PINT_config_pvfs2_rmspace(&server_config);
        if(ret < 0)
        {
            goto server_shutdown;
        }
        gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);
        gossip_debug(GOSSIP_SERVER_DEBUG, "PVFS2 Server: storage space removed. Exiting.\n");
        gossip_set_debug_mask(1, debug_mask);
        return(0);
    }

    /* create storage space and exit if requested */
    if (s_server_options.server_create_storage_space)
    {
        ret = PINT_config_pvfs2_mkspace(&server_config);
        if(ret < 0)
        {
            goto server_shutdown;
        }
        gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);
        gossip_debug(GOSSIP_SERVER_DEBUG, "PVFS2 Server: storage space created. Exiting.\n");
        gossip_set_debug_mask(1, debug_mask);
        return(0);
    }

    server_job_id_array = (job_id_t *)
        malloc(PVFS_SERVER_TEST_COUNT * sizeof(job_id_t));
    server_completed_job_p_array = (void **)
        malloc(PVFS_SERVER_TEST_COUNT * sizeof(void *));
    server_job_status_array = (job_status_s *)
        malloc(PVFS_SERVER_TEST_COUNT * sizeof(job_status_s));

    if (!server_job_id_array ||
        !server_completed_job_p_array ||
        !server_job_status_array)
    {
        if (server_job_id_array)
        {
            free(server_job_id_array);
            server_job_id_array = NULL;
        }
        if (server_completed_job_p_array)
        {
            free(server_completed_job_p_array);
            server_completed_job_p_array = NULL;
        }
        if (server_job_status_array)
        {
            free(server_job_status_array);
            server_job_status_array = NULL;
        }
        gossip_err("Error: failed to allocate arrays for "
                   "tracking completed jobs.\n");
        goto server_shutdown;
    }
    server_status_flag |= SERVER_JOB_OBJS_ALLOCATED;

    /* Initialize the server (many many steps) */
    ret = server_initialize(&server_status_flag, server_job_status_array);
    if (ret < 0)
    {
        gossip_err("Error: Could not initialize server; aborting.\n");
        goto server_shutdown;
    }

#ifndef __PVFS2_DISABLE_PERF_COUNTERS__
    /* kick off performance update state machine */
    ret = server_state_machine_alloc_noreq(PVFS_SERV_PERF_UPDATE,
        &(tmp_op));
    if (ret == 0)
    {
        ret = server_state_machine_start_noreq(tmp_op);
    }
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: failed to start perf update "
                    "state machine.\n", ret);
        goto server_shutdown;
    }
#endif

    /* kick off timer for expired jobs */
    ret = server_state_machine_alloc_noreq(
        PVFS_SERV_JOB_TIMER, &(tmp_op));
    if (ret == 0)
    {
        ret = server_state_machine_start_noreq(tmp_op);
    }
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: failed to start job timer "
                           "state machine.\n", ret);
        goto server_shutdown;
    }

    gossip_debug_fp(stderr, 'S', GOSSIP_LOGSTAMP_DATETIME,
                    "PVFS2 Server ready.\n");

    /* Initialization complete; process server requests indefinitely. */
    for ( ;; )  
    {
        int i, comp_ct = PVFS_SERVER_TEST_COUNT;

        if (signal_recvd_flag != 0)
        {
            /* If the signal is a SIGHUP, catch and reload configuration */
            if (signal_recvd_flag == SIGHUP)
            {
                reload_config();
                signal_recvd_flag = 0; /* Reset the flag */
            }
            else
            {
                /*
                 * If we received a signal and we have drained all the state
                 * machines that were in progress, we initiate a shutdown of
                 * the server. Find out if we can exit now * by checking if
                 * all s_ops (for expected messages) have either finished or
                 * timed out,
                 */
                if (qlist_empty(&inprogress_sop_list))
                {
                    ret = 0;
                    siglevel = signal_recvd_flag;
                    goto server_shutdown;
                }
                /* not completed. continue... */
            }
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
            goto server_shutdown;
        }

        /*
          Loop through the completed jobs and handle whatever comes
          next
        */
        for (i = 0; i < comp_ct; i++)
        {
            /* int unexpected_msg = 0; */
            struct PINT_smcb *smcb = server_completed_job_p_array[i];

               /* NOTE: PINT_state_machine_next() is a function that
                * is shared with the client-side state machine
                * processing, so it is defined in the src/common
                * directory.
                */
            ret = PINT_state_machine_continue(
                    smcb, &server_job_status_array[i]);

            if (SM_ACTION_ISERR(ret)) /* ret < 0 */
            {
                PVFS_perror_gossip("Error: state machine processing error", ret);
                ret = 0;
            }

            /* else ret == SM_ACTION_DEFERED */
        }
    }

  server_shutdown:
    server_shutdown(server_status_flag, ret, siglevel);
    /* NOTE: the server_shutdown() function does not return; it always ends
     * by calling exit.  This point in the code should never be reached.
     */
    return -1;
}

/*
 * Manipulate the pid file.  Don't bother returning an error in
 * the write stage, since there's nothing that can be done about it.
 */
static int create_pidfile(char *pidfile)
{
    return open(pidfile, (O_CREAT | O_WRONLY | O_TRUNC), 0644);
}

static void write_pidfile(int fd)
{
    pid_t pid = getpid();
    char pid_str[16] = {0};
    int len;
    int ret;

    snprintf(pid_str, 16, "%d\n", pid);
    len = strlen(pid_str);
    ret = write(fd, pid_str, len);
    if(ret < len)
    {
        gossip_err("Error: failed to write pid file.\n");
        close(fd);
        remove_pidfile();
        return;
    }
    close(fd);
    return;
}

static void remove_pidfile(void)
{
    assert(s_server_options.pidfile);
    unlink(s_server_options.pidfile);
}

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
    FILE *dummy;
    uint64_t debug_mask = 0;
    
    assert(server_config.logfile != NULL);

    if(!strcmp(server_config.logtype, "file"))
    {
        dummy = fopen(server_config.logfile, "a");
        if (dummy == NULL)
        {
            int tmp_errno = errno;
            gossip_err("error opening log file %s\n",
                    server_config.logfile);
            return -tmp_errno;
        }
        fclose(dummy);
    }

    /* redirect gossip to specified target if backgrounded */
    if (s_server_options.server_background)
    {
        if(!freopen("/dev/null", "r", stdin))
            gossip_err("Error: failed to reopen stdin.\n");
        if(!freopen("/dev/null", "w", stdout))
            gossip_err("Error: failed to reopen stdout.\n");
        if(!freopen("/dev/null", "w", stderr))
            gossip_err("Error: failed to reopen stderr.\n");

        if(!strcmp(server_config.logtype, "syslog"))
        {
            ret = gossip_enable_syslog(LOG_INFO);
        }
        else if(!strcmp(server_config.logtype, "file"))
        {
            ret = gossip_enable_file(server_config.logfile, "a");
        }
        else
        {
            ret = gossip_enable_stderr();
        }

        if (ret < 0)
        {
            gossip_err("error opening log file %s\n",
                        server_config.logfile);
            return ret;
        }
        /* log starting message again so it appears in log file, not just
         * console
         */
        gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);
        gossip_debug(GOSSIP_SERVER_DEBUG,
           "PVFS2 Server version %s starting.\n", PVFS2_VERSION);
        debug_mask = PVFS_debug_eventlog_to_mask(server_config.event_logging);
        gossip_set_debug_mask(1, debug_mask);
    }

    /* handle backgrounding, setting up working directory, and so on. */
    ret = server_setup_process_environment(
        s_server_options.server_background);
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

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "Initialization completed successfully.\n");

    return ret;
}

/* server_setup_process_environment()
 *
 * performs normal daemon initialization steps
 *
 * returns 0 on success, -PVFS_EINVAL on failure (details will be logged to
 * gossip)
 */
static int server_setup_process_environment(int background)
{
    pid_t new_pid = 0;
    int pid_fd = -1;

    /*
     * Manage a pid file if requested (for init scripts).  Create
     * the file in the parent before the chdir, but let the child
     * write his pid and delete it when exiting.
     */
    if (s_server_options.pidfile)
    {
        pid_fd = create_pidfile(s_server_options.pidfile);
        if (pid_fd < 0)
        {
            gossip_err("Failed to create pid file %s: %s\n",
                       s_server_options.pidfile, strerror(errno));
            return(-PVFS_EINVAL);
        }
    }

    if (chdir("/"))
    {
        gossip_err("cannot change working directory to \"/\" "
                    "(errno = %x). aborting.\n", errno);
        return(-PVFS_EINVAL);
    }

    umask(0077);

    if (background)
    {
        new_pid = fork();
        if (new_pid < 0)
        {
            gossip_err("error in fork() system call (errno = %x). "
                        "aborting.\n", errno);
            return(-PVFS_EINVAL);
        }
        else if (new_pid > 0)
        {
            /* exit parent */
            exit(0);
        }

        new_pid = setsid();
        if (new_pid < 0)
        {
            gossip_err("error in setsid() system call.  aborting.\n");
            return(-PVFS_EINVAL);
        }
    }
    if (pid_fd >= 0)
    {
        /* note: pid_fd closed by write_pidfile() */
        write_pidfile(pid_fd);
        atexit(remove_pidfile);
    }
    server_controlling_pid = getpid();
    return 0;
}

/* server_initialize_subsystems()
 *
 * This:
 * - initializes distribution subsystem
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
    int ret = -PVFS_EINVAL;
    char *cur_merged_handle_range = NULL;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs;
    TROVE_context_id trove_context = -1;
    char buf[16] = {0};
    PVFS_fs_id orig_fsid;
    PVFS_ds_flags init_flags = 0;
    int bmi_flags = BMI_INIT_SERVER;
    int shm_key_hint;
    int server_index;

    if(server_config.enable_events)
    {
        ret = PINT_event_init(PINT_EVENT_TRACE_TAU);
        if (ret < 0)
        {
            gossip_err("Error initializing event interface.\n");
            return (ret);
        }

        /* Define the state machine event:
         *   START: (client_id, request_id, rank, handle, op_id)
         *   STOP: ()
         */
        PINT_event_define_event(
            NULL, "sm", "%d%d%d%llu%d", "", &PINT_sm_event_id);

        *server_status_flag |= SERVER_EVENT_INIT;
    }

    /* Initialize distributions */
    ret = PINT_dist_initialize(0);
    if (ret < 0)
    {
        gossip_err("Error initializing distribution interface.\n");
        return ret;
    }
    *server_status_flag |= SERVER_DIST_INIT;

    ret = PINT_encode_initialize();
    if (ret < 0)
    {
        gossip_err("PINT_encode_initialize() failed.\n");
        return ret;
    }

    *server_status_flag |= SERVER_ENCODER_INIT;

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "Passing %s as BMI listen address.\n",
                 server_config.host_id);

    /* does the configuration dictate that we bind to a specific address? */
    if(server_config.tcp_bind_specific)
    {
        bmi_flags |= BMI_TCP_BIND_SPECIFIC;
    }

    /* Have bmi automatically increment reference count on addresses any
     * time a new unexpected message appears.  The server will decrement it
     * once it has completed processing related to that request.
     */
    bmi_flags |= BMI_AUTO_REF_COUNT;

    ret = BMI_initialize(server_config.bmi_modules, 
                         server_config.host_id,
                         bmi_flags);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: BMI_initialize", ret);
        return ret;
    }
#ifdef USE_TRUSTED
    /* Pass the server_config file pointer to the lower
     * levels for the trusted connections related functions to be
     * called
     */
    BMI_set_info(0, BMI_TRUSTED_CONNECTION, (void *) &server_config);
    gossip_debug(GOSSIP_SERVER_DEBUG, "Enabling trusted connections!\n");
#endif
    *server_status_flag |= SERVER_BMI_INIT;

    ret = trove_collection_setinfo(0, 0, TROVE_DB_CACHE_SIZE_BYTES,
                                   &server_config.db_cache_size_bytes);
    /* this should never fail */
    assert(ret == 0);
    ret = trove_collection_setinfo(0, 0, TROVE_MAX_CONCURRENT_IO,
                                   &server_config.trove_max_concurrent_io);
    /* this should never fail */
    assert(ret == 0);

    /* help trove chose a differentiating shm key if needed for Berkeley DB */
    shm_key_hint = generate_shm_key_hint(&server_index);
    gossip_debug(GOSSIP_SERVER_DEBUG, "Server using shm key hint: %d\n", shm_key_hint);
    ret = trove_collection_setinfo(0, 0, TROVE_SHM_KEY_HINT, &shm_key_hint);
    assert(ret == 0);

    if(server_config.db_cache_type && (!strcmp(server_config.db_cache_type,
                                               "mmap")))
    {
        /* set db cache type to mmap rather than sys */
        init_flags |= TROVE_DB_CACHE_MMAP;
    }

    /* Set the buffer size according to configuration file */
    BMI_set_info(0, BMI_TCP_BUFFER_SEND_SIZE, 
                 (void *)&server_config.tcp_buffer_size_send);
    BMI_set_info(0, BMI_TCP_BUFFER_RECEIVE_SIZE, 
                 (void *)&server_config.tcp_buffer_size_receive);

    ret = trove_initialize(
        server_config.trove_method, 
        trove_coll_to_method_callback,
        server_config.storage_path,
        init_flags);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: trove_initialize", ret);

        gossip_err("\n***********************************************\n");
        gossip_err("Invalid Storage Space: %s\n\n",
                   server_config.storage_path);
        gossip_err("Storage initialization failed.  The most "
                   "common reason\nfor this is that the storage space "
                   "has not yet been\ncreated or is located on a "
                   "partition that has not yet\nbeen mounted.  "
                   "If you'd like to create the storage space,\n"
                   "re-run this program with a -f option.\n");
        gossip_err("\n***********************************************\n");
        return ret;
    }

    *server_status_flag |= SERVER_TROVE_INIT;

    ret = PINT_cached_config_initialize();
    if(ret < 0)
    {
        gossip_err("Error initializing cached_config interface.\n");
        return(ret);
    }

    /* initialize the flow interface */
    ret = PINT_flow_initialize(server_config.flow_modules, 0);

    if (ret < 0)
    {
        PVFS_perror_gossip("Error: PINT_flow_initialize", ret);
        return ret;
    }

    *server_status_flag |= SERVER_FLOW_INIT;

    cur = server_config.file_systems;
    while(cur)
    {
        cur_fs = PINT_llist_head(cur);
        if (!cur_fs)
        {
            break;
        }

        ret = PINT_cached_config_handle_load_mapping(cur_fs);
        if(ret)
        {
            PVFS_perror("Error: PINT_handle_load_mapping", ret);
            return(ret);
        }

        /*
           set storage hints if any.  if any of these fail, we
           can't error out since they're just hints.  thus, we
           complain in logging and continue.
           */
        ret = trove_collection_setinfo(
            cur_fs->coll_id, 0,
            TROVE_DIRECTIO_THREADS_NUM,
            (void *)&cur_fs->directio_thread_num);
        if (ret < 0)
        {
            gossip_err("Error setting directio threads num\n");
        }

        ret = trove_collection_setinfo(
            cur_fs->coll_id, 0,
            TROVE_DIRECTIO_OPS_PER_QUEUE,
            (void *)&cur_fs->directio_ops_per_queue);
        if (ret < 0)
        {
            gossip_err("Error setting directio ops per queue\n");
        }

        ret = trove_collection_setinfo(
            cur_fs->coll_id, 0,
            TROVE_DIRECTIO_TIMEOUT,
            (void *)&cur_fs->directio_timeout);
        if (ret < 0)
        {
            gossip_err("Error setting directio threads num\n");
        }

        orig_fsid = cur_fs->coll_id;
        ret = trove_collection_lookup(
            cur_fs->trove_method,
            cur_fs->file_system_name, &(cur_fs->coll_id), NULL, NULL);

        if (ret < 0)
        {
            gossip_err("Error initializing filesystem %s\n",
                        cur_fs->file_system_name);
            return ret;
        }

        if(orig_fsid != cur_fs->coll_id)
        {
            gossip_err("Error: configuration file does not match storage collection.\n");
            gossip_err("   config file fs_id: %d\n", (int)orig_fsid);
            gossip_err("   storage fs_id: %d\n", (int)cur_fs->coll_id);
            gossip_err("Warning: This most likely means that the configuration\n");
            gossip_err("   files have been regenerated without destroying and\n");
            gossip_err("   recreating the corresponding storage collection.\n");
            return(-PVFS_ENODEV);
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
         * error out if we're not configured to house either a meta or
         * data handle range at all.
         */
        if (!cur_merged_handle_range)
        {
            gossip_err("Error: Invalid handle range for host %s "
                        "(alias %s) specified in file system %s\n",
                        server_config.host_id,
                        PINT_config_get_host_alias_ptr(
                            &server_config, server_config.host_id),
                        cur_fs->file_system_name);
            return -1;
        }
        else
        {
            ret = trove_open_context(cur_fs->coll_id, &trove_context);
            if (ret < 0)
            {
                gossip_err("Error initializing trove context\n");
                return ret;
            }

            /*
              set storage hints if any.  if any of these fail, we
              can't error out since they're just hints.  thus, we
              complain in logging and continue.
            */
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context, 
                TROVE_COLLECTION_HANDLE_TIMEOUT,
                (void *)&cur_fs->handle_recycle_timeout_sec);
            if (ret < 0)
            {
                gossip_err("Error setting handle timeout\n");
            }

            if (cur_fs->attr_cache_keywords &&
                cur_fs->attr_cache_size &&
                cur_fs->attr_cache_max_num_elems)
            {
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_KEYWORDS,
                    (void *)cur_fs->attr_cache_keywords);
                if (ret < 0)
                {
                    gossip_err("Error setting attr cache keywords\n");
                }
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_SIZE,
                    (void *)&cur_fs->attr_cache_size);
                if (ret < 0)
                {
                    gossip_err("Error setting attr cache size\n");
                }
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_MAX_NUM_ELEMS,
                    (void *)&cur_fs->attr_cache_max_num_elems);
                if (ret < 0)
                {
                    gossip_err("Error setting attr cache max num elems\n");
                }
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_INITIALIZE,
                    (void *)0);
                if (ret < 0)
                {
                    gossip_err("Error initializing the attr cache\n");
                }
            }

            /*
              add configured merged handle range for this host/fs.
              NOTE: if the attr cache was properly configured above,
              this next setinfo may have the opportunity to cache
              a number of attributes on startup during an iterate.
            */
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_HANDLE_RANGES,
                (void *)cur_merged_handle_range);
            if (ret < 0)
            {
                gossip_err("Error adding handle range %s to "
                            "filesystem %s\n",
                            cur_merged_handle_range,
                            cur_fs->file_system_name);
                return ret;
            }

            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_COALESCING_HIGH_WATERMARK,
                (void *)&cur_fs->coalescing_high_watermark);
            if(ret < 0)
            {
                gossip_err("Error setting coalescing high watermark\n");
                return ret;
            }

            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_COALESCING_LOW_WATERMARK,
                (void *)&cur_fs->coalescing_low_watermark);
            if(ret < 0)
            {
                gossip_err("Error setting coalescing low watermark\n");
                return ret;
            }
            
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_META_SYNC_MODE,
                (void *)&cur_fs->trove_sync_meta);
            if(ret < 0)
            {
                gossip_err("Error setting coalescing low watermark\n");
                return ret;
            } 
            
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_IMMEDIATE_COMPLETION,
                (void *)&cur_fs->immediate_completion);
            if(ret < 0)
            {
                gossip_err("Error setting trove immediate completion\n");
                return ret;
            } 

            gossip_debug(GOSSIP_SERVER_DEBUG, "File system %s using "
                         "handles:\n\t%s\n", cur_fs->file_system_name,
                         cur_merged_handle_range);

            gossip_debug(GOSSIP_SERVER_DEBUG, "Sync on metadata update "
                         "for %s: %s\n", cur_fs->file_system_name,
                         ((cur_fs->trove_sync_meta == TROVE_SYNC) ?
                          "yes" : "no"));

            gossip_debug(GOSSIP_SERVER_DEBUG, "Sync on I/O data update "
                         "for %s: %s\n", cur_fs->file_system_name,
                         ((cur_fs->trove_sync_data == TROVE_SYNC) ?
                          "yes" : "no"));

            gossip_debug(GOSSIP_SERVER_DEBUG, "Export options for "
                         "%s:\n RootSquash %s\n AllSquash %s\n ReadOnly %s\n"
                         " AnonUID %u\n AnonGID %u\n", cur_fs->file_system_name,
                         (cur_fs->exp_flags & TROVE_EXP_ROOT_SQUASH) ? "yes" : "no",
                         (cur_fs->exp_flags & TROVE_EXP_ALL_SQUASH)  ? "yes" : "no",
                         (cur_fs->exp_flags & TROVE_EXP_READ_ONLY)   ? "yes" : "no",
                         cur_fs->exp_anon_uid, cur_fs->exp_anon_gid);

            /* format and pass sync mode to the flow implementation */
            snprintf(buf, 16, "%d,%d", cur_fs->coll_id,
                     cur_fs->trove_sync_data);
            PINT_flow_setinfo(NULL, FLOWPROTO_DATA_SYNC_MODE, buf);

            trove_close_context(cur_fs->coll_id, trove_context);
            free(cur_merged_handle_range);
        }

        cur = PINT_llist_next(cur);
    }

    *server_status_flag |= SERVER_CACHED_CONFIG_INIT;

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "Storage Init Complete (%s)\n", SERVER_STORAGE_MODE);
    gossip_debug(GOSSIP_SERVER_DEBUG, "%d filesystem(s) initialized\n",
                 PINT_llist_count(server_config.file_systems));

    /*
     * Migrate database if needed
     */
    ret = trove_migrate(server_config.trove_method,server_config.storage_path);
    if (ret < 0)
    {
        gossip_err("trove_migrate failed: ret=%d\n", ret);
        return(ret);
    }

    ret = job_time_mgr_init();
    if(ret < 0)
    {
        PVFS_perror_gossip("Error: job_time_mgr_init", ret);
        return(ret);
    }

    *server_status_flag |= SERVER_JOB_TIME_MGR_INIT;

    /* initialize Job Interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: job_initialize", ret);
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

    ret = PINT_req_sched_initialize();
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: PINT_req_sched_intialize", ret);
        return ret;
    }
    *server_status_flag |= SERVER_REQ_SCHED_INIT;

#ifndef __PVFS2_DISABLE_PERF_COUNTERS__
    PINT_server_pc = PINT_perf_initialize(server_keys);
    if(!PINT_server_pc)
    {
        gossip_err("Error initializing performance counters.\n");
        return(ret);
    }
    *server_status_flag |= SERVER_PERF_COUNTER_INIT;
#endif

    ret = precreate_pool_initialize(server_index);
    if (ret < 0)
    {
        gossip_err("Error initializing precreate pool.\n");
        return (ret);
    }

    *server_status_flag |= SERVER_PRECREATE_INIT;

    return ret;
}

static int server_setup_signal_handlers(void)
{
    struct sigaction new_action;
    struct sigaction ign_action;
    struct sigaction hup_action;
    hup_action.sa_sigaction = (void *)hup_sighandler;
    sigemptyset (&hup_action.sa_mask);
    hup_action.sa_flags = SA_RESTART | SA_SIGINFO;
#ifdef __PVFS2_SEGV_BACKTRACE__
    struct sigaction segv_action;

    segv_action.sa_sigaction = (void *)bt_sighandler;
    sigemptyset (&segv_action.sa_mask);
    segv_action.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONESHOT;
#endif

    /* Set up the structure to specify the new action. */
    new_action.sa_handler = server_sig_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    ign_action.sa_handler = SIG_IGN;
    sigemptyset (&ign_action.sa_mask);
    ign_action.sa_flags = 0;

    /* catch these */
    sigaction (SIGILL, &new_action, NULL);
    sigaction (SIGTERM, &new_action, NULL);
    sigaction (SIGHUP, &hup_action, NULL);
    sigaction (SIGINT, &new_action, NULL);
    sigaction (SIGQUIT, &new_action, NULL);
#ifdef __PVFS2_SEGV_BACKTRACE__
    sigaction (SIGSEGV, &segv_action, NULL);
#else
    sigaction (SIGSEGV, &new_action, NULL);
#endif

    /* ignore these */
    sigaction (SIGPIPE, &ign_action, NULL);
    sigaction (SIGUSR1, &ign_action, NULL);
    sigaction (SIGUSR2, &ign_action, NULL);

    return 0;
}

#ifdef __PVFS2_SEGV_BACKTRACE__

#if defined(REG_EIP)
#  define REG_INSTRUCTION_POINTER REG_EIP
#elif defined(REG_RIP)
#  define REG_INSTRUCTION_POINTER REG_RIP
#else
#  error Unknown instruction pointer location for your architecture, configure without --enable-segv-backtrace.
#endif

/* bt_signalhandler()
 *
 * prints a stack trace from a signal handler; code taken from a Linux
 * Journal article
 *
 * no return value
 */
static void bt_sighandler(int sig, siginfo_t *info, void *secret)
{
    void *trace[16];
    char **messages = (char **)NULL;
    int i, trace_size = 0;
    ucontext_t *uc = (ucontext_t *)secret;

    /* Do something useful with siginfo_t */
    if (sig == SIGSEGV)
    {
        gossip_err("PVFS2 server: signal %d, faulty address is %p, " 
            "from %p\n", sig, info->si_addr, 
            (void*)uc->uc_mcontext.gregs[REG_INSTRUCTION_POINTER]);
    }
    else
    {
        gossip_err("PVFS2 server: signal %d\n", sig);
    }

    trace_size = backtrace(trace, 16);
    /* overwrite sigaction with caller's address */
    trace[1] = (void *) uc->uc_mcontext.gregs[REG_INSTRUCTION_POINTER];

    messages = backtrace_symbols(trace, trace_size);
    /* skip first stack frame (points here) */
    for (i=1; i<trace_size; ++i)
        gossip_err("[bt] %s\n", messages[i]);

    signal_recvd_flag = sig;
    return;
}
#endif

/* hup_signalhandler()
 *
 * Reload mutable configuration values. If there are errors, leave server in
 * a running state.
 *
 * NOTE: this _only_ reloads configuration values related to squashing,
 * readonly, and trusted settings.  It does not allow reloading of arbitrary
 * configuration file settings.
 *
 * no return value
 */
static void hup_sighandler(int sig, siginfo_t *info, void *secret)
{
    uint64_t debug_mask;
    int debug_on;

    /* Let's make sure this message is printed out */
    gossip_get_debug_mask(&debug_on, &debug_mask); /* Need to set back later */
    gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG); /* Make sure debug set */
    gossip_debug(GOSSIP_SERVER_DEBUG, "PVFS2 received server: signal %d\n", sig);
    gossip_set_debug_mask(debug_on, debug_mask); /* Set to original values */

    /* Set the flag so the next server loop picks it up and reloads config */
    signal_recvd_flag = sig;
}

static void reload_config(void)
{
    struct server_configuration_s sighup_server_config;
    PINT_llist *orig_filesystems = NULL;
    PINT_llist *hup_filesystems  = NULL;
    struct filesystem_configuration_s *orig_fs;
    struct filesystem_configuration_s *hup_fs;
    int tmp_value = 0;
    char **tmp_ptr = NULL;
    int *tmp_int_ptr = NULL;

    /* We received a SIGHUP. Update configuration in place */
    if (PINT_parse_config(&sighup_server_config, fs_conf, s_server_options.server_alias) < 0)
    {
        gossip_err("Error: Please check your config files.\n");
        gossip_err("Error: SIGHUP unable to update configuration.\n");
        PINT_config_release(&sighup_server_config); /* Free memory */
    }
    else /* Successful load of config */
    {
        orig_filesystems = server_config.file_systems;
        /* Loop and update all stored file systems */
        while(orig_filesystems)
        {
            int found_matching_config = 0;

            orig_fs = PINT_llist_head(orig_filesystems);
            if(!orig_fs)
            {
               break;
            }
            hup_filesystems = sighup_server_config.file_systems;

            /* Find the matching fs from sighup */
            while(hup_filesystems)
            {
                hup_fs = PINT_llist_head(hup_filesystems);
                if ( !hup_fs )
                {
                    break;
                }
                if( hup_fs->coll_id == orig_fs->coll_id )
                {
                    found_matching_config = 1;
                    break;
                }
                hup_filesystems = PINT_llist_head(hup_filesystems);
            }
            if(!found_matching_config)
            {
                gossip_err("Error: SIGHUP unable to update configuration"
                           "Matching configuration not found.\n");
                break;
            }
            /* Update root squashing. Prelude is only place to accesses
             * these values, so no need to lock around them. Swap the
             * needed pointers so that server config gets new values,
             * and the old values get freed up
            */
            orig_fs->exp_flags = hup_fs->exp_flags;

            tmp_value = orig_fs->root_squash_count;
            orig_fs->root_squash_count = hup_fs->root_squash_count;
            hup_fs->root_squash_count = tmp_value;

            tmp_ptr = orig_fs->root_squash_hosts;
            orig_fs->root_squash_hosts = hup_fs->root_squash_hosts;
            hup_fs->root_squash_hosts = tmp_ptr;

            tmp_int_ptr = orig_fs->root_squash_netmasks;
            orig_fs->root_squash_netmasks = hup_fs->root_squash_netmasks;
            hup_fs->root_squash_netmasks = tmp_int_ptr;

            tmp_value = orig_fs->root_squash_exceptions_count;
            orig_fs->root_squash_exceptions_count = hup_fs->root_squash_exceptions_count;
            hup_fs->root_squash_exceptions_count = tmp_value;

            tmp_ptr = orig_fs->root_squash_exceptions_hosts;
            orig_fs->root_squash_exceptions_hosts = hup_fs->root_squash_exceptions_hosts;
            hup_fs->root_squash_exceptions_hosts = tmp_ptr;

            tmp_int_ptr = orig_fs->root_squash_exceptions_netmasks;
            orig_fs->root_squash_exceptions_netmasks = hup_fs->root_squash_exceptions_netmasks;
            hup_fs->root_squash_exceptions_netmasks = tmp_int_ptr;

            /* Update all squashing. Prelude is only place to accesses
             * these values, so no need to lock around them. Swap
             * pointers so that server config gets new values, and
             * the old values get freed up
             */
            tmp_value = orig_fs->all_squash_count;
            orig_fs->all_squash_count = hup_fs->all_squash_count;
            hup_fs->all_squash_count = tmp_value;

            tmp_ptr = orig_fs->all_squash_hosts;
            orig_fs->all_squash_hosts = hup_fs->all_squash_hosts;
            hup_fs->all_squash_hosts = tmp_ptr;

            tmp_int_ptr = orig_fs->all_squash_netmasks;
            orig_fs->all_squash_netmasks = hup_fs->all_squash_netmasks;
            hup_fs->all_squash_netmasks = tmp_int_ptr;

            /* Update read only. Prelude is only place to accesses
             * these values, so no need to lock around them. Swap
             * pointers so that server config gets new values, and
             * the old values get freed up
             */
            tmp_value = orig_fs->ro_count;
            orig_fs->ro_count = hup_fs->ro_count;
            hup_fs->ro_count = tmp_value;

            tmp_ptr = orig_fs->ro_hosts;
            orig_fs->ro_hosts = hup_fs->ro_hosts;
            hup_fs->ro_hosts = tmp_ptr;

            tmp_int_ptr = orig_fs->ro_netmasks;
           orig_fs->ro_netmasks = hup_fs->ro_netmasks;
            hup_fs->ro_netmasks = tmp_int_ptr;

            orig_fs->exp_anon_uid = hup_fs->exp_anon_uid;
            orig_fs->exp_anon_gid = hup_fs->exp_anon_gid;

            orig_filesystems = PINT_llist_next(orig_filesystems);
        }
#ifdef USE_TRUSTED
        server_config.ports_enabled = sighup_server_config.ports_enabled;
        server_config.allowed_ports[0] = sighup_server_config.allowed_ports[0];
        server_config.allowed_ports[1] = sighup_server_config.allowed_ports[1];
        server_config.network_enabled = sighup_server_config.network_enabled;

        tmp_value = server_config.allowed_networks_count;
        server_config.allowed_networks_count = sighup_server_config.allowed_networks_count;
        sighup_server_config.allowed_networks_count = tmp_value;

        tmp_ptr = server_config.allowed_networks;
        server_config.allowed_networks = sighup_server_config.allowed_networks;
        sighup_server_config.allowed_networks = tmp_ptr;

        tmp_int_ptr = server_config.allowed_masks;
        server_config.allowed_masks = sighup_server_config.allowed_masks;
        sighup_server_config.allowed_masks = tmp_int_ptr;

        /* security and security_dtor will be updated in a call
         * to BMI_set_info. Need to save old values so they are
         * deleted on cleanup
         */
        sighup_server_config.security = server_config.security;
        sighup_server_config.security_dtor = server_config.security_dtor;

        /* The set_info call grabs the interface_mutex, so we are
         * basically using that to lock this resource
         */
        BMI_set_info(0, BMI_TRUSTED_CONNECTION, (void *) &server_config);
#endif
        PINT_config_release(&sighup_server_config); /* Free memory */
    }
}

static int server_shutdown(
    PINT_server_status_flag status,
    int ret, int siglevel)
{
    if (siglevel == SIGSEGV)
    {
        gossip_err("SIGSEGV: skipping cleanup; exit now!\n");
        exit(EXIT_FAILURE);
    }

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "*** server shutdown in progress ***\n");

    if (status & SERVER_PRECREATE_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting precreate pool "
                     "           [   ...   ]\n");
        precreate_pool_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         precreate pool "
                     "           [ stopped ]\n");
    }

    if (status & SERVER_STATE_MACHINE_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting state machine "
                     "processor   [   ...   ]\n");
        PINT_state_machine_halt();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         state machine "
                     "processor   [ stopped ]\n");
    }

    if (status & SERVER_CACHED_CONFIG_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting cached "
                     "config interface   [   ...   ]\n");
        PINT_cached_config_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         cached "
                     "config interface   [ stopped ]\n");
    }

    if (status & SERVER_EVENT_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting event "
                     "profiling interface [   ...   ]\n");
        PINT_event_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         event "
                     "profiling interface [ stopped ]\n");
    }

    if (status & SERVER_REQ_SCHED_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting request "
                     "scheduler         [   ...   ]\n");
        PINT_req_sched_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         request "
                     "scheduler         [ stopped ]\n");
    }
        
    if (status & SERVER_JOB_CTX_INIT)
    {
        job_close_context(server_job_context);
    }

    if (status & SERVER_JOB_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting job "
                     "interface             [   ...   ]\n");
        job_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         job "
                     "interface             [ stopped ]\n");
    }

    if (status & SERVER_JOB_TIME_MGR_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting job time "
                     "mgr interface    [   ...   ]\n");
        job_time_mgr_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         job time "
                     "mgr interface    [ stopped ]\n");
    }

    if (status & SERVER_FLOW_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting flow "
                     "interface            [   ...   ]\n");
        PINT_flow_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         flow "
                     "interface            [ stopped ]\n");
    }

    if (status & SERVER_BMI_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting bmi "
                     "interface             [   ...   ]\n");
        BMI_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         bmi "
                     "interface             [ stopped ]\n");
    }

    if (status & SERVER_TROVE_INIT)
    {
        PINT_llist *cur;
        struct filesystem_configuration_s *cur_fs;
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting storage "
                     "interface         [   ...   ]\n");

        cur = server_config.file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            trove_collection_clear(cur_fs->trove_method, cur_fs->coll_id);

            cur = PINT_llist_next(cur);
        }

        trove_finalize(server_config.trove_method);
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         storage "
                     "interface         [ stopped ]\n");
    }

    if (status & SERVER_ENCODER_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting encoder "
                     "interface         [   ...   ]\n");
        PINT_encode_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         encoder "
                     "interface         [ stopped ]\n");
    }

    if (status & SERVER_DIST_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting dist "
                     "interface            [   ...   ]\n");
        PINT_dist_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         dist "
                     "interface            [ stopped ]\n");
    }

    if (status & SERVER_PERF_COUNTER_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting performance "
                     "interface     [   ...   ]\n");
        PINT_perf_finalize(PINT_server_pc);
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         performance "
                     "interface     [ stopped ]\n");
    }

    if (status & SERVER_GOSSIP_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "[*] halting logging interface\n");
        gossip_disable();
    }

    if (status & SERVER_CONFIG_INIT)
    {
        PINT_config_release(&server_config);
    }

    if (status & SERVER_JOB_OBJS_ALLOCATED)
    {
        free(server_job_id_array);
        free(server_completed_job_p_array);
        free(server_job_status_array);
    }

    if(siglevel == 0 && ret != 0)
    {
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

static void server_sig_handler(int sig)
{
    struct sigaction new_action;

    if (getpid() == server_controlling_pid)
    {
        if (sig != SIGSEGV)
        {
            gossip_err("\nPVFS2 server got signal %d "
                       "(server_status_flag: %d)\n",
                       sig, (int)server_status_flag);
        }

        /* ignore further invocations of this signal */
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction (sig, &new_action, NULL);

        /* set the signal_recvd_flag on critical errors to cause the
         * server to exit gracefully on the next work cycle
         */
        signal_recvd_flag = sig;
        /*
         * iterate through all the machines that we had posted for
         * unexpected BMI messages and deallocate them.
         * From now the server will only try and finish operations
         * that are already in progress, wait for them to timeout
         * or complete before initiating shutdown
         */
        server_purge_unexpected_recv_machines();
    }
}

static void usage(int argc, char **argv)
{
    gossip_err("Usage: %s: [OPTIONS] <global_config_file> "
               "\n\n", argv[0]);
    gossip_err("  -d, --foreground\t"
               "will keep server in the foreground\n");
    gossip_err("  -f, --mkfs\t\twill cause server to "
               "create file system storage and exit\n");
    gossip_err("  -h, --help\t\twill show this message\n");
    gossip_err("  -r, --rmfs\t\twill cause server to "
               "remove file system storage and exit\n");
    gossip_err("  -v, --version\t\toutput version information "
               "and exit\n");
    gossip_err("  -p, --pidfile <file>\twrite process id to file\n");
    gossip_err("  -a, --alias <alias>\tuse the specified alias for this node\n");
}

static int server_parse_cmd_line_args(int argc, char **argv)
{
    int ret = 0, option_index = 0;
    int total_arguments = 0;
    const char *cur_option = NULL;
    static struct option long_opts[] =
    {
        {"foreground",0,0,0},
        {"mkfs",0,0,0},
        {"help",0,0,0},
        {"rmfs",0,0,0},
        {"version",0,0,0},
        {"pidfile",1,0,0},
        {"alias",0,0,0},
        {0,0,0,0}
    };

    while ((ret = getopt_long(argc, argv,"dfhrvp:a:e",
                              long_opts, &option_index)) != -1)
    {
        total_arguments++;
        switch (ret)
        {
            case 0:
                cur_option = long_opts[option_index].name;
                assert(cur_option);

                if (strcmp("foreground", cur_option) == 0)
                {
                    goto do_foreground;
                }
                else if (strcmp("mkfs", cur_option) == 0)
                {
                    goto do_mkfs;
                }
                else if (strcmp("help", cur_option) == 0)
                {
                    goto do_help;
                }
                else if (strcmp("rmfs", cur_option) == 0)
                {
                    goto do_rmfs;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    goto do_version;
                }
                else if (strcmp("pidfile", cur_option) == 0)
                {
                    goto do_pidfile;
                }
                else if (strcmp("alias", cur_option) == 0)
                {
                    goto do_alias;
                }
                break;
            case 'v':
          do_version:
                printf("%s (mode: %s)\n", PVFS2_VERSION,
                       SERVER_STORAGE_MODE);
                return PVFS2_VERSION_REQUEST;
            case 'r':
          do_rmfs:
                s_server_options.server_remove_storage_space = 1;
            case 'f':
          do_mkfs:
                s_server_options.server_create_storage_space = 1;
                break;
            case 'd':
          do_foreground:
                s_server_options.server_background = 0;
                break;
            case 'p':
          do_pidfile:
                total_arguments++;
                s_server_options.pidfile = optarg;
                if(optarg[0] != '/')
                {
                    gossip_err("Error: pidfile must be specified with an absolute path.\n");
                    goto parse_cmd_line_args_failure;
                }
                break;
            case 'a':
          do_alias:
                total_arguments++;
                s_server_options.server_alias = strdup(optarg);
                break;
            case '?':
            case 'h':
          do_help:
            default:
          parse_cmd_line_args_failure:
                usage(argc, argv);
                if(s_server_options.server_alias)
                {
                    free(s_server_options.server_alias);
                }
                return 1;
        }
    }

    if(argc < optind)
    {
        gossip_err("Missing config file in command line arguments\n");
        goto parse_cmd_line_args_failure;
    }

    fs_conf = argv[optind++];

    if(argc - total_arguments > 2)
    {
        /* Assume user is passing in a server.conf.  Bit of a hack here to
         * support server.conf files in the old format by appending the
         * server.conf options onto the fs.conf.
         */
        gossip_err("The two config file format is no longer supported.  "
                   "Generate a single fs.conf that uses the new format with the "
                   "pvfs2-config-convert script.\n\n");
        goto parse_cmd_line_args_failure;
    }

    if (s_server_options.server_alias == NULL)
    {
        /* Try to guess the alias from the hostname */
        s_server_options.server_alias = PINT_util_guess_alias();
    }
    return 0;
}

/* server_post_unexpected_recv()
 *
 * Allocates space for an unexpected BMI message and posts this.
 *
 * Returns 0 on success, -PVFS_error on failure.
 */
int server_post_unexpected_recv(job_status_s *js_p)
{
    int ret = -PVFS_EINVAL;
    /* job_id_t j_id; */
    struct PINT_smcb *smcb = NULL;
    struct PINT_server_op *s_op;

    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_post_unexpected_recv\n");

    if (js_p)
    {
        ret = PINT_smcb_alloc(&smcb, BMI_UNEXPECTED_OP,
                sizeof(struct PINT_server_op),
                server_op_state_get_machine,
                server_state_machine_terminate,
                server_job_context);
        if (ret < 0)
        {
            gossip_lerr("Error: failed to allocate SMCB "
                        "of op type %x\n", BMI_UNEXPECTED_OP);
            return ret;
        }
        s_op = (struct PINT_server_op *)PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
        memset(s_op, 0, sizeof(PINT_server_op));
        s_op->op = BMI_UNEXPECTED_OP;
        s_op->target_handle = PVFS_HANDLE_NULL;
        s_op->target_fs_id = PVFS_FS_ID_NULL;
        /* Add an unexpected s_ops to the list */
        qlist_add_tail(&s_op->next, &posted_sop_list);

        ret = PINT_state_machine_start(smcb, js_p);
        if(ret == SM_ACTION_TERMINATE)
        {
            /* error posting unexpected */
            PINT_smcb_free(smcb);
            return js_p->error_code;
        }
    }
    return ret;
}

/* server_purge_unexpected_recv_machines()
 *
 * removes any s_ops that were posted to field unexpected BMI messages
 *
 * returns 0 on success and -PVFS_errno on failure.
 */
static int server_purge_unexpected_recv_machines(void)
{
    struct qlist_head *tmp = NULL, *tmp2 = NULL;

    if (qlist_empty(&posted_sop_list))
    {
        gossip_err("WARNING: Found empty posted operation list!\n");
        return -PVFS_EINVAL;
    }
    qlist_for_each_safe (tmp, tmp2, &posted_sop_list)
    {
        PINT_server_op *s_op = qlist_entry(tmp, PINT_server_op, next);

        /* Remove s_op from the posted_sop_list */
        /* don't see a reason to remove this */
        /* will be removed in state machine */
        /* if and when message completes after cancellation */
        /* qlist_del(&s_op->next); */

        /* mark the message for cancellation */
        s_op->op_cancelled = 1;

        /* cancel the pending job_bmi_unexp operation */
        job_bmi_unexp_cancel(s_op->unexp_id);
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
int server_state_machine_start(
    PINT_smcb *smcb,
    job_status_s *js_p)
{
    PINT_server_op *s_op = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    int ret = -PVFS_EINVAL;
    PVFS_id_gen_t tmp_id;

    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_state_machine_start %p\n",smcb);

    ret = PINT_decode(s_op->unexp_bmi_buff.buffer,
                      PINT_DECODE_REQ,
                      &s_op->decoded,
                      s_op->unexp_bmi_buff.addr,
                      s_op->unexp_bmi_buff.size);

    /* acknowledge that the unexpected buffer has been used up.
     * If *someone* decides to do in-place decoding, then we will have to move
     * this back to state_machine_complete().
     */
    if (ret == -PVFS_EPROTONOSUPPORT)
    {
        /* we have a protocol mismatch of some sort; try to trigger a
         * response that gives a helpful error on client side even
         * though we can't interpret what the client was asking for
         */
        ret = PINT_smcb_set_op(smcb, PVFS_SERV_PROTO_ERROR);
    }
    else if (ret == 0)
    {
        s_op->req  = (struct PVFS_server_req *)s_op->decoded.buffer;
        ret = PINT_smcb_set_op(smcb, s_op->req->op);
        s_op->op = s_op->req->op;
        PVFS_hint_add(&s_op->req->hints, PVFS_HINT_SERVER_ID_NAME, sizeof(uint32_t), &server_config.host_index);
        PVFS_hint_add(&s_op->req->hints, PVFS_HINT_OP_ID_NAME, sizeof(uint32_t), &s_op->req->op);
    }
    else
    {
        PVFS_perror_gossip("Error: PINT_decode failure", ret);
        return ret;
    }
    /* Remove s_op from posted_sop_list and move it to the inprogress_sop_list */
    qlist_del(&s_op->next);
    qlist_add_tail(&s_op->next, &inprogress_sop_list);

    /* set timestamp on the beginning of this state machine */
    id_gen_fast_register(&tmp_id, s_op);

    if(s_op->req)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "client:%d, reqid:%d, rank:%d\n",
                     PINT_HINT_GET_CLIENT_ID(s_op->req->hints),
                     PINT_HINT_GET_REQUEST_ID(s_op->req->hints),
                     PINT_HINT_GET_RANK(s_op->req->hints));
        PINT_EVENT_START(PINT_sm_event_id, server_controlling_pid,
                         NULL, &s_op->event_id,
                         PINT_HINT_GET_CLIENT_ID(s_op->req->hints),
                         PINT_HINT_GET_REQUEST_ID(s_op->req->hints),
                         PINT_HINT_GET_RANK(s_op->req->hints),
                         PINT_HINT_GET_HANDLE(s_op->req->hints),
                         s_op->req->op);
        s_op->resp.op = s_op->req->op;
    }

    s_op->addr = s_op->unexp_bmi_buff.addr;
    s_op->tag  = s_op->unexp_bmi_buff.tag;

    if (!ret)
    {
        gossip_err("Error: server does not implement request type: %d\n",
                   (int)s_op->req->op);
        PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);
        return -PVFS_ENOSYS;
    }

    return PINT_state_machine_invoke(smcb, js_p);
}

/* server_state_machine_alloc_noreq()
 * 
 * allocates and initializes a server state machine that can later be
 * started with server_state_machine_start_noreq()
 *
 * returns 0 on success, -PVFS_error on failure
 */
int server_state_machine_alloc_noreq(
    enum PVFS_server_op op,
    struct PINT_smcb **new_op)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_state_machine_alloc_noreq %d\n",op);

    if (new_op)
    {
        PINT_server_op *tmp_op;
        ret = PINT_smcb_alloc(new_op, op, 
                sizeof(struct PINT_server_op),
                server_op_state_get_machine,
                server_state_machine_terminate,
                server_job_context);
        if (ret < 0)
        {
            gossip_lerr("Error: failed to allocate SMCB "
                        "of op type %x\n", op);
            return ret;
        }
        tmp_op = PINT_sm_frame(*new_op, PINT_FRAME_CURRENT);
        tmp_op->op = op;
        tmp_op->target_handle = PVFS_HANDLE_NULL;
        tmp_op->target_fs_id = PVFS_FS_ID_NULL;

        /* NOTE: We do not add these state machines to the 
         * in-progress or posted sop lists 
         */

        ret = 0;
    }
    return ret;
}

/* server_state_machine_start_noreq()
 * 
 * similar in purpose to server_state_machine_start(), except that it
 * kicks off a state machine instance without first receiving a client
 * side request
 *
 * PINT_server_op structure must have been previously allocated using
 * server_state_machine_alloc_noreq().
 *
 * returns 0 on success, -PVFS_error on failure
 */
int server_state_machine_start_noreq(struct PINT_smcb *smcb)
{
    struct PINT_server_op *new_op = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    int ret = -PVFS_EINVAL;
    job_status_s tmp_status;

    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_state_machine_start_noreq %p\n",smcb);

    tmp_status.error_code = 0;

    if (new_op)
    {

        /* add to list of state machines started without a request */
        qlist_add_tail(&new_op->next, &noreq_sop_list);

        /* execute first state */
        ret = PINT_state_machine_start(smcb, &tmp_status);
        if (ret < 0)
        {
            gossip_lerr("Error: failed to start state machine.\n");
            return ret;
        }
    }
    return ret;
}

/* server_state_machine_complete()
 *
 * function to be called at the completion of state machine execution;
 * it frees up any resources associated with the state machine that were
 * allocated before the state machine started executing.  Also returns
 * appropriate return value to make the state machine stop transitioning
 *
 * returns 0
 */
int server_state_machine_complete(PINT_smcb *smcb)
{
    PINT_server_op *s_op = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    PVFS_id_gen_t tmp_id;

    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_state_machine_complete %p\n",smcb);

    /* set a timestamp on the completion of the state machine */
    id_gen_fast_register(&tmp_id, s_op);

    if(s_op->req)
    {
        PINT_EVENT_END(PINT_sm_event_id, server_controlling_pid,
                       NULL, s_op->event_id, 0);
    }

    /* release the decoding of the unexpected request */
    if (ENCODING_IS_VALID(s_op->decoded.enc_type))
    {
        PVFS_hint_free(s_op->decoded.stub_dec.req.hints);

        PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);
    }

    BMI_set_info(s_op->unexp_bmi_buff.addr, BMI_DEC_ADDR_REF, NULL);
    BMI_unexpected_free(s_op->unexp_bmi_buff.addr, 
                        s_op->unexp_bmi_buff.buffer);
    s_op->unexp_bmi_buff.buffer = NULL;


   /* Remove s_op from the inprogress_sop_list */
    qlist_del(&s_op->next);

    return SM_ACTION_TERMINATE;
}

int server_state_machine_terminate(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    /* free the operation structure itself */
    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_state_machine_terminate %p\n",smcb);
    PINT_smcb_free(smcb);
    return SM_ACTION_TERMINATE;
}

struct server_configuration_s *get_server_config_struct(void)
{
    return &server_config;
}

/* server_op_get_machine()
 * 
 * looks up the state machine for the op * given and returns it, or
 * NULL of the op is out of range.
 * pointer to this function set in the control block of server state
 * machines.
 */
struct PINT_state_machine_s *server_op_state_get_machine(int op)
{
    gossip_debug(GOSSIP_SERVER_DEBUG,
            "server_op_state_get_machine %d\n",op);

    switch (op)
    {
    case BMI_UNEXPECTED_OP :
        {
            return &pvfs2_unexpected_sm;
            break;
        }
    default :
        {
            if (op >= 0 && op < PVFS_SERV_NUM_OPS)
                return PINT_server_req_table[op].params->state_machine;
            else
                return NULL;
            break;
        }
    }
}

static TROVE_method_id trove_coll_to_method_callback(TROVE_coll_id coll_id)
{
    struct filesystem_configuration_s * fs_config;

    fs_config = PINT_config_find_fs_id(&server_config, coll_id);
    if(!fs_config)
    {
        return server_config.trove_method;
    }
    return fs_config->trove_method;
}

#ifndef GOSSIP_DISABLE_DEBUG
void PINT_server_access_debug(PINT_server_op * s_op,
                              int64_t debug_mask,
                              const char * format,
                              ...)
{
    static char pint_access_buffer[GOSSIP_BUF_SIZE];
    struct passwd* pw;
    struct group* gr;
    va_list ap;

    if ((gossip_debug_on) &&
        (gossip_debug_mask & debug_mask) &&
        (gossip_facility))
    {
        va_start(ap, format);

        pw = getpwuid(s_op->req->credentials.uid);
        gr = getgrgid(s_op->req->credentials.gid);
        snprintf(pint_access_buffer, GOSSIP_BUF_SIZE,
            "%s.%s@%s H=%llu S=%p: %s: %s",
            ((pw) ? pw->pw_name : "UNKNOWN"),
            ((gr) ? gr->gr_name : "UNKNOWN"),
            BMI_addr_rev_lookup_unexpected(s_op->addr),
            llu(s_op->target_handle),
            s_op,
            PINT_map_server_op_to_string(s_op->req->op),
            format);

        __gossip_debug_va(debug_mask, 'A', pint_access_buffer, ap);

        va_end(ap);
    }
}
#endif

/* generate_shm_key_hint()
 *
 * Makes a best effort to produce a unique shm key (for Trove's Berkeley
 * DB use) for each server.  By default it will base this on the server's
 * position in the fs.conf, but it will fall back to using a random number
 *
 * returns integer key
 */
static int generate_shm_key_hint(int* server_index)
{
    struct host_alias_s *cur_alias = NULL;
    struct filesystem_configuration_s *first_fs;

    *server_index = 1;

    PINT_llist *cur = server_config.host_aliases;

    /* iterate through list of aliases in configuration file */
    while(cur)
    {
        cur_alias = PINT_llist_head(cur);
        if(!cur_alias)
        {
            break;
        }
        if(strcmp(cur_alias->bmi_address, server_config.host_id) == 0)
        {
            /* match */
            /* space the shm keys out by 10 to allow for Berkeley DB using 
             * using more than one key on each server
             */
            first_fs = PINT_llist_head(server_config.file_systems);
            return(first_fs->coll_id + (*server_index)*10);
        }

        (*server_index)++;
        cur = PINT_llist_next(cur);
    }
    
    /* If we reach this point, we didn't find this server in the alias list.
     * This is not a normal situation, but fall back to using a random
     * number for the key just to be safe.
     */
    srand((unsigned int)time(NULL));
    return(rand());
}

/* precreate_pool_initialize()
 * 
 * starts the infrastructure for managing pools of precreated handles
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int precreate_pool_initialize(int server_index)
{
    PINT_llist *cur_f = server_config.file_systems;
    struct filesystem_configuration_s *cur_fs;
    int ret = -1;
    PVFS_handle pool_handle;
    int server_count;
    PVFS_BMI_addr_t* addr_array;
    const char* host;
    int i;
    int server_type;
    int handle_count = 0;
    int fs_count = 0;

    /* iterate through list of file systems */
    while(cur_f)
    {
        cur_fs = PINT_llist_head(cur_f);
        if (!cur_fs)
        {
            break;
        }

        fs_count++;

        /* am I a meta server in this file system? */
        ret = PINT_cached_config_check_type(
            cur_fs->coll_id,
            server_config.host_id,
            &server_type);
        if(ret < 0)
        {
            gossip_err("Error: %s not found in configuration file.\n", 
                server_config.host_id);
            gossip_err("Error: configuration file is inconsistent.\n");
            return(ret);
        }
        if(!(server_type & PINT_SERVER_TYPE_META))
        {
            /* This server is not a meta server for this file system; 
             * skip doing any precreate setup steps.
             */
            cur_f = PINT_llist_next(cur_f);
            continue;
        }

        /* how many I/O servers do we have? */
        ret = PINT_cached_config_count_servers(
            cur_fs->coll_id, PINT_SERVER_TYPE_IO, &server_count);
        if(ret < 0)
        {
            gossip_err("Error: unable to count servers for fsid: %d\n", 
                (int)cur_fs->coll_id);
            return(ret);
        }
        
        addr_array = malloc(server_count*sizeof(PVFS_BMI_addr_t));
        if(!addr_array)
        {
            gossip_err("Error: unable to allocate book keeping information for precreate pools.\n");
            return(-PVFS_ENOMEM);
        }

        /* resolve addrs for each I/O server */
        ret = PINT_cached_config_get_server_array(
            cur_fs->coll_id, PINT_SERVER_TYPE_IO,
            addr_array, &server_count);
        if(ret < 0)
        {
            gossip_err("Error: unable retrieve servers for fsid: %d\n", 
                (int)cur_fs->coll_id);
            return(ret);
        }

        for(i=0; i<server_count; i++)
        {
            host = PINT_cached_config_map_addr(
                cur_fs->coll_id, addr_array[i], &server_type);
            if(!strcmp(host, server_config.host_id) == 0)
            {
                /* this is a peer server */
                /* make sure a pool exists for that server,fsid pair */
                ret = precreate_pool_setup_server(host, 
                    cur_fs->coll_id, &pool_handle);
                if(ret < 0)
                {
                    gossip_err("Error: precreate_pool_initialize failed to setup pool for %s\n", server_config.host_id);
                    return(ret);
                }

                /* count current handles */
                ret = precreate_pool_count(cur_fs->coll_id, pool_handle, 
                    &handle_count);
                if(ret < 0)
                {
                    gossip_err("Error: precreate_pool_initialize failed to count pool for %s\n", server_config.host_id);
                    return(ret);
                }

                /* prepare the job interface to use this pool */
                ret = job_precreate_pool_register_server(host, 
                    cur_fs->coll_id, pool_handle, handle_count);
                assert(ret != 0);
                if(ret < 0)
                {
                    gossip_err("Error: precreate_pool_initialize failed to register pool for %s\n", server_config.host_id);
                    return(ret);
                }

                /* launch sm to take care of refilling */
                ret = precreate_pool_launch_refiller(host, addr_array[i],
                    cur_fs->coll_id, pool_handle);
                if(ret < 0)
                {
                    gossip_err("Error: precreate_pool_initialize failed to launch refiller SM for %s\n", server_config.host_id);
                    return(ret);
                }
            }
        }

        job_precreate_pool_set_index(server_index);

        cur_f = PINT_llist_next(cur_f);

    }

    return(0);
}

/* precreate_pool_finalize()
 *
 * shuts down infrastructure for managing pools of precreated handles
 */
static void precreate_pool_finalize(void)
{
    /* TODO: anything to do here? */
    /* TODO: maybe try to stop pending refiller sms? */
    return;
}

/* precreate_pool_setup_server()
 *  
 * This function makes sure that a pool is present for the specified server
 *
 */
static int precreate_pool_setup_server(const char* host, PVFS_fs_id fsid,
    PVFS_handle* pool_handle)
{
    job_status_s js;
    job_id_t job_id;
    int ret;
    int outcount;
    PVFS_handle_extent_array ext_array;

    PVFS_ds_keyval key;
    PVFS_ds_keyval val;

    /* look for the pool handle for this server */
    key.buffer_sz = strlen(host) + strlen("precreate-pool-") + 1;
    key.buffer = malloc(key.buffer_sz);
    if(!key.buffer)
    {
        return(-ENOMEM);
    }
    snprintf((char*)key.buffer, key.buffer_sz, "precreate-pool-%s", host);
    key.read_sz = 0;

    val.buffer = pool_handle;
    val.buffer_sz = sizeof(*pool_handle);
    val.read_sz = 0;

    ret = job_trove_fs_geteattr(fsid, &key, &val, 0, NULL, 0, &js, 
        &job_id, server_job_context, NULL);
    while(ret == 0)
    {
        ret = job_test(job_id, &outcount, NULL, &js, 
            PVFS2_SERVER_DEFAULT_TIMEOUT_MS, server_job_context);
    }
    if(ret < 0)
    {
        gossip_err("Error: precreate_pool failed to read fs eattrs.\n");
        free(key.buffer);
        return(ret);
    }
    if(js.error_code && js.error_code != -TROVE_ENOENT)
    {
        gossip_err("Error: precreate_pool failed to read fs eattrs.\n");
        free(key.buffer);
        return(js.error_code);
    }
    else if(js.error_code == -TROVE_ENOENT)
    {
        /* handle doesn't exist yet; let's create it */
        gossip_debug(GOSSIP_SERVER_DEBUG, "precreate_pool didn't find handle for %s; creating now.\n", host);

        /* find extent array for ourselves */
        ret = PINT_cached_config_get_server(
            fsid, server_config.host_id, PINT_SERVER_TYPE_META, &ext_array);
        if(ret < 0)
        {
            gossip_err("Error: PINT_cached_config_get_meta() failure.\n");
            free(key.buffer);
            return(ret);
        }

        /* create a trove object for the pool */
        ret = job_trove_dspace_create(fsid, &ext_array, PVFS_TYPE_INTERNAL,
            NULL, TROVE_SYNC, NULL, 0, &js, &job_id, server_job_context, NULL);
        while(ret == 0)
        {
            ret = job_test(job_id, &outcount, NULL, &js, 
                PVFS2_SERVER_DEFAULT_TIMEOUT_MS, server_job_context);
        }
        if(ret < 0 || js.error_code)
        {
            gossip_err("Error: precreate_pool failed to create pool.\n");
            free(key.buffer);
            return(ret < 0 ? ret : js.error_code);
        }

        *pool_handle = js.handle;

        /* store reference to pool handle as collection eattr */
        ret = job_trove_fs_seteattr(fsid, &key, &val, TROVE_SYNC, NULL, 0, &js, 
            &job_id, server_job_context, NULL);
        while(ret == 0)
        {
            ret = job_test(job_id, &outcount, NULL, &js, 
                PVFS2_SERVER_DEFAULT_TIMEOUT_MS, server_job_context);
        }
        if(ret < 0 || js.error_code)
        {
            gossip_err("Error: failed to record precreate pool handle.\n");
            gossip_err("Warning: fsck may be needed to recover lost handle.\n");
            free(key.buffer);
            return(ret < 0 ? ret : js.error_code);
        }
        gossip_debug(GOSSIP_SERVER_DEBUG, "precreate_pool created handle %llu for %s.\n", llu(*pool_handle), host);

    }
    else
    {
        /* handle already exists */
        gossip_debug(GOSSIP_SERVER_DEBUG, "precreate_pool found handle %llu for %s.\n", llu(*pool_handle), host);
    }

    free(key.buffer);
    return(0);
}

/* precreate_pool_count()
 *
 * counts the number of handles stored in a persistent precreate pool
 */
static int precreate_pool_count(
    PVFS_fs_id fsid, PVFS_handle pool_handle, int* count)
{
    int ret;
    job_status_s js;
    job_id_t job_id;
    int outcount;
    PVFS_ds_keyval_handle_info handle_info;

    /* try to get the current number of handles from the pool */
    ret = job_trove_keyval_get_handle_info(
        fsid, pool_handle, TROVE_KEYVAL_HANDLE_COUNT, &handle_info,
        NULL, 0, &js, &job_id, server_job_context, NULL);
    while(ret == 0)
    {
        ret = job_test(job_id, &outcount, NULL, &js, 
            PVFS2_SERVER_DEFAULT_TIMEOUT_MS, server_job_context);
    }
    if(ret < 0)
    {
        return(ret);
    }
    
    if(js.error_code == -TROVE_ENOENT)
    {
        /* this really means there aren't any keyvals there yet */
        handle_info.count = 0;
    }
    else if(js.error_code != 0)
    {
        return(js.error_code);
    }

    *count = handle_info.count;

    return(0);
}

static int precreate_pool_launch_refiller(const char* host, 
    PVFS_BMI_addr_t addr, PVFS_fs_id fsid, PVFS_handle pool_handle)
{
    struct PINT_smcb *tmp_smcb = NULL;
    struct PINT_server_op *s_op;
    int ret;

    /* allocate smcb */
    ret = server_state_machine_alloc_noreq(PVFS_SERV_PRECREATE_POOL_REFILLER,
        &(tmp_smcb));
    if (ret < 0)
    {
        return(ret);
    }

    s_op = PINT_sm_frame(tmp_smcb, PINT_FRAME_CURRENT);
    s_op->u.precreate_pool_refiller.host = strdup(host);
    if(!s_op->u.precreate_pool_refiller.host)
    {
        PINT_smcb_free(tmp_smcb);
        return(ret);
    }

    ret = PINT_cached_config_get_server(
        fsid, host, PINT_SERVER_TYPE_IO, 
        &s_op->u.precreate_pool_refiller.data_handle_extent_array);
    if(ret < 0)
    {
        free(s_op->u.precreate_pool_refiller.host);
        PINT_smcb_free(tmp_smcb);
        return(ret);
    }

    s_op->u.precreate_pool_refiller.pool_handle = pool_handle;
    s_op->u.precreate_pool_refiller.fsid = fsid;
    s_op->u.precreate_pool_refiller.host_addr = addr;

    /* start sm */
    ret = server_state_machine_start_noreq(tmp_smcb);
    if (ret < 0)
    {
        free(s_op->u.precreate_pool_refiller.host);
        PINT_smcb_free(tmp_smcb);
        return(ret);
    }

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

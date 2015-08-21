/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <errno.h>
#include <assert.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "pvfs2-internal.h"
#include "acache.h"
#include "ncache.h"
#include "rcache.h"
#include "client-capcache.h"
#include "pint-cached-config.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "pint-dist-utils.h"
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "PINT-reqproto-encode.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "client-state-machine.h"
#include "src/server/request-scheduler/request-scheduler.h"
#include "job-time-mgr.h"
#include "pint-util.h"
#include "pint-event.h"
#include "init-vars.h"

PINT_smcb *g_smcb = NULL; 

extern job_context_id pint_client_sm_context;

PINT_event_id PINT_client_sys_event_id;

int pint_client_pid;

/* set to one when init is done */
#ifdef WIN32
int pvfs_sys_init_flag = 0;
#endif 

typedef enum
{
    CLIENT_NO_INIT           =       0,
    CLIENT_ENCODER_INIT      = (1 << 0),
    CLIENT_BMI_INIT          = (1 << 1),
    CLIENT_FLOW_INIT         = (1 << 2),
    CLIENT_JOB_INIT          = (1 << 3),
    CLIENT_JOB_CTX_INIT      = (1 << 4),
    CLIENT_ACACHE_INIT       = (1 << 5),
    CLIENT_NCACHE_INIT       = (1 << 6),
    CLIENT_CONFIG_MGR_INIT   = (1 << 7),
    CLIENT_REQ_SCHED_INIT    = (1 << 8),
    CLIENT_JOB_TIME_MGR_INIT = (1 << 9),
    CLIENT_DIST_INIT         = (1 << 10),
    CLIENT_SECURITY_INIT     = (1 << 11),
    CLIENT_RCACHE_INIT       = (1 << 12),
    CLIENT_CAPCACHE_INIT     = (1 << 13)
} PINT_client_status_flag;

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal
 * data structures.  Must be called before any other system interface
 * function.
 *
 * This should run once and only once even in multithreaded environment.
 *
 * the default_debug_mask is used if not overridden by the
 * PVFS2_DEBUGMASK environment variable at run-time.  allowable string
 * formats of the env variable are the same as the EventLogging line
 * in the server configuration file.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_sys_initialize(uint64_t default_debug_mask)
{
#ifndef WIN32
    static int pvfs_sys_init_flag = 0; /* set to one when init is done */
#endif
    static int pvfs_sys_init_in_progress = 0;
    static gen_mutex_t init_mutex = GEN_RECURSIVE_MUTEX_INITIALIZER_NP;

    int ret = -PVFS_EINVAL;
    const char *debug_mask_str = NULL, *debug_file = NULL;
    PINT_client_status_flag client_status_flag = CLIENT_NO_INIT;
    PINT_smcb *smcb = NULL;
    uint64_t debug_mask = 0;
    char *event_mask = NULL;
	char *relatime_timeout_str = NULL;

    if (pvfs_sys_init_flag)
    {
        /* quick test and out */
        return 0;
    }

    /* multiple initers block here until init is done */
    gen_mutex_lock(&init_mutex);
    if (pvfs_sys_init_flag || pvfs_sys_init_in_progress)
    {
        /* a loop back from same process got through mutex */
        gen_mutex_unlock(&init_mutex);
        return 0;
    }
    pvfs_sys_init_in_progress = 1;

    /* ready to initialize */

#ifdef WIN32
    pint_client_pid = (int) GetCurrentProcessId();
#else
    pint_client_pid = getpid();
#endif

    gossip_enable_stderr();

    debug_mask_str = getenv("PVFS2_DEBUGMASK");
    debug_mask = (debug_mask_str ?
                  PVFS_debug_eventlog_to_mask(debug_mask_str) :
                  default_debug_mask);
    gossip_set_debug_mask(1,debug_mask);

    debug_file = getenv("PVFS2_DEBUGFILE");
    if (debug_file)
    {
        gossip_enable_file(debug_file, "w");
    }

    /* Gather preferrred relatime_timeout:
     *   if relatime < 0, then relatime feature disabled
     *   if relatime == 0, then always update atime
     *   if relatime > 0, then atime is updated only if relatime seconds have
     *                    elapsed since last atime update. (We won't update
     *                    atime for every write like the Linux Kernel's
     *                    relatime. Ideally, do that server side.)
     */
    relatime_timeout_str = getenv("PVFS2_RELATIME_TIMEOUT");
    if(relatime_timeout_str != NULL)
    {
        relatime_timeout = atoi(relatime_timeout_str);
#if 0
        gossip_err("%s: Detected environment variable --> "
                   "PVFS2_RELATIME_TIMEOUT=%d\n",
                   __func__,
                   relatime_timeout);
#endif
    }
    else
    {
        relatime_timeout = 24 * 60 * 60; /* Default timeout of 1 Day. */
    }

    ret = PINT_event_init(PINT_EVENT_TRACE_TAU);

    /*  ignore error */
#if 0
    if (ret < 0)
    {
        gossip_err("Error initializing event interface.\n");
        return (ret);
    }
#endif

    /**
     * (ClientID, Rank, RequestID, Handle, Sys)
     */
    PINT_event_define_event(NULL, "sys", "%d%d%d%llu%d", "",
                            &PINT_client_sys_event_id);

    event_mask = getenv("PVFS2_EVENTMASK");
    if (event_mask)
    {
        PINT_event_enable(event_mask);
    }

    ret = id_gen_safe_initialize();
    if(ret < 0)
    {
        gossip_lerr("Error initializing id_gen_safe\n");
        goto error_exit;
    }

    /* Initialize the distribution subsystem */
    ret = PINT_dist_initialize(NULL);
    if (ret < 0)
    {
        gossip_lerr("Error initializing distributions.\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_DIST_INIT;
    
    /* Initialize the security subsystem */
    ret = PINT_client_security_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing security\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_SECURITY_INIT;
    
    /* initialize the protocol encoder */
    ret = PINT_encode_initialize();
    if (ret < 0)
    {
        gossip_lerr("Protocol encoder initialize failure\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_ENCODER_INIT;
    
    /* initialize bmi and the bmi session identifier */
    ret = BMI_initialize(NULL,NULL,0);
    if (ret < 0)
    {
        gossip_lerr("BMI initialize failure\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_BMI_INIT;

    /* initialize the flow interface */
    ret = PINT_flow_initialize(NULL, 0);
    if (ret < 0)
    {
        gossip_lerr("Flow initialize failure.\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_FLOW_INIT;

    /* initialize the request scheduler (used mainly for timers) */
    ret = PINT_req_sched_initialize();
    if (ret < 0)
    {
        gossip_lerr("Req sched initialize failure.\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_REQ_SCHED_INIT;

    /* initialize the job timeout mgr */
    ret = job_time_mgr_init();
    if (ret < 0)
    {
        gossip_lerr("Job time mgr initialize failure.\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_JOB_TIME_MGR_INIT;

    /* initialize the job interface and the job context */
    ret = job_initialize(0);
    if (ret < 0)
    {
        gossip_lerr("Error initializing job interface: %s\n",
                    strerror(-ret));
        goto error_exit;
    }
    client_status_flag |= CLIENT_JOB_INIT;

    /* initialize the state machine engine */
    ret = PINT_client_state_machine_initialize();
    if (ret < 0)
    {
        gossip_lerr("job_open_context() failure.\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_JOB_CTX_INIT;

    /* initialize the attribute cache and set the default timeout */
    ret = PINT_acache_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing attribute cache\n");
        goto error_exit;        
    }
    client_status_flag |= CLIENT_ACACHE_INIT;

    /* initialize the client capcache and set the default timeout */
    ret = PINT_client_capcache_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing client capcache\n");
        goto error_exit;
    }
    client_status_flag |= CLIENT_CAPCACHE_INIT;

    /* initialize the name lookup cache and set the default timeout */
    ret = PINT_ncache_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing name lookup cache\n");
        goto error_exit;        
    }        
    client_status_flag |= CLIENT_NCACHE_INIT;

    /* initialize the readdir cache and set the default timeout */
    ret = PINT_rcache_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing readdir cache\n");
        goto error_exit;        
    }
    client_status_flag |= CLIENT_RCACHE_INIT;

    /* initialize the server configuration manager */
    ret = PINT_server_config_mgr_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing server configuration manager\n");
        goto error_exit;        
    }        
    client_status_flag |= CLIENT_CONFIG_MGR_INIT;

    /* initialize the handle mapping interface */
    ret = PINT_cached_config_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing handle mapping interface\n");
        goto error_exit;
    }

    /* start job timer */
    PINT_smcb_alloc(&smcb,
                    PVFS_CLIENT_JOB_TIMER,
                    sizeof(struct PINT_client_sm),
                    client_op_state_get_machine,
                    NULL,
                    pint_client_sm_context);
    if(!smcb)
    {
	ret = (-PVFS_ENOMEM);
        goto local_exit;
    }

    ret = PINT_client_state_machine_post(smcb, NULL, NULL);
    if (ret < 0)
    {
	gossip_lerr("Error posting job timer.\n");
	goto error_exit;
    }
    /* keep track of this pointer for freeing on finalize */
    g_smcb = smcb;

    ret = 0;
    goto local_exit;

error_exit:

    id_gen_safe_finalize();

    if (client_status_flag & CLIENT_CAPCACHE_INIT)
    {
        PINT_client_capcache_finalize();
    }

    if (client_status_flag & CLIENT_CONFIG_MGR_INIT)
    {
        PINT_server_config_mgr_finalize();
    }

    if (client_status_flag & CLIENT_NCACHE_INIT)
    {
        PINT_ncache_finalize();
    }

    if (client_status_flag & CLIENT_ACACHE_INIT)
    {
        PINT_acache_finalize();
    }

    if (client_status_flag & CLIENT_JOB_TIME_MGR_INIT)
    {
        job_time_mgr_finalize();
    }

    if (client_status_flag & CLIENT_JOB_CTX_INIT)
    {
        PINT_client_state_machine_finalize();
    }

    if (client_status_flag & CLIENT_JOB_INIT)
    {
        job_finalize();
    }

    if (client_status_flag & CLIENT_FLOW_INIT)
    {
        PINT_flow_finalize();
    }

    if (client_status_flag & CLIENT_REQ_SCHED_INIT)
    {
        PINT_req_sched_finalize();
    }

    if (client_status_flag & CLIENT_BMI_INIT)
    {
        BMI_finalize();
    }

    if (client_status_flag & CLIENT_ENCODER_INIT)
    {
        PINT_encode_finalize();
    }

    if (client_status_flag & CLIENT_SECURITY_INIT)
    {
        PINT_client_security_finalize();
    }

    if (client_status_flag & CLIENT_DIST_INIT)
    {
        PINT_dist_finalize();
    }

    PINT_smcb_free(smcb);

local_exit:

#ifdef WIN32
    pvfs_sys_init_in_progress = 0;
#endif

    gen_mutex_unlock(&init_mutex);

    pvfs_sys_init_flag = 1;

    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

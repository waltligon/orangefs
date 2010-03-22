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
#include <unistd.h>

#include "acache.h"
#include "ncache.h"
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

PINT_smcb *g_smcb = NULL; 

extern job_context_id pint_client_sm_context;

PINT_event_id PINT_client_sys_event_id;

int pint_client_pid;

typedef enum
{
    CLIENT_NO_INIT         =      0,
    CLIENT_ENCODER_INIT    = (1 << 0),
    CLIENT_BMI_INIT        = (1 << 1),
    CLIENT_FLOW_INIT       = (1 << 2),
    CLIENT_JOB_INIT        = (1 << 3),
    CLIENT_JOB_CTX_INIT    = (1 << 4),
    CLIENT_ACACHE_INIT     = (1 << 5),
    CLIENT_NCACHE_INIT     = (1 << 6),
    CLIENT_CONFIG_MGR_INIT = (1 << 7),
    CLIENT_REQ_SCHED_INIT  = (1 << 8),
    CLIENT_JOB_TIME_MGR_INIT = (1 << 9),
    CLIENT_DIST_INIT       = (1 << 10)
} PINT_client_status_flag;

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal
 * data structures.  Must be called before any other system interface
 * function.
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
    int ret = -PVFS_EINVAL;
    const char *debug_mask_str = NULL, *debug_file = NULL;
    PINT_client_status_flag client_status_flag = CLIENT_NO_INIT;
    PINT_smcb *smcb = NULL;
    uint64_t debug_mask = 0;
    char *event_mask = NULL;

    pint_client_pid = getpid();

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

    ret = PINT_event_init(PINT_EVENT_TRACE_TAU);

/*  ignore error *
 *  if (ret < 0)
    {
        gossip_err("Error initializing event interface.\n");
        return (ret);
    } */


    /**
     * (ClientID, Rank, RequestID, Handle, Sys)
     */
    PINT_event_define_event(NULL, "sys", "%d%d%d%llu%d", "", &PINT_client_sys_event_id);

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
    
    /* initlialize the protocol encoder */
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

    /* initialize the name lookup cache and set the default timeout */
    ret = PINT_ncache_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing name lookup cache\n");
        goto error_exit;        
    }        
    client_status_flag |= CLIENT_NCACHE_INIT;

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
    PINT_smcb_alloc(&smcb, PVFS_CLIENT_JOB_TIMER,
            sizeof(struct PINT_client_sm),
            client_op_state_get_machine,
            NULL,
            pint_client_sm_context);
    if(!smcb)
    {
	return(-PVFS_ENOMEM);
    }

    ret = PINT_client_state_machine_post(smcb, NULL, NULL);
    if (ret < 0)
    {
	gossip_lerr("Error posting job timer.\n");
	goto error_exit;
    }
    /* keep track of this pointer for freeing on finalize */
    g_smcb = smcb;

    PINT_util_digest_init();

   return 0;

  error_exit:

    id_gen_safe_finalize();

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

    if (client_status_flag & CLIENT_DIST_INIT)
    {
        PINT_dist_finalize();
    }

    PINT_smcb_free(smcb);

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

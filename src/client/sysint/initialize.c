/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "acache.h"
#include "ncache.h"
#include "pint-cached-config.h"
#include "pvfs2-sysint.h"
#include "pint-dist-utils.h"
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "pint-servreq.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "client-state-machine.h"
#include "request-scheduler.h"
#include "job-time-mgr.h"

job_context_id PVFS_sys_job_context = -1;

PINT_client_sm *g_sm_p = NULL;

extern gen_mutex_t *g_session_tag_mt_lock;

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
int PVFS_sys_initialize(int default_debug_mask)
{
    int ret = -PVFS_EINVAL, debug_mask = 0;
    char *debug_mask_str = NULL, *debug_file = NULL;
    PINT_client_status_flag client_status_flag = CLIENT_NO_INIT;
    PINT_client_sm *sm_p = NULL;

    sm_p = (PINT_client_sm*)malloc(sizeof(PINT_client_sm));
    if(!sm_p)
    {
	return(-PVFS_ENOMEM);
    }

    /* keep track of this pointer for freeing on finalize */
    g_sm_p = sm_p;
    memset(sm_p, 0, sizeof(*sm_p));

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

    /* Initialize the distribution subsystem */
    ret = PINT_dist_initialize();
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
    g_session_tag_mt_lock = gen_mutex_build();
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

    ret = job_open_context(&PVFS_sys_job_context);
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
    PINT_acache_set_timeout(PINT_ACACHE_TIMEOUT_MS);
    client_status_flag |= CLIENT_ACACHE_INIT;

    /* initialize the name lookup cache and set the default timeout */
    ret = PINT_ncache_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing name lookup cache\n");
        goto error_exit;        
    }        
    PINT_ncache_set_timeout(PINT_NCACHE_TIMEOUT_MS);
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
    ret = PINT_bucket_initialize();
    if (ret < 0)
    {
        gossip_lerr("Error initializing handle mapping interface\n");
        goto error_exit;
    }

    ret = PINT_client_state_machine_post(
        sm_p, PVFS_CLIENT_JOB_TIMER, NULL, NULL);
    if (ret < 0)
    {
	gossip_lerr("Error posting job timer.\n");
	goto error_exit;
    }

    return 0;

  error_exit:

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
        job_close_context(PVFS_sys_job_context);
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

    free(sm_p);

    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

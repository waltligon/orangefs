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
#include "pint-bucket.h"
#include "pvfs2-sysint.h"
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "pint-servreq.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "client-state-machine.h"

job_context_id PVFS_sys_job_context = -1;

extern gen_mutex_t *g_session_tag_mt_lock;

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal data
 * structures.  Must be called before any other system interface function.
 *
 * the default_debug_mask is used if not overridden by the PVFS2_DEBUGMASK
 * environment variable at run-time.  allowable string formats of the
 * env variable are the same as the EventLogging line in the server
 * configuration file.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_sys_initialize(
    int default_debug_mask)
{
    int ret = -1;
    char *debug_mask_str = NULL;
    int debug_mask = 0;
    char *debug_file = 0;

    enum {
	NONE_INIT_FAIL = 0,
	ENCODER_INIT_FAIL,
	BMI_INIT_FAIL,
	FLOW_INIT_FAIL,
	JOB_INIT_FAIL,
	JOB_CONTEXT_FAIL,
	ACACHE_INIT_FAIL,
	NCACHE_INIT_FAIL,
	BUCKET_INIT_FAIL
    } init_fail = NONE_INIT_FAIL;

    gossip_enable_stderr();

    debug_mask_str = getenv("PVFS2_DEBUGMASK");
    debug_mask = (debug_mask_str ?
                  PVFS_debug_eventlog_to_mask(debug_mask_str) :
                  default_debug_mask);
    gossip_set_debug_mask(1,debug_mask);

    debug_file = getenv("PVFS2_DEBUGFILE");
    if (debug_file)
	gossip_enable_file(debug_file, "w");

    /* initialize protocol encoder */
    ret = PINT_encode_initialize();
    if(ret < 0)
    {
	init_fail = ENCODER_INIT_FAIL;
	goto return_error;
    }
    
    /* Initialize BMI */
    ret = BMI_initialize(NULL,NULL,0);
    if (ret < 0)
    {
	init_fail = BMI_INIT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"BMI initialize failure\n");
	goto return_error;
    }

    /* initialize bmi session identifier, TODO: DOCUMENT THIS */
    g_session_tag_mt_lock = gen_mutex_build();

    /* Initialize flow */
    ret = PINT_flow_initialize(NULL, 0);
    if (ret < 0)
    {
	init_fail = FLOW_INIT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Flow initialize failure.\n");
	goto return_error;
    }

    /* Initialize the job interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
	init_fail = JOB_INIT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Error initializing job interface: %s\n",strerror(-ret));
	goto return_error;
    }

    ret = job_open_context(&PVFS_sys_job_context);
    if(ret < 0)
    {
	init_fail = JOB_CONTEXT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG, "job_open_context() failure.\n");
	goto return_error;
    }
	
    /* Initialize the pinode cache */
    ret = PINT_acache_initialize();
    if (ret < 0)
    {
	init_fail = ACACHE_INIT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Error initializing pinode cache\n");
	goto return_error;	
    }
    PINT_acache_set_timeout(PINT_ACACHE_TIMEOUT * 1000);

    /* Initialize the directory cache */
    ret = PINT_ncache_initialize();
    if (ret < 0)
    {
	init_fail = NCACHE_INIT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Error initializing directory cache\n");
	goto return_error;	
    }	
    PINT_ncache_set_timeout(PINT_NCACHE_TIMEOUT * 1000);

    /* Initialize the configuration management interface */
    ret = PINT_bucket_initialize();
    if (ret < 0)
    {
	init_fail = BUCKET_INIT_FAIL;
	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Error initializing config management\n");
	goto return_error;	
    }

    return(0);

 return_error:
    switch(init_fail) {
	case BUCKET_INIT_FAIL:
	    PINT_ncache_finalize();
	case NCACHE_INIT_FAIL:
	    PINT_acache_finalize();
	case ACACHE_INIT_FAIL:
	case JOB_CONTEXT_FAIL:
	    job_finalize();
	case JOB_INIT_FAIL:
	    PINT_flow_finalize();
	case FLOW_INIT_FAIL:
	    BMI_finalize();
	case BMI_INIT_FAIL:
	    PINT_encode_finalize();
	case ENCODER_INIT_FAIL:
	case NONE_INIT_FAIL:
	    /* nothing to do for either of these */
	    break;
    }
    gossip_disable();

    return(ret);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

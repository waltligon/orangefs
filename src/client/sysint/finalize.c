/* 
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* System Interface Finalize Implementation */
#include <malloc.h>

#include "pint-sysint.h"
#include "pcache.h"
#include "pint-dcache.h"
#include "gen-locks.h"
#include "pint-bucket.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "PINT-reqproto-encode.h"

extern struct server_configuration_s g_server_config;
extern gen_mutex_t *g_session_tag_mt_lock;
extern job_context_id PVFS_sys_job_context;

/* PVFS_finalize
 *
 * shuts down the PVFS system interface
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_finalize()
{
    /* Free the dcache */
    PINT_dcache_finalize();
    /* free all pinode structures */
    PINT_pcache_finalize();
	
    /* shut down bucket interface */
    PINT_bucket_finalize();

    PINT_server_config_release(&g_server_config);
	
    /* get rid of the mutex for the BMI session tag identifier */
    gen_mutex_lock(g_session_tag_mt_lock);
    gen_mutex_unlock(g_session_tag_mt_lock);
    gen_mutex_destroy(g_session_tag_mt_lock);

    /* finalize the I/O interfaces */
    job_close_context(PVFS_sys_job_context);
    job_finalize();

    PINT_flow_finalize();

    BMI_finalize();

    PINT_encode_finalize();

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/* 
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* System Interface Finalize Implementation */
#include <malloc.h>

#include "pint-sysint-utils.h"
#include "acache.h"
#include "ncache.h"
#include "gen-locks.h"
#include "pint-bucket.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "PINT-reqproto-encode.h"
#include "client-state-machine.h"

extern job_context_id PVFS_sys_job_context;

/* declared in initialize.c */
extern gen_mutex_t *g_session_tag_mt_lock;
extern gen_mutex_t *g_server_config_mutex;

/* PVFS_finalize
 *
 * shuts down the PVFS system interface
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_finalize()
{
    PINT_ncache_finalize();
    PINT_acache_finalize();
    PINT_bucket_finalize();

    /* flush all known server configurations */
    PINT_server_config_mgr_finalize();

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

    PINT_release_pvfstab();

    gossip_disable();

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

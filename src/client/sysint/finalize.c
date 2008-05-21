/* 
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* System Interface Finalize Implementation */
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "pint-sysint-utils.h"
#include "acache.h"
#include "ncache.h"
#include "gen-locks.h"
#include "pint-cached-config.h"
#include "pint-dist-utils.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "PINT-reqproto-encode.h"
#include "client-state-machine.h"
#include "src/server/request-scheduler/request-scheduler.h"
#include "job-time-mgr.h"
#include "pint-util.h"
#include "pint-event.h"

extern job_context_id pint_client_sm_context;

extern PINT_smcb *g_smcb;

/* PVFS_finalize
 *
 * shuts down the PVFS system interface
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_finalize()
{
    id_gen_safe_finalize();

    PINT_util_digest_finalize();
    PINT_ncache_finalize();
    PINT_acache_finalize();
    PINT_cached_config_finalize();

    /* flush all known server configurations */
    PINT_server_config_mgr_finalize();

    /* finalize the I/O interfaces */
    job_time_mgr_finalize();
    job_close_context(pint_client_sm_context);
    job_finalize();

    PINT_flow_finalize();

    PINT_req_sched_finalize();

    BMI_finalize();

    PINT_encode_finalize();

    PINT_dist_finalize();

    PINT_event_finalize();

    PINT_release_pvfstab();

    gossip_disable();

    PINT_client_state_machine_release(g_smcb);

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

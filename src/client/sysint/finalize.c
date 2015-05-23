/* 
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* System Interface Finalize Implementation */
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "pvfs2-internal.h"
#include "pint-sysint-utils.h"
#include "acache.h"
#include "ncache.h"
#include "rcache.h"
#include "client-capcache.h"
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

#ifdef WIN32
extern int pvfs_sys_init_flag;
#endif

/* PVFS_finalize
 *
 * shuts down the PVFS system interface
 * should not be recursive so simple run-once mechanism
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_finalize()
{
    static int finiflag = 0;
    static gen_mutex_t finimutex = GEN_MUTEX_INITIALIZER;
    char * perf_counters_to_display = NULL;

    /* first time runs, other wait until completed then exit */
    if (finiflag)
    {
        return 0;
    }
    gen_mutex_lock(&finimutex);
    if (finiflag)
    {
        gen_mutex_unlock(&finimutex);
        return 0;
    }

    id_gen_safe_finalize();

    /* If desired, display cache perf counters before they are finalized. */
    perf_counters_to_display = getenv("PVFS2_COUNTERS_AT_FINALIZE");
    if(perf_counters_to_display)
    {
        if(PINT_ncache_get_pc() &&
           strstr(perf_counters_to_display, "ncache"))
        {
            gossip_err("%s: DISPLAYING PERF COUNTERS FOR NCACHE:\n%s",
                __func__,
                PINT_perf_generate_text(PINT_ncache_get_pc(), 4096));
        }
        if(PINT_acache_get_pc() &&
           strstr(perf_counters_to_display, "acache"))
        {
            gossip_err("%s: DISPLAYING PERF COUNTERS FOR ACACHE:\n%s",
                __func__,
                PINT_perf_generate_text(PINT_acache_get_pc(), 4096));
        
        }
        if(PINT_client_capcache_get_pc() &&
           strstr(perf_counters_to_display, "capcache"))
        {
            gossip_err("%s: DISPLAYING PERF COUNTERS FOR CAPCACHE\n%s",
                __func__,
                PINT_perf_generate_text(PINT_client_capcache_get_pc(), 4096));
        }
    }

    PINT_client_capcache_finalize();
    PINT_rcache_finalize();
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

    /* release timer_queue resources, if there are any */
    PINT_timer_queue_finalize();

    BMI_finalize();

    PINT_encode_finalize();

    PINT_client_security_finalize();

    PINT_dist_finalize();

    PINT_event_finalize();

    PINT_release_pvfstab();

    gossip_disable();

    PINT_client_state_machine_release(g_smcb);

#ifdef WIN32
    pvfs_sys_init_flag = 0;
#endif

    finiflag = 1;
    
    gen_mutex_unlock(&finimutex);
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

/*
 * Copyright © 2014 Omnibond Systems LLC
 *
 * See COPYING in top-level directory.
 *
 * Client-side capability cache definitions
 *
 */
#ifndef __CLIENT_CAPCACHE_H
#define __CLIENT_CAPCACHE_H

#include "pvfs2-types.h"
#include "gen-locks.h"
#include "quicklist.h"
#include "quickhash.h"
#include "tcache.h"
#include "pint-perf-counter.h"

/** @see PINT_tcache_options */
#define PINT_client_capcache_options PINT_tcache_options

#define CLIENT_CAPCACHE_TIMEOUT_MSECS TCACHE_TIMEOUT_MSECS
#define CLIENT_CAPCACHE_NUM_ENTRIES TCACHE_NUM_ENTRIES
#define CLIENT_CAPCACHE_HARD_LIMIT TCACHE_HARD_LIMIT
#define CLIENT_CAPCACHE_SOFT_LIMIT TCACHE_SOFT_LIMIT
#define CLIENT_CAPCACHE_ENABLE TCACHE_ENABLE
#define CLIENT_CAPCACHE_RECLAIM_PERCENTAGE TCACHE_RECLAIM_PERCENTAGE

/* timeout buffer in seconds */
#define CLIENT_CAPCACHE_TIMEOUT_BUFFER    5

enum 
{
   PERF_CLIENT_CAPCACHE_NUM_ENTRIES = 0,
   PERF_CLIENT_CAPCACHE_SOFT_LIMIT = 1,
   PERF_CLIENT_CAPCACHE_HARD_LIMIT = 2,
   PERF_CLIENT_CAPCACHE_HITS = 3,
   PERF_CLIENT_CAPCACHE_MISSES = 4,
   PERF_CLIENT_CAPCACHE_UPDATES = 5,
   PERF_CLIENT_CAPCACHE_PURGES = 6,
   PERF_CLIENT_CAPCACHE_REPLACEMENTS = 7,
   PERF_CLIENT_CAPCACHE_DELETIONS = 8,
   PERF_CLIENT_CAPCACHE_ENABLED = 9,
};

/** client_capcache performance counter keys */
extern struct PINT_perf_key client_capcache_keys[];

void PINT_client_capcache_enable_perf_counter(struct PINT_perf_counter* pc);

int PINT_client_capcache_initialize(void);

void PINT_client_capcache_finalize(void);

int PINT_client_capcache_get_info(
    enum PINT_client_capcache_options option,
    unsigned int *arg);

int PINT_client_capcache_set_info(
    enum PINT_client_capcache_options option,
    unsigned int arg);

int PINT_client_capcache_get_cached_entry(
    PVFS_object_ref refn,
    PVFS_uid uid,
    PVFS_capability *cap);

int PINT_client_capcache_update(
    PVFS_object_ref refn,
    PVFS_uid uid,
    const PVFS_capability *cap);

void PINT_client_capcache_invalidate(
    PVFS_object_ref refn,
    PVFS_uid uid);

struct PINT_perf_counter* PINT_client_capcache_get_pc(void);

#endif /* __CLIENT_CAPCACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


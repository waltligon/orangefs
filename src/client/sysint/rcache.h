/*
 * Copyright © Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */

#ifndef __RCACHE_H
#define __RCACHE_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gen-locks.h"
#include "quicklist.h"
#include "quickhash.h"
#include "tcache.h"
#include "pint-perf-counter.h"

/** \defgroup rcache Readdir/Readdirplus Cache (rcache)
 *
 * The rcache implements a simple client side cache for PVFS2 readdir operation.
 * expiration timeout is disabled. 
 * The rcache is built on top of the generic tcache caching component. The RCACHE
 * component will cache the following:
 * - directory meta handle
 * - token value (readdir session_no + dirent position)
 * - dirdata_index (indicating which dirdata server to use)
 * .
 *
 * The tcache implements a simple component for caching data structures that
 * can be referenced by unique, opaque keys.  A timeout is associated with each 
 * entry to dictate when it will expire.  Specific caches such as the
 * attribute or name cache may be built on top of this one.
 *
 * Notes:
 * - See tcache for policy documentation
 * .
 *
 * Operations that may retrieve items from rcache:
 * - readdir/readdirplus
 * .
 *
 * Operations that may insert items into the cache:
 * - readdir/readdirplus
 * .
 *
 * Operations that may DELETE items from the cache:
 * .
 *
 * @{
 */

/** \file
 * Declarations for the Readdir Cache (rcache) component.
 */

/** @see PINT_tcache_options */
#define PINT_rcache_options PINT_tcache_options
enum {
RCACHE_TIMEOUT_MSECS = TCACHE_TIMEOUT_MSECS,
RCACHE_NUM_ENTRIES = TCACHE_NUM_ENTRIES,
RCACHE_HARD_LIMIT = TCACHE_HARD_LIMIT,
RCACHE_SOFT_LIMIT = TCACHE_SOFT_LIMIT,
RCACHE_ENABLE = TCACHE_ENABLE,
RCACHE_RECLAIM_PERCENTAGE = TCACHE_RECLAIM_PERCENTAGE,
};

enum 
{
   PERF_RCACHE_NUM_ENTRIES = 0,
   PERF_RCACHE_SOFT_LIMIT = 1,
   PERF_RCACHE_HARD_LIMIT = 2,
   PERF_RCACHE_HITS = 3,
   PERF_RCACHE_MISSES = 4,
   PERF_RCACHE_UPDATES = 5,
   PERF_RCACHE_PURGES = 6,
   PERF_RCACHE_REPLACEMENTS = 7,
   PERF_RCACHE_DELETIONS = 8, 
   PERF_RCACHE_ENABLED = 9,
};

/** rcache performance counter keys */
extern struct PINT_perf_key rcache_keys[];

void PINT_rcache_enable_perf_counter(struct PINT_perf_counter* pc);

int PINT_rcache_initialize(void);

void PINT_rcache_finalize(void);

int PINT_rcache_get_info(
    enum PINT_rcache_options option,
    unsigned int* arg);

int PINT_rcache_set_info(
    enum PINT_rcache_options option,
    unsigned int arg);

int PINT_rcache_get_cached_index(
    const PVFS_object_ref* ref,
    const PVFS_ds_position token,
    int32_t *index); 

int PINT_rcache_insert(
    const PVFS_object_ref* ref,     
    const PVFS_ds_position token,
    const int32_t dirdata_index); 

void PINT_rcache_invalidate(
    const PVFS_object_ref* ref,
    const PVFS_ds_position token);

#endif /* __RCACHE_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


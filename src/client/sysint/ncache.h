/*
 * Copyright © Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */

#ifndef __NCACHE_H
#define __NCACHE_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gen-locks.h"
#include "quicklist.h"
#include "quickhash.h"
#include "tcache.h"
#include "pint-perf-counter.h"

/** \defgroup ncache Name Cache (ncache)
 *
 * The ncache implements a simple client side cache for PVFS2 files.
 * A timeout is associated with each entry to dictate when it will expire. 
 * The ncache is built on top of the generic tcache caching component. The NCACHE
 * component will cache the following:
 * - parent handle
 * - entry for a specific file OR directory within the parent directory
 * - entry handle
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
 * Operations that may retrieve items from ncache:
 * - lookup
 * - rename  (Current implementation does not yet use ncache)
 * .
 *
 * Operations that may insert items into the cache:
 * - pvfs2-lookup
 * - pvfs2-rename  (Current implementation does not yet use ncache)
 * - pvfs2-symlink
 * - pvfs2-readdir
 * - pvfs2-mkdir
 * - pvfs2-create
 * .
 *
 * Operations that may DELETE items from the cache:
 * - pvfs2-remove
 * - pvfs2-rename
 * - any failed sysint operation from the list of operations that retrieve
 *   items from NCACHE
 * .
 *
 * @{
 */

/** \file
 * Declarations for the Name Cache (ncache) component.
 */

/** @see PINT_tcache_options */
#define PINT_ncache_options PINT_tcache_options
enum {
NCACHE_TIMEOUT_MSECS = TCACHE_TIMEOUT_MSECS,
NCACHE_NUM_ENTRIES = TCACHE_NUM_ENTRIES,
NCACHE_HARD_LIMIT = TCACHE_HARD_LIMIT,
NCACHE_SOFT_LIMIT = TCACHE_SOFT_LIMIT,
NCACHE_ENABLE = TCACHE_ENABLE,
NCACHE_RECLAIM_PERCENTAGE = TCACHE_RECLAIM_PERCENTAGE,
};

enum 
{
   PERF_NCACHE_NUM_ENTRIES = 0,
   PERF_NCACHE_SOFT_LIMIT = 1,
   PERF_NCACHE_HARD_LIMIT = 2,
   PERF_NCACHE_HITS = 3,
   PERF_NCACHE_MISSES = 4,
   PERF_NCACHE_UPDATES = 5,
   PERF_NCACHE_PURGES = 6,
   PERF_NCACHE_REPLACEMENTS = 7,
   PERF_NCACHE_DELETIONS = 8, 
   PERF_NCACHE_ENABLED = 9,
};

/** ncache performance counter keys */
extern struct PINT_perf_key ncache_keys[];

void PINT_ncache_enable_perf_counter(struct PINT_perf_counter* pc);

int PINT_ncache_initialize(void);

void PINT_ncache_finalize(void);

int PINT_ncache_get_info(
    enum PINT_ncache_options option,
    unsigned int* arg);

int PINT_ncache_set_info(
    enum PINT_ncache_options option,
    unsigned int arg);

int PINT_ncache_get_cached_entry(
    const char* entry, 
    PVFS_object_ref* entry_ref,
    const PVFS_object_ref* parent_ref); 

int PINT_ncache_update(
    const char* entry, 
    const PVFS_object_ref* entry_ref, 
    const PVFS_object_ref* parent_ref); 

void PINT_ncache_invalidate(
    const char* entry, 
    const PVFS_object_ref* parent_ref); 

#endif /* __NCACHE_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


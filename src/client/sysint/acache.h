/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#ifndef __ACACHE_H
#define __ACACHE_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gen-locks.h"
#include "quicklist.h"
#include "quickhash.h"
#include "tcache.h"
#include "pint-perf-counter.h"

/** \defgroup acache Attribute Cache (acache)
 *
 * The acache implements a simple client side cache for PVFS2 attibutes as
 * well as logical file sizes.  A timeout is associated with each attribute 
 * structure to dictate when it will expire, and the attribute mask is used 
 * to determine which fields in the attribute structure are valid at a given
 * time.  The acache is built on top of the generic tcache caching component.
 *
 * The tcache implements a simple component for caching data structures that
 * can be referenced by unique, opaque keys.  A timeout is associated with each 
 * entry to dictate when it will expire.  Specific caches such as the
 * attribute or name cache may be built on top of this one.
 *
 * Notes:
 * - See tcache for policy documentation
 * - Note that the acache never explicitly deletes an entry.  Instead, it
 *   will invalidate an entry but leave it in the cache.
 * .
 *
 * Operations that may retrieve items from acache:
 * - truncate
 * - symlink
 * - rename
 * - readdir
 * - mkdir
 * - lookup
 * - io
 * - getattr
 * - flush
 * - create
 * - remove
 * - mgmt-get-dfile-array
 * - setattrib
 * .
 *
 * Operations that may insert items into the cache:
 * - create
 * - getattr
 * - setattr
 * - mkdir
 * - symlink
 * .
 *
 * Operations that may invalidate items in the cache:
 * - remove
 * - rename
 * - io (size only)
 * - truncate (size only)
 * - any failed sysint operation from the list of operations that retrieve
 * attributes
 * .
 *
 * @{
 */

/** \file
 * Declarations for the Attribute Cache (acache) component.
 */

/** @see PINT_tcache_options */
#define PINT_acache_options PINT_tcache_options

#define ACACHE_TIMEOUT_MSECS TCACHE_TIMEOUT_MSECS
#define ACACHE_NUM_ENTRIES TCACHE_NUM_ENTRIES
#define ACACHE_HARD_LIMIT TCACHE_HARD_LIMIT
#define ACACHE_SOFT_LIMIT TCACHE_SOFT_LIMIT
#define ACACHE_ENABLE TCACHE_ENABLE
#define ACACHE_RECLAIM_PERCENTAGE TCACHE_RECLAIM_PERCENTAGE

#define STATIC_ACACHE_OPT 1024

#define STATIC_ACACHE_TIMEOUT_MSECS (TCACHE_TIMEOUT_MSECS | STATIC_ACACHE_OPT)
#define STATIC_ACACHE_NUM_ENTRIES (TCACHE_NUM_ENTRIES | STATIC_ACACHE_OPT)
#define STATIC_ACACHE_HARD_LIMIT (TCACHE_HARD_LIMIT | STATIC_ACACHE_OPT)
#define STATIC_ACACHE_SOFT_LIMIT (TCACHE_SOFT_LIMIT | STATIC_ACACHE_OPT)
#define STATIC_ACACHE_ENABLE (TCACHE_ENABLE | STATIC_ACACHE_OPT)
#define STATIC_ACACHE_RECLAIM_PERCENTAGE (TCACHE_RECLAIM_PERCENTAGE | STATIC_ACACHE_OPT)

enum 
{
   PERF_ACACHE_NUM_ENTRIES = 0,
   PERF_ACACHE_SOFT_LIMIT = 1,
   PERF_ACACHE_HARD_LIMIT = 2,
   PERF_ACACHE_HITS = 3,
   PERF_ACACHE_MISSES = 4,
   PERF_ACACHE_UPDATES = 5,
   PERF_ACACHE_PURGES = 6,
   PERF_ACACHE_REPLACEMENTS = 7,
   PERF_ACACHE_DELETIONS = 8,
   PERF_ACACHE_ENABLED = 9,
};

/** acache performance counter keys */
extern struct PINT_perf_key acache_keys[];

void PINT_acache_enable_perf_counter(struct PINT_perf_counter* pc,
    struct PINT_perf_counter* static_pc);

int PINT_acache_initialize(void);

void PINT_acache_finalize(void);

int PINT_acache_get_info(
    enum PINT_acache_options option,
    unsigned int* arg);

int PINT_acache_set_info(
    enum PINT_acache_options option,
    unsigned int arg);

int PINT_acache_get_cached_entry(
    PVFS_object_ref refn,
    PVFS_object_attr* attr,
    int* attr_status,
    PVFS_size* size,
    int* size_status);

int PINT_acache_update(
    PVFS_object_ref refn, 
    PVFS_object_attr *attr,
    PVFS_size* size);

void PINT_acache_invalidate(
    PVFS_object_ref refn);

void PINT_acache_invalidate_size(
    PVFS_object_ref refn);

#endif /* __ACACHE_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


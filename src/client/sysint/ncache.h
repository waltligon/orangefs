/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef _PINT_NCACHE_H
#define _PINT_NCACHE_H

#include "pint-sysint-utils.h"

/* number of entries allowed in the cache */
#define PINT_NCACHE_MAX_ENTRIES 1024

/* number of milliseconds that cache entries will remain valid */
#define PINT_NCACHE_TIMEOUT_MS 30000

/* value passed out to indicate lookups that didn't find a match */
#define PINT_NCACHE_HANDLE_INVALID 0

int PINT_ncache_lookup(
        char *name,
        int want_resolved,
        PVFS_object_ref parent,
        PVFS_object_ref *entry);

int PINT_ncache_insert(
        char *name, 
        int abs_resolved,
        PVFS_object_ref entry,
        PVFS_object_ref parent);

int PINT_ncache_flush(void);

int PINT_ncache_remove(
        char *name, 
        int abs_resolved,
        PVFS_object_ref parent,
        int *item_found);

int PINT_ncache_initialize(void);

int PINT_ncache_finalize(void);

int PINT_ncache_get_timeout(void);

void PINT_ncache_set_timeout(int max_timeout_ms);

#endif 

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */

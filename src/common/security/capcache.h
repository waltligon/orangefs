/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Capability cache declarations
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CAPCACHE

#ifndef _CAPCACHE_H
#define _CAPCACHE_H
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "seccache.h"

/* Capcache Macros - Adjust accordingly */
/* Key type, so it can be swapped easily */
#define KEY_T                   uint64_t
/* Default timeout of capability-cache entry in seconds */
#define CAPCACHE_TIMEOUT        10
/* Number of capability cache entries capability cache can hold */
#define CAPCACHE_ENTRY_LIMIT    256
/* The total capability cache size 64 MB */
#define CAPCACHE_SIZE_LIMIT     (1024 * 1024 * 64)
/* Number of indexes in our chained hash-table */
#define CAPCACHE_HASH_LIMIT      128
/* Frequency (in lookups) to debugging stats */
#define CAPCACHE_STATS_FREQ     1000

/* Global Variables */
/* TODO: remove   extern seccache_t *capcache; */

/* Externally Visible Capability Cache API */
int PINT_capcache_init(void);
int PINT_capcache_finalize(void);
seccache_entry_t *PINT_capcache_lookup(PVFS_capability *cap);
int PINT_capcache_insert(PVFS_capability *cap);
int PINT_capcache_remove(seccache_entry_t *entry);
int PINT_capcache_quick_sign(PVFS_capability *cap);
/* End of Externally Visible Capability Cache API */

#endif /* _CAPCACHE_H */
#endif /* ENABLE_CAPCACHE */

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

#include "pvfs2-types.h"
#include "llist.h"

/* Macro that helps print function info */
#define CAPCACHE_ENTER_FN() do { \
                                gossip_debug(GOSSIP_CAPCACHE_DEBUG, "%s: enter\n", __func__); \
                            } while (0)
#define CAPCACHE_EXIT_FN()  do { \
                                gossip_debug(GOSSIP_CAPCACHE_DEBUG, "%s: exit\n", __func__); \
                            } while (0)

/* Capcache Macros - Adjust accordingly */
/* Key type, so it can be swapped easily */
#define KEY_T                   uint64_t
/* Default timeout of capability-cache entry in seconds */
#define CAPCACHE_TIMEOUT        10
/* Number of capability cache entries capability cache can hold */
#define CAPCACHE_ENTRY_LIMIT    256
/* The total capability cache size 64 MB */
#define CAPCACHE_SIZE_CAP       (1024 * 1024 * 64)
/* Number of indexes in our chained hash-table */
#define CAPCACHE_HASH_MAX       128
/* Frequency (in lookups) to debugging stats */
#define CAPCACHE_STATS_FREQ     1000

/* Capability Cache Locking */
#define LOCK_TYPE 3
#if (LOCK_TYPE == 0) /* No Locking */
    #define capcache_lock_t uint64_t
#elif (LOCK_TYPE == 1)
    #include <pthread.h>
    #define capcache_lock_t pthread_mutex_t
#elif (LOCK_TYPE == 2)
    #include <pthread.h>
    #define capcache_lock_t pthread_spinlock_t
#elif (LOCK_TYPE == 3)
    #include "gen-locks.h"
    #define capcache_lock_t gen_mutex_t
#endif
#define LOCK_SIZE sizeof(capcache_lock_t)
/* END OF Capability Cache Locking */

/* Capability Cache Structure */
/* 64-Bit Alligned */
typedef struct capcache_s {
    PVFS_size size_limit; /* Limit in bytes of Capability Cache */
    PVFS_time default_timeout_length;
    uint64_t entry_limit;
    struct capcache_stats_s *stats;
    PINT_llist_p entries[CAPCACHE_HASH_MAX];
} capcache_t;

/* Capability Cache Entry */
/* 64-Bit Alligned */
typedef struct capcache_entry_s {
    PVFS_time expiration;
    PVFS_capability *cap;
} capcache_entry_t;

/* Capability Cache Entry */
/* 64-Bit Alligned */
typedef struct capcache_stats_s {
    uint64_t inserts;
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t removed;
    uint64_t expired;
    uint64_t entry_cnt;
    PVFS_size cache_size;
} capcache_stats_t;

/* Global Variables */
extern struct capcache_s * capcache;

/* Externally Visible Capability Cache API */
int PINT_capcache_init(void); /**/
int PINT_capcache_finalize(void); /**/
struct capcache_entry_s * PINT_capcache_lookup_entry(PVFS_capability *cap);
int PINT_capcache_insert_entry(const PVFS_capability * cap); /**/
int PINT_capcache_remove_entry(struct capcache_entry_s *entry); /**/
/* End of Externally Visible Capability Cache API */

#endif /* _CAPCACHE_H */
#endif /* ENABLE_CAPCACHE */

#include <pvfs2-config.h>
#ifdef OFS_CAPCACHE_ENABLE
#ifndef _CAPCACHE_H
#define _CAPCACHE_H
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <pvfs2-types.h>

#define DBG 1
/* Macro that helps print function info */
#ifdef DBG
    #define PFI gossip_debug(GOSSIP_CAPCACHE_DEBUG, "{%s}\n", __func__);
#else
    #define PFI
#endif

/* Capcache Macros - Adjust accordingly */
#define KEY_T                   uint64_t            /* Key type, so it can be swapped easily */
#define CAPCACHE_TIMEOUT        60                  /* Default timeout of capability-cache entry in seconds */
#define CAPCACHE_ENTRY_LIMIT    256                 /* Number of capability cache entries capability cache can hold */
#define CAPCACHE_SIZE_CAP       (1024 * 1024 * 64)  /* The total capability cache size 64 MB */
#define CAPCACHE_HASH_MAX       16                  /* Number of indexes in our chained hash-table */

/* Capability Cache Locking */
#define LOCK_TYPE 0
#if (LOCK_TYPE == 0) /* No Locking */
    #define capcache_lock_t uint64_t
#elif (LOCK_TYPE == 1)
    #include <pthread.h>
    #define capcache_lock_t pthread_mutex_t
#elif (LOCK_TYPE == 2)
    #include <pthread.h>
    #define capcache_lock_t pthread_spinlock_t
#elif (LOCK_TYPE == 3)
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
    struct capcache_entry_s * entries[CAPCACHE_HASH_MAX];
} capcache_t;

/* Capability Cache Entry */
/* 64-Bit Alligned */
typedef struct capcache_entry_s {
    PVFS_time expiration;
    struct capcache_entry_s *next;
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
int PINT_capcache_remove_entry(void); /**/
/* End of Externally Visible Capability Cache API */

#endif /* _CAPCACHE_H */
#endif /* OFS_CAPCACHE_ENABLE */

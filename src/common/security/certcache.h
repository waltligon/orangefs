/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Certificate cache declarations
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CERTCACHE

#ifndef _CERTCACHE_H
#define _CERTCACHE_H
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "pvfs2-types.h"
#include "llist.h"

/* Macro that helps print function info */
#define CERTCACHE_ENTER_FN() do { \
                                gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: enter\n", __func__); \
                            } while (0)
#define CERTCACHE_EXIT_FN()  do { \
                                gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: exit\n", __func__); \
                            } while (0)

/* certcache Macros - Adjust accordingly */
/* Key type, so it can be swapped easily */
#define KEY_T                   uint64_t
/* Default timeout of certificate-cache entry in seconds */
#define CERTCACHE_TIMEOUT        10
/* Number of certificate cache entries certificate cache can hold */
#define CERTCACHE_ENTRY_LIMIT    256
/* The total certificate cache size 64 MB */
#define CERTCACHE_SIZE_CAP       (1024 * 1024 * 64)
/* Number of indexes in our chained hash-table */
#define CERTCACHE_HASH_MAX       128
/* Maximum size (in chars) of cert subject */
#define CERTCACHE_SUBJECT_SIZE   512
/* Frequency (in lookups) to debugging stats */
#define CERTCACHE_STATS_FREQ     1000


/* Certificate Cache Locking */
#define CERTCACHE_LOCK_TYPE 3
#if (CERTCACHE_LOCK_TYPE == 0) /* No Locking */
    #define certcache_lock_t uint64_t
#elif (CERTCACHE_LOCK_TYPE == 1)
    #include <pthread.h>
    #define certcache_lock_t pthread_mutex_t
#elif (CERTCACHE_LOCK_TYPE == 2)
    #include <pthread.h>
    #define certcache_lock_t pthread_spinlock_t
#elif (CERTCACHE_LOCK_TYPE == 3)
    #include "gen-locks.h"
    #define certcache_lock_t gen_mutex_t
#endif
#define CERTCACHE_LOCK_SIZE sizeof(certcache_lock_t)
/* END OF Certificate Cache Locking */

/* Certificate Cache Structure */
/* 64-Bit Alligned */
typedef struct certcache_s {
    PVFS_size size_limit; /* Limit in bytes of Certificate Cache */
    PVFS_time default_timeout_length;
    uint64_t entry_limit;
    struct certcache_stats_s *stats;
    PINT_llist_p entries[CERTCACHE_HASH_MAX];
} certcache_t;

/* Certificate Cache Entry */
/* 64-Bit Alligned */
typedef struct certcache_entry_s {
    PVFS_time expiration;
    char subject[CERTCACHE_SUBJECT_SIZE];
    PVFS_uid uid;
    uint32_t num_groups;
    PVFS_gid *group_array;
} certcache_entry_t;

/* Certificate Cache Entry */
/* 64-Bit Alligned */
typedef struct certcache_stats_s {
    uint64_t inserts;
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t removed;
    uint64_t expired;
    uint64_t entry_cnt;
    PVFS_size cache_size;
} certcache_stats_t;

/* Global Variables */
extern struct certcache_s * certcache;

/* Externally Visible Certificate Cache API */
int PINT_certcache_init(void); /**/
int PINT_certcache_finalize(void); /**/
struct certcache_entry_s * PINT_certcache_lookup_entry(PVFS_certificate *cert);
int PINT_certcache_insert_entry(const PVFS_certificate * cert, 
                                PVFS_uid uid,
                                uint32_t num_groups,
                                PVFS_gid *group_array); /**/
int PINT_certcache_remove_entry(struct certcache_entry_s *entry); /**/
/* End of Externally Visible Certificate Cache API */

#endif /* _CERTCACHE_H */
#endif /* ENABLE_CERTCACHE */

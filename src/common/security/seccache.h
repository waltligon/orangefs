/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Security cache declarations
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_CACHE_H
#define _SECURITY_CACHE_H

#include <stdint.h>

#include "pvfs2-types.h"
#include "llist.h"
#include "gen-locks.h"

/* macros that debug entering/exiting functions */
#define SECCACHE_ENTER_FN() do { \
                                gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: enter\n", __func__); \
                            } while (0)
#define SECCACHE_EXIT_FN()  do { \
                                gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: exit\n", __func__); \
                            } while (0)

/*** seccache default property values ***/
/* number of entries cache can hold */
#define SECCACHE_ENTRY_LIMIT_DEFAULT     256
/* the total security cache size (64 MB) */
#define SECCACHE_SIZE_LIMIT_DEFAULT      (1024 * 1024 * 64)
/* number of indexes in our chained hash-table */
#define SECCACHE_HASH_LIMIT_DEFAULT      128
/* entry timeout (ms) */
#define SECCACHE_TIMEOUT_DEFAULT         60000
/* frequency (in lookups) to debugging stats */
#define SECCACHE_STATS_FREQ_DEFAULT      1000

/* cache locking */
#define seccache_lock_t gen_mutex_t
#define SECCACHE_LOCK_SIZE sizeof(seccache_lock_t)

typedef enum {
    SECCACHE_ENTRY_LIMIT,
    SECCACHE_SIZE_LIMIT,
    SECCACHE_HASH_LIMIT,
    SECCACHE_TIMEOUT,
    SECCACHE_STATS_FREQ
} seccache_prop_t;

typedef struct seccache_entry_s {
    PVFS_time expiration;
    void *data;
    PVFS_size data_size;
} seccache_entry_t;

#define SECCACHE_ENTRY(entry)    ((seccache_entry_t *) entry)

/* implementation-defined cache functions */
/* note: some entry params declared "void *" for compatibility with
   PINT_llist functions */
typedef struct seccache_methods_s {
    int (*expired)(void *entry1, void *entry2);
    void (*set_expired)(seccache_entry_t *entry, PVFS_time timeout);
    uint16_t (*get_index)(void *data, uint64_t hash_limit);
    int (*compare)(void *data, void *entry);
    void (*cleanup)(void *entry);
    void (*debug)(const char *prefix, void *data);
} seccache_methods_t;

/* cache stats structure */
/* 64-bit aligned */
typedef struct seccache_stats_s {
    uint64_t inserts;
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t removed;
    uint64_t expired;
    uint64_t entry_count;
    PVFS_size cache_size;
} seccache_stats_t;

/* cache structure */
/* 64-bit aligned */
typedef struct seccache_s {
    const char *desc;
    seccache_methods_t methods;
    seccache_lock_t lock;
    uint64_t entry_limit;
    PVFS_size size_limit;
    uint64_t hash_limit;
    PVFS_time timeout;
    uint64_t stats_freq;
    uint64_t stat_count;
    seccache_stats_t stats;
    PINT_llist_p *hash_table;
} seccache_t;

/*** externally visible cache API ***/

/* returns a new security cache structure */
seccache_t *PINT_seccache_new(const char *desc,
                              seccache_methods_t *methods,
                              uint64_t hash_limit);
/* set a security cache property (entry max etc.) */
int PINT_seccache_set(seccache_t *cache,
                      seccache_prop_t prop,
                      uint64_t propval);
/* returns security cache property or MAX_INT on error */
uint64_t PINT_seccache_get(seccache_t *cache,
                           seccache_prop_t prop);

int PINT_seccache_expired_default(void *entry1,
                                  void *entry2);

/* lock cache for special operations */
int PINT_seccache_lock(seccache_t *cache);

/* unlock cache */
int PINT_seccache_unlock(seccache_t *cache);

void PINT_seccache_reset_stats(seccache_t *cache);

/* deletes cache, freeing all memory */
void PINT_seccache_cleanup(seccache_t *cache);

/* locates an entry using default compare method */
seccache_entry_t *PINT_seccache_lookup(seccache_t *cache, 
                                       void *data);

/* locates an entry using specified compare function */
seccache_entry_t *PINT_seccache_lookup_cmp(seccache_t *cache,
                                           int (*compare)(void *, void *),
                                           void *data);

/* inserts an entry with the given data */
int PINT_seccache_insert(seccache_t *cache,
                         void *data,
                         PVFS_size data_size);

/* removes an entry */
int PINT_seccache_remove(seccache_t *cache,
                         seccache_entry_t *entry);

#endif


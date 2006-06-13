/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#ifndef __TCACHE_H
#define __TCACHE_H

#include <sys/time.h>
#include "pvfs2-types.h"
#include "quicklist.h"
#include "quickhash.h"


/** \defgroup tcache Timeout Cache (tcache)
 *
 * The tcache implements a simple component for caching data structures that
 * can be referenced by unique, opaque keys.  A timeout is associated with each 
 * entry to dictate when it will expire.  Specific caches such as the
 * attribute or name cache may be built on top of this one.
 *
 * Notes:
 * - This interface is not thread safe.  Caller must provided any necessary
 * protection against race conditions.
 * - Also note that keys should be considered immutable once an item is
 * inserted into the cache.
 * - The caller is responsible for allocating memory for payloads 
 * and for copying the payload if the information needs to be kept
 * after a lookup.  However, the tcache is responsible for freeing the
 * payload.
 * .
 * Terminology:
 * - DELETE: Process of removing a specific entry at the request of the caller
 * - PURGE: Process of removing an entry because there is not enough room in
 *   the cache (see RECLAIM)
 * - EXPIRED: CACHE entry that is older than CACHE_TIMEOUT_MSECS and is
 *   still in the CACHE
 * - RECLAIM: Process of purging up to CACHE_RECLAIM_PERCENTAGE
 *   entries from the CACHE that are EXPIRED
 * - REFRESH: Process of updating an existing entry in cache with a
 *   new CACHE_TIMEOUT_MSECS
 * .
 * Policies: 
 * - Cached items become EXPIRED after CACHE_TIMEOUT_MSECS from the
 *   time of insertion.  Expiration can be turned off if cache entries
 *   don't decay by setting TCACHE_ENABLE_EXPIRATION to 0.
 * - CACHE_SOFT_LIMIT defined how many entries must exist before a
 *   RECLAIM of cached entries is attempted
 * - CACHE_RECLAIM_PERCENTAGE specifies the maximum number of
 *   entries in the cache that will be PURGED on a single pass. 
 *   The max number of entries will be calculated as
 *   "CACHE_RECLAIM_PERCENTAGE/100 * SOFT_LIMIT"
 * - CACHE_HARD_LIMIT will specify the maximum number of entries
 *   that can exist in the cache.
 * - Once CACHE_HARD_LIMIT is reached, an item that needs to be
 *   cached will replace the least recently used item in the cache.
 * - To turn OFF caching, set the CACHE_TIMEOUT_MSECS to 0 or set the
 *   "enable" option to 0
 * - keys and data cached are void * types
 * - must be provided comparison function to search for cache entries
 * - must be provided hash function to index cache entries
 * .
 *
 * @{
 */

/** \file
 * Declarations for the Timeout Cache (tcache) component.
 */

/** enumeration for algorithms to use to determine how to search the tcache 
 * to find an entry to be replaced.
 * @see PINT_tcache_options
 */
enum PINT_tcache_replace_algorithms
{
    LEAST_RECENTLY_USED = 1, /**< find the least recently used entry */
};

/** Describes a single entry in the tcache. */
struct PINT_tcache_entry
{
    void* payload;                   /**< data to store, must be matchable to a unique key*/
    struct timeval expiration_date;  /**< when the entry will expire */
    struct qhash_head hash_link;     /**< link to primary data structure */
    struct qlist_head lru_list_link; /**< link to time ordered LRU list */
};

/** Describes a tcache instance */
struct PINT_tcache
{
    /** comparison function */
    int (*compare_key_entry)(void* key, struct qhash_head* link);
    /** hash function */
    int (*hash_key)(void* key, int table_size);
    /** function that can be used to free payload pointer */
    int (*free_payload)(void* payload);

    unsigned int timeout_msecs; /**< timeout to use with each entry */
    unsigned int expiration_enabled; /**< do cache entries expire? */
    unsigned int num_entries;   /**< current number of entries in tcache */
    unsigned int hard_limit;    /**< hard limit on number of entries */
    unsigned int soft_limit;    /**< soft limit on number of entries */
    unsigned int reclaim_percentage;    /**< what percentage to reclaim at soft limit */
    enum PINT_tcache_replace_algorithms replacement_algorithm; /**< what algorithm to use to find entry to replace */
    unsigned int enable;        /**< is the cache enabled? */

    /** hash table */
    struct qhash_table* h_table;
    /** lru list */
    struct qlist_head lru_list;
};

/** enumeration of options to get_info() and set_info() calls 
 * @see PINT_tcache_get_info()
 * @see PINT_tcache_set_info()
 */
enum PINT_tcache_options
{
    TCACHE_TIMEOUT_MSECS = 1, /**< get/set tcache timeout value */
    TCACHE_NUM_ENTRIES = 2,   /**< get number of entries (not setable) */
    TCACHE_HARD_LIMIT = 3,    /**< get/set hard limit */
    TCACHE_SOFT_LIMIT = 4,    /**< get/set soft limit */
    TCACHE_ENABLE = 5,        /**< enable/disable tcache */
    TCACHE_RECLAIM_PERCENTAGE = 6,  /**< get/set reclaim percentage */
    TCACHE_REPLACE_ALGORITHM = 7,   /**< determines algorithm to use to find an 
                                      *  entry to replace
                                      */
    TCACHE_ENABLE_EXPIRATION = 8, /**< turn on/off expiration */
};

struct PINT_tcache* PINT_tcache_initialize(
    int (*compare_key_entry) (void *key, struct qhash_head* link),
    int (*hash_key) (void *key, int table_size),
    int (*free_payload) (void* payload),
    int table_size);

void PINT_tcache_finalize(struct PINT_tcache* tcache);

int PINT_tcache_get_info(
    struct PINT_tcache* tcache,
    enum PINT_tcache_options option,
    unsigned int* arg);

int PINT_tcache_set_info(
    struct PINT_tcache* tcache,
    enum PINT_tcache_options option,
    unsigned int arg);

int PINT_tcache_insert_entry(
    struct PINT_tcache* tcache,
    void* key,
    void* payload,
    int* purged);

int PINT_tcache_lookup(
    struct PINT_tcache* tcache,
    void* key,
    struct PINT_tcache_entry** entry,
    int* status);

int PINT_tcache_reclaim(
    struct PINT_tcache* tcache,
    int* reclaimed);

int PINT_tcache_delete(
    struct PINT_tcache* tcache,
    struct PINT_tcache_entry* entry);

int PINT_tcache_refresh_entry(
    struct PINT_tcache* tcache,
    struct PINT_tcache_entry* entry);

#endif /* __TCACHE_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


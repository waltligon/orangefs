/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "tcache.h"
#include "gossip.h"

/** \file
 *  \ingroup tcache
 * Implementation of the Timeout Cache (tcache) component.
 */
enum {
TCACHE_DEFAULT_TIMEOUT_MSECS   = 4000,
TCACHE_DEFAULT_EXPIRATION_ENABLED = 1,
TCACHE_DEFAULT_SOFT_LIMIT     =  5120,
TCACHE_DEFAULT_HARD_LIMIT     = 10240,
TCACHE_DEFAULT_RECLAIM_PERCENTAGE = 25,
TCACHE_DEFAULT_TABLE_SIZE     =  1019,
TCACHE_DEFAULT_REPLACE_ALGORITHM  = LEAST_RECENTLY_USED,
};

static int check_expiration(
    struct PINT_tcache* tcache,
    struct PINT_tcache_entry* entry, /**< tcached entry */
    struct timeval * tv); /**< time interval to check expiration against */
static int tcache_lookup_oldest(
    struct PINT_tcache* tcache,       /**< pointer to tcache instance */
    struct PINT_tcache_entry** entry, /**< tcache entry (output) */
    int* status);                     /**< indicates if the entry is expired or not */

/**
 * Initializes a tcache instance
 * \return pointer to tcache on success, NULL on failure
 */
struct PINT_tcache* PINT_tcache_initialize(
    int (*compare_key_entry) (void *key, struct qhash_head* link), /**< function 
    to compare keys with payloads within entry, return 1 on match, 0 if not match */
    int (*hash_key) (void *key, int table_size), /**< function to hash keys */
    int (*free_payload) (void* payload), /**< function to free payload members (used during reclaim) */
    int table_size) /**< size of hash table to use */
{
    struct PINT_tcache* tcache_tmp = NULL;

    /* check parameters */
    assert(compare_key_entry);
    assert(hash_key);
    assert(free_payload);

    tcache_tmp = (struct PINT_tcache*)calloc(1, sizeof(struct PINT_tcache));
    if(!tcache_tmp)
    {
        return(NULL);
    }

    tcache_tmp->compare_key_entry = compare_key_entry;
    tcache_tmp->hash_key = hash_key;
    tcache_tmp->free_payload = free_payload;

    tcache_tmp->timeout_msecs = TCACHE_DEFAULT_TIMEOUT_MSECS;
    tcache_tmp->expiration_enabled = TCACHE_DEFAULT_EXPIRATION_ENABLED;
    tcache_tmp->soft_limit = TCACHE_DEFAULT_SOFT_LIMIT;
    tcache_tmp->hard_limit = TCACHE_DEFAULT_HARD_LIMIT;
    tcache_tmp->reclaim_percentage = TCACHE_DEFAULT_RECLAIM_PERCENTAGE;
    tcache_tmp->replacement_algorithm = TCACHE_DEFAULT_REPLACE_ALGORITHM;
    tcache_tmp->num_entries = 0;
    tcache_tmp->enable = 1;

    tcache_tmp->h_table = qhash_init(compare_key_entry, hash_key,
        (table_size <= 0 ? TCACHE_DEFAULT_TABLE_SIZE : table_size));
    if(!tcache_tmp->h_table)
    {
        free(tcache_tmp);
        return(NULL);
    }

    INIT_QLIST_HEAD(&(tcache_tmp->lru_list));

    return(tcache_tmp);
}

/** Finalizes and destroys a tcache instance, frees all payloads */
void PINT_tcache_finalize(
    struct PINT_tcache* tcache) /**< tcache instance to destroy */
{
    int i;
    struct qlist_head *iterator = NULL, *scratch = NULL;
    struct PINT_tcache_entry* tmp_entry;

    /* TODO: simplify by iterating LRU list instead of hash table */

    /* iterate through hash table and destroy all entries */
    for(i = 0; i < tcache->h_table->table_size; i++)
    {
        qlist_for_each_safe(iterator, scratch, &(tcache->h_table->array[i]))
        {
            tmp_entry = qlist_entry(iterator, struct PINT_tcache_entry,
                hash_link);
            assert(tmp_entry);

            PINT_tcache_delete(tcache, tmp_entry);
        }
    }

    /* assuming that all entries were destroyed in hash table, then there
     * should be nothing left to clean up in LRU list 
     */
    
    qhash_finalize(tcache->h_table);

    /* make sure that we haven't lost any entries */
    assert(tcache->num_entries == 0);

    free(tcache);
    tcache = NULL;

    return;
}

/**
 * Retrieves parameters from a tcache instance
 * @see PINT_tcache_options
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_get_info(
    struct PINT_tcache* tcache,      /**< pointer to tcache instance */
    enum PINT_tcache_options option, /**< option to read */
    unsigned int* arg)               /**< output value */
{
    int ret = -1;

    assert(arg);

    switch(option)
    {
        case TCACHE_TIMEOUT_MSECS:
            *arg = tcache->timeout_msecs;
            ret = 0;
            break;
        case TCACHE_ENABLE_EXPIRATION:
            *arg = tcache->expiration_enabled;
            ret = 0;
            break;
        case TCACHE_NUM_ENTRIES:
            *arg = tcache->num_entries;
            ret = 0;
            break;
        case TCACHE_HARD_LIMIT:
            *arg = tcache->hard_limit;
            ret = 0;
            break;
        case TCACHE_SOFT_LIMIT:
            *arg = tcache->soft_limit;
            ret = 0;
            break;
        case TCACHE_ENABLE:
            *arg = tcache->enable;
            ret = 0;
            break;
        case TCACHE_RECLAIM_PERCENTAGE:
            *arg = tcache->reclaim_percentage;
            ret = 0;
            break;
        case TCACHE_REPLACE_ALGORITHM:
            *arg = tcache->replacement_algorithm;
            ret = 0;
            break;
        /* leave out "default" on purpose so we get a compile warning if a case
         * is not handled
         */
    }
    return(ret);
}

/**
 * Sets optional parameters on a tcache instance
 * @see PINT_tcache_options
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_set_info(
    struct PINT_tcache* tcache,      /**< pointer to tcache instance */
    enum PINT_tcache_options option, /**< option to modify */
    unsigned int arg)         /**< input value */
{
    int ret = -1;

    switch(option)
    {
        case TCACHE_TIMEOUT_MSECS:
            tcache->timeout_msecs = arg;
            if(tcache->timeout_msecs == 0)
            {
                /* shortcut: disable acache if the timeout is zero to avoid
                 * overhead of storing objects that will immediately expire
                 */
                tcache->enable = 0;
            }
            else
            {
                tcache->enable = 1;
            }
            ret = 0;
            break;
        case TCACHE_ENABLE_EXPIRATION:
            tcache->expiration_enabled = arg;
            ret = 0;
            break;
        case TCACHE_NUM_ENTRIES:
            /* this parameter cannot be set */
            ret = -PVFS_EINVAL;
            break;
        case TCACHE_HARD_LIMIT:
            if(arg < 1)
            {
                return(-PVFS_EINVAL);
            }
            tcache->hard_limit = arg;
            ret = 0;
            break;
        case TCACHE_SOFT_LIMIT:
            if(arg < 1)
            {
                return(-PVFS_EINVAL);
            }
            tcache->soft_limit = arg;
            ret = 0;
            break;
        case TCACHE_ENABLE:
            if(arg != 1 && arg != 0)
            {
                return(-PVFS_EINVAL);
            }
            tcache->enable = arg;
            ret = 0;
            break;
        case TCACHE_RECLAIM_PERCENTAGE:
            if(arg > 100)
            {
                return(-PVFS_EINVAL);
            }
            tcache->reclaim_percentage = arg;
            ret = 0;
            break;
        case TCACHE_REPLACE_ALGORITHM:
            if(arg < 1)
            {
                return(-PVFS_EINVAL);
            }
            tcache->replacement_algorithm = arg;
            ret = 0;
            break;
        /* leave out "default" on purpose so we get a compile warning if a case
         * is not handled
         */
    }
    return(ret);
}

/**
 * Adds an entry to the tcache.  Caller must not retain a pointer to the 
 * payload; it could be destroyed at any time.
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_insert_entry(
    struct PINT_tcache* tcache, /**< pointer to tcache instance */
    void* key,                  /**< that uniquely identifies the payload */
    void* payload,              /**< data to store in the cache */
    int* purged)                /**< number of entries purged to make room */
{
    struct PINT_tcache_entry* tmp_entry = NULL;
    int tmp_status = 0;
    int ret = -1;

    *purged = 0;
  
    if(tcache->enable == 0)
    {
        /* cache has been disabled, do nothing except discard payload*/
        tcache->free_payload(payload);
        return(0);
    }

    /* are we over the soft limit? */
    if(tcache->num_entries >= tcache->soft_limit)
    {
        /* try to reclaim some entries */
        ret = PINT_tcache_reclaim(tcache, purged);
        if(ret < 0)
        {
            return(ret);
        }
    }

    /* are we over the hard limit? */
    if(tcache->num_entries >= tcache->hard_limit)
    {
        /* remove oldest entry. Each algorithm must follow the return interface 
         * definition like tcache_lookup_oldest 
         */
        switch(tcache->replacement_algorithm)
        {
            case LEAST_RECENTLY_USED:
                ret = tcache_lookup_oldest(tcache, &tmp_entry, &tmp_status);
                break;
        }

        if(ret < 0)
        {
            return(ret);
        }
        /* we don't care about the status- we need to remove an entry
         * regardless
         */
        ret = PINT_tcache_delete(tcache, tmp_entry);
        if(ret < 0)
        {
            return(ret);
        }
        *purged = 1;
    }

    /* create new entry */
    tmp_entry = (struct PINT_tcache_entry*)calloc(1, sizeof(struct
        PINT_tcache_entry));
    if(!tmp_entry)
    {
        return(-PVFS_ENOMEM);
    }
    tmp_entry->payload = payload;

    /* set expiration date */
    ret = PINT_tcache_refresh_entry(tcache, tmp_entry);
    if(ret < 0)
    {
        free(tmp_entry);
        return(ret);
    }

    /* add to hash table */
    qhash_add(tcache->h_table, key, &tmp_entry->hash_link);

    /* add to LRU list (tail) */
    qlist_add_tail(&tmp_entry->lru_list_link, &tcache->lru_list);
    
    tcache->num_entries++;

    return(0);
}

/**
 * Looks up an entry.  Subsequent tcache function calls may destroy the entry
 * and payload.  Therefore the caller should copy the payload data if it
 * intends to use it after another tcache function call.
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_lookup(
    struct PINT_tcache* tcache,       /**< pointer to tcache instance */
    void* key,                        /**< that uniquely identifies the payload */
    struct PINT_tcache_entry** entry, /**< tcache entry (output) */
    int* status)                      /**< indicates if the entry is expired or not */
{
    struct qhash_head* link;

    *status = -PVFS_EINVAL;
    *entry = NULL;

    link = qhash_search(tcache->h_table, key);
    if(!link)
    {
        return(-PVFS_ENOENT);
    }

    /* find entry */
    *entry = qlist_entry(link, struct PINT_tcache_entry, hash_link);

    /* check status. Let the function determine expiration */
    *status = check_expiration(tcache, *entry, NULL);

    /* put at tail of LRU */
    qlist_del(&((*entry)->lru_list_link));
    qlist_add_tail(&((*entry)->lru_list_link), &tcache->lru_list);

    return(0);
}

/**
 * Looks up the oldest entry in the tcache.  Subsequent tcache function 
 * calls may destroy the entry and payload.  Therefore the caller should 
 * copy the payload data if it intends to use it after another tcache 
 * function call.
 * \return 0 on success, -PVFS_error on failure
 */
int tcache_lookup_oldest(
    struct PINT_tcache* tcache,       /**< pointer to tcache instance */
    struct PINT_tcache_entry** entry, /**< tcache entry (output) */
    int* status)                      /**< indicates if the entry is expired or not */
{
    *entry = NULL;
    *status = -PVFS_EINVAL;

    if(qlist_empty(&tcache->lru_list))
    {
        return(-PVFS_ENOENT);
    }

    /* find pointer to item at head of LRU list */
    *entry = qlist_entry(tcache->lru_list.next, struct PINT_tcache_entry,
        lru_list_link);
    assert(*entry);

    /* do not move it */

    /* check status. */
    *status = check_expiration(tcache, *entry, NULL);

    return(0);
}

/**
 * Tries to purge and destroy expired entries, up to
 * TCACHE_RECLAIM_PERCENTAGE of the current soft limit value.  The
 * payload_free() function is used to destroy the payload associated with
 * reclaimed entries. 
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_reclaim(
    struct PINT_tcache* tcache, /**< pointer to tcache instance */
    int* reclaimed)             /**< number of entries reclaimed */
{
    struct qlist_head *iterator = NULL, *scratch = NULL;
    struct PINT_tcache_entry* tmp_entry;
    int entries_to_purge = (tcache->reclaim_percentage *
        tcache->soft_limit)/100; 
    int status = 0;
    int ret;
    struct timeval tv;

    *reclaimed = 0;
    
    /* Capture a moment in time to check for expiration. This keeps the 
     * check_expiration function call from constantly having to call 
     * gettimeofday for each tcached entry 
     */
    gettimeofday(&tv, NULL);
    
    /* work down LRU list to try oldest entries */
    qlist_for_each_safe(iterator, scratch, &(tcache->lru_list))
    {
        tmp_entry = qlist_entry(iterator, struct PINT_tcache_entry,
            lru_list_link);
        assert(tmp_entry);

        /* check status */
        status = check_expiration(tcache, tmp_entry, &tv);
        /* break if not expired */
        if(status == 0)
        {
            break;
        }

        /* delete entry otherwise */
        ret = PINT_tcache_delete(tcache, tmp_entry);
        if(ret < 0)
        {
            return(ret);
        }
        entries_to_purge--;
        (*reclaimed)++;
        
        /* break if we hit percentage cap */
        if(entries_to_purge <= 0)
        {
            break;
        }
    }

    return(0);
}

/**
 * Removes and destroys specified tcache entry.  The payload_free() function
 * will be used to destroy payload data.
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_delete(
    struct PINT_tcache* tcache,      /**< pointer to tcache instance */
    struct PINT_tcache_entry* entry) /**< entry to remove and destroy */
{
    /* remove from hash table */
    qhash_del(&entry->hash_link);

    /* remove from lru list */
    qlist_del(&entry->lru_list_link);

    tcache->num_entries--;

    /* destroy payload and entry */
    tcache->free_payload(entry->payload);
    free(entry);

    return(0);
}

/**
 * Updates the timestamp on the specified entry to TCACHE_TIMEOUT
 * milliseconds in the future
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_tcache_refresh_entry(
    struct PINT_tcache* tcache,      /**< pointer to tcache instance */
    struct PINT_tcache_entry* entry) /**< entry to refresh */
{

    if(!tcache->expiration_enabled) return 0;

    gettimeofday(&entry->expiration_date, NULL);
    entry->expiration_date.tv_sec += (tcache->timeout_msecs / 1000);
    entry->expiration_date.tv_usec += ((tcache->timeout_msecs % 1000)*1000);
    if(entry->expiration_date.tv_usec > 1000000)
    {
        entry->expiration_date.tv_usec -= 1000000;
        entry->expiration_date.tv_sec += 1;
    }
    return(0);
}


/* check_expiration()
 *
 * checks to see if a given entry is expired or not
 * 
 * returns -PVFS_ETIME if expired, 0 if not
 */
static int check_expiration(
    struct PINT_tcache* tcache,
    struct PINT_tcache_entry* entry, /* tcached entry */
    struct timeval * tv) /* time interval to check expiration against. Pass in
                            NULL value to have function get current time */
{
    struct timeval local_tv;
    struct timeval *tmp_tv = NULL;

    if(!tcache->expiration_enabled) return 0;

    /* Blindly assign our variable to the passed in parameter, check for 
       validity later */
    tmp_tv = tv;

    /* If the timeval parameter is NULL, we must get a current timeval to check
     * for expiration
     */
    if(tmp_tv == NULL)
    {
        gettimeofday(&local_tv, NULL);
        tmp_tv = &local_tv;
    }

    if(tmp_tv->tv_sec > entry->expiration_date.tv_sec)
    {
        return(-PVFS_ETIME);
    }
    
    if(tmp_tv->tv_sec < entry->expiration_date.tv_sec)
    {
        return(0);
    }

    if(tmp_tv->tv_usec > entry->expiration_date.tv_usec)
    {
        return(-PVFS_ETIME);
    }

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


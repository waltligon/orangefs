/*
 * Copyright © Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */
  
#include <assert.h>
  
#include "pvfs2-attr.h"
#include "ncache.h"
#include "tcache.h"
#include "pint-util.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-internal.h"
#include <string.h>
  
/** \file
 *  \ingroup ncache
 * Implementation of the Name Cache (ncache) component.
 */
  
/* compile time defaults */
enum {
NCACHE_DEFAULT_TIMEOUT_MSECS  =  3000,
NCACHE_DEFAULT_SOFT_LIMIT     =  5120,
NCACHE_DEFAULT_HARD_LIMIT     = 10240,
NCACHE_DEFAULT_RECLAIM_PERCENTAGE = 25,
NCACHE_DEFAULT_REPLACE_ALGORITHM = LEAST_RECENTLY_USED,
};

struct PINT_perf_key ncache_keys[] = 
{
   {"NCACHE_NUM_ENTRIES", PERF_NCACHE_NUM_ENTRIES, PINT_PERF_PRESERVE},
   {"NCACHE_SOFT_LIMIT", PERF_NCACHE_SOFT_LIMIT, PINT_PERF_PRESERVE},
   {"NCACHE_HARD_LIMIT", PERF_NCACHE_HARD_LIMIT, PINT_PERF_PRESERVE},
   {"NCACHE_HITS", PERF_NCACHE_HITS, 0},
   {"NCACHE_MISSES", PERF_NCACHE_MISSES, 0},
   {"NCACHE_UPDATES", PERF_NCACHE_UPDATES, 0},
   {"NCACHE_PURGES", PERF_NCACHE_PURGES, 0},
   {"NCACHE_REPLACEMENTS", PERF_NCACHE_REPLACEMENTS, 0},
   {"NCACHE_DELETIONS", PERF_NCACHE_DELETIONS, 0},
   {"NCACHE_ENABLED", PERF_NCACHE_ENABLED, PINT_PERF_PRESERVE},
   {NULL, 0, 0},
};

/* data to be stored in a cached entry */
struct ncache_payload
{
    PVFS_object_ref entry_ref;      /* PVFS2 object reference to entry */
    PVFS_object_ref parent_ref;     /* PVFS2 object reference to parent */
    int entry_status;               /* is the entry valid? */
    char* entry_name;
};

struct ncache_key
{
    PVFS_object_ref parent_ref;
    const char* entry_name;
};
  
static struct PINT_tcache* ncache = NULL;
static gen_mutex_t ncache_mutex = GEN_MUTEX_INITIALIZER;
  
static int ncache_compare_key_entry(void* key, struct qhash_head* link);
static int ncache_hash_key(void* key, int table_size);
static int ncache_free_payload(void* payload);
static struct PINT_perf_counter* ncache_pc = NULL;

/**
 * Enables perf counter instrumentation of the ncache
 */
void PINT_ncache_enable_perf_counter(
    struct PINT_perf_counter* pc)     /**< perf counter instance to use */
{
    gen_mutex_lock(&ncache_mutex);

    ncache_pc = pc;
    assert(ncache_pc);

    /* set initial values */
    PINT_perf_count(ncache_pc, PERF_NCACHE_SOFT_LIMIT,
        ncache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(ncache_pc, PERF_NCACHE_HARD_LIMIT,
        ncache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(ncache_pc, PERF_NCACHE_ENABLED,
        ncache->enable, PINT_PERF_SET);

    gen_mutex_unlock(&ncache_mutex);

    return;
}

/**
 * Initializes the ncache 
 * \return pointer to tcache on success, NULL on failure
 */
int PINT_ncache_initialize(void)
{
    int ret = -1;
    unsigned int ncache_timeout_msecs;
    char * ncache_timeout_str = NULL;
  
    gen_mutex_lock(&ncache_mutex);
  
    /* create tcache instance */
    ncache = PINT_tcache_initialize(ncache_compare_key_entry,
                                    ncache_hash_key,
                                    ncache_free_payload,
                                    -1 /* default tcache table size */);
    if(!ncache)
    {
        gen_mutex_unlock(&ncache_mutex);
        return(-PVFS_ENOMEM);
    }
  
    /* fill in defaults that are specific to ncache */
    ncache_timeout_str = getenv("PVFS2_NCACHE_TIMEOUT");
    if (ncache_timeout_str != NULL) 
        ncache_timeout_msecs = (unsigned int)strtoul(ncache_timeout_str,NULL,0);
    else
        ncache_timeout_msecs = NCACHE_DEFAULT_TIMEOUT_MSECS;

    ret = PINT_tcache_set_info(ncache, TCACHE_TIMEOUT_MSECS,
                               ncache_timeout_msecs);
                
    if(ret < 0)
    {
        PINT_tcache_finalize(ncache);
        gen_mutex_unlock(&ncache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(ncache, TCACHE_HARD_LIMIT, 
                               NCACHE_DEFAULT_HARD_LIMIT);
    if(ret < 0)
    {
        PINT_tcache_finalize(ncache);
        gen_mutex_unlock(&ncache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(ncache, TCACHE_SOFT_LIMIT, 
                               NCACHE_DEFAULT_SOFT_LIMIT);
    if(ret < 0)
    {
        PINT_tcache_finalize(ncache);
        gen_mutex_unlock(&ncache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(ncache, TCACHE_RECLAIM_PERCENTAGE,
                               NCACHE_DEFAULT_RECLAIM_PERCENTAGE);
    if(ret < 0)
    {
        PINT_tcache_finalize(ncache);
        gen_mutex_unlock(&ncache_mutex);
        return(ret);
    }
  
    gen_mutex_unlock(&ncache_mutex);
    return(0);
}
  
/** Finalizes and destroys the ncache, frees all cached entries */
void PINT_ncache_finalize(void)
{
    gen_mutex_lock(&ncache_mutex);

    assert(ncache != NULL);
    PINT_tcache_finalize(ncache);
    ncache = NULL;

    gen_mutex_unlock(&ncache_mutex);
    return;
}
  
/**
 * Retrieves parameters from the ncache 
 * @see PINT_tcache_options
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_ncache_get_info(
    enum PINT_ncache_options option, /**< option to read */
    unsigned int* arg)               /**< output value */
{
    int ret = -1;
  
    gen_mutex_lock(&ncache_mutex);
    ret = PINT_tcache_get_info(ncache, option, arg);
    gen_mutex_unlock(&ncache_mutex);
  
    return(ret);
}
  
/**
 * Sets optional parameters in the ncache
 * @see PINT_tcache_options
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_ncache_set_info(
    enum PINT_ncache_options option, /**< option to modify */
    unsigned int arg)                /**< input value */
{
    int ret = -1;
  
    gen_mutex_lock(&ncache_mutex);
    ret = PINT_tcache_set_info(ncache, option, arg);

    /* record any resulting parameter changes */
    PINT_perf_count(ncache_pc, PERF_NCACHE_SOFT_LIMIT,
        ncache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(ncache_pc, PERF_NCACHE_HARD_LIMIT,
        ncache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(ncache_pc, PERF_NCACHE_ENABLED,
        ncache->enable, PINT_PERF_SET);
    PINT_perf_count(ncache_pc, PERF_NCACHE_NUM_ENTRIES,
        ncache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&ncache_mutex);

    return(ret);
}
  
/** 
 * Retrieves a _copy_ of a cached object reference, and reports the
 * status to indicate if they are valid or  not
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_ncache_get_cached_entry(
    const char* entry,                 /**< path of obect to look up*/
    PVFS_object_ref* entry_ref,        /**< PVFS2 object looked up */
    const PVFS_object_ref* parent_ref) /**< Parent of PVFS2 object */
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct ncache_payload* tmp_payload;
    struct ncache_key entry_key;
    int status;

    gossip_debug(GOSSIP_NCACHE_DEBUG, 
                 "ncache: get_cached_entry(): [%s]\n",entry);
  
    entry_key.entry_name = entry;
    entry_key.parent_ref.handle = parent_ref->handle;
    entry_key.parent_ref.fs_id = parent_ref->fs_id;

    gen_mutex_lock(&ncache_mutex);

    /* lookup entry */
    ret = PINT_tcache_lookup(ncache, (void *) &entry_key, &tmp_entry, &status);
    if(ret < 0 || status != 0)
    {
        gossip_debug(GOSSIP_NCACHE_DEBUG, 
            "ncache: miss: name=[%s]\n", entry_key.entry_name);
        PINT_perf_count(ncache_pc, PERF_NCACHE_MISSES, 1, PINT_PERF_ADD);
        gen_mutex_unlock(&ncache_mutex);
        /* Return -PVFS_ENOENT if the entry has expired */
        if(status != 0)
        {   
            return(-PVFS_ENOENT);
        }
        return(ret);
    }
    tmp_payload = tmp_entry->payload;
  
    gossip_debug(GOSSIP_NCACHE_DEBUG, "ncache: status=%d, entry_status=%d\n",
                 status, tmp_payload->entry_status);

    /* copy out entry ref if valid */
    if(tmp_payload->entry_status == 0 && 
       tmp_payload->parent_ref.handle == parent_ref->handle)
    {
        gossip_debug(GOSSIP_NCACHE_DEBUG, "ncache: copying out ref.\n");
        *entry_ref = tmp_payload->entry_ref;
    }
  
    if(tmp_payload->entry_status == 0) 
    {
        /* return success if we got _anything_ out of the cache */
        PINT_perf_count(ncache_pc, PERF_NCACHE_HITS, 1, PINT_PERF_ADD);
        gen_mutex_unlock(&ncache_mutex);
        return(0);
    }

    gen_mutex_unlock(&ncache_mutex);
  
    PINT_perf_count(ncache_pc, PERF_NCACHE_MISSES, 1, PINT_PERF_ADD);
    return(-PVFS_ETIME);
}
  
/**
 * Invalidates a cache entry (if present)
 */
void PINT_ncache_invalidate(
    const char* entry,                  /**< path of obect */
    const PVFS_object_ref* parent_ref)  /**< Parent of PVFS2 object */
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct ncache_key entry_key;
    int tmp_status;
  
    gossip_debug(GOSSIP_NCACHE_DEBUG, "ncache: invalidate(): entry=%s\n",
                 entry);
  
    gen_mutex_lock(&ncache_mutex);
  
    entry_key.entry_name = entry;
    entry_key.parent_ref.handle = parent_ref->handle;
    entry_key.parent_ref.fs_id = parent_ref->fs_id;

    /* find out if the entry is in the cache */
    ret = PINT_tcache_lookup(ncache, 
                             &entry_key,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        PINT_tcache_delete(ncache, tmp_entry);
        PINT_perf_count(ncache_pc, PERF_NCACHE_DELETIONS, 1,
                        PINT_PERF_ADD);
    }

    PINT_perf_count(ncache_pc, PERF_NCACHE_NUM_ENTRIES,
                    ncache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&ncache_mutex);
    return;
}
  
/** 
 * Adds a name to the cache, or updates it if already present.  
 * The given name is _copied_ into the cache.   
 *
 * \note NOTE: All previous information for the object will be discarded,
 * even if there is still time remaining before it expires.
 *
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_ncache_update(
    const char* entry,                     /**< entry to update */
    const PVFS_object_ref* entry_ref,      /**< entry ref to update */
    const PVFS_object_ref* parent_ref)     /**< parent ref to update */
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct ncache_payload* tmp_payload;
    struct ncache_key entry_key;
    int status;
    int purged;
    unsigned int enabled;

    /* skip out immediately if the cache is disabled */
    PINT_tcache_get_info(ncache, TCACHE_ENABLE, &enabled);
    if(!enabled)
    {
        return(0);
    }
    
    gossip_debug(GOSSIP_NCACHE_DEBUG, "ncache: update(): name [%s]\n",entry);
  
    if(!entry_ref->handle)
    {
        return(-PVFS_EINVAL);
    }
  
    /* create new payload with updated information */
    tmp_payload = (struct ncache_payload*) 
                        calloc(1,sizeof(struct ncache_payload));
    if(tmp_payload == NULL)
    {
        return(-PVFS_ENOMEM);
    }

    tmp_payload->parent_ref.handle = parent_ref->handle;
    tmp_payload->parent_ref.fs_id = parent_ref->fs_id;
    tmp_payload->entry_ref.handle = entry_ref->handle;
    tmp_payload->entry_ref.fs_id = entry_ref->fs_id;

    tmp_payload->entry_status = 0;
    tmp_payload->entry_name = (char*) calloc(1, strlen(entry) + 1);
    if(tmp_payload->entry_name == NULL)
    {
        free(tmp_payload);
        return(-PVFS_ENOMEM);
    }
    memcpy(tmp_payload->entry_name, entry, strlen(entry) + 1);

    gen_mutex_lock(&ncache_mutex);

    entry_key.entry_name = entry;
    entry_key.parent_ref.handle = parent_ref->handle;
    entry_key.parent_ref.fs_id = parent_ref->fs_id;

    /* find out if the entry is already in the cache */
    ret = PINT_tcache_lookup(ncache, 
                             &entry_key,
                             &tmp_entry,
                             &status);
    if(ret == 0)
    {
        /* found match in cache; destroy old payload, replace, and
         * refresh time stamp
         */
        ncache_free_payload(tmp_entry->payload);
        tmp_entry->payload = tmp_payload;
        ret = PINT_tcache_refresh_entry(ncache, tmp_entry);
        PINT_perf_count(ncache_pc, PERF_NCACHE_UPDATES, 1, PINT_PERF_ADD);
    }
    else
    {
        /* not found in cache; insert new payload*/
        ret = PINT_tcache_insert_entry(ncache, 
                                       &entry_key,
                                       tmp_payload, 
                                       &purged);
        /* the purged variable indicates how many entries had to be purged
         * from the tcache to make room for this new one
         */
        if(purged == 1)
        {
            /* since only one item was purged, we count this as one item being
             * replaced rather than as a purge and an insert
             */
            PINT_perf_count(ncache_pc, PERF_NCACHE_REPLACEMENTS,purged,
                PINT_PERF_ADD);
        }
        else
        {
            /* otherwise we just purged as part of reclaimation */
            /* if we didn't purge anything, then the "purged" variable will
             * be zero and this counter call won't do anything.
             */
            PINT_perf_count(ncache_pc, PERF_NCACHE_PURGES, purged,
                PINT_PERF_ADD);
        }
    }
    
    PINT_perf_count(ncache_pc, PERF_NCACHE_NUM_ENTRIES,
        ncache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&ncache_mutex);
  
    /* cleanup if we did not succeed for some reason */
    if(ret < 0)
    {
        ncache_free_payload(tmp_payload);
    }
  
    gossip_debug(GOSSIP_NCACHE_DEBUG, "ncache: update(): return=%d\n", ret);
    return(ret);
}
  
/* ncache_compare_key_entry()
 *
 * compares an opaque key (char* in this case) against a payload to see
 * if there is a match
 *
 * returns 1 on match, 0 otherwise
 */
static int ncache_compare_key_entry(void* key, struct qhash_head* link)
{
    struct ncache_key* real_key = (struct ncache_key*)key;
    struct ncache_payload* tmp_payload = NULL;
    struct PINT_tcache_entry* tmp_entry = NULL;
  
    tmp_entry = qhash_entry(link, struct PINT_tcache_entry, hash_link);
    assert(tmp_entry);

    tmp_payload = (struct ncache_payload*)tmp_entry->payload;
     /* If the following aren't equal, we know we don't have a match... Maybe
     * these integer comparisons will be quicker than a strcmp each time?
     *   - parent_ref.handle 
     *   - parent_ref.fs_id
     *   - entry_name length
     */
    if( real_key->parent_ref.handle  != tmp_payload->parent_ref.handle ||
        real_key->parent_ref.fs_id   != tmp_payload->parent_ref.fs_id  ||
        strlen(real_key->entry_name) != strlen(tmp_payload->entry_name) )
    {
        /* One of the above cases failed, so we know these aren't a match */
        return(0);
    }
    
    if( strcmp(real_key->entry_name, tmp_payload->entry_name) == 0 )
    {
        /* The strings matches */
        return(1);
    }

    return(0);
}
  
/* ncache_hash_key()
 *
 * hash function for character pointers
 *
 * returns hash index 
 */
static int ncache_hash_key(void* key, int table_size)
{
    struct ncache_key* real_key = (struct ncache_key*) key;
    int tmp_ret = 0;
    unsigned int sum = 0, i = 0;

    while(real_key->entry_name[i] != '\0')
    {
        sum += (unsigned int) real_key->entry_name[i];
        i++;
    }
    sum += real_key->parent_ref.handle + real_key->parent_ref.fs_id;
    tmp_ret =  sum % table_size;
    return(tmp_ret);
}
  
/* ncache_free_payload()
 *
 * frees payload that has been stored in the ncache 
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int ncache_free_payload(void* payload)
{
    struct ncache_payload* tmp_payload = (struct ncache_payload*)payload;
  
    free(tmp_payload->entry_name);
    free(tmp_payload);
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


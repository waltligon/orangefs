/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */
  
#include <assert.h>
  
#include "pvfs2-attr.h"
#include "acache.h"
#include "tcache.h"
#include "pint-util.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-internal.h"
  
/** \file
 *  \ingroup acache
 * Implementation of the Attribute Cache (acache) component.
 */
  
/* compile time defaults */
#define ACACHE_DEFAULT_TIMEOUT_MSECS    5000
#define ACACHE_DEFAULT_SOFT_LIMIT       5120
#define ACACHE_DEFAULT_HARD_LIMIT      10240
#define ACACHE_DEFAULT_RECLAIM_PERCENTAGE 25
#define ACACHE_DEFAULT_REPLACE_ALGORITHM   LEAST_RECENTLY_USED

struct PINT_perf_key acache_keys[] = 
{
   {"ACACHE_NUM_ENTRIES", PERF_ACACHE_NUM_ENTRIES, PINT_PERF_PRESERVE},
   {"ACACHE_SOFT_LIMIT", PERF_ACACHE_SOFT_LIMIT, PINT_PERF_PRESERVE},
   {"ACACHE_HARD_LIMIT", PERF_ACACHE_HARD_LIMIT, PINT_PERF_PRESERVE},
   {"ACACHE_HITS", PERF_ACACHE_HITS, 0},
   {"ACACHE_MISSES", PERF_ACACHE_MISSES, 0},
   {"ACACHE_UPDATES", PERF_ACACHE_UPDATES, 0},
   {"ACACHE_PURGES", PERF_ACACHE_PURGES, 0},
   {"ACACHE_REPLACEMENTS", PERF_ACACHE_REPLACEMENTS, 0},
   {"ACACHE_ENABLED", PERF_ACACHE_ENABLED, PINT_PERF_PRESERVE},
   {NULL, 0, 0},
};

/* data to be stored in a cached entry */
struct acache_payload
{
    PVFS_object_ref refn;    /* PVFS2 object reference */
    PVFS_object_attr attr;   /* cached attributes */  
    int attr_status;         /* are the attributes valid? */
    PVFS_size size;          /* cached size */
    int size_status;         /* is the size valid? */
};
  
static struct PINT_tcache* acache = NULL;
static gen_mutex_t acache_mutex = GEN_MUTEX_INITIALIZER;
  
static int acache_compare_key_entry(void* key, struct qhash_head* link);
static int acache_hash_key(void* key, int table_size);
static int acache_free_payload(void* payload);
static struct PINT_perf_counter* acache_pc = NULL;

/**
 * Enables perf counter instrumentation of the acache
 */
void PINT_acache_enable_perf_counter(
    struct PINT_perf_counter* pc)     /**< perf counter instance to use */
{
    gen_mutex_lock(&acache_mutex);

    acache_pc = pc;
    assert(acache_pc);

    /* set initial values */
    PINT_perf_count(acache_pc, PERF_ACACHE_SOFT_LIMIT,
        acache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_HARD_LIMIT,
        acache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_ENABLED,
        acache->enable, PINT_PERF_SET);

    gen_mutex_unlock(&acache_mutex);

    return;
}

/**
 * Initializes the acache 
 * \return pointer to tcache on success, NULL on failure
 */
int PINT_acache_initialize(void)
{
    int ret = -1;
  
    gen_mutex_lock(&acache_mutex);
  
    /* create tcache instance */
    acache = PINT_tcache_initialize(acache_compare_key_entry,
                                    acache_hash_key,
                                    acache_free_payload);
    if(!acache)
    {
        gen_mutex_unlock(&acache_mutex);
        return(-PVFS_ENOMEM);
    }
  
    /* fill in defaults that are specific to acache */
    ret = PINT_tcache_set_info(acache, TCACHE_TIMEOUT_MSECS,
                               ACACHE_DEFAULT_TIMEOUT_MSECS);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(acache, TCACHE_HARD_LIMIT, 
                               ACACHE_DEFAULT_HARD_LIMIT);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(acache, TCACHE_SOFT_LIMIT, 
                               ACACHE_DEFAULT_SOFT_LIMIT);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(acache, TCACHE_RECLAIM_PERCENTAGE,
                               ACACHE_DEFAULT_RECLAIM_PERCENTAGE);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
  
    gen_mutex_unlock(&acache_mutex);
    return(0);
}
  
/** Finalizes and destroys the acache, frees all cached entries */
void PINT_acache_finalize(void)
{
    gen_mutex_lock(&acache_mutex);

    assert(acache != NULL);
    PINT_tcache_finalize(acache);
    acache = NULL;

    gen_mutex_unlock(&acache_mutex);
    return;
}
  
/**
 * Retrieves parameters from the acache 
 * @see PINT_tcache_options
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_acache_get_info(
    enum PINT_acache_options option, /**< option to read */
    unsigned int* arg)                   /**< output value */
{
    int ret = -1;
  
    gen_mutex_lock(&acache_mutex);
    ret = PINT_tcache_get_info(acache, option, arg);
    gen_mutex_unlock(&acache_mutex);
  
    return(ret);
}
  
/**
 * Sets optional parameters in the acache
 * @see PINT_tcache_options
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_acache_set_info(
    enum PINT_acache_options option, /**< option to modify */
    unsigned int arg)             /**< input value */
{
    int ret = -1;
  
    gen_mutex_lock(&acache_mutex);
    ret = PINT_tcache_set_info(acache, option, arg);

    /* record any resulting parameter changes */
    PINT_perf_count(acache_pc, PERF_ACACHE_SOFT_LIMIT,
        acache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_HARD_LIMIT,
        acache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_ENABLED,
        acache->enable, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES,
        acache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&acache_mutex);

    return(ret);
}
  
/** 
 * Retrieves a _copy_ of a cached attributes structure.  Also retrieves the
 * logical file size (if the object in question is a file) and reports the
 * status of both the attributes and size to indicate if they are valid or
 * not
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_acache_get_cached_entry(
    PVFS_object_ref refn,  /**< PVFS2 object to look up */
    PVFS_object_attr* attr,/**< attributes of the object */
    int* attr_status,      /**< indicates if the attributes are expired or not */
    PVFS_size* size,       /**< logical size of the object (only valid for files) */
    int* size_status)      /**< indicates if the size has expired or not */
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct acache_payload* tmp_payload;
    int status;
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: get_cached_entry(): H=%llu\n",
                 llu(refn.handle));
  
    /* assume everything is timed out for starters */
    *attr_status = -PVFS_ETIME;
    *size_status = -PVFS_ETIME;
  
    gen_mutex_lock(&acache_mutex);
  
    /* lookup entry */
    ret = PINT_tcache_lookup(acache, &refn, &tmp_entry, &status);
    if(ret < 0 || status != 0)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: miss: H=%llu\n",
                     llu(refn.handle));
        PINT_perf_count(acache_pc, PERF_ACACHE_MISSES, 1, PINT_PERF_ADD);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
    tmp_payload = tmp_entry->payload;
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: status=%d, attr_status=%d, size_status=%d\n",
                 status, tmp_payload->attr_status, tmp_payload->size_status);

    /* copy out attributes if valid */
    if(tmp_payload->attr_status == 0)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: copying out attr.\n");
        ret = PINT_copy_object_attr(attr, &(tmp_payload->attr));
        if(ret < 0)
        {
            gen_mutex_unlock(&acache_mutex);
            return(ret);
        }
        *attr_status = 0;
    }
  
    /* copy out size if valid */
    if(tmp_payload->size_status == 0)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: copying out size.\n");
        *size = tmp_payload->size;
        *size_status = 0;
    }
  
    gen_mutex_unlock(&acache_mutex);
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, 
                 "acache: hit: H=%llu, "
                 "size_status=%d, attr_status=%d\n",
                 llu(refn.handle), *size_status, *attr_status);
  
    if(*size_status == 0 || *attr_status == 0)
    {
        /* return success if we got _anything_ out of the cache */
        PINT_perf_count(acache_pc, PERF_ACACHE_HITS, 1, PINT_PERF_ADD);
        return(0);
    }
  
    PINT_perf_count(acache_pc, PERF_ACACHE_MISSES, 1, PINT_PERF_ADD);
    return(-PVFS_ETIME);
}
  
/**
 * Invalidates a cache entry (if present)
 */
void PINT_acache_invalidate(
    PVFS_object_ref refn)
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    int tmp_status;
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: invalidate(): H=%llu\n",
                 llu(refn.handle));
  
    gen_mutex_lock(&acache_mutex);
  
    /* find out if the entry is in the cache */
    ret = PINT_tcache_lookup(acache, 
                             &refn,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        PINT_tcache_purge(acache, tmp_entry);
    }

    PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES,
                    acache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&acache_mutex);
    return;
}
  
  
/**
 * Invalidates only the logical size assocated with an entry (if present)
 */
void PINT_acache_invalidate_size(
    PVFS_object_ref refn)
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct acache_payload* tmp_payload;
    int tmp_status;
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: invalidate_size(): H=%llu\n",
                 llu(refn.handle));
  
    gen_mutex_lock(&acache_mutex);

    /* find out if the entry is in the cache */
    ret = PINT_tcache_lookup(acache, 
                             &refn,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        /* found match in cache; set size to invalid */
        tmp_payload = tmp_entry->payload;
        tmp_payload->size_status = -PVFS_ETIME;
    }
  
    PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES,
                    acache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&acache_mutex);
    return;
}
  
/** 
 * Adds a set of attributes to the cache, or updates them if they are already
 * present.  The given attributes are _copied_ into the cache.   Size
 * parameter will not be updated if it is NULL.
 *
 * \note NOTE: All previous attribute and size information for the object
 * will be discarded, even if there is still time remaining before it expires
 * and the new attributes and/or size contain less information.
 *
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_acache_update(
    PVFS_object_ref refn,   /**< object to update */
    PVFS_object_attr *attr, /**< attributes to copy into cache */
    PVFS_size* size)        /**< logical file size (NULL if not available) */
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct acache_payload* tmp_payload;
    int status;
    int removed;
    unsigned int enabled;

    /* skip out immediately if the cache is disabled */
    PINT_tcache_get_info(acache, TCACHE_ENABLE, &enabled);
    if(!enabled)
    {
        return(0);
    }
    
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: update(): H=%llu\n",
                 llu(refn.handle));
  
    if(!attr && !size)
    {
        return(-PVFS_EINVAL);
    }
  
    /* create new payload with updated information */
    tmp_payload = (struct acache_payload*)calloc(1, sizeof(struct
                                                           acache_payload));
    tmp_payload->refn = refn;
    tmp_payload->attr_status = -PVFS_ETIME;
    tmp_payload->size_status = -PVFS_ETIME;
  
    /* fill in attributes */
    if(attr)
    {
        ret = PINT_copy_object_attr(&(tmp_payload->attr), attr);
        if(ret < 0)
        {
            free(tmp_payload);
            return(ret);
        }
        tmp_payload->attr_status = 0;
    }
  
    /* fill in size */
    if(size)
    {
        tmp_payload->size = *size;
        tmp_payload->size_status = 0;
    }
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: update(): attr_status=%d, size_status=%d\n",
                 tmp_payload->attr_status, tmp_payload->size_status);
  
    gen_mutex_lock(&acache_mutex);

    /* find out if the entry is already in the cache */
    ret = PINT_tcache_lookup(acache, 
                             &refn,
                             &tmp_entry,
                             &status);
    if(ret == 0)
    {
        /* found match in cache; destroy old payload, replace, and
         * refresh time stamp
         */
        acache_free_payload(tmp_entry->payload);
        tmp_entry->payload = tmp_payload;
        ret = PINT_tcache_refresh_entry(acache, tmp_entry);
        PINT_perf_count(acache_pc, PERF_ACACHE_UPDATES, 1, PINT_PERF_ADD);
    }
    else
    {
        /* not found in cache; insert new payload*/
        ret = PINT_tcache_insert_entry(acache, &refn, tmp_payload, &removed);
        if(removed == 1)
        {
            /* assume an entry was replaced */
            PINT_perf_count(acache_pc, PERF_ACACHE_REPLACEMENTS, removed,
                PINT_PERF_ADD);
        }
        else
        {
            /* otherwise we just purged as part of reclaimation */
            /* NOTE: it is ok if the removed value happens to be zero */
            PINT_perf_count(acache_pc, PERF_ACACHE_PURGES, removed,
                PINT_PERF_ADD);
        }
    }
    PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES,
        acache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&acache_mutex);
  
    /* cleanup if we did not succeed for some reason */
    if(ret < 0)
    {
        acache_free_payload(tmp_payload);
    }
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: update(): return=%d\n", ret);
  
    return(ret);
}
  
/* acache_compare_key_entry()
 *
 * compares an opaque key (object ref in this case) against a payload to see
 * if there is a match
 *
 * returns 1 on match, 0 otherwise
 */
static int acache_compare_key_entry(void* key, struct qhash_head* link)
{
    PVFS_object_ref* real_key = (PVFS_object_ref*)key;
    struct acache_payload* tmp_payload = NULL;
    struct PINT_tcache_entry* tmp_entry = NULL;
  
    tmp_entry = qlist_entry(link, struct PINT_tcache_entry, hash_link);
    assert(tmp_entry);
  
    tmp_payload = (struct acache_payload*)tmp_entry->payload;
    if(real_key->handle == tmp_payload->refn.handle &&
       real_key->fs_id == tmp_payload->refn.fs_id)
    {
        return(1);
    }
  
    return(0);
}
  
/* acache_hash_key()
 *
 * hash function for object references
 *
 * returns hash index 
 */
static int acache_hash_key(void* key, int table_size)
{
    PVFS_object_ref* real_key = (PVFS_object_ref*)key;
    int tmp_ret = 0;

    tmp_ret = (real_key->handle)%table_size;
    return(tmp_ret);
}
  
/* acache_free_payload()
 *
 * frees payload that has been stored in the acache 
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int acache_free_payload(void* payload)
{
    struct acache_payload* tmp_payload = (struct acache_payload*)payload;
  
    PINT_free_object_attr(&tmp_payload->attr);
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


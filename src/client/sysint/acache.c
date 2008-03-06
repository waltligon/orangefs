/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */
  
#include <assert.h>
#include <string.h>
  
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
#define ACACHE_DEFAULT_TIMEOUT_MSECS 5000
#define ACACHE_DEFAULT_SOFT_LIMIT 5120
#define ACACHE_DEFAULT_HARD_LIMIT 10240
#define ACACHE_DEFAULT_RECLAIM_PERCENTAGE 25
#define ACACHE_DEFAULT_REPLACE_ALGORITHM LEAST_RECENTLY_USED

/* this one is modeled after TROVE_DEFAULT_HANDLE_PURGATORY_SEC */
#define STATIC_ACACHE_DEFAULT_TIMEOUT_MSECS 360000

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
   {"ACACHE_DELETIONS", PERF_ACACHE_DELETIONS, 0},
   {"ACACHE_ENABLED", PERF_ACACHE_ENABLED, PINT_PERF_PRESERVE},
   {NULL, 0, 0},
};

/* non-static data to be stored in a cached entry */
struct acache_payload
{
    PVFS_object_ref refn;    /* PVFS2 object reference */
    PVFS_object_attr attr;   /* cached attributes */  
    int attr_status;         /* are the attributes valid? */
    PVFS_size size;          /* cached size */
    int size_status;         /* is the size valid? */
};
 
/* static data to be stored in a cached entry */
struct static_payload
{
    PVFS_object_ref refn;    /* PVFS2 object reference */
    uint32_t mask;

    /* static fields that can be cached separately */
    PVFS_ds_type objtype;
    PINT_dist *dist;
    uint32_t dist_size;
    PVFS_handle *dfile_array;
    uint32_t dfile_count;
};
  
static struct PINT_tcache* acache = NULL;
static struct PINT_tcache* static_acache = NULL;
static gen_mutex_t acache_mutex = GEN_MUTEX_INITIALIZER;
  
static int acache_compare_key_entry(void* key, struct qhash_head* link);
static int acache_free_payload(void* payload);
static int static_compare_key_entry(void* key, struct qhash_head* link);
static int static_free_payload(void* payload);

static int acache_hash_key(void* key, int table_size);
static struct PINT_perf_counter* acache_pc = NULL;
static struct PINT_perf_counter* static_pc = NULL;
static int set_tcache_defaults(struct PINT_tcache* instance);

static void load_payload(struct PINT_tcache* instance, 
    PVFS_object_ref refn,
    void* payload,
    struct PINT_perf_counter* pc);

/**
 * Enables perf counter instrumentation of the acache
 */
void PINT_acache_enable_perf_counter(
    struct PINT_perf_counter* pc_in, /**< counter for non static fields */
    struct PINT_perf_counter* static_pc_in) /**< counter for static fields */
{
    gen_mutex_lock(&acache_mutex);

    acache_pc = pc_in;
    assert(acache_pc);

    static_pc = static_pc_in;
    assert(static_pc);

    /* set initial values */
    PINT_perf_count(acache_pc, PERF_ACACHE_SOFT_LIMIT,
        acache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_HARD_LIMIT,
        acache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(acache_pc, PERF_ACACHE_ENABLED,
        acache->enable, PINT_PERF_SET);

    PINT_perf_count(static_pc, PERF_ACACHE_SOFT_LIMIT,
        static_acache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(static_pc, PERF_ACACHE_HARD_LIMIT,
        static_acache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(static_pc, PERF_ACACHE_ENABLED,
        static_acache->enable, PINT_PERF_SET);

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
  
    /* create tcache instances */
    acache = PINT_tcache_initialize(acache_compare_key_entry,
                                    acache_hash_key,
                                    acache_free_payload,
                                    -1 /* default tcache table size */);
    if(!acache)
    {
        gen_mutex_unlock(&acache_mutex);
        return(-PVFS_ENOMEM);
    }
 
    static_acache = PINT_tcache_initialize(static_compare_key_entry,
                                    acache_hash_key,
                                    static_free_payload,
                                    -1 /* default tcache table size */);
    if(!static_acache)
    {
        PINT_tcache_finalize(acache);
        gen_mutex_unlock(&acache_mutex);
        return(-PVFS_ENOMEM);
    }
  
    /* fill in defaults that are specific to non-static cache */
    ret = PINT_tcache_set_info(acache, TCACHE_TIMEOUT_MSECS,
                               ACACHE_DEFAULT_TIMEOUT_MSECS);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        PINT_tcache_finalize(static_acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }

    /* fill in defaults that are specific to static cache */
    ret = PINT_tcache_set_info(static_acache, TCACHE_TIMEOUT_MSECS,
                               STATIC_ACACHE_DEFAULT_TIMEOUT_MSECS);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        PINT_tcache_finalize(static_acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }

    /* fill in defaults that are common to both */
    ret = set_tcache_defaults(acache);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        PINT_tcache_finalize(static_acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
 
    ret = set_tcache_defaults(static_acache);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        PINT_tcache_finalize(static_acache);
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

    PINT_tcache_finalize(acache);
    PINT_tcache_finalize(static_acache);
    acache = NULL;
    static_acache = NULL;

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

    if(option & STATIC_ACACHE_OPT)
    {
        /* this is a static acache option; strip mask and pass along to
         * tcache
         */
        option -= STATIC_ACACHE_OPT;
        ret = PINT_tcache_get_info(static_acache, option, arg);
    }
    else
    {
        ret = PINT_tcache_get_info(acache, option, arg);
    }
  
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

    if(option & STATIC_ACACHE_OPT)
    {
        /* this is a static acache option; strip mask and pass along to
         * tcache
         */
        option -= STATIC_ACACHE_OPT;
        ret = PINT_tcache_set_info(static_acache, option, arg);

        /* record any parameter changes that may have resulted*/
        PINT_perf_count(static_pc, PERF_ACACHE_SOFT_LIMIT,
            static_acache->soft_limit, PINT_PERF_SET);
        PINT_perf_count(static_pc, PERF_ACACHE_HARD_LIMIT,
            static_acache->hard_limit, PINT_PERF_SET);
        PINT_perf_count(static_pc, PERF_ACACHE_ENABLED,
            static_acache->enable, PINT_PERF_SET);
        PINT_perf_count(static_pc, PERF_ACACHE_NUM_ENTRIES,
            static_acache->num_entries, PINT_PERF_SET);
    }
    else
    {
        ret = PINT_tcache_set_info(acache, option, arg);

        /* record any parameter changes that may have resulted*/
        PINT_perf_count(acache_pc, PERF_ACACHE_SOFT_LIMIT,
            acache->soft_limit, PINT_PERF_SET);
        PINT_perf_count(acache_pc, PERF_ACACHE_HARD_LIMIT,
            acache->hard_limit, PINT_PERF_SET);
        PINT_perf_count(acache_pc, PERF_ACACHE_ENABLED,
            acache->enable, PINT_PERF_SET);
        PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES,
            acache->num_entries, PINT_PERF_SET);
    }

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
    struct static_payload* tmp_static_payload;
    int status;
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: get_cached_entry(): H=%llu\n",
                 llu(refn.handle));
  
    /* assume everything is timed out for starters */
    *attr_status = -PVFS_ETIME;
    *size_status = -PVFS_ETIME;
    attr->mask = 0;
  
    gen_mutex_lock(&acache_mutex);
  
    /* lookup static components */
    ret = PINT_tcache_lookup(static_acache, &refn, &tmp_entry, &status);
    if(ret < 0 || status != 0)
    {
        PINT_perf_count(static_pc, PERF_ACACHE_MISSES, 1, PINT_PERF_ADD);
        gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: miss static: H=%llu\n",
                     llu(refn.handle));
        tmp_static_payload = NULL;
    }
    else
    {
        PINT_perf_count(static_pc, PERF_ACACHE_HITS, 1, PINT_PERF_ADD);
        tmp_static_payload = tmp_entry->payload;
    }
  
    /* lookup non-static components */
    ret = PINT_tcache_lookup(acache, &refn, &tmp_entry, &status);
    if(ret < 0 || status != 0)
    {
        PINT_perf_count(acache_pc, PERF_ACACHE_MISSES, 1, PINT_PERF_ADD);
        gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: miss non-static: H=%llu\n",
                     llu(refn.handle));
        tmp_payload = NULL;
    }
    else
    {
        PINT_perf_count(acache_pc, PERF_ACACHE_HITS, 1, PINT_PERF_ADD);
        tmp_payload = tmp_entry->payload;
    }

    if(!tmp_payload && !tmp_static_payload)
    {
        /* missed everything */
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }
 
#if 0
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: status=%d, attr_status=%d, size_status=%d\n",
                 status, tmp_payload->attr_status, tmp_payload->size_status);
#endif

    /* copy out non-static attributes if valid */
    if(tmp_payload && tmp_payload->attr_status == 0)
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
    if(tmp_payload && tmp_payload->size_status == 0)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: copying out size.\n");
        *size = tmp_payload->size;
        *size_status = 0;
    }
  
    /* copy out static attributes if valid */
    if(tmp_static_payload)
    {
        attr->mask |= tmp_static_payload->mask;
        if(tmp_static_payload->mask & PVFS_ATTR_COMMON_TYPE)
        {
            attr->objtype = tmp_static_payload->objtype;
        }
        if(tmp_static_payload->mask & PVFS_ATTR_META_DFILES)
        {
            if(attr->u.meta.dfile_array)
                free(attr->u.meta.dfile_array);
            attr->u.meta.dfile_array = 
                malloc(tmp_static_payload->dfile_count*sizeof(PVFS_handle));
            if(!attr->u.meta.dfile_array)
            {
                gen_mutex_unlock(&acache_mutex);
                return(-PVFS_ENOMEM);
            }
            memcpy(attr->u.meta.dfile_array, tmp_static_payload->dfile_array,
                tmp_static_payload->dfile_count*sizeof(PVFS_handle));
            attr->u.meta.dfile_count = tmp_static_payload->dfile_count;
        }
        if(tmp_static_payload->mask & PVFS_ATTR_META_DIST)
        {
            if(attr->u.meta.dist)
                PINT_dist_free(attr->u.meta.dist);
            attr->u.meta.dist = PINT_dist_copy(tmp_static_payload->dist);
            if(!attr->u.meta.dist)
            {
                if(attr->u.meta.dfile_array)
                    free(attr->u.meta.dfile_array);
                gen_mutex_unlock(&acache_mutex);
                return(-PVFS_ENOMEM);
            }
            attr->u.meta.dist_size = tmp_static_payload->dist_size;
        }
        *attr_status = 0;
    }

    gen_mutex_unlock(&acache_mutex);
  
    gossip_debug(GOSSIP_ACACHE_DEBUG, 
                 "acache: hit: H=%llu, "
                 "size_status=%d, attr_status=%d\n",
                 llu(refn.handle), *size_status, *attr_status);
  
    if(*size_status == 0 || *attr_status == 0)
    {
        /* return success if we got _anything_ out of the cache */
        return(0);
    }
  
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
  
    /* find out if we have non-static items cached */
    ret = PINT_tcache_lookup(acache, 
                             &refn,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        PINT_tcache_delete(acache, tmp_entry);
        PINT_perf_count(acache_pc, PERF_ACACHE_DELETIONS, 1,
                        PINT_PERF_ADD);
    }
  
    /* find out if we have static items cached */
    ret = PINT_tcache_lookup(static_acache, 
                             &refn,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        PINT_tcache_delete(static_acache, tmp_entry);
        PINT_perf_count(static_pc, PERF_ACACHE_DELETIONS, 1,
                        PINT_PERF_ADD);
    }

    /* set the new current number of entries */
    PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES,
                    acache->num_entries, PINT_PERF_SET);
    PINT_perf_count(static_pc, PERF_ACACHE_NUM_ENTRIES,
                    static_acache->num_entries, PINT_PERF_SET);

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
    struct acache_payload* tmp_payload = NULL;
    struct static_payload* tmp_static_payload = NULL;
    unsigned int enabled;
    uint32_t old_mask;
    int ret = -1;

    /* skip out immediately if the cache is disabled */
    PINT_tcache_get_info(static_acache, TCACHE_ENABLE, &enabled);
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

    /* do we have static fields? */
    if(attr && (attr->mask & PVFS_STATIC_ATTR_MASK))
    {
        tmp_static_payload = 
            (struct static_payload*)calloc(1, sizeof(*tmp_static_payload));
        if(!tmp_static_payload)
        {
            ret = -PVFS_ENOMEM;
            goto err;
        }
        tmp_static_payload->refn = refn;
        tmp_static_payload->mask = attr->mask & PVFS_STATIC_ATTR_MASK;
        if(attr->mask & PVFS_ATTR_COMMON_TYPE)
        {
            tmp_static_payload->objtype = attr->objtype;
        }
        if(attr->mask & PVFS_ATTR_META_DFILES)
        {
            tmp_static_payload->dfile_array = 
                malloc(attr->u.meta.dfile_count*sizeof(PVFS_handle));
            if(!tmp_static_payload->dfile_array)
            {
                ret = -PVFS_ENOMEM;
                goto err;
            }
            memcpy(tmp_static_payload->dfile_array, attr->u.meta.dfile_array,
                attr->u.meta.dfile_count*sizeof(PVFS_handle));
            tmp_static_payload->dfile_count = attr->u.meta.dfile_count;
        }
        if(attr->mask & PVFS_ATTR_META_DIST)
        {
            tmp_static_payload->dist = PINT_dist_copy(attr->u.meta.dist);
            if(!tmp_static_payload->dist)
            {
                ret = -PVFS_ENOMEM;
                goto err;
            }
            tmp_static_payload->dist_size = attr->u.meta.dist_size;
        }
    }

    /* do we have size or other non-static fields? */
    if(size || (attr && (attr->mask & (~(PVFS_STATIC_ATTR_MASK)))))
    {
        tmp_payload = 
            (struct acache_payload*)calloc(1, sizeof(*tmp_payload));
        if(!tmp_payload)
        {
            ret = -PVFS_ENOMEM;
            goto err;
        }
        tmp_payload->refn = refn;
        tmp_payload->attr_status = -PVFS_ETIME;
        tmp_payload->size_status = -PVFS_ETIME;

        if(attr && (attr->mask & (~(PVFS_STATIC_ATTR_MASK))))
        {
            /* modify mask temporarily so that we only copy non-static fields
             * here
             */
            old_mask = attr->mask;
            attr->mask = (attr->mask & (~(PVFS_STATIC_ATTR_MASK)));
            ret = PINT_copy_object_attr(&(tmp_payload->attr), attr);
            if(ret < 0)
            {
                goto err;
            }
            tmp_payload->attr_status = 0;
            attr->mask = old_mask;
        }
      
        if(size)
        {
            tmp_payload->size = *size;
            tmp_payload->size_status = 0;
        }
 
    }
   
#if 0
    gossip_debug(GOSSIP_ACACHE_DEBUG, "acache: update(): attr_status=%d, size_status=%d\n",
                 tmp_payload->attr_status, tmp_payload->size_status);
#endif

    gen_mutex_lock(&acache_mutex);

    if(tmp_static_payload)
    {
        load_payload(static_acache, refn, tmp_static_payload, static_pc);
    }
    if(tmp_payload)
    {
        load_payload(acache, refn, tmp_payload, acache_pc);
    }

    gen_mutex_unlock(&acache_mutex);
  
    return(0);

err:

    if(tmp_static_payload)
    {
        if(tmp_static_payload->dfile_array)
            free(tmp_static_payload->dfile_array);
        if(tmp_static_payload->dist)
            PINT_dist_free(tmp_static_payload->dist);
        free(tmp_static_payload);
    }
    if(tmp_payload)
    {
        PINT_free_object_attr(&tmp_payload->attr);
        free(tmp_payload);
    }
    
    return(ret);
}
  
/* static_compare_key_entry()
 *
 * compares an opaque key (object ref in this case) against a payload to see
 * if there is a match
 *
 * returns 1 on match, 0 otherwise
 */
static int static_compare_key_entry(void* key, struct qhash_head* link)
{
    PVFS_object_ref* real_key = (PVFS_object_ref*)key;
    struct static_payload* tmp_payload = NULL;
    struct PINT_tcache_entry* tmp_entry = NULL;
  
    tmp_entry = qhash_entry(link, struct PINT_tcache_entry, hash_link);
    assert(tmp_entry);
  
    tmp_payload = (struct static_payload*)tmp_entry->payload;
    if(real_key->handle == tmp_payload->refn.handle &&
       real_key->fs_id == tmp_payload->refn.fs_id)
    {
        return(1);
    }
  
    return(0);
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
  
    tmp_entry = qhash_entry(link, struct PINT_tcache_entry, hash_link);
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
  
/* static_free_payload()
 *
 * frees payload that has been stored in the acache 
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int static_free_payload(void* payload)
{
    struct static_payload* tmp_static_payload = 
        (struct static_payload*)payload;
  
    if(tmp_static_payload->dfile_array)
    {
        free(tmp_static_payload->dfile_array);
    }
    if(tmp_static_payload->dist)
    {
        PINT_dist_free(tmp_static_payload->dist);
    }
    free(tmp_static_payload);
    return(0);

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


static int set_tcache_defaults(struct PINT_tcache* instance)
{
    int ret;

    ret = PINT_tcache_set_info(instance, TCACHE_HARD_LIMIT, 
                               ACACHE_DEFAULT_HARD_LIMIT);
    if(ret < 0)
    {
        return(ret);
    }
    ret = PINT_tcache_set_info(instance, TCACHE_SOFT_LIMIT, 
                               ACACHE_DEFAULT_SOFT_LIMIT);
    if(ret < 0)
    {
        return(ret);
    }
    ret = PINT_tcache_set_info(instance, TCACHE_RECLAIM_PERCENTAGE,
                               ACACHE_DEFAULT_RECLAIM_PERCENTAGE);
    if(ret < 0)
    {
        return(ret);
    }

    return(0);
}

static void load_payload(struct PINT_tcache* instance, 
    PVFS_object_ref refn,
    void* payload,
    struct PINT_perf_counter* pc)
{
    int status;
    int purged;
    struct PINT_tcache_entry* tmp_entry;
    int ret;

    /* find out if the entry is already in the cache */
    ret = PINT_tcache_lookup(instance, 
                             &refn,
                             &tmp_entry,
                             &status);
    if(ret == 0)
    {
        /* found match in cache; destroy old payload, replace, and
         * refresh time stamp
         */
        instance->free_payload(tmp_entry->payload);
        tmp_entry->payload = payload;
        ret = PINT_tcache_refresh_entry(instance, tmp_entry);
        /* this counts as an update of an existing entry */
        PINT_perf_count(pc, PERF_ACACHE_UPDATES, 1, PINT_PERF_ADD);
    }
    else
    {
        /* not found in cache; insert new payload*/
        ret = PINT_tcache_insert_entry(instance, 
            &refn, payload, &purged);
        /* the purged variable indicates how many entries had to be purged
         * from the tcache to make room for this new one
         */
        if(purged == 1)
        {
            /* since only one item was purged, we count this as one item being
             * replaced rather than as a purge and an insert 
             */
            PINT_perf_count(pc, PERF_ACACHE_REPLACEMENTS, purged, 
                PINT_PERF_ADD);
        }
        else
        {
            /* otherwise we just purged as part of reclaimation */
            /* if we didn't purge anything, then the "purged" variable will
             * be zero and this counter call won't do anything.
             */
            PINT_perf_count(pc, PERF_ACACHE_PURGES, purged,
                PINT_PERF_ADD);
        }
    }
    PINT_perf_count(pc, PERF_ACACHE_NUM_ENTRIES,
        instance->num_entries, PINT_PERF_SET);
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


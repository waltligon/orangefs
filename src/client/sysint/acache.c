/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <string.h>
#include <err.h>

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
#define ACACHE_DEFAULT_SOFT_LIMIT 5120
#define ACACHE_DEFAULT_HARD_LIMIT 10240
#define ACACHE_DEFAULT_RECLAIM_PERCENTAGE 25
#define ACACHE_DEFAULT_REPLACE_ALGORITHM LEAST_RECENTLY_USED
/* The timeout used for the acache payload. Should be greater than the
 * dynamic timeout. */
#define ACACHE_DEFAULT_TIMEOUT_MSECS 60000 /* 60 seconds */

/* The timeout used for dynamic attributes. */
/* Currently, the only dynamic attribute is size.
 * More dynamic attributes will likely be included in the future. */
#define ACACHE_DEFAULT_DYNAMIC_TIMEOUT_MSECS 10000 /* 10 seconds */

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
    {"ACACHE_NUM_ENTRIES_W_SIZE", PERF_ACACHE_NUM_ENTRIES_W_SIZE,
     PINT_PERF_PRESERVE},
    {"ACACHE_SIZE_HITS", PERF_ACACHE_SIZE_HITS, 0},
    {"ACACHE_SIZE_MISSES", PERF_ACACHE_SIZE_MISSES, 0},
    {"ACACHE_SIZE_UPDATES", PERF_ACACHE_SIZE_UPDATES, 0},
    {"ACACHE_SIZE_INVALIDATIONS", PERF_ACACHE_SIZE_INVALIDATIONS, 0},
    {NULL, 0, 0},
};

/* data to be stored in a cached entry */
struct acache_payload
{
    PVFS_object_ref refn;    /**< PVFS2 object reference */
    PVFS_object_attr attr;   /**< cached attributes */  
    /**< Time when the size attr was last updated. */
    struct timeval size_updated_timeval;
    PVFS_size size;          /**< cached size */
};

static struct PINT_tcache* acache = NULL;
static gen_mutex_t acache_mutex = GEN_MUTEX_INITIALIZER;

static int acache_compare_key_entry(const void* key, struct qhash_head* link);
static int acache_free_payload(void* payload);

static int acache_hash_key(const void* key, int table_size);
static struct PINT_perf_counter* acache_pc = NULL;

static int set_tcache_defaults(struct PINT_tcache* instance);

static void load_payload(struct PINT_tcache* instance, 
    PVFS_object_ref refn,
    void* payload,
    struct PINT_perf_counter* pc);

extern struct PINT_perf_counter * get_acache_pc(void)
{
    return acache_pc;
}

/**
 * Enables perf counter instrumentation of the acache
 */
void PINT_acache_enable_perf_counter(
    struct PINT_perf_counter* pc_in) /**< counter for cache fields */
{
    gen_mutex_lock(&acache_mutex);

    acache_pc = pc_in;
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

    ret = PINT_tcache_set_info(acache,
                               TCACHE_TIMEOUT_MSECS,
                               ACACHE_DEFAULT_TIMEOUT_MSECS);
    if(ret < 0)
    {
        PINT_tcache_finalize(acache);
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }

    /* fill in defaults that are common to both */
    ret = set_tcache_defaults(acache);
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

    if(option & STATIC_ACACHE_OPT)
    {
        /* this is a static acache option; strip mask and pass along to
         * tcache
         */
        option -= STATIC_ACACHE_OPT;
        ret = PINT_tcache_get_info(acache, option, arg);
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

    gen_mutex_unlock(&acache_mutex);

    return(ret);
}

/**
 * Retrieves a _copy_ of a cached attributes structure.  Also retrieves the
 * logical file size (if the object in question is a file) and reports the
 * status of both the attributes and size to indicate if they are valid or
 * not. All pointers passed to this function should be valid.
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_acache_get_cached_entry(
    PVFS_object_ref refn,  /**< PVFS2 object to look up */
    PVFS_object_attr* attr,/**< attributes of the object */
    int* attr_status,      /**< indicates if the attributes are expired */
    PVFS_size* size,       /**< logical size of the object */
    int* size_status)      /**< indicates if the size has expired */
{
    struct PINT_tcache_entry* tmp_entry;
    struct acache_payload* tmp_payload;
    int ret = -1;
    struct timeval current_time = { 0, 0};

    if(!attr || !attr_status || !size || !size_status)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: Invalid arguments: "
                     "one of the function parameters is NULL!\n",
                     __func__);
        return -PVFS_EINVAL;
    }

    gossip_debug(GOSSIP_ACACHE_DEBUG,
                 "%s: H=%llu\n",
                 __func__,
                 llu(refn.handle));

    /* assume everything is timed out for starters */
    *attr_status = -PVFS_ETIME;
    *size_status = -PVFS_ETIME;
    attr->mask = 0;

    gen_mutex_lock(&acache_mutex);

    /* lookup */
    ret = PINT_tcache_lookup(acache, &refn, &tmp_entry, attr_status);
    if(ret < 0 || *attr_status != 0)
    {
        PINT_perf_count(acache_pc, PERF_ACACHE_MISSES, 1, PINT_PERF_ADD);
        gossip_debug(GOSSIP_ACACHE_DEBUG, "%s: miss: H=%llu\n",
                     __func__,
                     llu(refn.handle));
        tmp_payload = NULL;
    }
    else
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: hit acache payload and attr_status is okay\n",
                   __func__);
        tmp_payload = tmp_entry->payload;

        /* First check if size has been invalidated explicitly. */
        if(tmp_payload->size_updated_timeval.tv_sec == 0)
        {
            gossip_debug(GOSSIP_ACACHE_DEBUG,
                         "%s: NOTE, size has been explicitly invalidated or "
                         "was never inserted with this acache payload.\n",
                         __func__);
            *size_status = -PVFS_ETIME;
        }
        /* Check if size is invalid due to size timeout */
        else
        {
            /* Get the time of day and store as milliseconds */
            PINT_util_get_current_timeval(&current_time);
            /* Get the difference in the current time and the time the size
             * was last refreshed. */
            int usecs_since_size_update = PINT_util_get_timeval_diff(
                &tmp_payload->size_updated_timeval, &current_time);
            gossip_debug(GOSSIP_ACACHE_DEBUG,
                         "%s: usecs_since_size_update = %d\n",
                         __func__,
                         usecs_since_size_update);
            /* TODO use client specified t/o instead of default */
            if(usecs_since_size_update >
               (ACACHE_DEFAULT_DYNAMIC_TIMEOUT_MSECS * 1000))
            {
                /* although payload was hit, size within has timed out */
                gossip_debug(GOSSIP_ACACHE_DEBUG,
                             "%s: size has timed out!\n",
                             __func__);
                *size_status = -PVFS_ETIME;
                /* TODO maybe handle this in getattr sm using status of size */
                /* Update the cached mask! */
                tmp_payload->attr.mask ^= PVFS_ATTR_DATA_SIZE;
            }
            else
            {
                *size_status = 0;
                *size = tmp_payload->size;
                gossip_debug(GOSSIP_ACACHE_DEBUG,
                             "%s: size is still valid for %d usecs!\n",
                             __func__,
                             ACACHE_DEFAULT_DYNAMIC_TIMEOUT_MSECS * 1000
                                - usecs_since_size_update);
                gossip_debug(GOSSIP_ACACHE_DEBUG,
                             "%s: size = %lld\n",
                             __func__,
                             lld(*size));
            }
        }
        PINT_perf_count(acache_pc, PERF_ACACHE_HITS, 1, PINT_PERF_ADD);
    }

    if(!tmp_payload)
    {
        /* missed everything */
        gen_mutex_unlock(&acache_mutex);
        return(ret);
    }

    gossip_debug(GOSSIP_ACACHE_DEBUG, 
                 "%s: hit: H=%llu, "
                 "attr_status=%d, size_status=%d\n",
                 __func__,
                 llu(refn.handle),
                 *attr_status,
                 *size_status);

    if(tmp_payload && *attr_status == 0)
    {
        ret = PINT_copy_object_attr(attr, &(tmp_payload->attr));
        if(ret < 0)
        {
            gen_mutex_unlock(&acache_mutex);
            return(ret);
        }
        *attr_status = 0;
    }

    if(*size_status == 0 || *attr_status == 0)
    {
        gen_mutex_unlock(&acache_mutex);
        /* return success if we got _anything_ out of the cache */
        return(0);
    }

    gen_mutex_unlock(&acache_mutex);
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

    /* set the new current number of entries */
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

    gen_mutex_lock(&acache_mutex);

    gossip_debug(GOSSIP_ACACHE_DEBUG,
                 "acache: invalidate_size(): H=%llu\n",
                 llu(refn.handle));

    /* find out if the entry is in the cache */
    ret = PINT_tcache_lookup(acache, 
                             &refn,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        /* found match in cache; set size to invalid */
        tmp_payload = tmp_entry->payload;
        /* Use this to indicate invalid size status. */
        tmp_payload->size_updated_timeval.tv_sec = 0;
        PINT_perf_count(acache_pc, PERF_ACACHE_SIZE_INVALIDATIONS, 1,
                        PINT_PERF_ADD);
        PINT_perf_count(acache_pc, PERF_ACACHE_NUM_ENTRIES_W_SIZE, 1,
                        PINT_PERF_SUB);
    }

    gen_mutex_unlock(&acache_mutex);
    return;
}

/**
 * Adds a set of attributes to the cache.
 * Replaces previously existing cache entry of same object reference if found.
 * The given attributes are _copied_ into the cache.
 * The size will not be cached if size is NULL.
 *
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_acache_update(
    PVFS_object_ref refn,   /**< object to update */
    PVFS_object_attr *attr, /**< attributes to copy into cache */
    PVFS_size* size)        /**< logical file size (NULL if not available) */
{
    struct acache_payload* tmp_payload = NULL;
    int ret = -1;

    gossip_debug(GOSSIP_ACACHE_DEBUG,
                 "%s: update(): H=%llu\n",
                 __func__,
                 llu(refn.handle));

    if(!attr)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: failing due to invalid:\n\tattr ptr (%p)\n",
                     __func__,
                     (void *) attr);
        return (-PVFS_EINVAL);
    }

    gossip_debug(GOSSIP_ACACHE_DEBUG,
                 "%s: copying incoming acache payload, incoming mask is: %x\n",
                 __func__,
                 attr->mask);

    tmp_payload = (struct acache_payload*) calloc(1, sizeof(*tmp_payload));
    if(!tmp_payload)
    {
        return -PVFS_ENOMEM;
    }

    tmp_payload->refn = refn;
    ret = PINT_copy_object_attr(&tmp_payload->attr, attr);
    if(ret != 0)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: PINT_copy_object_attr failed. ret = %d:\n"
                     "\tsrc = %p\tdest = %p\n",
                     __func__,
                     ret,
                     (void *) attr,
                     (void *) &tmp_payload->attr);
        acache_free_payload(tmp_payload);
        return -PVFS_ENOMEM;
    }

    if(size)
    {
        /* For debug output of current time. */
        char parsed_timeval[64] = { 0 };

        tmp_payload->size = *size;

        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: tmp_payload->size = %lld\n",
                     __func__,
                     lld(tmp_payload->size));

        /* Debug output of current time. */
        PINT_util_get_current_timeval(&tmp_payload->size_updated_timeval);
        PINT_util_parse_timeval(tmp_payload->size_updated_timeval,
                                parsed_timeval);
        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: timeval = %s\n",
                     __func__,
                     parsed_timeval);
    }
    else
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG,
                     "%s: NOTE, size is NULL. "
                     "No size inserted with this acache payload.\n",
                     __func__);
    }

    gossip_debug(GOSSIP_ACACHE_DEBUG,
                "%s: copied input payload, mask of copied payload is: %x\n",
                __func__,
                 tmp_payload->attr.mask);

    gen_mutex_lock(&acache_mutex);

    if(tmp_payload)
    {
        load_payload(acache, refn, tmp_payload, acache_pc);
    }

    gen_mutex_unlock(&acache_mutex);
    return(0);
}

/* acache_compare_key_entry()
 *
 * compares an opaque key (object ref in this case) against a payload to see
 * if there is a match
 *
 * returns 1 on match, 0 otherwise
 */
static int acache_compare_key_entry(const void* key, struct qhash_head* link)
{
    const PVFS_object_ref* real_key = (const PVFS_object_ref*)key;
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
static int acache_hash_key(const void* key, int table_size)
{
    const PVFS_object_ref* real_key = (const PVFS_object_ref*)key;
    int tmp_ret = 0;

    tmp_ret = (real_key->handle)%table_size;
    return(tmp_ret);
}
  
/* acache_free_payload()
 *
 * frees payload that has been stored in the acache 
 *
 * returns 0 on success
 */
static int acache_free_payload(void* payload)
{
    struct acache_payload * payload_p = payload;
    if(payload_p)
    {
        if(&payload_p->attr)
        {
            PINT_free_object_attr(&payload_p->attr);
        }
        free(payload_p);
    }
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
        /* Free the entry's old payload */
        instance->free_payload(tmp_entry->payload);

        /* Point to the new one */
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


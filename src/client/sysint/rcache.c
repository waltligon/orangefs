/*
 * Copyright © Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */
  
#include <assert.h>
  
#include "pvfs2-attr.h"
#include "rcache.h"
#include "tcache.h"
#include "pint-util.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-internal.h"
#include <string.h>
  
/** \file
 *  \ingroup rcache
 * Implementation of the Readdir Cache (rcache) component.
 */

/* following dbpf-keyval-pcache.c */
/* table size must be a multiple of 2 */
#define RCACHE_TABLE_SIZE (1<<10)
  
/* compile time defaults */
enum {
RCACHE_DEFAULT_TIMEOUT_MSECS  =  3000,
RCACHE_DEFAULT_SOFT_LIMIT     =  5120,
RCACHE_DEFAULT_HARD_LIMIT     = 10240,
RCACHE_DEFAULT_RECLAIM_PERCENTAGE = 25,
RCACHE_DEFAULT_REPLACE_ALGORITHM = LEAST_RECENTLY_USED,
};

struct PINT_perf_key rcache_keys[] = 
{
   {"RCACHE_NUM_ENTRIES", PERF_RCACHE_NUM_ENTRIES, PINT_PERF_PRESERVE},
   {"RCACHE_SOFT_LIMIT", PERF_RCACHE_SOFT_LIMIT, PINT_PERF_PRESERVE},
   {"RCACHE_HARD_LIMIT", PERF_RCACHE_HARD_LIMIT, PINT_PERF_PRESERVE},
   {"RCACHE_HITS", PERF_RCACHE_HITS, 0},
   {"RCACHE_MISSES", PERF_RCACHE_MISSES, 0},
   {"RCACHE_UPDATES", PERF_RCACHE_UPDATES, 0},
   {"RCACHE_PURGES", PERF_RCACHE_PURGES, 0},
   {"RCACHE_REPLACEMENTS", PERF_RCACHE_REPLACEMENTS, 0},
   {"RCACHE_DELETIONS", PERF_RCACHE_DELETIONS, 0},
   {"RCACHE_ENABLED", PERF_RCACHE_ENABLED, PINT_PERF_PRESERVE},
   {NULL, 0, 0},
};

/* data to be stored in a cached entry */
struct rcache_payload
{
    PVFS_object_ref ref;
    PVFS_ds_position token;
    int32_t dirdata_index;
};

struct rcache_key
{
    PVFS_object_ref ref;
    PVFS_ds_position token;
};
  
static struct PINT_tcache* rcache = NULL;
static gen_mutex_t rcache_mutex = GEN_MUTEX_INITIALIZER;
  
static int rcache_compare_key_entry(const void* key, struct qhash_head* link);
static int rcache_hash_key(const void* key, int table_size);
static int rcache_free_payload(void* payload);
static struct PINT_perf_counter* rcache_pc = NULL;

/**
 * Enables perf counter instrumentation of the rcache
 */
void PINT_rcache_enable_perf_counter(
    struct PINT_perf_counter* pc)     /**< perf counter instance to use */
{
    gen_mutex_lock(&rcache_mutex);

    rcache_pc = pc;
    assert(rcache_pc);

    /* set initial values */
    PINT_perf_count(rcache_pc, PERF_RCACHE_SOFT_LIMIT,
        rcache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(rcache_pc, PERF_RCACHE_HARD_LIMIT,
        rcache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(rcache_pc, PERF_RCACHE_ENABLED,
        rcache->enable, PINT_PERF_SET);

    gen_mutex_unlock(&rcache_mutex);

    return;
}

/**
 * Initializes the rcache 
 * \return 0 on success, NULL on failure
 */
int PINT_rcache_initialize(void)
{
    int ret = -1;
  
    gen_mutex_lock(&rcache_mutex);
  
    /* create tcache instance */
    rcache = PINT_tcache_initialize(rcache_compare_key_entry,
                                    rcache_hash_key,
                                    rcache_free_payload,
                                    RCACHE_TABLE_SIZE );
    if(!rcache)
    {
        gen_mutex_unlock(&rcache_mutex);
        return(-PVFS_ENOMEM);
    }
  
  /* turn off the cache expiration, refer to src/io/trove/trove-dbpf/dbpf-keyval-pcache.c */
    ret = PINT_tcache_set_info(rcache,
            TCACHE_ENABLE_EXPIRATION, 0);
    if(ret < 0)
    {
        PINT_tcache_finalize(rcache);
        gen_mutex_unlock(&rcache_mutex);
        return(ret);
    }

    ret = PINT_tcache_set_info(rcache, TCACHE_HARD_LIMIT, 
                               RCACHE_DEFAULT_HARD_LIMIT);
    if(ret < 0)
    {
        PINT_tcache_finalize(rcache);
        gen_mutex_unlock(&rcache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(rcache, TCACHE_SOFT_LIMIT, 
                               RCACHE_DEFAULT_SOFT_LIMIT);
    if(ret < 0)
    {
        PINT_tcache_finalize(rcache);
        gen_mutex_unlock(&rcache_mutex);
        return(ret);
    }
    ret = PINT_tcache_set_info(rcache, TCACHE_RECLAIM_PERCENTAGE,
                               RCACHE_DEFAULT_RECLAIM_PERCENTAGE);
    if(ret < 0)
    {
        PINT_tcache_finalize(rcache);
        gen_mutex_unlock(&rcache_mutex);
        return(ret);
    }
  
    gen_mutex_unlock(&rcache_mutex);
    return(0);
}
  
/** Finalizes and destroys the rcache, frees all cached entries */
void PINT_rcache_finalize(void)
{
    gen_mutex_lock(&rcache_mutex);

    assert(rcache != NULL);
    PINT_tcache_finalize(rcache);
    rcache = NULL;

    gen_mutex_unlock(&rcache_mutex);
    return;
}
  
/**
 * Retrieves parameters from the rcache 
 * @see PINT_tcache_options
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_rcache_get_info(
    enum PINT_rcache_options option, /**< option to read */
    unsigned int* arg)               /**< output value */
{
    int ret = -1;
  
    gen_mutex_lock(&rcache_mutex);
    ret = PINT_tcache_get_info(rcache, option, arg);
    gen_mutex_unlock(&rcache_mutex);
  
    return(ret);
}
  
/**
 * Sets optional parameters in the rcache
 * @see PINT_tcache_options
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_rcache_set_info(
    enum PINT_rcache_options option, /**< option to modify */
    unsigned int arg)                /**< input value */
{
    int ret = -1;
  
    gen_mutex_lock(&rcache_mutex);
    ret = PINT_tcache_set_info(rcache, option, arg);

    /* record any resulting parameter changes */
    PINT_perf_count(rcache_pc, PERF_RCACHE_SOFT_LIMIT,
        rcache->soft_limit, PINT_PERF_SET);
    PINT_perf_count(rcache_pc, PERF_RCACHE_HARD_LIMIT,
        rcache->hard_limit, PINT_PERF_SET);
    PINT_perf_count(rcache_pc, PERF_RCACHE_ENABLED,
        rcache->enable, PINT_PERF_SET);
    PINT_perf_count(rcache_pc, PERF_RCACHE_NUM_ENTRIES,
        rcache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&rcache_mutex);

    return(ret);
}
  
/** 
 * Retrieves cached dirdata index, and reports the
 * status to indicate if they are valid or  not
 * @return 0 on success, -PVFS_error on failure
 */
int PINT_rcache_get_cached_index(
    const PVFS_object_ref* ref,
    const PVFS_ds_position token,
    int32_t *index) 
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct rcache_payload* tmp_payload;
    struct rcache_key entry_key;
    int status;

    gossip_debug(GOSSIP_RCACHE_DEBUG, 
                 "rcache: get_cached_entry(): [fs_id=%d,handle=%llu,token=%llu]\n",ref->fs_id, llu(ref->handle),llu(token));
  
    entry_key.ref.handle = ref->handle;
    entry_key.ref.fs_id = ref->fs_id;
    entry_key.token = token;

    gen_mutex_lock(&rcache_mutex);

    /* lookup entry */
    ret = PINT_tcache_lookup(rcache, (void *) &entry_key, &tmp_entry, &status);
    if(ret < 0 || status != 0)
    {
        gossip_debug(GOSSIP_RCACHE_DEBUG, 
                "rcache: miss: [fs_id=%d,handle=%llu,token=%llu]\n",
                ref->fs_id, llu(ref->handle),llu(token));
        PINT_perf_count(rcache_pc, PERF_RCACHE_MISSES, 1, PINT_PERF_ADD);
        gen_mutex_unlock(&rcache_mutex);
        /* Return -PVFS_ENOENT if the entry has expired */
        if(status != 0)
        {   
            return(-PVFS_ENOENT);
        }
        return(ret);
    }
    tmp_payload = tmp_entry->payload;
  
    gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: status=%d, dirdata_index=%d\n",
            status, tmp_payload->dirdata_index);

    gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: copying out dirdata index.\n");
    *index = tmp_payload->dirdata_index;

    /* return success */
    PINT_perf_count(rcache_pc, PERF_RCACHE_HITS, 1, PINT_PERF_ADD);
    gen_mutex_unlock(&rcache_mutex);
    return(0);
}
  
/**
 * Invalidates a cache entry (if present)
 */
void PINT_rcache_invalidate(
    const PVFS_object_ref* ref,
    const PVFS_ds_position token)
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct rcache_key entry_key;
    int tmp_status;
  
    gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: invalidate(): handle=%llu, token=%llu\n", llu(ref->handle), llu(token));
  
    gen_mutex_lock(&rcache_mutex);
  
    entry_key.ref.handle = ref->handle;
    entry_key.ref.fs_id = ref->fs_id;
    entry_key.token = token;

    /* find out if the entry is in the cache */
    ret = PINT_tcache_lookup(rcache, 
                             &entry_key,
                             &tmp_entry,
                             &tmp_status);
    if(ret == 0)
    {
        PINT_tcache_delete(rcache, tmp_entry);
        PINT_perf_count(rcache_pc, PERF_RCACHE_DELETIONS, 1,
                        PINT_PERF_ADD);
    }

    PINT_perf_count(rcache_pc, PERF_RCACHE_NUM_ENTRIES,
                    rcache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&rcache_mutex);
    return;
}
  
/** 
 * Adds an entry to the cache, or updates it if already present.  
 *
 * \return 0 on success, -PVFS_error on failure
 */
int PINT_rcache_insert(
    const PVFS_object_ref* ref,     
    const PVFS_ds_position token,
    const int32_t dirdata_index) 
{
    int ret = -1;
    struct PINT_tcache_entry* tmp_entry;
    struct rcache_payload* tmp_payload;
    struct rcache_key entry_key;
    int status;
    int purged;

#if 0
    int enabled;

    /* Check if rcache is initialized. Initialize rcache if pointer is null. */
    if(!rcache)
    {
        gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: trying to insert when rcache is NULL!\n");
        PINT_rcache_initialize();
        if(!rcache)
        {
            gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: initialization caused by insert and null rcache failed!\n");
            return 0;
        }
    }

    /* Make sure the cache is enabled */
    PINT_tcache_get_info(rcache, TCACHE_ENABLE, &enabled);
    if(!enabled)
    {
assert(enabled == 1);
        gossip_debug(GOSSIP_RCACHE_DEBUG, "PINT_rcache_insert: enabling rcache\n");
        enabled = 1;
        PINT_tcache_set_info(rcache, TCACHE_ENABLE, enabled);
    }
#endif
    
    gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: insert(): handle=%llu, token=%llu\n", llu(ref->handle), llu(token));
  
    /* the token cannot be the kickstart value */
    if(!ref->handle || token == PVFS_ITERATE_START)
    {
        return(-PVFS_EINVAL);
    }
  
    /* create new payload with updated information */
    tmp_payload = (struct rcache_payload*) 
                        calloc(1,sizeof(struct rcache_payload));
    if(tmp_payload == NULL)
    {
        return(-PVFS_ENOMEM);
    }

    tmp_payload->ref.handle = ref->handle;
    tmp_payload->ref.fs_id = ref->fs_id;
    tmp_payload->token = token;
    tmp_payload->dirdata_index = dirdata_index;

    entry_key.ref.handle = ref->handle;
    entry_key.ref.fs_id = ref->fs_id;
    entry_key.token = token;

    gen_mutex_lock(&rcache_mutex);

    /* find out if the entry is already in the cache */
    ret = PINT_tcache_lookup(rcache, 
                             &entry_key,
                             &tmp_entry,
                             &status);
    if(ret == 0)
    {
        /* found match in cache; destroy old payload, replace, and
         * refresh time stamp
         */
        rcache_free_payload(tmp_entry->payload);
        tmp_entry->payload = tmp_payload;
        ret = PINT_tcache_refresh_entry(rcache, tmp_entry);
        PINT_perf_count(rcache_pc, PERF_RCACHE_UPDATES, 1, PINT_PERF_ADD);
    }
    else
    {
        /* not found in cache; insert new payload*/
        ret = PINT_tcache_insert_entry(rcache, 
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
            PINT_perf_count(rcache_pc, PERF_RCACHE_REPLACEMENTS,purged,
                PINT_PERF_ADD);
        }
        else
        {
            /* otherwise we just purged as part of reclaimation */
            /* if we didn't purge anything, then the "purged" variable will
             * be zero and this counter call won't do anything.
             */
            PINT_perf_count(rcache_pc, PERF_RCACHE_PURGES, purged,
                PINT_PERF_ADD);
        }
    }
    
    PINT_perf_count(rcache_pc, PERF_RCACHE_NUM_ENTRIES,
        rcache->num_entries, PINT_PERF_SET);

    gen_mutex_unlock(&rcache_mutex);
  
    /* cleanup if we did not succeed for some reason */
    if(ret < 0)
    {
        rcache_free_payload(tmp_payload);
    }
  
    gossip_debug(GOSSIP_RCACHE_DEBUG, "rcache: insert(): return=%d\n", ret);
    return(ret);
}
  
/* rcache_compare_key_entry()
 *
 * compares an opaque key (char* in this case) against a payload to see
 * if there is a match
 *
 * returns 1 on match, 0 otherwise
 */
static int rcache_compare_key_entry(const void* key, struct qhash_head* link)
{
    const struct rcache_key* real_key = (const struct rcache_key*)key;
    struct rcache_payload* tmp_payload = NULL;
    struct PINT_tcache_entry* tmp_entry = NULL;
  
    tmp_entry = qhash_entry(link, struct PINT_tcache_entry, hash_link);
    assert(tmp_entry);

    tmp_payload = (struct rcache_payload*)tmp_entry->payload;
     /* If the following aren't equal, we know we don't have a match
     *   - ref.handle 
     *   - ref.fs_id
     *   - token
     */
    if( real_key->ref.handle  != tmp_payload->ref.handle ||
        real_key->ref.fs_id   != tmp_payload->ref.fs_id  ||
        real_key->token != tmp_payload->token )
    {
        /* One of the above cases failed, so we know these aren't a match */
        return(0);
    }
    
    return(1);
}


/* hash from http://burtleburtle.net/bob/hash/evahash.html */
#define mix(a,b,c) \
do { \
  a=a-b;  a=a-c;  a=a^(c>>13); \
      b=b-c;  b=b-a;  b=b^(a<<8);  \
      c=c-a;  c=c-b;  c=c^(b>>13); \
      a=a-b;  a=a-c;  a=a^(c>>12); \
      b=b-c;  b=b-a;  b=b^(a<<16); \
      c=c-a;  c=c-b;  c=c^(b>>5);  \
      a=a-b;  a=a-c;  a=a^(c>>3);  \
      b=b-c;  b=b-a;  b=b^(a<<10); \
      c=c-a;  c=c-b;  c=c^(b>>15); \
} while(0)

  
/* rcache_hash_key()
 *
 * hash function for character pointers
 *
 * returns hash index 
 */
static int rcache_hash_key(const void* key, int table_size)
{
    const struct rcache_key* key_entry = (const struct rcache_key*) key;

    uint32_t a = (uint32_t)(key_entry->ref.handle >> 32);
    uint32_t b = (uint32_t)(key_entry->ref.handle & 0x00000000FFFFFFFF);
    uint32_t c = (uint32_t)(key_entry->token);

    mix(a,b,c);
    return (int)(c & (RCACHE_TABLE_SIZE-1));
}
  
/* rcache_free_payload()
 *
 * frees payload that has been stored in the rcache 
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int rcache_free_payload(void* payload)
{
    free(payload);
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


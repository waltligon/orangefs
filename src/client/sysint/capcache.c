/*
 * (C) 2008 Clemson University and The University of Chicago
 * 
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "pint-util.h"
#include "pvfs2-internal.h"
#include "gen-locks.h"
#include "quickhash.h"
#include "tcache.h"
#include "capcache.h"
#include "security-types.h"
#include "security-util.h"

/* default option values */
enum
{
    CAPCACHE_DEFAULT_TIMEOUT_MSECS = 3000,
    CAPCACHE_DEFAULT_SOFT_LIMIT = 5120,
    CAPCACHE_DEFAULT_HARD_LIMIT = 10240,
    CAPCACHE_DEFAULT_RECLAIM_PERCENTAGE = 25,
    CAPCACHE_DEFAULT_REPLACE_ALGORITHM = LEAST_RECENTLY_USED,
};

struct capcache_payload
{
    PVFS_object_ref objref;
    PVFS_capability *capability;
};


static struct PINT_tcache *capcache = NULL;
static gen_mutex_t capcache_mutex = GEN_MUTEX_INITIALIZER;


static int compare_key_entry(void*, struct qhash_head*);
static int hash_key(void*, int);
static int free_payload(void*);


int PINT_capcache_initialize(void)
{
    int ret = -PVFS_EINVAL;

    gen_mutex_lock(&capcache_mutex);

    capcache = PINT_tcache_initialize(compare_key_entry, hash_key,
                                      free_payload, -1);
    if (!capcache)
    {
        gen_mutex_unlock(&capcache_mutex);
        return -PVFS_ENOMEM;
    }

    ret = PINT_tcache_set_info(capcache, TCACHE_TIMEOUT_MSECS,
                               CAPCACHE_DEFAULT_TIMEOUT_MSECS);
    if (ret < 0)
    {
        PINT_tcache_finalize(capcache);
        gen_mutex_unlock(&capcache_mutex);
        return ret;
    }

    ret = PINT_tcache_set_info(capcache, TCACHE_SOFT_LIMIT,
                               CAPCACHE_DEFAULT_SOFT_LIMIT);
    if (ret < 0)
    {
        PINT_tcache_finalize(capcache);
        gen_mutex_unlock(&capcache_mutex);
        return ret;
    }

    ret = PINT_tcache_set_info(capcache, TCACHE_HARD_LIMIT,
                               CAPCACHE_DEFAULT_HARD_LIMIT);
    if (ret < 0)
    {
        PINT_tcache_finalize(capcache);
        gen_mutex_unlock(&capcache_mutex);
        return ret;
    }

    ret = PINT_tcache_set_info(capcache, TCACHE_RECLAIM_PERCENTAGE,
                               CAPCACHE_DEFAULT_RECLAIM_PERCENTAGE);
    if (ret < 0)
    {
        PINT_tcache_finalize(capcache);
        gen_mutex_unlock(&capcache_mutex);
        return ret;
    }

    gen_mutex_unlock(&capcache_mutex);

    return 0;
}

void PINT_capcache_finalize(void)
{
    gen_mutex_lock(&capcache_mutex);

    assert(capcache);
    PINT_tcache_finalize(capcache);
    capcache = NULL;

    return;
}

int PINT_capcache_get_info(
    enum PINT_capcache_options option,
    unsigned int *arg)
{
    int ret = -PVFS_EINVAL;

    gen_mutex_lock(&capcache_mutex);
    ret = PINT_tcache_get_info(capcache, option, arg);
    gen_mutex_unlock(&capcache_mutex);

    return ret;
}

int PINT_capcache_set_info(
    enum PINT_capcache_options option,
    unsigned int arg)
{
    int ret = -PVFS_EINVAL;

    gen_mutex_lock(&capcache_mutex);
    ret = PINT_tcache_set_info(capcache, option, arg);
    gen_mutex_unlock(&capcache_mutex);

    return ret;
}

int PINT_capcache_lookup(PVFS_object_ref *objref, PVFS_capability **capability)
{
    int ret = -PVFS_EINVAL;
    struct PINT_tcache_entry *entry;
    struct capcache_payload *payload;
    int status;

    gen_mutex_lock(&capcache_mutex);
    
    ret = PINT_tcache_lookup(capcache, &objref, &entry, &status);
    if (ret < 0)
    {
        gen_mutex_unlock(&capcache_mutex);
        return ret;
    }
    else if (status < 0)
    {
        gen_mutex_unlock(&capcache_mutex);
        return -PVFS_ENOENT;
    }

    payload = (struct capcache_payload*)entry->payload;
    *capability = PINT_dup_capability(payload->capability);
    if (!(*capability))
    {
        gen_mutex_unlock(&capcache_mutex);
        return -PVFS_ENOMEM;
    }

    gen_mutex_unlock(&capcache_mutex);
    
    return 0;
}

int PINT_capcache_insert(PVFS_object_ref *objref, PVFS_capability *capability)
{
    int ret = -PVFS_EINVAL;
    struct capcache_payload *payload;
    struct PINT_tcache_entry *entry;
    int status;

    payload = (struct capcache_payload*)malloc(
                                              sizeof(struct capcache_payload));
    if (!payload)
    {
        return -PVFS_ENOMEM;
    }

    payload->objref.handle = objref->handle;
    payload->objref.fs_id = objref->fs_id;
    
    payload->capability = PINT_dup_capability(capability);
    if (!payload->capability)
    {
        free(payload);
        return -PVFS_ENOMEM;
    }

    gen_mutex_lock(&capcache_mutex);

    /* TODO: check for old entry and replace */
    /* TODO: insert new entry */

    gen_mutex_unlock(&capcache_mutex);

    return 0;
}

static int compare_key_entry(void *key, struct qhash_head *link)
{
    PVFS_object_ref *objref = (PVFS_object_ref*)key;
    struct PINT_tcache_entry *entry;
    struct capcache_payload *payload;

    entry = qhash_entry(link, struct PINT_tcache_entry, hash_link);
    assert(entry);

    payload = (struct capcache_payload*)entry->payload;
    if (objref->handle != payload->objref.handle ||
        objref->fs_id != payload->objref.fs_id)
    {
        return 0;
    }

    return 1;
}

static int hash_key(void *key, int table_size)
{
    PVFS_object_ref *objref = (PVFS_object_ref*)key;
    int hash;

    hash = quickhash_64bit_hash(&objref->handle, table_size);
    hash ^= quickhash_32bit_hash(&objref->fs_id, table_size);

    return hash;
}

static int free_payload(void *payload)
{
    struct capcache_payload *tmp = (struct capcache_payload*)payload;

    PINT_release_capability(tmp->capability);
    free(tmp);

    return 0;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

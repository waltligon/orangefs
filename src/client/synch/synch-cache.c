/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "pvfs2.h"
#include "pvfs2-types.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "quicklist.h"
#include "quickhash.h"
#include "vec_prot.h"
#include "vec_prot_client.h"
#include "dlm_prot.h"
#include "dlm_prot_client.h"
#include "vec_common.h"
#include "synch-cache.h"

/*
 * This module takes care of caching vectors generated on a vector
 * put on every client. This vector is per-fsid/per-handle
 * Also takes care of caching dlm tokens if we so desired...
 */

struct dlm_handle_entry {
    /* For a given handle, we could be caching many dlm lock tokens */
    int ntokens;
    dlm_token_t   *tokens;
};

struct vec_handle_entry {
    /* For a given handle, we only cache the last vector if at all */
    vec_vectors_t v;
};

struct handle_entry {
    PVFS_object_ref ref;
    struct dlm_handle_entry dlm;
    struct vec_handle_entry vec;
    struct qlist_head hash_link;
};

static struct qhash_table *s_handle_table = NULL;
static int                 handle_table_size = 31;

static int vec_handle_entry_ctor(struct vec_handle_entry *entry, int vec_count)
{
    if (vec_ctor(&entry->v, vec_count) < 0) {
        return -ENOMEM;
    }
    return 0;
}

static void vec_handle_entry_dtor(struct vec_handle_entry *entry)
{
    if (entry) {
        vec_dtor(&entry->v);
    }
    return;
}

static int dlm_handle_entry_ctor(struct dlm_handle_entry *entry) __attribute__((unused));
static int dlm_handle_entry_ctor(struct dlm_handle_entry *entry)
{
    return 0;
}

static void dlm_handle_entry_dtor(struct dlm_handle_entry *entry)
{
    return;
}

static struct handle_entry *
handle_entry_ctor(PVFS_fs_id fsid, PVFS_handle handle)
{
    struct handle_entry *entry =
        (struct handle_entry *) calloc(1, sizeof(struct handle_entry));
    if (entry) {
        entry->ref.fs_id = fsid;
        entry->ref.handle = handle;
    }
    return entry;
}

static void handle_entry_dtor(struct handle_entry *entry)
{
    if (entry) {
        dlm_handle_entry_dtor(&entry->dlm);
        vec_handle_entry_dtor(&entry->vec);
        free(entry);
    }
    return;
}

static int object_compare(void *key, struct qhash_head *entry)
{
    struct handle_entry *o2 = NULL;
    PVFS_object_ref *o1 = (PVFS_object_ref *) key;

    o2 = qlist_entry(entry, struct handle_entry, hash_link);
    if (o1->handle == o2->ref.handle && o1->fs_id == o2->ref.fs_id) 
        return 1;
    return 0;
}

static int object_hash(void *key, int table_size)
{
    PVFS_object_ref *o1 = (PVFS_object_ref *) key;
    int hash_value = 0;

    hash_value = abs(hash2(o1->fs_id, o1->handle));
    return hash_value % table_size;
}

static int add_to_handle_table(struct handle_entry *entry)
{
    if (entry)
    {
        qhash_add(s_handle_table,
                (void *) &entry->ref, &entry->hash_link);
        return 0;
    }
    return -1;
}

static struct handle_entry *find_in_handle_table(PVFS_object_ref *ref)
{
    struct handle_entry *entry = NULL;

    if (ref) {
        struct qlist_head *link = qhash_search(s_handle_table, (void *) ref);
        if (link) {
            entry = qhash_entry(link, struct handle_entry, hash_link);
            if (entry->ref.fs_id != ref->fs_id || entry->ref.handle != ref->handle)
            {
                gossip_err("impossible happened: object id's dont match\n");
                return NULL;
            }
        }
    }
    return entry;
}

/* callers cannot free the return value */
static vec_vectors_t *vec_cache_get(PVFS_object_ref *ref)
{
    struct handle_entry *entry = find_in_handle_table(ref);
    if (entry) 
    {
        return &entry->vec.v;
    }
    /* Nothing cached here! */
    return NULL;
}

/* Insert a vector for a given file system object */
static int vec_cache_insert(PVFS_object_ref *ref, vec_vectors_t *new_vec)
{
    struct handle_entry *entry;
    
    if (!ref || !new_vec)
        return -EINVAL;

    entry = find_in_handle_table(ref);
    if (entry) {
        /* Update the existing entry's vector but make sure that vector counts matches */
        if (entry->vec.v.vec_vectors_t_val == NULL) {
            gossip_err("vector cache's vector pointer is NULL?\n");
            return -ENOMEM;
        }
        if (entry->vec.v.vec_vectors_t_len != new_vec->vec_vectors_t_len) {
            gossip_err("vector cache's vector lengths don't match (%d) instead of (%d)\n",
                    new_vec->vec_vectors_t_len, entry->vec.v.vec_vectors_t_len);
            return -EINVAL;
        }
    }
    else {
        entry = handle_entry_ctor(ref->fs_id, ref->handle);
        if (entry == NULL) {
            gossip_err("vector cache: allocation failed\n");
            return -ENOMEM;
        }
        if (vec_handle_entry_ctor(&entry->vec, new_vec->vec_vectors_t_len) < 0) {
            gossip_err("vector cache: allocation failed\n");
            handle_entry_dtor(entry);
            return -ENOMEM;
        }
        /* Add a new entry to the hash table */
        add_to_handle_table(entry);
    }
    /* Copy the vector provided */
    memcpy(entry->vec.v.vec_vectors_t_val, new_vec->vec_vectors_t_val,
                new_vec->vec_vectors_t_len * sizeof(uint32_t));
    return 0;
}

static int vec_cache_init(void)
{
    s_handle_table = qhash_init(object_compare, object_hash, handle_table_size);
    if (s_handle_table == NULL)
    {
        return -ENOMEM;
    }
    return 0;
}

static void vec_cache_finalize(void)
{
    int i;
    if (s_handle_table == NULL)
        return;
    for (i = 0; i < handle_table_size; i++) {
        struct qhash_head *hash_link;
        do {
            hash_link = qhash_search_and_remove_at_index(
                    s_handle_table, i);
            if (hash_link) {
                struct handle_entry *entry =
                    qhash_entry(hash_link, struct handle_entry, hash_link);
                handle_entry_dtor(entry);
            }
        } while (hash_link);
    }
    qhash_finalize(s_handle_table);
    s_handle_table = NULL;
    return;
}

/* For now we don't cache lock tokens, but this would be the place to do it if we so desired */

static int dlm_cache_init(void)
{
	return 0;
}

static void dlm_cache_finalize(void)
{
	return;
}

static int dlm_cache_insert(PVFS_object_ref *ref, dlm_token_t *token)
{
	return 0;
}

static dlm_token_t* dlm_cache_get(PVFS_object_ref *ref)
{
	return NULL;
}

int PINT_synch_cache_init(void)
{
    int ret;
    if ((ret = dlm_cache_init()) < 0) {
        return ret;
    }
    if ((ret = vec_cache_init()) < 0) {
        dlm_cache_finalize();
        return ret;
    }
    return 0;
}

void PINT_synch_cache_finalize(void)
{
    vec_cache_finalize();
    dlm_cache_finalize();
    return;
}

int PINT_synch_cache_insert(enum PVFS_synch_method method, 
        PVFS_object_ref *ref, void *item)
{
    if (method == PVFS_SYNCH_NONE)
        return 0;
    else if (method == PVFS_SYNCH_VECTOR)
        return vec_cache_insert(ref, (vec_vectors_t *) item);
    else 
        return dlm_cache_insert(ref, (dlm_token_t *) item);
}

void* PINT_synch_cache_get(enum PVFS_synch_method method,
        PVFS_object_ref *ref)
{
    if (method == PVFS_SYNCH_NONE)
        return 0;
    else if (method == PVFS_SYNCH_VECTOR)
        return vec_cache_get(ref);
    else 
        return dlm_cache_get(ref);
}

int PINT_synch_cache_invalidate(enum PVFS_synch_method method,
        PVFS_object_ref *ref)
{
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

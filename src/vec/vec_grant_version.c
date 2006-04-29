/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "vec_config.h"
#include "vec_prot.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <netinet/in.h>
#include <pthread.h>
#include "pvfs2.h"
#include "pvfs2-types.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "vec_common.h"
#include "quickhash.h"
#include "quicklist.h"
#include "vec_grant_version.h"

struct handle_entry {
    pthread_mutex_t lock;
    PVFS_object_ref ref;
    int stripe_size;
    int nservers;
    int current_vec_index;
    vec_svectors_t sv;
    struct qlist_head hash_link;
};

static struct qhash_table *s_handle_table = NULL;
static int                 handle_table_size = 113;

static struct handle_entry *
handle_entry_ctor(PVFS_fs_id fsid, PVFS_handle handle, int stripe_size, int nservers)
{
    struct handle_entry *entry =
        (struct handle_entry *) calloc(1, sizeof(struct handle_entry));
    if (entry) {
        pthread_mutex_init(&entry->lock, NULL);
        entry->ref.fs_id = fsid;
        entry->ref.handle = handle;
        entry->stripe_size = stripe_size;
        entry->nservers = nservers;
        entry->current_vec_index = 0;
        if (svec_ctor(&entry->sv, 1, nservers) < 0) {
            free(entry);
            entry = NULL;
        }
    }
    return entry;
}

static void handle_entry_dtor(struct handle_entry *entry)
{
    if (entry) {
        svec_dtor(&entry->sv, entry->sv.vec_svectors_t_len);
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

/* For a given object in a given file system, retrieve the 
 * last "N" version vectors, where "N" is specified in
 * svec->vec_svectors_t_len
 * Called by vec_get() which in turn is called for a read
 */
int PINT_get_vec(handle_vec_cache_args *req, vec_svectors_t *svec)
{
    int i, num_req_vectors, err, dst_index = 0;
    struct handle_entry *entry;
    
    if (!req || !svec || svec->vec_svectors_t_len <= 0) {
        gossip_err("vector cache: invalid parameter\n");
        return -EINVAL;
    }

    entry = find_in_handle_table(&req->ref);
    if (entry) {
        pthread_mutex_lock(&entry->lock);
        if (entry->nservers != req->nservers) {
            gossip_err("PINT_get_vec: nservers in entry (%d) does not match request (%d)\n",
                    entry->nservers, req->nservers);
            pthread_mutex_unlock(&entry->lock);
            return -EINVAL;
        }
    }
    else {
        entry = handle_entry_ctor(req->ref.fs_id, req->ref.handle, 
                req->stripe_size, req->nservers);
        if (entry == NULL) {
            gossip_err("PINT_get_vec: allocation failed\n");
            return -ENOMEM;
        }
        pthread_mutex_lock(&entry->lock);
        /* Add a new entry to the hash table */
        add_to_handle_table(entry);
    }
    num_req_vectors = MIN(entry->current_vec_index + 1, svec->vec_svectors_t_len);
    /* Fill the requested number of vectors in svec */
    dst_index = 0;
    for (i = entry->current_vec_index + 1 - num_req_vectors; 
            i <= entry->current_vec_index; i++) 
    {
        if ((err = vec_lcopy(&svec->vec_svectors_t_val[dst_index++], 
                        &entry->sv.vec_svectors_t_val[i])) < 0) {
            break;
        }
    }
    if (i != (entry->current_vec_index + 1)) {
        svec->vec_svectors_t_len = 0;
    }
    else {
        svec->vec_svectors_t_len = num_req_vectors;
    }
    svec_print(svec);
    pthread_mutex_unlock(&entry->lock);
    return err;
}

static int construct_span_vector(handle_vec_cache_args *req, vec_vectors_t *span_vector)
{
    int i, err;
    int start_server, end_server;

    if (!req || !span_vector)
        return -EINVAL;
    if ((err = vec_ctor(span_vector, req->nservers)) < 0) {
        return err;
    }
    start_server = req->offset / req->stripe_size;
    end_server   = (req->offset + req->size - 1) / req->stripe_size;
    for (i = start_server; i <= end_server; i++) {
        span_vector->vec_vectors_t_val[i % req->nservers] = 1;
    }
    return 0;
}

/* 
 * For a given object in a given file system, atomically increment and 
 * add a new vector to the set of version vectors for the object.
 * Return the vector prior to the increment in new_vec.
 * Called by vec_put() which in turn is called for a write
 */
int PINT_inc_vec(handle_vec_cache_args *req, vec_vectors_t *new_vec)
{
    struct handle_entry *entry;
    int err, old_index;
    vec_vectors_t span_vector;
    
    if (!req || !new_vec)
        return -EINVAL;

    entry = find_in_handle_table(&req->ref);
    if (entry) {
        pthread_mutex_lock(&entry->lock);
        if (entry->nservers != req->nservers) {
            gossip_err("PINT_inc_vec: nservers in entry (%d) does not match request (%d)\n",
                    entry->nservers, req->nservers);
            pthread_mutex_unlock(&entry->lock);
            return -EINVAL;
        }
    }
    else {
        entry = handle_entry_ctor(req->ref.fs_id, req->ref.handle, req->stripe_size, req->nservers);
        if (entry == NULL) {
            gossip_err("PINT_inc_vec: allocation failed\n");
            return -ENOMEM;
        }
        pthread_mutex_lock(&entry->lock);
        /* Add a new entry to the hash table */
        add_to_handle_table(entry);
    }
    /* Construct vector for the I/O */
    if ((err = construct_span_vector(req, &span_vector)) < 0) {
        pthread_mutex_unlock(&entry->lock);
        return err;
    }
    old_index = entry->current_vec_index;
    /* Copy the current vector to new_vec */
    if ((err = vec_copy(new_vec, &entry->sv.vec_svectors_t_val[old_index])) < 0) {
        vec_dtor(&span_vector);
        pthread_mutex_unlock(&entry->lock);
        return err;
    }
    entry->current_vec_index++;
    /* Extend the set vector by 1 */
    if ((err = svec_extend(&entry->sv, entry->current_vec_index + 1,
                    req->nservers)) < 0) {
        vec_dtor(&span_vector);
        pthread_mutex_unlock(&entry->lock);
        return err;
    }
    /* increment the appropriate elements of the vector */
    if ((err = vec_add(&entry->sv.vec_svectors_t_val[old_index + 1], 
                    &entry->sv.vec_svectors_t_val[old_index], &span_vector)) < 0) {
        vec_dtor(&span_vector);
        pthread_mutex_unlock(&entry->lock);
        return err;
    }
    vec_print(&entry->sv.vec_svectors_t_val[old_index + 1]);
    pthread_mutex_unlock(&entry->lock);
    return 0;
}

int PINT_handle_vec_cache_init(void)
{
    s_handle_table = qhash_init(object_compare, object_hash, handle_table_size);
    if (s_handle_table == NULL)
    {
        return -ENOMEM;
    }
    return 0;
}

void PINT_handle_vec_cache_finalize(void)
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


/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

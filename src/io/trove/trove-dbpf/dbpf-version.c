/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>
#include "trove.h"
#include "dbpf.h"
#include "dbpf-version.h"
#include "dbpf-version-buffer.h"
#include "quicklist.h"
#include "quickhash.h"

typedef struct
{
    TROVE_vtag_s vtag;
    TROVE_offset *stream_offset_array;
    TROVE_size *stream_size_array;
    int stream_count;
    dbpf_version_buffer_ref buffer_ref;

    struct qlist_head link;
} dbpf_version;

typedef struct
{
    TROVE_coll_id coll_id;
    TROVE_handle handle;

    struct qlist_head link;
} dbpf_version_key;

typedef struct
{
    dbpf_version_key * key;
    TROVE_vtag_s last_committed;
    struct qlist_head version_list;
    struct qhash_head hash_link;
} dbpf_version_entry;

static int dbpf_version_list_sorted_insert(
    struct qlist_head * list, dbpf_version * new_vers);
static int dbpf_version_destroy(
        TROVE_coll_id coll_id, TROVE_handle handle, dbpf_version * version);

static int dbpf_version_entry_compare(void *key, struct qhash_head *link)
{
    dbpf_version_key * refkey = (dbpf_version_key *)key;
    dbpf_version_entry * entry = (dbpf_version_entry *)link;

    if(entry->key == refkey)
    {
        return 1;
    }

    if(entry->key->coll_id == refkey->coll_id &&
       entry->key->handle == refkey->handle)
    {
	return 1;
    }
    return 0;
}
    
static int dbpf_version_key_hash(void *key, int table_size)
{
     dbpf_version_key * k = (dbpf_version_key *)key;

     return (k->coll_id ^ 
             ((uint32_t)(k->handle >> 32)) ^
             ((uint32_t)(k->handle & 0x00000000FFFFFFFF))) % table_size;
}

static struct qhash_table * dbpf_version_table = NULL;
static QLIST_HEAD(dbpf_version_keys);

int dbpf_version_initialize()
{
    dbpf_version_table = qhash_init(dbpf_version_entry_compare, 
                                    dbpf_version_key_hash,
                                    1021);
    if(!dbpf_version_table)
    {
	return -TROVE_ENOMEM;
    }

    return 0;
}

int dbpf_version_finalize()
{
    qhash_finalize(dbpf_version_table);
    return 0;
}

int dbpf_version_add(TROVE_coll_id coll_id,
                     TROVE_handle handle,
                     TROVE_vtag_s * vtag,
                     char ** mem_buffers,
                     TROVE_size * mem_sizes,
                     int mem_count,
                     TROVE_offset *stream_offsets,
                     TROVE_size *stream_sizes,
                     int stream_count)
{
    dbpf_version * vers;
    struct qhash_head * entry;
    dbpf_version_entry * version_entry;
    dbpf_version_key version_key;
    dbpf_version_key * keyp;
    int res = 0;

    vers = malloc(sizeof(dbpf_version));
    if(!vers)
    {
        res = -TROVE_ENOMEM;
        goto error_exit;
    }

    res = dbpf_version_buffer_create(
            coll_id, handle, vtag->version, mem_buffers,
            mem_sizes, mem_count, &vers->buffer_ref);
    if(res < 0)
    {
        goto destroy_vers;
    }

    vers->vtag = *vtag;
    vers->stream_offset_array = malloc(stream_count * sizeof(TROVE_offset));
    if(!vers->stream_offset_array)
    {
        res = -TROVE_ENOMEM;
        goto destroy_buffer;
    }
    memcpy(vers->stream_offset_array, stream_offsets, 
           (stream_count * sizeof(TROVE_offset)));
    
    vers->stream_size_array = malloc(stream_count * sizeof(TROVE_size));
    if(!vers->stream_size_array)
    {
        res = -TROVE_ENOMEM;
        goto destroy_stream_offset_array;
    }
    memcpy(vers->stream_size_array, stream_sizes,
           (stream_count * sizeof(TROVE_size)));
    
    vers->stream_count = stream_count;

    version_key.coll_id = coll_id;
    version_key.handle = handle;
    entry = qhash_search(dbpf_version_table, &version_key);
    if(!entry)
    {
        dbpf_version_entry * newentry;

        keyp = malloc(sizeof(dbpf_version_key));
        if(!keyp)
        {
            res = -TROVE_ENOMEM;
            goto destroy_stream_size_array;
        }

        keyp->coll_id = coll_id;
        keyp->handle = handle;
        qlist_add_tail(&keyp->link, &dbpf_version_keys);

        newentry = malloc(sizeof(dbpf_version_entry));
        if(!newentry)
        {
            res = -TROVE_ENOMEM;
            goto destroy_keyp;
        }

        newentry->key = keyp;
        newentry->last_committed.version = 0;
        INIT_QLIST_HEAD(&newentry->version_list);
        qhash_add(dbpf_version_table, newentry->key, &newentry->hash_link);
        entry = &newentry->hash_link;

    }

    version_entry = qhash_entry(entry, dbpf_version_entry, hash_link);
    dbpf_version_list_sorted_insert(&version_entry->version_list, vers);

    return 0;

destroy_keyp:
    free(keyp);

destroy_stream_size_array:
    free(vers->stream_size_array);
    
destroy_stream_offset_array:
    free(vers->stream_offset_array);
    
destroy_buffer:
    dbpf_version_buffer_destroy(
            version_entry->key->coll_id,
            version_entry->key->handle,
            &vers->buffer_ref);

destroy_vers:
    free(vers);

error_exit:
    return res;

}

int dbpf_version_find_commit(TROVE_coll_id * coll_id,
                             TROVE_handle * handle,
                             TROVE_offset ** stream_offsets,
                             TROVE_size ** stream_sizes,
                             int * stream_count,
                             char ** membuff,
                             TROVE_size * memsize)
{
    struct qlist_head * next;
    dbpf_version_key * key;
    dbpf_version_entry * version_entry;
    struct qhash_head * entry;
    struct qlist_head * commit_entry;
    dbpf_version * commit;
    
    /* cycle through all the handles in the table and check
     * if any buffers can be committed
     */
    qlist_for_each(next, &dbpf_version_keys)
    {
        key = qlist_entry(next, dbpf_version_key, link);
        entry = qhash_search(dbpf_version_table, key);
        
        assert(entry);
        version_entry = qhash_entry(entry, dbpf_version_entry, hash_link);
        
        commit_entry = version_entry->version_list.next;
        while(commit_entry != &version_entry->version_list)
        {
            commit = qlist_entry(
                commit_entry, dbpf_version, link);
        
            if(commit->vtag.version !=
               (version_entry->last_committed.version + 1))
            {
                break;
            }

            version_entry->last_committed.version = commit->vtag.version;

            *handle = version_entry->key->handle;
            
            *stream_offsets = malloc(
                    commit->stream_count * sizeof(TROVE_offset));
            if(!*stream_offsets)
            {
                return -TROVE_ENOMEM;
            }
            memcpy(*stream_offsets, commit->stream_offset_array, 
                   (commit->stream_count * sizeof(TROVE_offset)));

            *stream_sizes = malloc(commit->stream_count * sizeof(TROVE_size));
            if(!*stream_sizes)
            {
                return -TROVE_ENOMEM;
            }
            memcpy(*stream_sizes, commit->stream_size_array,
                   (commit->stream_count * sizeof(TROVE_size)));

            *stream_count = commit->stream_count;

            dbpf_version_buffer_get(&commit->buffer_ref, membuff, memsize);

            commit_entry->next->prev = commit_entry->prev;
            commit_entry->prev->next = commit_entry->next;
            commit_entry = commit_entry->next;
            dbpf_version_destroy(
                    version_entry->key->coll_id,
                    version_entry->key->handle,
                    commit);

            if(commit_entry == &version_entry->version_list)
            {
                /* no more entries in list.  remove hash entry */
                entry = qhash_search_and_remove(dbpf_version_table, key);
                version_entry = qhash_entry(
                        entry, dbpf_version_entry, hash_link);
                
                free(version_entry);
            }
    
            /* instead of continuing to get versions that can be committed,
             * we just return for now.  Later we can add some joining
             * code that merges the versions into one commit buffer
             */
            return 1;
        }
    }

    return 0;
}

static int dbpf_version_destroy(
        TROVE_coll_id coll_id,
        TROVE_handle handle,
        dbpf_version * version)
{
    if(version->stream_offset_array)
    {
        free(version->stream_offset_array);
    }

    if(version->stream_size_array)
    {
        free(version->stream_size_array);
    }

    dbpf_version_buffer_destroy(coll_id, handle, &version->buffer_ref);

    free(version);
    return 0;
}

static int dbpf_version_list_sorted_insert(
    struct qlist_head * list, dbpf_version * new_vers)
{
    struct qlist_head * pos;
    dbpf_version * vers;
    
    qlist_for_each(pos, list)
    {
        vers = qlist_entry(pos, dbpf_version, link);

        if(new_vers->vtag.version < vers->vtag.version)
        {
            new_vers->link.next = pos;
            new_vers->link.prev = pos->prev;
            pos->prev->next = &new_vers->link;
            pos->prev = &new_vers->link;
            
            break;
        }
    }
    
    if(pos == list)
    {
        /* insert at the end */
        new_vers->link.next = pos;
        new_vers->link.prev = pos->prev;
        pos->prev->next = &new_vers->link;
        pos->prev = &new_vers->link;
    }

    return 0;
}

extern size_t PINT_dbpf_version_allowed_buffer_size;

int dbpf_version_set_allowed_buffer_size(size_t size)
{
    PINT_dbpf_version_allowed_buffer_size = size;
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

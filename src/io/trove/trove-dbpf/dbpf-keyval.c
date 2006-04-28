/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * dbpf_keyval_write semantics:
 *
 * Value buffer is supplied by user with size of buffer
 * If size is too small DB returns an error (Cannot allocated memory)
 * and return the size of the buffer needed in val_p->read_sz
 *
 * WBL 6/05
 *
 */

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-attr-cache.h"
#include "dbpf-keyval-pcache.h"
#include "gossip.h"
#include "pvfs2-internal.h"
#include "pint-perf-counter.h"

extern struct PINT_perf_counter *PINT_server_pc;

#define DBPF_MAX_KEY_LENGTH PVFS_NAME_MAX

/**
 * The keyval database contains attributes for pvfs2 handles
 * (files, directories, symlinks, etc.) that are not considered
 * common attributes.  The following table lists all the currently
 * stored keyvals, based on their handle type:
 *
 * <table>
 * <th><td>Handle Type</td><td>Key Class</td><td>Key</td><td>Value</td><td>Description</td></th>
 * <tr><td>meta-file</td><td>COMMON</td><td>metafile_handles</td><td>Datafile Array</td><td>Holds the list of datafile handles that exist for this metafile</td></tr>
 * <tr><td>meta-file</td><td>COMMON</td><td>metafile_dist</td><td>Distribution</td><td>Holds the distribution type</td></tr>
 * <tr><td>symlink</td><td>COMMON</td><td>symlink_target</td><td>Target Handle</td><td>Holds the file handle that this symlink points to</td></tr>
 * <tr><td>directory</td><td>COMMON</td><td>dir_ent</td><td>Entries Handle</td><td>Holds the handle that manages the directory entries for this directory</td></tr>
 * <tr><td>directory</td><td>COMMON</td><td>dirdata_size</td><td>Size</td><td>Holds the number of entries in the directory</td></tr>
 * <tr><td>dir-ent</td><td>COMPONENT</td><td>&lt;component name&gt;</td><td>Entry Handle</td><td>Holds the directory entry (with name &lt;component name &gt;) handle</td></tr>
 * <tr><td>ALL</td><td>XATTR</td><td>&lt;extended attribute name&gt;</td><td>&lt;extended attribute content&gt;</td><td>Holds the extended attribute that can be defined for any handle type (commonly files and directories).</td></tr>
 * </table>
 */

/* Structure for key in the keyval DB */
/* The keys in the keyval database are now stored as the following
 * struct (dbpf_keyval_db_entry).  The size of key field (the common
 * name or component name of the key) is not explicitly specified in the
 * struct, instead it is calculated from the DBT->size field of the
 * berkeley db key using the macros below.  Its important that the
 * 'size' and 'ulen' fields of the DBT key struct are set correctly when
 * calling get and put.  'size' should be the actual size of the string, 'ulen'
 * should be the size available for the dbpf_keyval_db_entry struct, most
 * likely sizeof(struct dbpf_keyval_db_entry).
 */

struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char key[DBPF_MAX_KEY_LENGTH];
};

#define DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(_size) \
    (sizeof(TROVE_handle) + _size)

#define DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(_size) \
    (_size - sizeof(TROVE_handle))

extern gen_mutex_t dbpf_attr_cache_mutex;

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p);


#define DBPF_ITERATE_CURRENT_POSITION 1

static int dbpf_keyval_iterate_get_first_entry(
    TROVE_handle handle, 
    DBC * dbc_p);

static int dbpf_keyval_iterate_step_to_position(
    TROVE_handle handle, 
    TROVE_ds_position pos,
    DBC * dbc_p);

static int dbpf_keyval_iterate_skip_to_position(
    TROVE_handle handle, 
    TROVE_ds_position pos, 
    PINT_dbpf_keyval_pcache *pcache,
    DBC * dbc_p);

static int dbpf_keyval_iterate_cursor_get(
    TROVE_handle handle, 
    DBC * dbc_p, 
    TROVE_keyval_s * key, 
    TROVE_keyval_s * data, 
    uint32_t db_flags);

static int dbpf_keyval_read(TROVE_coll_id coll_id,
                            TROVE_handle handle,
                            TROVE_keyval_s *key_p,
                            TROVE_keyval_s *val_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag, 
                            void *user_ptr,
                            TROVE_context_id context_id,
                            TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {handle, coll_id};

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Read "
                 "of %s\n", (char *)key_p->buffer);

    gen_mutex_lock(&dbpf_attr_cache_mutex);
    cache_elem = dbpf_attr_cache_elem_lookup(ref);
    if (cache_elem)
    {
        dbpf_keyval_pair_cache_elem_t *keyval_pair =
            dbpf_attr_cache_elem_get_data_based_on_key(
                cache_elem, key_p->buffer);
        if (keyval_pair)
        {
            dbpf_attr_cache_keyval_pair_fetch_cached_data(
                cache_elem, keyval_pair, val_p->buffer,
                &val_p->buffer_sz);
            val_p->read_sz = val_p->buffer_sz;
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            return 1;
        }
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_READ,
                        handle,
                        coll_p,
                        dbpf_keyval_read_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.k_read.key = key_p;
    q_op_p->op.u.k_read.val = val_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    memset(&key, 0, sizeof(key));

    key_entry.handle = op_p->handle;
    memcpy(key_entry.key, 
           op_p->u.k_read.key->buffer, 
           op_p->u.k_read.key->buffer_sz);
    key.data = &key_entry;
    key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
        op_p->u.k_read.key->buffer_sz);

    memset(&data, 0, sizeof(data));
    data.data = op_p->u.k_read.val->buffer;
    data.ulen = op_p->u.k_read.val->buffer_sz;
    data.flags = DB_DBT_USERMEM;

    ret = op_p->coll_p->keyval_db->get(
        op_p->coll_p->keyval_db, NULL, &key, &data, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "warning: keyval read error on handle %llu and "
                     "key=%*s (%s)\n", llu(op_p->handle),
                     op_p->u.k_read.key->buffer_sz,
                     (char *)op_p->u.k_read.key->buffer, 
                     db_strerror(ret));

        /* if data buffer is too small returns a memory error */
        if (data.ulen < data.size)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "warning: Value buffer too small %d < %d\n",
                         data.ulen, data.size);
            /* let the user know */
            op_p->u.k_read.val->read_sz = data.size;
        }

        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }
    op_p->u.k_read.val->read_sz = data.size;

    /* cache this data in the attr cache if we can */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    if (dbpf_attr_cache_elem_set_data_based_on_key(
            ref, key_entry.key,
            op_p->u.k_read.val->buffer, data.size))
    {
        /*
NOTE: this can happen if the keyword isn't registered, or if
there is no associated cache_elem for this key
*/
        gossip_debug(
            GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data retrieved "
            "(key is %s)\n", (char *)key_entry.key);
    }
    else
    {
        gossip_debug(
            GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
            "retrieved (key is %s)\n",
            (char *)key_entry.key);
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    return 1;

return_error:
    return ret;
}

static int dbpf_keyval_write(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             TROVE_vtag_s *vtag,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_WRITE,
                        handle,
                        coll_p,
                        dbpf_keyval_write_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.k_write.key = *key_p;
    q_op_p->op.u.k_write.val = *val_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
        
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return 0;
}

static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    u_int32_t dbflags = 0;
    struct dbpf_keyval_db_entry key_entry;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "dbpf_keyval_write_op_svc: handle: %llu, key: %*s\n",
                 llu(op_p->handle),
                 op_p->u.k_write.key.buffer_sz,
                 (char *)op_p->u.k_write.key.buffer);

    key_entry.handle = op_p->handle;

    assert(op_p->u.k_write.key.buffer_sz <= DBPF_MAX_KEY_LENGTH);
    memcpy(key_entry.key, 
           op_p->u.k_write.key.buffer,
           op_p->u.k_write.key.buffer_sz);

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = &key_entry;
    key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
        op_p->u.k_write.key.buffer_sz);
    data.data = op_p->u.k_write.val.buffer;
    data.size = op_p->u.k_write.val.buffer_sz;

    /* If TROVE_NOOVERWRITE flag was set, make sure that we don't create the
     * key if it exists */
    if ((op_p->flags & TROVE_NOOVERWRITE))
    {
        dbflags |= DB_NOOVERWRITE;
    }
    /* if TROVE_ONLYOVERWRITE flag was set, make sure that the key exists
     * before overwriting it */
    else if ((op_p->flags & TROVE_ONLYOVERWRITE))
    {
        DBT tmpdata;

        memset(&tmpdata, 0, sizeof(tmpdata));
        tmpdata.ulen = op_p->u.k_write.val.buffer_sz;
        tmpdata.data = (void *) malloc(tmpdata.ulen);
        tmpdata.flags = DB_DBT_USERMEM;
        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &tmpdata, 0);
        /* A failed get implies that keys possibly did not exist */
        if (ret != 0)
        {
            /* The only case where we are ok is val buffer
             *  is too small */
            if (tmpdata.ulen < tmpdata.size)
            {
                ret = 0;
            }
            else
            {
                ret = -dbpf_db_error_to_trove_error(ret);
            }
        }
        free(tmpdata.data);
        /* If there was an error, we need to return right here */
        if (ret != 0)
        {
            goto return_error;
        }
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "keyval_db->put(handle= %llu, key= %*s (%d)) size=%d\n",
                 llu(key_entry.handle), 
                 op_p->u.k_write.key.buffer_sz,
                 key_entry.key,
                 op_p->u.k_write.key.buffer_sz,
                 key.size);

    ret = op_p->coll_p->keyval_db->put(
        op_p->coll_p->keyval_db, NULL, &key, &data, dbflags);
    /* Either a put error or key already exists */
    if (ret != 0)
    {
        op_p->coll_p->keyval_db->err(
            op_p->coll_p->keyval_db, ret, "DB->put");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Write "
                 "of %s\n", (char *)key_entry.key);

    /*
       now that the data is written to disk, update the cache if it's
       an attr keyval we manage.
       */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    cache_elem = dbpf_attr_cache_elem_lookup(ref);
    if (cache_elem)
    {
        if (dbpf_attr_cache_elem_set_data_based_on_key(
                ref, key_entry.key,
                op_p->u.k_write.val.buffer, data.size))
        {
            /*
NOTE: this can happen if the keyword isn't registered,
or if there is no associated cache_elem for this key
*/
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data written "
                "(key is %s)\n", (char *)key_entry.key);
        }
        else
        {
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                "written (key is %s)\n",
                (char *)key_entry.key);
        }
    }

    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    DBPF_DB_SYNC_IF_NECESSARY(op_p, 
                              op_p->coll_p->keyval_db);

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

    return 1;

return_error:
    return ret;
}

static int dbpf_keyval_remove(TROVE_coll_id coll_id,
                              TROVE_handle handle,
                              TROVE_keyval_s *key_p,
                              TROVE_ds_flags flags,
                              TROVE_vtag_s *vtag,
                              void *user_ptr,
                              TROVE_context_id context_id,
                              TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_REMOVE_KEY,
                        handle,
                        coll_p,
                        dbpf_keyval_remove_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.k_remove.key = *key_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
      
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return 0;
}

static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "dbpf_keyval_remove_op_svc: handle: %llu, key: %*s\n",
                 llu(op_p->handle),
                 op_p->u.k_remove.key.buffer_sz,
                 (char *)op_p->u.k_remove.key.buffer);
                 
    ret = PINT_dbpf_keyval_remove(op_p->coll_p->keyval_db, op_p->handle,
                                  &op_p->u.k_remove.key);
    if (ret != 0)
    {
        goto return_error;
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, 
                              op_p->coll_p->keyval_db);

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

    return 1;

return_error:
    return ret;
}

static int dbpf_keyval_validate(TROVE_coll_id coll_id,
                                TROVE_handle handle,
                                TROVE_ds_flags flags,
                                TROVE_vtag_s *vtag,
                                void* user_ptr,
                                TROVE_context_id context_id,
                                TROVE_op_id *out_op_id_p)
{
    return -TROVE_ENOSYS;
}

static int dbpf_keyval_iterate(TROVE_coll_id coll_id,
                               TROVE_handle handle,
                               TROVE_ds_position *position_p,
                               TROVE_keyval_s *key_array,
                               TROVE_keyval_s *val_array,
                               int *inout_count_p,
                               TROVE_ds_flags flags,
                               TROVE_vtag_s *vtag,
                               void *user_ptr,
                               TROVE_context_id context_id,
                               TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_ITERATE,
                        handle,
                        coll_p,
                        dbpf_keyval_iterate_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.k_iterate.key_array = key_array;
    q_op_p->op.u.k_iterate.val_array = val_array;
    q_op_p->op.u.k_iterate.position_p = position_p;
    q_op_p->op.u.k_iterate.count_p = inout_count_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_keyval_iterate_op_svc()
 *
 * Operation:
 *
 * If position is TROVE_ITERATE_START, we set the position to the
 * start of the database (keyval space) and read, returning the
 * position of the last read keyval.
 *
 * If position is TROVE_ITERATE_END, then we hit the end previously,
 * so we just return that we are done and that there are 0 things
 * read.
 *
 * Otherwise we read and return the position of the last read keyval.
 *
 * In all cases we read using DB_NEXT.  This is ok because it behaves
 * like DB_FIRST (read the first record) when called with an
 * uninitialized cursor (so we just don't initialize the cursor in the
 * TROVE_ITERATE_START case).
 *
 */
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p)
{
    int count, ret;

    assert(*op_p->u.k_iterate.count_p > 0);

    count = *op_p->u.k_iterate.count_p;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                  "dbpf_keyval_iterate_op_svc: starting: fsid: %u, "
                  "handle: %llu, pos: %u\n", 
                 op_p->coll_p->coll_id, 
                 llu(op_p->handle),
                 *op_p->u.k_iterate.position_p);
    
    /* if they passed in that they are at the end, return 0.
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate.count_p = 0;
        return 1;
    }

    ret = PINT_dbpf_keyval_iterate(op_p->coll_p->keyval_db,
                                   op_p->handle,
                                   op_p->coll_p->pcache,
                                   op_p->u.k_iterate.key_array,
                                   op_p->u.k_iterate.val_array,
                                   &count,
                                   *op_p->u.k_iterate.position_p,
                                   NULL);
    if (ret == -TROVE_ENOENT)
    {
        *op_p->u.k_iterate.position_p = TROVE_ITERATE_END;
    }
    else if(ret != 0)
    {
        return ret;
    }
    else
    {
        if(*op_p->u.k_iterate.position_p == TROVE_ITERATE_START)
        {
            *op_p->u.k_iterate.position_p = count;
        }
        else
        {
            *op_p->u.k_iterate.position_p += count;
        }

        if(count != 0)
        {
            /* insert the key of the last entry read based on
             * its position
             */
            ret = PINT_dbpf_keyval_pcache_insert(
                op_p->coll_p->pcache, 
                op_p->handle,
                *op_p->u.k_iterate.position_p,
                op_p->u.k_iterate.key_array[count-1].buffer, 
                op_p->u.k_iterate.key_array[count-1].read_sz);
        }
    }
    
    *op_p->u.k_iterate.count_p = count;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                 "dbpf_keyval_iterate_op_svc: finished: "
                 "position: %d, count: %d\n", 
                 *op_p->u.k_iterate.position_p, *op_p->u.k_iterate.count_p);

    return 1;
}

static int dbpf_keyval_iterate_keys(TROVE_coll_id coll_id,
                                    TROVE_handle handle,
                                    TROVE_ds_position *position_p,
                                    TROVE_keyval_s *key_array,
                                    int *inout_count_p,
                                    TROVE_ds_flags flags,
                                    TROVE_vtag_s *vtag,
                                    void *user_ptr,
                                    TROVE_context_id context_id,
                                    TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_ITERATE_KEYS,
                        handle,
                        coll_p,
                        dbpf_keyval_iterate_keys_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.k_iterate_keys.key_array = key_array;
    q_op_p->op.u.k_iterate_keys.position_p = position_p;
    q_op_p->op.u.k_iterate_keys.count_p = inout_count_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_keyval_iterate_keys_op_svc()
 *
 * Operation:
 *
 * If position is TROVE_ITERATE_START, we set the position to the
 * start of the database (keyval space) and read, returning the
 * position of the last read keyval.
 *
 * If position is TROVE_ITERATE_END, then we hit the end previously,
 * so we just return that we are done and that there are 0 things
 * read.
 *
 * Otherwise we read and return the position of the last read keyval.
 *
 * In all cases we read using DB_NEXT.  This is ok because it behaves
 * like DB_FIRST (read the first record) when called with an
 * uninitialized cursor (so we just don't initialize the cursor in the
 * TROVE_ITERATE_START case).
 *
 */
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p)
{
    int count, ret;

    count = *op_p->u.k_iterate_keys.count_p;

    /* if they passed in that they are at the end, return 0.
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate_keys.count_p = 0;
        return 1;
    }

    ret = PINT_dbpf_keyval_iterate(op_p->coll_p->keyval_db,
                                   op_p->handle,
                                   op_p->coll_p->pcache,
                                   (count != 0) ?
                                   op_p->u.k_iterate_keys.key_array : NULL,
                                   NULL,
                                   &count,
                                   *op_p->u.k_iterate_keys.position_p,
                                   NULL);
    if (ret == -TROVE_ENOENT)
    {
        *op_p->u.k_iterate_keys.position_p = TROVE_ITERATE_END;
    }
    else if(ret != 0)
    {
        return ret;
    }
    else
    {
        if(*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_START)
        {
            *op_p->u.k_iterate_keys.position_p = count;
        }
        else
        {
            *op_p->u.k_iterate_keys.position_p += count;
        }

        if(count != 0 && *op_p->u.k_iterate_keys.count_p != 0)
        {
            ret = PINT_dbpf_keyval_pcache_insert(
                op_p->coll_p->pcache, 
                op_p->handle, 
                *op_p->u.k_iterate_keys.position_p,
                op_p->u.k_iterate_keys.key_array[count-1].buffer,
                op_p->u.k_iterate_keys.key_array[count-1].read_sz);
        }
    }

    *op_p->u.k_iterate_keys.count_p = count;
    return 1;
}


static int dbpf_keyval_read_list(TROVE_coll_id coll_id,
                                 TROVE_handle handle,
                                 TROVE_keyval_s *key_array,
                                 TROVE_keyval_s *val_array,
                                 int count,
                                 TROVE_ds_flags flags,
                                 TROVE_vtag_s *vtag,
                                 void *user_ptr,
                                 TROVE_context_id context_id,
                                 TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_READ_LIST,
                        handle,
                        coll_p,
                        dbpf_keyval_read_list_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.k_read_list.key_array = key_array;
    q_op_p->op.u.k_read_list.val_array = val_array;
    q_op_p->op.u.k_read_list.count = count;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;

    for(i = 0; i < op_p->u.k_read_list.count; i++)
    {
        key_entry.handle = op_p->handle;
        memcpy(key_entry.key, 
               op_p->u.k_read_list.key_array[i].buffer,
               op_p->u.k_read_list.key_array[i].buffer_sz);

        memset(&key, 0, sizeof(key));
        key.data = &key_entry;
        key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_read_list.key_array[i].buffer_sz);
        key.flags = DB_DBT_USERMEM;

        memset(&data, 0, sizeof(data));
        data.data = op_p->u.k_read_list.val_array[i].buffer;
        data.ulen = op_p->u.k_read_list.val_array[i].buffer_sz;
        data.flags = DB_DBT_USERMEM;

        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if (ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->get");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        op_p->u.k_read_list.val_array[i].read_sz = data.size;
    }

    return 1;

return_error:
    return ret;
}

static int dbpf_keyval_write_list(TROVE_coll_id coll_id,
                                  TROVE_handle handle,
                                  TROVE_keyval_s *key_array,
                                  TROVE_keyval_s *val_array,
                                  int count,
                                  TROVE_ds_flags flags,
                                  TROVE_vtag_s *vtag,
                                  void *user_ptr,
                                  TROVE_context_id context_id,
                                  TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_WRITE_LIST,
                        handle,
                        coll_p,
                        dbpf_keyval_write_list_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.k_write_list.key_array = key_array;
    q_op_p->op.u.k_write_list.val_array = val_array;
    q_op_p->op.u.k_write_list.count = count;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return 0;
}

static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    int k;
    char tmpdata[PVFS_NAME_MAX];
    key_entry.handle = op_p->handle;

    /* read each key to see if it is present */
    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memcpy(key_entry.key, op_p->u.k_write_list.key_array[k].buffer,
               op_p->u.k_write_list.key_array[k].buffer_sz);

        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
        key.data = &key_entry;
        key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_write_list.key_array[k].buffer_sz);
        key.flags |= DB_DBT_USERMEM;

        data.data = tmpdata;
        data.ulen = PVFS_NAME_MAX;
        data.flags |= DB_DBT_USERMEM;

        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if (ret != 0)
        {
            if(ret == DB_NOTFOUND && ((op_p->flags & TROVE_NOOVERWRITE) ||
                                      (!(op_p->flags & TROVE_ONLYOVERWRITE))))
            {
                /* this means key is not in DB, which is what we
                 * want for the no-overwrite case - so go to the next key
                 */
                continue;
            }

            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->get");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
    }

    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memcpy(key_entry.key, op_p->u.k_write_list.key_array[k].buffer,
               op_p->u.k_write_list.key_array[k].buffer_sz);

        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));

        key.data = &key_entry;
        key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_write_list.key_array[k].buffer_sz);

        data.flags = 0;
        data.data = op_p->u.k_write_list.val_array[k].buffer;
        data.size = op_p->u.k_write_list.val_array[k].buffer_sz;

        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "keyval_db->put(handle= %llu, key= %*s (%d)) size=%d\n",
                     llu(key_entry.handle), 
                     op_p->u.k_write_list.key_array[k].buffer_sz,
                     key_entry.key,
                     op_p->u.k_write_list.key_array[k].buffer_sz,
                     key.size);

        ret = op_p->coll_p->keyval_db->put(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if (ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->put");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Write "
                     "of %s\n", (char *)op_p->u.k_write_list.key_array[k].buffer);

        /*
           now that the data is written to disk, update the cache if it's
           an attr keyval we manage.
           */
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        cache_elem = dbpf_attr_cache_elem_lookup(ref);
        if (cache_elem)
        {
            if (dbpf_attr_cache_elem_set_data_based_on_key(
                    ref, key_entry.key,
                    op_p->u.k_write_list.val_array[k].buffer, data.size))
            {
                /*
NOTE: this can happen if the keyword isn't registered,
or if there is no associated cache_elem for this key
*/
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data written "
                    "(key is %s)\n", 
                    (char *)key_entry.key);
            }
            else
            {
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                    "written (key is %s)\n",
                    (char *)key_entry.key);
            }
        }
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, 
                              op_p->coll_p->keyval_db);

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);
    return 1;

return_error:
    return ret;
}

static int dbpf_keyval_flush(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        KEYVAL_FLUSH,
                        handle,
                        coll_p,
                        dbpf_keyval_flush_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;

    if ((ret = op_p->coll_p->keyval_db->sync(
                op_p->coll_p->keyval_db, 0)) != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    return 1;

return_error:
    return ret;
}    

int PINT_dbpf_keyval_iterate(
    DB *db_p,
    TROVE_handle handle,
    PINT_dbpf_keyval_pcache *pcache,    
    TROVE_keyval_s *keys_array,
    TROVE_keyval_s *values_array,
    int *count,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_iterate_callback callback)
{

    int ret = -TROVE_EINVAL, i=0, get_key_count=0;
    DBC *dbc_p = NULL;
    char keybuffer[PVFS_NAME_MAX];
    TROVE_keyval_s skey;
    TROVE_keyval_s *key;
    TROVE_keyval_s *val = NULL;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "Entered: PINT_dpbf_keyval_iterate\n");

    skey.buffer = keybuffer;
    skey.buffer_sz = PVFS_NAME_MAX;
    key = &skey;

    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "Exited: PINT_dpbf_keyval_iterate\n");

        gossip_lerr("db_p->cursor failed\n");
        *count = 0;
        return -dbpf_db_error_to_trove_error(ret);
    }

    if(pos == TROVE_ITERATE_START)
    {
        ret = dbpf_keyval_iterate_get_first_entry(handle, dbc_p);
        if(ret != 0)
        {
            goto return_error;
        }

        ret = DBPF_ITERATE_CURRENT_POSITION; 
    }
    else
    {
        ret = dbpf_keyval_iterate_skip_to_position(
            handle, pos, pcache, dbc_p);
        if(ret != 0 && ret != DBPF_ITERATE_CURRENT_POSITION)
        {
            goto return_error;
        }
    }

    if (*count == 0)
    {
        get_key_count = 1;
    }

    if(ret == DBPF_ITERATE_CURRENT_POSITION)
    {
        if(keys_array)
        {
            key = &keys_array[0];
            if(values_array)
            {
                val = &values_array[0];
            }
        }

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p,  key, val, DB_CURRENT);
        if(ret != 0)
        {
            goto return_error;
        }

        if(callback)
        {
            key->buffer_sz = key->read_sz;
            ret = callback(db_p, handle, key);
            if(ret != 0)
            {
                goto return_error;
            }
        }
        
        i = 1;
    }

    for(; i < *count || get_key_count; ++i)
    {
        if(keys_array)
        {
            key = &keys_array[i];
            if(values_array)
            {
                val = &values_array[i];
            }
        }
        else
        {
            /* must reset buffer_sz */
            key->buffer_sz = PVFS_NAME_MAX;
        }

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, key, val, DB_NEXT);
        if(ret != 0)
        {
            goto return_error;
        }

        if(callback)
        {
            key->buffer_sz = key->read_sz;
            ret = callback(db_p, handle, key);
            if(ret != 0)
            {
                goto return_error;
            }
        }
    }

return_error:

    *count = i;

    /* free the cursor */
    if(dbc_p)
    {
        dbc_p->c_close(dbc_p);
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "Exited: PINT_dpbf_keyval_iterate\n");

    return ret;
}

int PINT_dbpf_keyval_remove(
    DB *db_p, TROVE_handle handle, TROVE_keyval_s *key)
{
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    DBT db_key;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "PINT_dbpf_keyval_remove: handle (%llu), key: (%d) %*s\n",
                 llu(handle), key->buffer_sz, key->buffer_sz, (char *)key->buffer);

    key_entry.handle = handle;
    memcpy(key_entry.key, key->buffer, key->buffer_sz);

    memset(&db_key, 0, sizeof(db_key));
    db_key.data = &key_entry;
    db_key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(key->buffer_sz);

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "keyval_db->del(handle= %llu, key= %*s (%d)) size=%d\n",
                 llu(key_entry.handle),
                 key->buffer_sz,
                 key_entry.key,
                 key->buffer_sz,
                 db_key.size);

    ret = db_p->del(db_p, NULL, &db_key, 0);
    if(ret != 0)
    {
        db_p->err(db_p, ret, "DB->del");
        ret = -dbpf_db_error_to_trove_error(ret);
    }

    return ret;
}

static int dbpf_keyval_iterate_get_first_entry(
    TROVE_handle handle, DBC * dbc_p)
{
    int ret = 0;
    TROVE_keyval_s key;

    key.buffer = "\0";
    key.buffer_sz = 0;

    /* use Berkeley DB's DB_SET_RANGE functionality to move the cursor
     * to the first matching entry after the key with the specified handle.
     * This is done by creating a key that has a null component string.
     */
    ret = dbpf_keyval_iterate_cursor_get(
        handle, dbc_p, &key, NULL, DB_SET_RANGE);
    if(ret != 0)
    {
        return ret;
    }

    return 0;
}

static int dbpf_keyval_iterate_skip_to_position(
    TROVE_handle handle,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_pcache *pcache,
    DBC * dbc_p)
{
    int ret = 0;
    TROVE_keyval_s key;

    assert(pos != TROVE_ITERATE_START);

    memset(&key, 0, sizeof(TROVE_keyval_s));

    ret = PINT_dbpf_keyval_pcache_lookup(
        pcache, handle, pos, 
        (const char **)&key.buffer, &key.buffer_sz);
    if(ret == -PVFS_ENOENT)
    {
        /* if the lookup fails (because the server was restarted)
         * we fall back to stepping through all the entries to get
         * to the position
         */

        return dbpf_keyval_iterate_step_to_position(handle, pos, dbc_p);
    }

    ret = dbpf_keyval_iterate_cursor_get(
        handle, dbc_p, &key, NULL, DB_SET);
    if(ret == -TROVE_ENOENT)
    {
        /* cache lookup succeeded but entry is no longer in the DB, so
         * we assume its been deleted in the interim and we just set to
         * the next one with SET_RANGE
         */

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, &key, NULL, DB_SET_RANGE);
        if(ret != 0)
        {
            return ret;
        }

        return DBPF_ITERATE_CURRENT_POSITION;
    }

    return ret;
}

static int dbpf_keyval_iterate_step_to_position(
    TROVE_handle handle,
    TROVE_ds_position pos,
    DBC * dbc_p)
{
    int i = 0;
    int ret;
    TROVE_keyval_s key;

    memset(&key, 0, sizeof(TROVE_keyval_s));

    assert(pos != TROVE_ITERATE_START);

    ret = dbpf_keyval_iterate_get_first_entry(handle, dbc_p);
    if(ret != 0)
    {
        return ret;
    }

    for(i = 0; i < pos; ++i)
    {
        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, &key, NULL, DB_NEXT);
        if(ret != 0)
        {
            return ret;
        }
    }

    return 0;
}

static int dbpf_keyval_iterate_cursor_get(
    TROVE_handle handle,
    DBC * dbc_p,
    TROVE_keyval_s * key,
    TROVE_keyval_s * data,
    uint32_t db_flags)
{
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    DBT db_key, db_data;
    char dummy_data[PVFS_NAME_MAX];
    int key_sz;

    key_entry.handle = handle;

    assert(key->buffer_sz >= 0);
    if(key->buffer_sz != 0)
    {
        memcpy(key_entry.key, key->buffer, key->buffer_sz);
    }

    memset(&db_key, 0, sizeof(DBT));
    db_key.data = &key_entry;
    db_key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(key->buffer_sz);
    db_key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH);
    db_key.flags |= DB_DBT_USERMEM;

    memset(&db_data, 0, sizeof(DBT));
    db_data.flags |= DB_DBT_USERMEM;

    if(data)
    {
        db_data.data = data->buffer;
        db_data.ulen = data->buffer_sz;
    }
    else
    {
        db_data.data = dummy_data;
        db_data.ulen = PVFS_NAME_MAX;
    }

    ret = dbc_p->c_get(dbc_p, &db_key, &db_data, db_flags);
    if (ret == DB_NOTFOUND)
    {
        return -TROVE_ENOENT;
    }
#ifdef HAVE_DB_BUFFER_SMALL
    else if(ret == DB_BUFFER_SMALL) 
#else
    else if(ret == ENOMEM)
#endif
    {
        db_data.data = malloc(db_data.size);
        if(!db_data.data)
        {
            return -TROVE_ENOMEM;
        }

        db_data.ulen = db_data.size;

        ret = dbc_p->c_get(dbc_p, &db_key, &db_data, db_flags);
        if(ret == DB_NOTFOUND)
        {
            return -TROVE_ENOENT;
        }
            
        if(data)
        {
            memcpy(data->buffer, db_data.data, data->buffer_sz);
            free(db_data.data);
            data->read_sz = data->buffer_sz;
        }
    }

    if (ret != 0)
    {
        gossip_lerr("Failed to perform cursor get:"
                    "\n\thandle: %llu\n\ttype: %d\n\tkey: %s\n",
                    llu(key_entry.handle), db_flags, key_entry.key);
        return -dbpf_db_error_to_trove_error(ret);
    }

    if(key_entry.handle != handle)
    {
            return -TROVE_ENOENT;
    }

    key_sz = (DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.size) > key->buffer_sz) ?
        key->buffer_sz : DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.size);

    memcpy(key->buffer, key_entry.key, key_sz);

    key->read_sz = key_sz;
    
    if(data)
    {
        data->read_sz = (data->buffer_sz < db_data.size) ?
            data->buffer_sz : db_data.size;
    }

    return 0;
}

int PINT_trove_dbpf_keyval_compare(
    DB * dbp, const DBT * a, const DBT * b)
{
    const struct dbpf_keyval_db_entry * db_entry_a;
    const struct dbpf_keyval_db_entry * db_entry_b;

    db_entry_a = (const struct dbpf_keyval_db_entry *) a->data;
    db_entry_b = (const struct dbpf_keyval_db_entry *) b->data;

    if(db_entry_a->handle != db_entry_b->handle)
    {
        return (db_entry_a->handle < db_entry_b->handle) ? -1 : 1;
    }

    if(a->size > b->size)
    {
        return 1;
    }

    if(a->size < b->size)
    {
        return -1;
    }

    /* must be equal */
    return (strncmp(db_entry_a->key, db_entry_b->key, 
                    DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size)));
}

struct TROVE_keyval_ops dbpf_keyval_ops =
{
    dbpf_keyval_read,
    dbpf_keyval_write,
    dbpf_keyval_remove,
    dbpf_keyval_validate,
    dbpf_keyval_iterate,
    dbpf_keyval_iterate_keys,
    dbpf_keyval_read_list,
    dbpf_keyval_write_list,
    dbpf_keyval_flush
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

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

static uint32_t readdir_session = 0;

extern int synccount;

extern gen_mutex_t dbpf_attr_cache_mutex;

static int dbpf_keyval_do_remove(
    dbpf_db *db_p, TROVE_handle handle, char type,
    TROVE_keyval_s *key, TROVE_keyval_s *val);

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_get_handle_info_op_svc(struct dbpf_op *op_p);

#define DBPF_ITERATE_CURRENT_POSITION 1

static int dbpf_keyval_iterate_get_first_entry(
    TROVE_handle handle, 
    char type,
    dbpf_cursor *dbc);

static int dbpf_keyval_iterate_step_to_position(
    TROVE_handle handle, 
    char type,
    TROVE_ds_position pos,
    dbpf_cursor *dbc);

static int dbpf_keyval_iterate_skip_to_position(
    TROVE_handle handle, 
    char type,
    TROVE_ds_position pos, 
    PINT_dbpf_keyval_pcache *pcache,
    dbpf_cursor *dbc);

static int dbpf_keyval_iterate_cursor_get(
    TROVE_handle handle, 
    char type,
    dbpf_cursor * dbc,
    TROVE_keyval_s * key, 
    TROVE_keyval_s * data, 
    uint32_t db_flags);

enum dbpf_handle_info_action
{
    DBPF_KEYVAL_HANDLE_COUNT_INCREMENT,
    DBPF_KEYVAL_HANDLE_COUNT_DECREMENT
};

static int dbpf_keyval_handle_info_ops(struct dbpf_op * op_p,
                                       enum dbpf_handle_info_action action);

static int dbpf_keyval_read(TROVE_coll_id coll_id,
                            TROVE_handle handle,
                            TROVE_keyval_s *key_p,
                            TROVE_keyval_s *val_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_context_id context_id,
                            TROVE_op_id *out_op_id_p,
                            PVFS_hint  hints)
{
    int ret;
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {handle, coll_id};
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Read "
                 "of %s\n", (char *)key_p->buffer);

    gen_mutex_lock(&dbpf_attr_cache_mutex);
    cache_elem = dbpf_attr_cache_elem_lookup(ref);
    if (cache_elem && (!(flags & TROVE_BINARY_KEY)))
    {
        dbpf_keyval_pair_cache_elem_t *keyval_pair =
            dbpf_attr_cache_elem_get_data_based_on_key(
                cache_elem, key_p->buffer);
        if (keyval_pair)
        {
            val_p->read_sz = val_p->buffer_sz;
            /* note: dbpf_attr_cache_keyval_pair_fetch_cached_data() will
             * update read_sz appropriately
             */
            ret = dbpf_attr_cache_keyval_pair_fetch_cached_data(
                cache_elem, keyval_pair, val_p->buffer,
                &val_p->read_sz);
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            if(ret < 0)
            {
                return ret;
            }
            return 1;
        }
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_READ,
        coll_p,
        handle,
        dbpf_keyval_read_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_keyval_read_event_id;
    DBPF_EVENT_START(coll_p, q_op_p, event_type, &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints));

    /* initialize the op-specific members */
    op_p->u.k_read.key = key_p;
    op_p->u.k_read.val = val_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p)
{
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data key, data;
    int ret;

    key_entry.handle = op_p->handle;
    if (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY)
    {
        key_entry.type = DBPF_DIRECTORY_ENTRY_TYPE;
    }
    else
    {
        key_entry.type = DBPF_ATTRIBUTE_TYPE;
    }
    memcpy(key_entry.key, 
           op_p->u.k_read.key->buffer, 
           op_p->u.k_read.key->buffer_sz);
    key.data = &key_entry;
    key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(op_p->u.k_read.key->buffer_sz);

    data.data = op_p->u.k_read.val->buffer;
    data.len = op_p->u.k_read.val->buffer_sz;

    ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &data);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "warning: keyval read error on handle %llu and "
                     "key=%*s (%s)\n", llu(op_p->handle),
                     op_p->u.k_read.key->buffer_sz,
                     (char *)op_p->u.k_read.key->buffer, 
                     strerror(ret));

        /* if data buffer is too small returns ERANGE error */
        if (data.len > op_p->u.k_read.val->buffer_sz)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "warning: Value buffer too small %d < %lu\n",
                         op_p->u.k_read.val->buffer_sz, data.len);
            /* let the user know */
            op_p->u.k_read.val->read_sz = data.len;
            ret = ERANGE;
        }

        ret = -ret;
        goto return_error;
    }
    op_p->u.k_read.val->read_sz = data.len;

    /* cache this data in the attr cache if we can */
    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        if (dbpf_attr_cache_elem_set_data_based_on_key(
                ref, key_entry.key,
                op_p->u.k_read.val->buffer, data.len))
        {
            /*
             * NOTE: this can happen if the keyword isn't registered, or if
             * there is no associated cache_elem for this key
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
    }

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
                             TROVE_op_id *out_op_id_p,
                             PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_WRITE,
        coll_p,
        handle,
        dbpf_keyval_write_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_keyval_write_event_id;
    DBPF_EVENT_START(coll_p, q_op_p, event_type, &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints));

   /* initialize the op-specific members */
    op_p->u.k_write.key = *key_p;
    op_p->u.k_write.val = *val_p;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}

static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p)
{
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data key, data;
    int ret;

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "dbpf_keyval_write_op_svc: handle: %llu, key: %*s\n",
                     llu(op_p->handle),
                     op_p->u.k_write.key.buffer_sz,
                     (char *)op_p->u.k_write.key.buffer);
    }

    key_entry.handle = op_p->handle;
    if (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY)
    {
        key_entry.type = DBPF_DIRECTORY_ENTRY_TYPE;
    }
    else
    {
        key_entry.type = DBPF_ATTRIBUTE_TYPE;
    }

    assert(op_p->u.k_write.key.buffer_sz <= DBPF_MAX_KEY_LENGTH);
    memcpy(key_entry.key, 
           op_p->u.k_write.key.buffer,
           op_p->u.k_write.key.buffer_sz);

    key.data = &key_entry;
    key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(op_p->u.k_write.key.buffer_sz);
    data.data = op_p->u.k_write.val.buffer;
    data.len = op_p->u.k_write.val.buffer_sz;

    /* if TROVE_ONLYOVERWRITE flag was set, make sure that the key exists
     * before overwriting it */
    if ((op_p->flags & TROVE_ONLYOVERWRITE))
    {
        struct dbpf_data tmpdata;

        tmpdata.data = malloc(op_p->u.k_write.val.buffer_sz);
        tmpdata.len = op_p->u.k_write.val.buffer_sz;
        ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &tmpdata);
        /* A failed get implies that keys possibly did not exist */
        if (ret != 0)
        {
            /* The only case where we are ok is val buffer
             *  is too small */
            if (tmpdata.len > op_p->u.k_write.val.buffer_sz)
            {
                ret = 0;
            }
            else
            {
                if(ret != TROVE_ENOENT)
                {
                    gossip_err("TROVE:DBPF: keyval dbpf_db_get");
                }
                ret = -ret;
            }

        }
        free(tmpdata.data);
        /* If there was an error, we need to return right here */
        if (ret != 0)
        {
            goto return_error;
        }
    }

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "keyval_db->put(handle= %llu, key= %*s (%d)) size=%zu\n",
                     llu(key_entry.handle), 
                     op_p->u.k_write.key.buffer_sz,
                     key_entry.key,
                     op_p->u.k_write.key.buffer_sz,
                     key.len);
    }

    /* If TROVE_NOOVERWRITE flag was set, make sure that we don't create the
     * key if it exists */
    if ((op_p->flags & TROVE_NOOVERWRITE))
    {
        ret = dbpf_db_putonce(op_p->coll_p->keyval_db, &key, &data);
    }
    else
    {
        ret = dbpf_db_put(op_p->coll_p->keyval_db, &key, &data);
    }
    /* Either a put error or key already exists */
    if (ret != 0)
    {
	gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
		     "dbpf_db_put keyval failed. ret=%d\n", ret);

        ret = -ret;
        goto return_error;
    }

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Write "
                     "of %s\n", (char *)key_entry.key);
    }

    if(op_p->flags & TROVE_NOOVERWRITE)
    {
        ret = dbpf_keyval_handle_info_ops(
            op_p, DBPF_KEYVAL_HANDLE_COUNT_INCREMENT);
        if(ret != 0)
        {
            goto return_error;
        }
    }

    /*
     * now that the data is written to disk, update the cache if it's
     * an attr keyval we manage.
     */
    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        dbpf_attr_cache_elem_t *cache_elem;
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        cache_elem = dbpf_attr_cache_elem_lookup(ref);
        if (cache_elem)
        {
            if (dbpf_attr_cache_elem_set_data_based_on_key(
                    ref, key_entry.key,
                    op_p->u.k_write.val.buffer, data.len))
            {
                /*
                 * NOTE: this can happen if the keyword isn't registered,
                 * or if there is no associated cache_elem for this key
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
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

return_error:
    return ret;
}

static int dbpf_keyval_remove(TROVE_coll_id coll_id,
                              TROVE_handle handle,
                              TROVE_keyval_s *key_p,
                              TROVE_keyval_s *val_p,
                              TROVE_ds_flags flags,
                              TROVE_vtag_s *vtag,
                              void *user_ptr,
                              TROVE_context_id context_id,
                              TROVE_op_id *out_op_id_p,
                              PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_REMOVE_KEY,
        coll_p,
        handle,
        dbpf_keyval_remove_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    /* initialize op-specific members */
    op_p->hints = hints;
    op_p->u.k_remove.key = *key_p;
    if(val_p)
    {
        op_p->u.k_remove.val = *val_p;
    }
    else
    {
        op_p->u.k_remove.val.buffer = NULL;
    }
      
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "dbpf_keyval_remove_op_svc: handle: %llu, key: %*s\n",
                     llu(op_p->handle),
                     op_p->u.k_remove.key.buffer_sz,
                     (char *)op_p->u.k_remove.key.buffer);
    }
                 
    ret = dbpf_keyval_do_remove(op_p->coll_p->keyval_db, 
                                op_p->handle,
                                (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY ?
                                     DBPF_DIRECTORY_ENTRY_TYPE :
                                     DBPF_ATTRIBUTE_TYPE),
                                &op_p->u.k_remove.key,
                                &op_p->u.k_remove.val);
    if (ret != 0)
    {
        goto return_error;
    }

    ret = dbpf_keyval_handle_info_ops(op_p, DBPF_KEYVAL_HANDLE_COUNT_DECREMENT);
    if(ret != 0)
    {
        goto return_error;
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

return_error:
    return ret;
}

static int dbpf_keyval_remove_list(TROVE_coll_id coll_id,
                                  TROVE_handle handle,
                                  TROVE_keyval_s *key_array,
                                  TROVE_keyval_s *val_array,
                                  int *error_array,
                                  int count,
                                  TROVE_ds_flags flags,
                                  TROVE_vtag_s *vtag,
                                  void *user_ptr,
                                  TROVE_context_id context_id,
                                  TROVE_op_id *out_op_id_p,
                                  PVFS_hint hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_WRITE_LIST,
        coll_p,
        handle,
        dbpf_keyval_remove_list_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

   /* initialize the op-specific members */
    op_p->u.k_remove_list.key_array = key_array;
    op_p->u.k_remove_list.val_array = val_array;
    op_p->u.k_remove_list.error_array = error_array;
    op_p->u.k_remove_list.count = count;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_remove_list_op_svc(struct dbpf_op *op_p)
{
    struct dbpf_keyval_db_entry key_entry;
    TROVE_keyval_handle_info info;
    int remove_count = 0, ret, k;
    struct dbpf_data key, data;

    /* read each key to see if it is present */
    for (k = 0; k < op_p->u.k_remove_list.count; k++)
    {
        ret = dbpf_keyval_do_remove(op_p->coll_p->keyval_db,
                                    op_p->handle,
                                    (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY ?
                                         DBPF_DIRECTORY_ENTRY_TYPE :
                                         DBPF_ATTRIBUTE_TYPE),
                                    &op_p->u.k_remove_list.key_array[k],
                                    &op_p->u.k_remove_list.val_array[k]);
        if(ret != 0)
        {
            op_p->u.k_remove_list.error_array[k] = ret;
        }
        else
        {
            remove_count++;
        }
    }

    if(op_p->flags & TROVE_KEYVAL_HANDLE_COUNT)
    {
        key_entry.handle = op_p->handle;
        key_entry.type = DBPF_COUNT_TYPE;
        key.data = &key_entry;
        key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);
        data.data = &info;
        data.len = sizeof(TROVE_keyval_handle_info);
        ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &data);
        if(ret == TROVE_ENOENT)
        {
            /* doesn't exist yet so we can set to 0 */
            memset(&info, 0, sizeof(TROVE_keyval_handle_info));
            data.len = sizeof(TROVE_keyval_handle_info);
        }
        else if(ret != 0)
        {
            gossip_err("TROVE:DBPF: keyval dbpf_db_get");
            return -ret;
        }

        info.count -= remove_count;

        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[DBPF KEYVAL]: handle_info keyval_remove_list: handle: %llu, count: %d\n",
                 llu(op_p->handle), info.count); 

        ret = dbpf_db_put(op_p->coll_p->keyval_db, &key, &data);
        if(ret != 0)
        {
            gossip_err("TROVE:DBPF: dbpf_db_put keyval handle info ops");
            return -ret;
        }
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

    return ret;
}

static int dbpf_keyval_validate(TROVE_coll_id coll_id,
                                TROVE_handle handle,
                                TROVE_ds_flags flags,
                                TROVE_vtag_s *vtag,
                                void* user_ptr,
                                TROVE_context_id context_id,
                                TROVE_op_id *out_op_id_p,
                                PVFS_hint  hints)
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
                               TROVE_op_id *out_op_id_p,
                               PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_ITERATE,
        coll_p,
        handle,
        dbpf_keyval_iterate_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

     /* initialize op-specific members */
    op_p->u.k_iterate.key_array = key_array;
    op_p->u.k_iterate.val_array = val_array;
    op_p->u.k_iterate.position_p = position_p;
    op_p->u.k_iterate.count_p = inout_count_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
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
    uint64_t tmp_pos = 0;
    PINT_dbpf_keyval_iterate_callback tmp_callback = NULL;
    int i;

    assert(*op_p->u.k_iterate.count_p > 0);

    count = *op_p->u.k_iterate.count_p;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                  "dbpf_keyval_iterate_op_svc: starting: fsid: %u, "
                  "handle: %llu, pos: %llu\n", 
                 op_p->coll_p->coll_id, 
                 llu(op_p->handle),
                 llu(*op_p->u.k_iterate.position_p));
    
    /* if they passed in that they are at the end, return 0.
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate.count_p = 0;
        return 1;
    }

    if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
    {
        tmp_callback = PINT_dbpf_dspace_remove_keyval;
    }

    ret = PINT_dbpf_keyval_iterate(op_p->coll_p->keyval_db,
                                   op_p->handle,
                                   (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY ?
                                       DBPF_DIRECTORY_ENTRY_TYPE :
                                       DBPF_ATTRIBUTE_TYPE),
                                   op_p->coll_p->pcache,
                                   op_p->u.k_iterate.key_array,
                                   op_p->u.k_iterate.val_array,
                                   &count,
                                   *op_p->u.k_iterate.position_p,
                                   tmp_callback);
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
            *op_p->u.k_iterate.position_p = count-1;
            /* store a session identifier in the second 16 bits */
            tmp_pos += readdir_session;
            *op_p->u.k_iterate.position_p += (tmp_pos << 32);
            readdir_session++;
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
/*
                (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY ?
                    DBPF_DIRECTORY_ENTRY_TYPE :
                    DBPF_ATTRIBUTE_TYPE),
*/
                *op_p->u.k_iterate.position_p,
                op_p->u.k_iterate.key_array[count-1].buffer, 
                op_p->u.k_iterate.key_array[count-1].read_sz);
        }

        if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
        {
            for(i=0; i<count; i++)
            {
                ret = dbpf_keyval_handle_info_ops(op_p, DBPF_KEYVAL_HANDLE_COUNT_DECREMENT);
                if(ret < 0)
                {
                    return(ret);
                }
            }
        }
    }
    
    *op_p->u.k_iterate.count_p = count;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                 "dbpf_keyval_iterate_op_svc: finished: "
                 "position: %llu, count: %d\n", 
                 llu(*op_p->u.k_iterate.position_p), *op_p->u.k_iterate.count_p);

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
                                    TROVE_op_id *out_op_id_p,
                                    PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_ITERATE_KEYS,
        coll_p,
        handle,
        dbpf_keyval_iterate_keys_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

 /* initialize op-specific members */
    op_p->u.k_iterate_keys.key_array = key_array;
    op_p->u.k_iterate_keys.position_p = position_p;
    op_p->u.k_iterate_keys.count_p = inout_count_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
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
    PINT_dbpf_keyval_iterate_callback tmp_callback = NULL;
    int i;
    char type;

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

    if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
    {
        tmp_callback = PINT_dbpf_dspace_remove_keyval;
    }

    /* set type */
    if(op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY)
    {
        type = DBPF_DIRECTORY_ENTRY_TYPE;
    }
    else
    {
        type = DBPF_ATTRIBUTE_TYPE;
    }

    ret = PINT_dbpf_keyval_iterate(op_p->coll_p->keyval_db,
                                   op_p->handle,
                                   type,
                                   op_p->coll_p->pcache,
                                   (count != 0) ?
                                   op_p->u.k_iterate_keys.key_array : NULL,
                                   NULL,
                                   &count,
                                   *op_p->u.k_iterate_keys.position_p,
                                   tmp_callback);
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
            *op_p->u.k_iterate_keys.position_p = count-1;
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
/*
                (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY ?
                    DBPF_DIRECTORY_ENTRY_TYPE :
                    DBPF_ATTRIBUTE_TYPE),
*/
                *op_p->u.k_iterate_keys.position_p,
                op_p->u.k_iterate_keys.key_array[count-1].buffer,
                op_p->u.k_iterate_keys.key_array[count-1].read_sz);
        }
        if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
        {
            for(i=0; i<count; i++)
            {
                ret = dbpf_keyval_handle_info_ops(op_p, DBPF_KEYVAL_HANDLE_COUNT_DECREMENT);
                if(ret < 0)
                {
                    return(ret);
                }
            }
        }
    }

    *op_p->u.k_iterate_keys.count_p = count;
    return 1;
}


static int dbpf_keyval_read_list(TROVE_coll_id coll_id,
                                 TROVE_handle handle,
                                 TROVE_keyval_s *key_array,
                                 TROVE_keyval_s *val_array,
                                 TROVE_ds_state *err_array,
                                 int count,
                                 TROVE_ds_flags flags,
                                 TROVE_vtag_s *vtag,
                                 void *user_ptr,
                                 TROVE_context_id context_id,
                                 TROVE_op_id *out_op_id_p,
                                 PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_READ_LIST,
        coll_p,
        handle,
        dbpf_keyval_read_list_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    /* initialize the op-specific members */
    op_p->u.k_read_list.key_array = key_array;
    op_p->u.k_read_list.val_array = val_array;
    op_p->u.k_read_list.err_array = err_array;
    op_p->u.k_read_list.count = count;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p)
{
    int ret, i = 0;
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data key, data;
    int success_count = 0;

    for(i = 0; i < op_p->u.k_read_list.count; i++)
    {
        key_entry.handle = op_p->handle;
        if (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY)
        {
            key_entry.type = DBPF_DIRECTORY_ENTRY_TYPE;
        }
        else
        {
            key_entry.type = DBPF_ATTRIBUTE_TYPE;
        }

        memcpy(key_entry.key, 
               op_p->u.k_read_list.key_array[i].buffer,
               op_p->u.k_read_list.key_array[i].buffer_sz);

        key.data = &key_entry;
        key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_read_list.key_array[i].buffer_sz);

        data.data = op_p->u.k_read_list.val_array[i].buffer;
        data.len = op_p->u.k_read_list.val_array[i].buffer_sz;

        ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &data);
        if (ret != 0)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                         "keyval read list (get) %s failed with error %s\n",
                         key_entry.key, strerror(ret));
            /* if data buffer is too small returns ERANGE error */
            if (data.len > op_p->u.k_read_list.val_array[i].buffer_sz)
            {
                gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "warning: Value buffer too small %d < %lu\n",
                         op_p->u.k_read_list.val_array[i].buffer_sz, data.len);
                /* let the user know */
                op_p->u.k_read_list.val_array[i].read_sz = data.len;
                /* this is still a success */
                success_count++;
            }
            else
            {
                op_p->u.k_read_list.val_array[i].read_sz = 0;
            }

            op_p->u.k_read_list.err_array[i] = -ret;
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                    "Trove error set to %d\n",
                    op_p->u.k_read_list.err_array[i]);
        }
        else
        {
            success_count++;
            op_p->u.k_read_list.err_array[i] = 0;
            op_p->u.k_read_list.val_array[i].read_sz = data.len;
        }
    }

    if(success_count)
    {
        /* return success if we read at least one of the requested keys */
        return 1;
    }
    else
    {
        /* if everything failed, then return first error code */
        return(op_p->u.k_read_list.err_array[0]);
    }
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
                                  TROVE_op_id *out_op_id_p,
                                  PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_WRITE_LIST,
        coll_p,
        handle,
        dbpf_keyval_write_list_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

   /* initialize the op-specific members */
    op_p->u.k_write_list.key_array = key_array;
    op_p->u.k_write_list.val_array = val_array;
    op_p->u.k_write_list.count = count;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    ret = dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
    return ret;
}

static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    int k;
    char tmpdata[PVFS_NAME_MAX];
    key_entry.handle = op_p->handle;
    if (op_p->flags & TROVE_KEYVAL_DIRECTORY_ENTRY)
    {
        key_entry.type = DBPF_DIRECTORY_ENTRY_TYPE;
    }
    else
    {
        key_entry.type = DBPF_ATTRIBUTE_TYPE;
    }

    /* read each key to see if it is present */
    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memcpy(key_entry.key, op_p->u.k_write_list.key_array[k].buffer,
               op_p->u.k_write_list.key_array[k].buffer_sz);

        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
        key.data = &key_entry;
        key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_write_list.key_array[k].buffer_sz);

        data.data = tmpdata;
        data.len = PVFS_NAME_MAX;

        ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &data);

        /* Do not worry about the case where the key is there but the data
         * is simply too big for the temporary data buffer used
         */
        if (ret != 0)
        {
            if(ret == TROVE_ENOENT && ((op_p->flags & TROVE_NOOVERWRITE) ||
                                      (!(op_p->flags & TROVE_ONLYOVERWRITE))))
            {
                /* this means key is not in DB, which is what we
                 * want for the no-overwrite case - so go to the next key
                 */
                continue;
            }

            gossip_err("TROVE:DBPF: keyval dbpf_db_get");
            ret = -ret;
            goto return_error;
        }
    }

    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memcpy(key_entry.key, op_p->u.k_write_list.key_array[k].buffer,
               op_p->u.k_write_list.key_array[k].buffer_sz);

        key.data = &key_entry;
        key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_write_list.key_array[k].buffer_sz);

        /* allow NULL val array (writes an empty value to each position */
        if(!op_p->u.k_write_list.val_array)
        {
            data.data = NULL;
            data.len = 0;
        }
        else
        {
            data.data = op_p->u.k_write_list.val_array[k].buffer;
            data.len = op_p->u.k_write_list.val_array[k].buffer_sz;
        }

        if(!(op_p->flags & TROVE_BINARY_KEY))
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "keyval_db->put(handle=%llu, key=%*s (%d)) size=%zu\n",
                         llu(key_entry.handle), 
                         op_p->u.k_write_list.key_array[k].buffer_sz,
                         key_entry.key,
                         op_p->u.k_write_list.key_array[k].buffer_sz,
                         key.len);
        }

        ret = dbpf_db_put(op_p->coll_p->keyval_db, &key, &data);
        if (ret != 0)
        {
            gossip_err("TROVE:DBPF: dbpf_db_put keyval write list (ret %d)\n", ret);
            ret = -ret;
            goto return_error;
        }

        if(!(op_p->flags & TROVE_BINARY_KEY))
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Write "
                         "of %s\n", (char *)op_p->u.k_write_list.key_array[k].buffer);
        }

        if(op_p->flags & TROVE_NOOVERWRITE)
        {
            ret = dbpf_keyval_handle_info_ops(
                op_p, DBPF_KEYVAL_HANDLE_COUNT_INCREMENT);
            if(ret != 0)
            {
                goto return_error;
            }
        }

        /*
           now that the data is written to disk, update the cache if it's
           an attr keyval we manage.
           */
        if(!(op_p->flags & TROVE_BINARY_KEY))
        {
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            cache_elem = dbpf_attr_cache_elem_lookup(ref);
            if (cache_elem)
            {
                if (dbpf_attr_cache_elem_set_data_based_on_key(
                        ref, key_entry.key,
                        data.data, data.len))
                {
                    /*
                     * NOTE: this can happen if the keyword isn't registered,
                     * or if there is no associated cache_elem for this key
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
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

return_error:
    return ret;
}

static int dbpf_keyval_flush(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p,
                             PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_FLUSH,
        coll_p,
        handle,
        dbpf_keyval_flush_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;

    ret = dbpf_db_sync(op_p->coll_p->keyval_db);
    if (ret != 0)
    {
        ret = -ret;
        goto return_error;
    }

    return 1;

return_error:
    return ret;
}    

int PINT_dbpf_keyval_iterate(
    dbpf_db *db,
    TROVE_handle handle,
    char type,
    PINT_dbpf_keyval_pcache *pcache,    
    TROVE_keyval_s *keys_array,
    TROVE_keyval_s *values_array,
    int *count,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_iterate_callback callback)
{

    int ret = -TROVE_EINVAL, i=0, get_key_count=0;
    dbpf_cursor *dbc;
    char keybuffer[PVFS_NAME_MAX];
    TROVE_keyval_s skey;
    TROVE_keyval_s *key;
    TROVE_keyval_s *val = NULL;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "Entered: PINT_dpbf_keyval_iterate\n");

    skey.buffer = keybuffer;
    skey.buffer_sz = PVFS_NAME_MAX;
    key = &skey;

    ret = dbpf_db_cursor(db, &dbc, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "Exited: PINT_dpbf_keyval_iterate\n");

        gossip_lerr("dbpf_db_cursor failed: db error %s\n", strerror(ret));
        *count = 0;
        return -ret;
    }

    if(pos == TROVE_ITERATE_START)
    {
        ret = dbpf_keyval_iterate_get_first_entry(handle, type, dbc);
        if(ret != 0)
        {
            goto return_error;
        }

        ret = DBPF_ITERATE_CURRENT_POSITION; 
    }
    else
    {
        ret = dbpf_keyval_iterate_skip_to_position(
            handle, type, pos, pcache, dbc);
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
            handle, type, dbc,  key, val, DBPF_DB_CURSOR_CURRENT);
        if(ret != 0)
        {
            goto return_error;
        }

        if(callback)
        {
            key->buffer_sz = key->read_sz;
            ret = callback(dbc, handle, key, NULL);
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
            handle, type, dbc, key, val, DBPF_DB_CURSOR_NEXT);
        if(ret != 0)
        {
            goto return_error;
        }

        #if 0 
        /* not safe to print this if binary keys may be present */
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "iterate key: %*s, val: %llu\n", 
                     key->read_sz, (char *)key->buffer, 
                     (val ? llu(*(PVFS_handle *)val->buffer) : 0));
        #endif

        if(callback)
        {
            key->buffer_sz = key->read_sz;
            ret = callback(dbc, handle, key, NULL);
            if(ret != 0)
            {
                goto return_error;
            }
        }
    }

return_error:

    *count = i;

    /* free the cursor */
    dbpf_db_cursor_close(dbc);

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "Exited: PINT_dpbf_keyval_iterate\n");

    return ret;
}

static int dbpf_keyval_do_remove(
    dbpf_db *db_p, TROVE_handle handle, char type,
    TROVE_keyval_s *key, TROVE_keyval_s *val)
{
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data db_key, db_val;

    #if 0
    /* not safe to print this if it may be a binary key */
    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "PINT_dbpf_keyval_remove: handle (%llu), key: (%d) %*s\n",
                 llu(handle), key->buffer_sz, key->buffer_sz, (char *)key->buffer);
    #endif

    key_entry.handle = handle;
    key_entry.type = type;

    memcpy(key_entry.key, key->buffer, key->buffer_sz);

    db_key.data = &key_entry;
    db_key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(key->buffer_sz);

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "keyval_db->del(handle= %llu, type = %c, key= %*s (%d)) "
                 "size=%zu\n",
                 llu(key_entry.handle),
                 key_entry.type,
                 key->buffer_sz,
                 key_entry.key,
                 key->buffer_sz,
                 db_key.len);


    if(val && val->buffer)
    {
        db_val.data = val->buffer;
        db_val.len = val->buffer_sz;
        ret = dbpf_db_get(db_p, &db_key, &db_val);
        if(ret != 0)
        {
            ret = -ret;
        }
        val->read_sz = db_val.len;
    }

    ret = dbpf_db_del(db_p, &db_key);
    if(ret != 0)
    {
        ret = -ret;
    }

    return ret;
}

static int dbpf_keyval_iterate_get_first_entry(
    TROVE_handle handle, char type, dbpf_cursor *dbc)
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
        handle, type, dbc, &key, NULL, DBPF_DB_CURSOR_SET_RANGE);
    if(ret != 0)
    {
        return ret;
    }

    if(key.buffer_sz == 0)
    {
        /* skip handle_info */
        ret = dbpf_keyval_iterate_cursor_get(
            handle, type, dbc, &key, NULL, DBPF_DB_CURSOR_NEXT);
    }

    return 0;
}

static int dbpf_keyval_iterate_skip_to_position(
    TROVE_handle handle,
    char type,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_pcache *pcache,
    dbpf_cursor *dbc)
{
    int ret = 0;
    TROVE_keyval_s key;

    assert(pos != TROVE_ITERATE_START);

    memset(&key, 0, sizeof(TROVE_keyval_s));

    ret = PINT_dbpf_keyval_pcache_lookup(
        pcache, handle, pos, 
        (const void **)&key.buffer, &key.buffer_sz);
    if(ret == -PVFS_ENOENT)
    {
        /* if the lookup fails (because the server was restarted)
         * we fall back to stepping through all the entries to get
         * to the position
         */
        /* strip the session out of the position; we need to use a true
         * integer offset if we get past the cache
         */
        pos = pos & 0xffffffff;
        return dbpf_keyval_iterate_step_to_position(handle, type, pos, dbc);
    }

    ret = dbpf_keyval_iterate_cursor_get(
        handle, type, dbc, &key, NULL, DBPF_DB_CURSOR_SET);
    if(ret == -TROVE_ENOENT)
    {
        /* cache lookup succeeded but entry is no longer in the DB, so
         * we assume its been deleted in the interim and we just set to
         * the next one with SET_RANGE
         */

        ret = dbpf_keyval_iterate_cursor_get(
            handle, type, dbc, &key, NULL, DBPF_DB_CURSOR_SET_RANGE);
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
    char type,
    TROVE_ds_position pos,
    dbpf_cursor *dbc)
{
    int i = 0;
    int ret;
    TROVE_keyval_s key;

    assert(pos != TROVE_ITERATE_START);

    ret = dbpf_keyval_iterate_get_first_entry(handle, type, dbc);
    if(ret != 0)
    {
        return ret;
    }

    for(i = 0; i < pos; ++i)
    {
        memset(&key, 0, sizeof(TROVE_keyval_s));

        ret = dbpf_keyval_iterate_cursor_get(
            handle, type, dbc, &key, NULL, DBPF_DB_CURSOR_NEXT);
        if(ret != 0)
        {
            return ret;
        }
    }

    return 0;
}

/**
 * dbpf_keyval_iterate_cursor_get is part of a set of iterate functions
 * that abstact the DB functions so to allow us to iterate over directory
 * entries and xattrs easily.  This function takes the handle and fills in
 * the key and value to get in the iteration step.  The iterate step can
 * be to set with the db_flags parameter to the initial iterate position 
 * (DB_SET_RANGE), get the next position (DB_NEXT), or even get the current 
 * position (DB_CURRENT).
 *
 * The key parameter is filled in up to the space available specified in
 * buffer_sz.  The read_sz value is set to the amount filled in.
 * Finally, if the buffer_sz is less than the size of the available key
 * length, buffer_sz is set to the size of the available key.  Note that
 * buffer_sz may end up being more than it was set to when key was passed
 * in, this is useful for the SET_RANGE flag when checking for the null
 * keyval string (handle info item) which must be skipped over.
 *
 * The data parameter can be null, in which case only the key is filled in.
 */
static int dbpf_keyval_iterate_cursor_get(
    TROVE_handle handle,
    char type,
    dbpf_cursor *dbc,
    TROVE_keyval_s * key,
    TROVE_keyval_s * data,
    uint32_t db_flags)
{
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data db_key, db_data;
    char dummy_data[PVFS_NAME_MAX];
    int key_sz;

    key_entry.handle = handle;
    key_entry.type = type;

    assert(key->buffer_sz >= 0);
    if(key->buffer_sz != 0)
    {
        memcpy(key_entry.key, key->buffer, key->buffer_sz);
    }

    db_key.data = &key_entry;
    db_key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(key->buffer_sz);

    if(data)
    {
        db_data.data = data->buffer;
        db_data.len = data->buffer_sz;
    }
    else
    {
        db_data.data = dummy_data;
        db_data.len = PVFS_NAME_MAX;
    }

    ret = dbpf_db_cursor_get(dbc, &db_key, &db_data, db_flags,
        DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH));
    if (ret == TROVE_ENOENT)
    {
        return -TROVE_ENOENT;
    }

    if (ret != 0)
    {
        gossip_lerr("Failed to perform cursor get:"
                    "\n\thandle: %llu\n\ttype: %d\n\tdb error: %s\n",
                    llu(key_entry.handle), db_flags, strerror(ret));
        return -ret;
    }

    if(key_entry.handle != handle || key_entry.type != type)
    {
            return -TROVE_ENOENT;
    }

    key_sz = (DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.len) > key->buffer_sz) ?
        key->buffer_sz : DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.len);

    memcpy(key->buffer, key_entry.key, key_sz);

    /* only adjust the buffer size if the key didn't fit into the
     * buffer
     */
    if(key->buffer_sz < DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.len))
    {
        key->buffer_sz = DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.len);
    }

    key->read_sz = key_sz;
    
    if(data)
    {
        data->read_sz = (data->buffer_sz < db_data.len) ?
            data->buffer_sz : db_data.len;
    }

    return 0;
}

static int dbpf_keyval_get_handle_info(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_keyval_handle_info *info,
    void * user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id *out_op_id_p,
    PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if(coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_GET_HANDLE_INFO,
        coll_p,
        handle,
        dbpf_keyval_get_handle_info_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    op_p->u.k_get_handle_info.info = info;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);
    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_get_handle_info_op_svc(struct dbpf_op * op_p)
{
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_data key, data;
    int ret;

    memset(&key_entry, 0, sizeof(key_entry));
    key_entry.handle = op_p->handle;
    key_entry.type = DBPF_COUNT_TYPE;

    key.data = &key_entry;
    key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);
    data.data = op_p->u.k_get_handle_info.info;
    data.len = sizeof(TROVE_keyval_handle_info);

    ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &data);
    if(ret != 0)
    {
        if(ret != TROVE_ENOENT)
        {
            gossip_err("TROVE:DBPF: keyval dbpf_db_get (handle info)");
        }

        return -ret;
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[DBPF KEYVAL]: handle_info get: handle: %llu, count: %d\n",
                 llu(op_p->handle), op_p->u.k_get_handle_info.info->count); 

    return 1;
}    

/**
 * keyval attrs are special parameters that can exist as metadata for
 * a keyval or set of keyvals (such as all the keyvals for directory
 * entries).  The keys for these special keyvals are the handle 
 * and a null string.
 */
static int dbpf_keyval_handle_info_ops(struct dbpf_op * op_p, 
                                       enum dbpf_handle_info_action action)
{
    struct dbpf_data key, data;
    int ret = -TROVE_EINVAL;
    TROVE_keyval_handle_info info;
    struct dbpf_keyval_db_entry key_entry;

    if(op_p->flags & TROVE_KEYVAL_HANDLE_COUNT)
    {
        key_entry.handle = op_p->handle;
        key_entry.type = DBPF_COUNT_TYPE;
        key.data = &key_entry;
        key.len = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);
        data.data = &info;
        data.len = sizeof(TROVE_keyval_handle_info);

        ret = dbpf_db_get(op_p->coll_p->keyval_db, &key, &data);
        if(ret == TROVE_ENOENT)
        {
            /* doesn't exist yet so we can set to 0 */
            memset(&info, 0, sizeof(TROVE_keyval_handle_info));
        }
        else if(ret != 0)
        {
            gossip_err("TROVE:DBPF: keyval dbpf_db_get");
            return -ret;
        }
       
        if(action == DBPF_KEYVAL_HANDLE_COUNT_INCREMENT)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: handle_info "
                         "count increment: handle: %llu, value: %d\n",
                         llu(op_p->handle), info.count);
            info.count++;
        }
        else if(action == DBPF_KEYVAL_HANDLE_COUNT_DECREMENT)
        {
            if(info.count <= 0)
            {
                gossip_lerr(
                     "[DBPF KEYVAL]: ERROR: handle_info "
                     "count decrement: handle: %llu, value: %d\n",
                     llu(op_p->handle), info.count);
            }
            assert(info.count > 0);

            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: handle_info "
                         "count decrement: handle: %llu, value: %d\n",
                         llu(op_p->handle), info.count);
            info.count--;

            if(info.count == 0)
            {
                gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                             "[DBPF KEYVAL]: handle_info "
                             "count decremented to zero, removing keyval\n");
                /* special case if we get down to zero remove this
                 * keyval as well
                 */
                ret = dbpf_db_del(op_p->coll_p->keyval_db, &key);
                if(ret != 0)
                {
                    gossip_err("TROVE:DBPF: keyval dbpf_db_del");
                    return -ret;
                }

                return 0;
            }
        }

        ret = dbpf_db_put(op_p->coll_p->keyval_db, &key, &data);
        if (ret != 0)
        {
            gossip_err("TROVE:DBPF: dbpf_db_put keyval handle info ops");
            return -ret;
        }
    }

    return 0;
}

struct TROVE_keyval_ops dbpf_keyval_ops =
{
    dbpf_keyval_read,
    dbpf_keyval_write,
    dbpf_keyval_remove,
    dbpf_keyval_remove_list,
    dbpf_keyval_validate,
    dbpf_keyval_iterate,
    dbpf_keyval_iterate_keys,
    dbpf_keyval_read_list,
    dbpf_keyval_write_list,
    dbpf_keyval_flush,
    dbpf_keyval_get_handle_info
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

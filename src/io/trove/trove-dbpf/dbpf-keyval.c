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
#include "gossip.h"
#include "pvfs2-internal.h"

extern gen_mutex_t dbpf_attr_cache_mutex;

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p);


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

    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Trove KeyVal Read "
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
    int ret = -TROVE_EINVAL, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    memset(&key, 0, sizeof(key));
    key.data = op_p->u.k_read.key->buffer;
    key.size = op_p->u.k_read.key->buffer_sz;

    memset(&data, 0, sizeof(data));
    data.data = op_p->u.k_read.val->buffer;
    data.ulen = op_p->u.k_read.val->buffer_sz;
    data.flags = DB_DBT_USERMEM;

    ret = tmp_ref.db_p->get(tmp_ref.db_p, NULL, &key, &data, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "warning: keyval read error on handle %llu and "
                     "key=%s (%s)\n", llu(op_p->handle),
                     (char *)key.data, db_strerror(ret));

        /* if data buffer is too small returns a memory error */
        if (data.ulen < data.size)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
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
            ref, op_p->u.k_read.key->buffer,
            op_p->u.k_read.val->buffer, data.size))
    {
        /*
          NOTE: this can happen if the keyword isn't registered, or if
          there is no associated cache_elem for this key
        */
        gossip_debug(
            GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data retrieved "
            "(key is %s)\n", (char *)op_p->u.k_read.key->buffer);
    }
    else
    {
        gossip_debug(
            GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
            "retrieved (key is %s)\n",
            (char *)op_p->u.k_read.key->buffer);
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
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
        
    return 0;
}

static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    TROVE_size k_size;
    DB_BTREE_STAT *k_stat_p = NULL;
    u_int32_t dbflags = 0;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = op_p->u.k_write.key.buffer;
    key.size = op_p->u.k_write.key.buffer_sz;
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
        ret = tmp_ref.db_p->get(tmp_ref.db_p, NULL, &key, &tmpdata, 0);
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

    ret = tmp_ref.db_p->put(tmp_ref.db_p, NULL, &key, &data, dbflags);
    /* Either a put error or key already exists */
    if (ret != 0)
    {
        tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->put");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Trove KeyVal Write "
                 "of %s\n", (char *)op_p->u.k_write.key.buffer);

    /*
      now that the data is written to disk, update the cache if it's
      an attr keyval we manage.
    */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    cache_elem = dbpf_attr_cache_elem_lookup(ref);
    if (cache_elem)
    {
        if (dbpf_attr_cache_elem_set_data_based_on_key(
                ref, op_p->u.k_write.key.buffer,
                op_p->u.k_write.val.buffer, data.size))
        {
            /*
              NOTE: this can happen if the keyword isn't registered,
              or if there is no associated cache_elem for this key
            */
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data written "
                "(key is %s)\n", (char *)op_p->u.k_write.key.buffer);
        }
        else
        {
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                "written (key is %s)\n",
                (char *)op_p->u.k_write.key.buffer);
        }
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    /* this may have increased the number of keyvals stored in this
     * object; update the k_size in the cache
     */
    ret = tmp_ref.db_p->stat(tmp_ref.db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_STAT
			     (DB_TXN *) NULL,
#endif
                             &k_stat_p,
#ifdef HAVE_UNKNOWN_PARAMETER_TO_DB_STAT
                             NULL,
#endif 
                             0);
    if (ret == 0)
    {
        k_size = (TROVE_size)k_stat_p->bt_ndata;
        free(k_stat_p);

        gen_mutex_lock(&dbpf_attr_cache_mutex);
        dbpf_attr_cache_ds_attr_update_cached_data_ksize(ref, k_size);
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }
    else
    {
        tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->stat");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
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
        
    return 0;
}

static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key;
    TROVE_size k_size;
    DB_BTREE_STAT *k_stat_p = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    memset (&key, 0, sizeof(key));
    key.data = op_p->u.k_remove.key.buffer;
    key.size = op_p->u.k_remove.key.buffer_sz;
    ret = tmp_ref.db_p->del(tmp_ref.db_p, NULL, &key, 0);

    if (ret != 0)
    {
        tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->del");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    /* this may have decreased the number of keyvals stored in this
     * object; update the k_size in the cache
     */
    ret = tmp_ref.db_p->stat(tmp_ref.db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_STAT
			     (DB_TXN *) NULL,
#endif
                             &k_stat_p,
#ifdef HAVE_UNKNOWN_PARAMETER_TO_DB_STAT
                             NULL,
#endif 
                             0);
    if (ret == 0)
    {
        k_size = (TROVE_size)k_stat_p->bt_ndata;
        free(k_stat_p);

        gen_mutex_lock(&dbpf_attr_cache_mutex);
        dbpf_attr_cache_ds_attr_update_cached_data_ksize(ref, k_size);
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }
    else
    {
        tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->stat");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
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
    int ret = -TROVE_EINVAL, i=0, got_db = 0;
    db_recno_t recno;
    struct open_cache_ref tmp_ref;
    DBC *dbc_p = NULL;
    DBT key, data;

    /* if they passed in that they are at the end, return 0.
     *
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate.count_p = 0;
        return 1;
    }

    /*
      create keyval space if it doesn't exist -- assume it's empty,
      but note that we can't distinguish from some other kind of error
      at this point
    */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    ret = tmp_ref.db_p->cursor(tmp_ref.db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    /* we have two choices here: 'seek' to a specific key by either a:
     * specifying a key or b: using record numbers in the db.  record numbers
     * will serialize multiple modification requests. We are going with record
     * numbers for now. i don't know if that's a problem, but it is something
     * to keep in mind if at some point simultaneous modifications to pvfs
     * perform badly.   -- robl */
    if (*op_p->u.k_iterate.position_p != TROVE_ITERATE_START)
    {
        /* need to position cursor before reading.  note that this will
         * actually position the cursor over the last thing that was read
         * on the last call, so we don't need to return what we get back.
         */

        /* here we make sure that the key is big enough to hold the
         * position that we need to pass in.
         */
        memset(&key, 0, sizeof(key));
        if (sizeof(recno) < op_p->u.k_iterate.key_array[0].buffer_sz)
        {
            key.data = op_p->u.k_iterate.key_array[0].buffer;
            key.size = key.ulen =
                op_p->u.k_iterate.key_array[0].buffer_sz;
        }
        else
        {
            key.data = &recno;
            key.size = key.ulen = sizeof(recno);
        }
        *(TROVE_ds_position *)key.data = *op_p->u.k_iterate.position_p;
        key.flags |= DB_DBT_USERMEM;

        memset(&data, 0, sizeof(data));
        data.data = op_p->u.k_iterate.val_array[0].buffer;
        data.size = data.ulen = op_p->u.k_iterate.val_array[0].buffer_sz;
        data.flags |= DB_DBT_USERMEM;

        /* position the cursor */
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
        if (ret == DB_NOTFOUND)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
    }

    for (i = 0; i < *op_p->u.k_iterate.count_p; i++)
    {
        memset(&key, 0, sizeof(key));
        key.data = op_p->u.k_iterate.key_array[i].buffer;
        key.size = key.ulen = op_p->u.k_iterate.key_array[i].buffer_sz;
        key.flags |= DB_DBT_USERMEM;

        memset(&data, 0, sizeof(data));
        data.data = op_p->u.k_iterate.val_array[i].buffer;
        data.size = data.ulen = op_p->u.k_iterate.val_array[i].buffer_sz;
        data.flags |= DB_DBT_USERMEM;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret == DB_NOTFOUND)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        op_p->u.k_iterate.key_array[i].read_sz = key.size;
        op_p->u.k_iterate.val_array[i].read_sz = data.size;
    }
    
return_ok:
    if (ret == DB_NOTFOUND)
    {
        *op_p->u.k_iterate.position_p = TROVE_ITERATE_END;
    }
    else
    {
        char buf[64];
        /* get the record number to return.
         *
         * note: key field is ignored by c_get in this case.  sort of.
         * i'm not actually sure what they mean by "ignored", because
         * it sure seems to matter what you put in there...
         */
        memset(&key, 0, sizeof(key));
        key.data  = buf;
        key.size  = key.ulen = 64;
        key.dlen  = 64;
        key.doff  = 0;
        key.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;

        memset(&data, 0, sizeof(data));
        data.data = &recno;
        data.size = data.ulen = sizeof(recno);
        data.flags |= DB_DBT_USERMEM;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_GET_RECNO);
        if (ret == DB_NOTFOUND)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "warning: keyval "
                         "iterate -- notfound\n");
        }
        else if (ret != 0)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "warning: keyval iterate "
                         "-- some other failure @ recno\n");
        }
        assert(recno != TROVE_ITERATE_START && recno != TROVE_ITERATE_END);
        *op_p->u.k_iterate.position_p = recno;
    }
    /* 'position' points us to record we just read, or is set to END */

    *op_p->u.k_iterate.count_p = i;

    /* free the cursor */
    ret = dbc_p->c_close(dbc_p);
    if (ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    /* give up the db reference */
    dbpf_open_cache_put(&tmp_ref);
    return 1;
    
return_error:
    gossip_lerr("dbpf_keyval_iterate_op_svc: %s\n", db_strerror(ret));
    *op_p->u.k_iterate.count_p = i;

    if(dbc_p)
    {
        dbc_p->c_close(dbc_p);
    }

    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
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
    int ret = -TROVE_EINVAL, i=0, got_db = 0, get_key_count = 0;
    struct open_cache_ref tmp_ref;
    DBC *dbc_p = NULL;
    DBT key, data;

    /* if they passed in that they are at the end, return 0.
     *
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate_keys.count_p = 0;
        return 1;
    }
    /*
      create keyval space if it doesn't exist -- assume it's empty,
      but note that we can't distinguish from some other kind of error
      at this point
    */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        gossip_lerr("dbpf_open_cache_get failed\n");
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    ret = tmp_ref.db_p->cursor(tmp_ref.db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        gossip_lerr("db_p->cursor failed\n");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    if (*op_p->u.k_iterate_keys.position_p != TROVE_ITERATE_START)
    {
        char buf[16];
        memset(&key, 0, sizeof(key));
        /* Hold a large enough key record value */
        key.data = buf;
        key.size = key.ulen = 16;
        key.flags |= DB_DBT_USERMEM;
        *(TROVE_ds_position *)key.data = *op_p->u.k_iterate_keys.position_p;

        memset(&data, 0, sizeof(data));
        data.data = op_p->u.k_iterate_keys.key_array[0].buffer;
        data.size = data.ulen = op_p->u.k_iterate_keys.key_array[0].buffer_sz;
        data.flags |= DB_DBT_USERMEM;

        /* position the cursor */
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
        if (ret == DB_NOTFOUND)
        {
            memset(&key, 0, sizeof(key));
            memset(&data, 0, sizeof(data));
            goto return_ok;
        }
        else if (ret != 0)
        {
            memset(&key, 0, sizeof(key));
            memset(&data, 0, sizeof(data));
            gossip_lerr("dbc_p->c_get (DB_SET_RECNO) failed\n");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
    }

    if (*op_p->u.k_iterate_keys.count_p == 0)
    {
        get_key_count = 1;
    }
    i = 0;
    for (;;)
    {
        memset(&key, 0, sizeof(key));
        if (get_key_count == 0)
        {
            key.data = op_p->u.k_iterate_keys.key_array[i].buffer;
            key.size = key.ulen = op_p->u.k_iterate_keys.key_array[i].buffer_sz;
            key.flags |= DB_DBT_USERMEM;
        }
        else if (get_key_count == 1) {
            key.flags |= DB_DBT_MALLOC;
        }
        memset(&data, 0, sizeof(data));
        data.flags |= DB_DBT_MALLOC;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret == DB_NOTFOUND)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            gossip_lerr("dbc_p->c_get (DB_NEXT) failed?\n");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
        if (data.data) 
            free(data.data);
        /* Were we asked for a specified number of keys? */
        if (get_key_count == 0)
        {
            op_p->u.k_iterate_keys.key_array[i].read_sz = key.size;
            i++;
            /* Okay, are we done? */
            if (i >= *op_p->u.k_iterate_keys.count_p)
                break;
        }
        /* okay, keep going */
        else if (get_key_count == 1) {
            i++;
            if (key.data)
                free(key.data);
        }
    }
    
return_ok:
    if (ret == DB_NOTFOUND)
    {
        *op_p->u.k_iterate_keys.position_p = TROVE_ITERATE_END;
    }
    else {
        char buf[64];
        db_recno_t recno;

        /* Fetch the record number to return to caller */
        memset(&key, 0, sizeof(key));
        key.data  = buf;
        key.size  = key.ulen = 64;
        key.dlen  = 64;
        key.doff  = 0;
        key.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;

        memset(&data, 0, sizeof(data));
        data.data = &recno;
        data.size = data.ulen = sizeof(recno);
        data.flags |= DB_DBT_USERMEM;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_GET_RECNO);
        if (ret == DB_NOTFOUND)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "warning: keyval "
                         "iterate_keys -- notfound\n");
        }
        else if (ret != 0)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "warning: keyval iterate_keys "
                         "-- some other failure @ recno\n");
        }
        assert(recno != TROVE_ITERATE_START && recno != TROVE_ITERATE_END);
        *op_p->u.k_iterate_keys.position_p = recno;
    }
    /* 'position' points us to record we just read, or is set to END */

    *op_p->u.k_iterate_keys.count_p = i;

    /* free the cursor */
    ret = dbc_p->c_close(dbc_p);
    if (ret != 0)
    {
        gossip_lerr("dbc_p->c_close failed\n");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    /* give up the db reference */
    dbpf_open_cache_put(&tmp_ref);
    return 1;
    
return_error:
    gossip_lerr("dbpf_keyval_iterate_keys_op_svc: %s\n", db_strerror(ret));
    *op_p->u.k_iterate_keys.count_p = i;

    if(dbc_p)
    {
        dbc_p->c_close(dbc_p);
    }

    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
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
    int ret = -TROVE_EINVAL, got_db = 0, i = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    for(i = 0; i < op_p->u.k_read_list.count; i++)
    {
        memset(&key, 0, sizeof(key));
        key.data = op_p->u.k_read_list.key_array[i].buffer;
        key.size = op_p->u.k_read_list.key_array[i].buffer_sz;
        
        memset(&data, 0, sizeof(data));
        data.data = op_p->u.k_read_list.val_array[i].buffer;
        data.ulen = op_p->u.k_read_list.val_array[i].buffer_sz;
        data.flags = DB_DBT_USERMEM;
        
        ret = tmp_ref.db_p->get(tmp_ref.db_p, NULL, &key, &data, 0);
        if (ret != 0)
        {
            tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->get");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        op_p->u.k_read_list.val_array[i].read_sz = data.size;
    }

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
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

    return 0;
}

static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    TROVE_size k_size;
    DB_BTREE_STAT *k_stat_p = NULL;
    int k;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    if ((op_p->flags & TROVE_NOOVERWRITE) 
            || (op_p->flags & TROVE_ONLYOVERWRITE))
    {
        /* read each key to see if it is present */
        for (k = 0; k < op_p->u.k_write_list.count; k++)
        {
            memset(&key, 0, sizeof(key));
            memset(&data, 0, sizeof(data));
            key.data = op_p->u.k_write_list.key_array[k].buffer;
            key.size = op_p->u.k_write_list.key_array[k].buffer_sz;

            ret = tmp_ref.db_p->get(tmp_ref.db_p, NULL, &key, &data, 0);
            if (ret != 0)
            {
                if ((op_p->flags & TROVE_NOOVERWRITE) &&  (ret == DB_NOTFOUND))
                {
                    /* this means key is not in DB, which is what we
                     * want for the no-overwrite case - so go to the next key
                     */
                    continue;
                }
                tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->get");
                ret = -dbpf_db_error_to_trove_error(ret);
                goto return_error;
            }
        }
    }

    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
        key.data = op_p->u.k_write_list.key_array[k].buffer;
        key.size = op_p->u.k_write_list.key_array[k].buffer_sz;
        data.data = op_p->u.k_write_list.val_array[k].buffer;
        data.size = op_p->u.k_write_list.val_array[k].buffer_sz;

        ret = tmp_ref.db_p->put(tmp_ref.db_p, NULL, &key, &data, 0);
        if (ret != 0)
        {
            tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->put");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Trove KeyVal Write "
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
                    ref, op_p->u.k_write_list.key_array[k].buffer,
                    op_p->u.k_write_list.val_array[k].buffer, data.size))
            {
                /*
                NOTE: this can happen if the keyword isn't registered,
                or if there is no associated cache_elem for this key
                */
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data written "
                    "(key is %s)\n", (char *)op_p->u.k_write_list.key_array[k].buffer);
            }
            else
            {
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                    "written (key is %s)\n",
                    (char *)op_p->u.k_write_list.key_array[k].buffer);
            }
        }
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }

    /* this may have increased the number of keyvals stored in this
     * object; update the k_size in the cache
     */
    ret = tmp_ref.db_p->stat(tmp_ref.db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_STAT
		        (DB_TXN *) NULL,
#endif
                         &k_stat_p,
#ifdef HAVE_UNKNOWN_PARAMETER_TO_DB_STAT
                          NULL,
#endif 
                           0);
    if (ret == 0)
    {
        k_size = (TROVE_size)k_stat_p->bt_ndata;
        free(k_stat_p);

        gen_mutex_lock(&dbpf_attr_cache_mutex);
        dbpf_attr_cache_ds_attr_update_cached_data_ksize(ref, k_size);
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }
    else
    {
        tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->stat");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
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
    int ret = -TROVE_EINVAL, got_db = 0;
    struct open_cache_ref tmp_ref;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    else
    {
        got_db = 1;
    }

    if ((ret = tmp_ref.db_p->sync(tmp_ref.db_p, 0)) != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }
        
    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
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

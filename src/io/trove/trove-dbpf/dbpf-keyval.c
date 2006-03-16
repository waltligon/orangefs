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
    TROVE_ds_flags type;
    char key[DBPF_MAX_KEY_LENGTH];
};

#define DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(_size) \
    (sizeof(TROVE_handle) + sizeof(TROVE_ds_flags) + _size)

#define DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(_size) \
    (_size - sizeof(TROVE_handle) - sizeof(TROVE_ds_flags))

extern gen_mutex_t dbpf_attr_cache_mutex;

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p);

static int dbpf_keyval_iterate_get_first_entry(struct dbpf_op *op_p,
                                               DBC * dbc_p,
                                               char * key_array,
                                               int key_max_length,
                                               int * key_length,
                                               char * data_array,
                                               int data_max_length,
                                               int * data_length);

static int dbpf_keyval_iterate_step_to_position(struct dbpf_op *op_p,
                                                TROVE_ds_position pos,
                                                DBC * dbc_p);

static int dbpf_keyval_iterate_skip_to_position(struct dbpf_op *op_p,
                                                TROVE_ds_position pos,
                                                DBC * dbc_p);

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
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    memset(&key, 0, sizeof(key));

    key_entry.handle = op_p->handle;
    key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;
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

    key_entry.handle = op_p->handle;
    key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;

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

    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Trove KeyVal Write "
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
    struct dbpf_keyval_db_entry key_entry;
    DBT key;

    key_entry.handle = op_p->handle;
    key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;
    memcpy(key_entry.key, 
           op_p->u.k_remove.key.buffer, 
           op_p->u.k_remove.key.buffer_sz);

    memset (&key, 0, sizeof(key));
    key.data = &key_entry;
    key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(op_p->u.k_remove.key.buffer_sz);
    ret = op_p->coll_p->keyval_db->del(
        op_p->coll_p->keyval_db, NULL, &key, 0);

    if (ret != 0)
    {
        op_p->coll_p->keyval_db->err(
            op_p->coll_p->keyval_db, ret, "DB->del");
        ret = -dbpf_db_error_to_trove_error(ret);
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
    int ret = -TROVE_EINVAL, i=0;
    struct dbpf_keyval_db_entry key_entry;
    DBC *dbc_p = NULL;
    DBT key, data;
    int key_sz;

    assert(*op_p->u.k_iterate.count_p > 0);

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

    ret = op_p->coll_p->keyval_db->cursor(
        op_p->coll_p->keyval_db, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    if(*op_p->u.k_iterate.position_p == TROVE_ITERATE_START)
    {
        ret = dbpf_keyval_iterate_get_first_entry(
            op_p, dbc_p, 
            op_p->u.k_iterate.key_array[0].buffer,
            op_p->u.k_iterate.key_array[0].buffer_sz,
            &op_p->u.k_iterate.key_array[0].read_sz,
            op_p->u.k_iterate.val_array[0].buffer,
            op_p->u.k_iterate.val_array[0].buffer_sz,
            &op_p->u.k_iterate.val_array[0].read_sz);
        if(ret == DB_NOTFOUND)
        {
            /* assume no entries in db, set count = 0 and pos = ITERATE_END
             * and return ok */
            goto return_ok;
        }
        else if(ret != 0)
        {
            goto return_error;
        }

        i = 1;
    }
    else
    {
        ret = dbpf_keyval_iterate_skip_to_position(
            op_p,
            *op_p->u.k_iterate.position_p,
            dbc_p);
        if(ret != 0)
        {
            if(ret == DB_NOTFOUND)
            {
                goto return_ok;
            }
            else
            {
                goto return_error;
            }
        }
    }

    for (; i < *op_p->u.k_iterate.count_p; i++)
    {
        memset(&key, 0, sizeof(key));

        key.data = (void *)&key_entry;
        key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH);
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
#ifdef HAVE_DB_BUFFER_SMALL
        else if(ret == DB_BUFFER_SMALL && 
#else
        else if(ret == ENOMEM &&
#endif
                key_entry.handle != op_p->handle)
        {
            ret = DB_NOTFOUND;
            goto return_ok;
        }
        else if (ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        if(key_entry.handle != op_p->handle)
        {
            ret = DB_NOTFOUND;
            goto return_ok;
        }

        /* at this point we assume that if the handle is the correct value
         * that all keys must be components
         */
        assert(key_entry.type & (op_p->flags & TROVE_KEYVAL_TYPES));

        key_sz = (DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size) > 
                  op_p->u.k_iterate.key_array[i].buffer_sz) ?
            op_p->u.k_iterate.key_array[i].buffer_sz :
            DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size);

        memcpy(op_p->u.k_iterate.key_array[i].buffer, key_entry.key, key_sz);

        op_p->u.k_iterate.key_array[i].read_sz = key_sz;
        op_p->u.k_iterate.val_array[i].read_sz = data.size;
    }

return_ok:
    if (ret == DB_NOTFOUND)
    {
        *op_p->u.k_iterate.position_p = TROVE_ITERATE_END;
    }
    else
    {
        if(*op_p->u.k_iterate.position_p == TROVE_ITERATE_START)
        {
            *op_p->u.k_iterate.position_p = i;
        }
        else
        {
            *op_p->u.k_iterate.position_p += i;
        }

        if(i != 0)
        {
            /* insert the key of the last entry read based on
             * its position
             */
            ret = PINT_dbpf_keyval_pcache_insert(
                op_p->coll_p->pcache, op_p->handle,
                *op_p->u.k_iterate.position_p,
                op_p->u.k_iterate.key_array[(i-1)].buffer, 
                op_p->u.k_iterate.key_array[(i-1)].read_sz);
            if(ret != 0)
            {
                goto return_error;
            }
        }
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

    gossip_debug(GOSSIP_TROVE_DEBUG, 
                 "Finished keyval iterate: position: %d, count: %d\n", 
                 *op_p->u.k_iterate.position_p, *op_p->u.k_iterate.count_p);

    return 1;

return_error:
    gossip_lerr("dbpf_keyval_iterate_op_svc: %s\n", db_strerror(ret));
    *op_p->u.k_iterate.count_p = i;

    if(dbc_p)
    {
        dbc_p->c_close(dbc_p);
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
    int ret = -TROVE_EINVAL, i=0, get_key_count = 0;
    DBC *dbc_p = NULL;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    int data_length;

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
    ret = op_p->coll_p->keyval_db->cursor(
        op_p->coll_p->keyval_db, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        gossip_lerr("db_p->cursor failed\n");
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    if(*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_START)
    {
        ret = dbpf_keyval_iterate_get_first_entry(
            op_p, dbc_p,
            op_p->u.k_iterate_keys.key_array[0].buffer,
            op_p->u.k_iterate_keys.key_array[0].buffer_sz,
            &op_p->u.k_iterate_keys.key_array[0].read_sz,
            NULL,
            0,
            &data_length);
        if(ret != 0)
        {
            goto return_error;
        }

        i = 1;
    }
    else
    {
        ret = dbpf_keyval_iterate_skip_to_position(
            op_p,
            *op_p->u.k_iterate_keys.position_p,
            dbc_p);
        if(ret != 0)
        {
            if(ret == DB_NOTFOUND)
            {
                goto return_ok;
            }
            else
            {
                goto return_error;
            }
        }
    }

    if (*op_p->u.k_iterate_keys.count_p == 0)
    {
        get_key_count = 1;
    }

    for(; i < *op_p->u.k_iterate_keys.count_p || get_key_count; ++i)
    {
        memset(&key, 0, sizeof(key));
        key.flags |= DB_DBT_USERMEM;
        key.data = &key_entry;
        key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH);

        memset(&data, 0, sizeof(data));
        data.flags |= DB_DBT_USERMEM;
        data.ulen = 0;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret == DB_NOTFOUND)
        {
            goto return_ok;
        }
#ifdef HAVE_DB_BUFFER_SMALL
        else if(ret != 0 && ret != DB_BUFFER_SMALL)
#else
        else if(ret != 0 && ret != ENOMEM)
#endif
        {
            gossip_lerr("dbc_p->c_get (DB_NEXT) failed?\n");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        if(key_entry.handle != op_p->handle ||
           !(key_entry.type & (op_p->flags & TROVE_KEYVAL_TYPES)))
        {
            goto return_ok;
        }

        if(!get_key_count)
        {
            int key_sz = (op_p->u.k_iterate_keys.key_array[i].buffer_sz <
                          DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size)) ?
                op_p->u.k_iterate_keys.key_array[i].buffer_sz :
                DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size);

            memcpy(op_p->u.k_iterate_keys.key_array[i].buffer, 
                   key_entry.key, 
                   key_sz);
            op_p->u.k_iterate_keys.key_array[i].read_sz = key_sz;
        }
    }

return_ok:

    if (ret == DB_NOTFOUND)
    {
        *op_p->u.k_iterate_keys.position_p = TROVE_ITERATE_END;
    }
    else 
    {
        if(*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_START)
        {
            *op_p->u.k_iterate_keys.position_p = 1;
        }
        else
        {
            *op_p->u.k_iterate_keys.position_p += i;
        }

        if(i != 0)
        {
            ret = PINT_dbpf_keyval_pcache_insert(
                op_p->coll_p->pcache, op_p->handle,
                *op_p->u.k_iterate_keys.position_p,
                op_p->u.k_iterate_keys.key_array[(i-1)].buffer,
                op_p->u.k_iterate_keys.key_array[(i-1)].read_sz);
            if(ret != 0)
            {
                goto return_error;
            }
        }
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

    return 1;

return_error:
    gossip_lerr("dbpf_keyval_iterate_keys_op_svc: %s\n", db_strerror(ret));
    *op_p->u.k_iterate_keys.count_p = i;

    if(dbc_p)
    {
        dbc_p->c_close(dbc_p);
    }

    return ret;
}

static int dbpf_keyval_iterate_get_first_entry(
    struct dbpf_op *op_p,
    DBC * dbc_p,
    char * key_array,
    int key_max_length,
    int * key_length,
    char * data_array,
    int data_max_length,
    int * data_length)
{
    DBT key, data;
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    char static_array[PVFS_NAME_MAX];

    key_entry.handle = op_p->handle;
    key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;
    key_entry.key[0] = '\0';

    memset(&key, 0, sizeof(DBT));
    key.data = &key_entry;
    key.flags |= DB_DBT_USERMEM;

    /* setting the size of the key to compare to zero should return
     * the first entry with the same handle and type
     */
    key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);

    /* setting the bytes available for returning the found key
     * as the length of the entry with a max length for the key
     */
    key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH);
    
    memset(&data, 0, sizeof(DBT));
    data.data = static_array;
    data.ulen = PVFS_NAME_MAX;
    data.flags |= DB_DBT_USERMEM;

    /* use Berkeley DB's DB_SET_RANGE functionality to move the cursor
     * to the first matching entry after the key with the specified handle.
     * This is done by creating a key that has a null component string.
     */
    ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RANGE);
    if(ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        return ret;
    }

    if(!(key_entry.handle == op_p->handle && 
       key_entry.type & (op_p->flags & TROVE_KEYVAL_TYPES)))
    {
        return DB_NOTFOUND;
    }

    *key_length = (key_max_length < DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size)) ?
        key_max_length : DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size);
    if(key_array && key_max_length > 0)
    {
        memcpy(key_array, key_entry.key, *key_length);
    }

    *data_length = (data_max_length < data.size) ? 
        data_max_length : data.size;
    if(data_array && data_max_length > 0)
    {
        memcpy(data_array, static_array, *data_length);
    }
    
    return 0;
}

static int dbpf_keyval_iterate_skip_to_position(
    struct dbpf_op *op_p,
    TROVE_ds_position pos,
    DBC * dbc_p)
{
    int ret, len;
    const char * keyname;
    DBT key, data;
    struct dbpf_keyval_db_entry key_entry;
    TROVE_handle dummy_data;

    assert(pos != TROVE_ITERATE_START);

    ret = PINT_dbpf_keyval_pcache_lookup(
        op_p->coll_p->pcache, op_p->handle, pos, &keyname, &len);
    if(ret == -PVFS_ENOENT)
    {
        /* if the lookup fails (because the server was restarted)
         * we fall back to stepping through all the entries to get
         * to the position
         */

        return dbpf_keyval_iterate_step_to_position(op_p, pos, dbc_p);
    }

    key_entry.handle = op_p->handle;
    key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;
    memcpy(key_entry.key, keyname, len);

    memset(&key, 0, sizeof(key));

    key.data = (void *)&key_entry;
    key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(len);
    key.flags |= DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.flags |= DB_DBT_USERMEM;
    data.data = (void *)&dummy_data;
    data.ulen = sizeof(TROVE_handle);

    ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET);
    if(ret != 0 && ret == DB_NOTFOUND)
    {
        return ret;
    }
#ifdef HAVE_DB_BUFFER_SMALL
    else if(ret != 0 && ret != DB_BUFFER_SMALL)
#else
    else if(ret != 0 && ret != ENOMEM)
#endif
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        return ret;
    } 

    return 0;
}

static int dbpf_keyval_iterate_step_to_position(
    struct dbpf_op *op_p,
    TROVE_ds_position pos,
    DBC * dbc_p)
{
    int i = 0;
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    int key_sz, val_sz;
    char data_array[PVFS_NAME_MAX];
    int free_data = 0;

    assert(pos != TROVE_ITERATE_START);

    memset(&key, 0, sizeof(key));

    key.data = (void *)&key_entry;
    key.flags |= DB_DBT_USERMEM;
    key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH);

    memset(&data, 0, sizeof(data));
    data.flags |= DB_DBT_USERMEM;
    data.data = data_array;
    data.ulen = PVFS_NAME_MAX;

    ret = dbpf_keyval_iterate_get_first_entry(
        op_p, dbc_p,
        NULL, 0, &key_sz,
        NULL, 0, &val_sz);
    if(ret != 0)
    {
        if(ret == DB_NOTFOUND)
        {
            return ret;
        }
        else
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            return ret;
        }
    }

    for(i = 0; i < pos; ++i)
    {
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if(ret == DB_NOTFOUND)
        {
            return ret;
        }
#ifdef HAVE_DB_BUFFER_SMALL
        else if(ret == DB_BUFFER_SMALL)
#else
        else if(ret == ENOMEM)
#endif
        {
            free_data = 1;
            data.data = malloc(data.size);
            data.ulen = data.size;
            ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        }

        if(ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            return ret;
        }

        if(free_data) 
        {
            free(data.data);
        }

        if(key_entry.handle != op_p->handle ||
           !(key_entry.type & (op_p->flags & TROVE_KEYVAL_TYPES)))
        {
            ret = DB_NOTFOUND;
            return ret;
        }
    }

    return 0;
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
    int key_sz;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;

    for(i = 0; i < op_p->u.k_read_list.count; i++)
    {
        key_entry.handle = op_p->handle;
        key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;

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

        key_sz = (DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size) >
                  op_p->u.k_read_list.key_array[i].buffer_sz) ?
            op_p->u.k_read_list.key_array[i].buffer_sz :
            DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key.size);

        memcpy(op_p->u.k_read_list.key_array[i].buffer,
               key_entry.key, key_sz);
        key.size = op_p->u.k_read_list.key_array[i].buffer_sz;
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

    key_entry.handle = op_p->handle;
    key_entry.type = op_p->flags & TROVE_KEYVAL_TYPES;

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

        data.data = op_p->u.k_write_list.val_array[k].buffer;
        data.size = op_p->u.k_write_list.val_array[k].buffer_sz;

        ret = op_p->coll_p->keyval_db->put(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if (ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->put");
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

    if(db_entry_a->type != db_entry_b->type)
    {
        return (db_entry_a->type < db_entry_b->type) ? -1 : 1;
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

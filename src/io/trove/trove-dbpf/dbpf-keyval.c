/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <malloc.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-attr-cache.h"
#include "gossip.h"

/* Internal function prototypes */
static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);
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
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {handle, coll_id};

    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Trove KeyVal Read "
                 "of %s\n", (char *)key_p->buffer);

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
            return 1;
        }
    }

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
    q_op_p->op.u.k_read.key   = *key_p;
    q_op_p->op.u.k_read.val   = *val_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_keyval_read_op_svc()
 *
 * Service a queued keyval read operation.
 *
 * Returns 1 on completion, 0 on no progress, or < 0 on error.
 */
static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p)
{
    int error, ret, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
	error = ret;
	goto return_error;
    }
    else
    {
	got_db = 1;
    }

    memset(&key, 0, sizeof(key));
    key.data = op_p->u.k_read.key.buffer;
    key.size = op_p->u.k_read.key.buffer_sz;

    memset(&data, 0, sizeof(data));
    data.data = op_p->u.k_read.val.buffer;
    data.ulen = op_p->u.k_read.val.buffer_sz;
    data.flags = DB_DBT_USERMEM;

    ret = tmp_ref.db_p->get(tmp_ref.db_p, NULL, &key, &data, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "warning: keyval read error on handle %Lu and "
                     "key=%s (%s)\n", Lu(op_p->handle),
                     (char *)key.data, db_strerror(ret));
        error = -dbpf_db_error_to_trove_error(ret);

        goto return_error;
    }

    op_p->u.k_read.val.read_sz = data.size;

    /* cache this data in the attr cache if we can */
    if (dbpf_attr_cache_elem_set_data_based_on_key(
            ref, op_p->u.k_read.key.buffer,
            op_p->u.k_read.val.buffer, data.size))
    {
        /*
          NOTE: this can happen if the keyword isn't registered,
          or if there is no associated cache_elem for this key
        */
        gossip_debug(
            GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data retrieved "
            "(key is %s)\n", (char *)op_p->u.k_read.key.buffer);
    }
    else
    {
        gossip_debug(
            GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
            "retrieved (key is %s)\n",
            (char *)op_p->u.k_read.key.buffer);
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    return error;
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
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;

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
			KEYVAL_WRITE,
			handle,
			coll_p,
			dbpf_keyval_write_op_svc,
			user_ptr,
			flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.k_write.key   = *key_p;
    q_op_p->op.u.k_write.val   = *val_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
	
    return 0;
}

static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p)
{
    int error, ret, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
	error = ret;
	goto return_error;
    }
    else
    {
        got_db = 1;
    }

    /* we have a keyval space now, maybe a brand new one. */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = op_p->u.k_write.key.buffer;
    key.size = op_p->u.k_write.key.buffer_sz;
    data.data = op_p->u.k_write.val.buffer;
    data.size = op_p->u.k_write.val.buffer_sz;

    ret = tmp_ref.db_p->put(tmp_ref.db_p, NULL, &key, &data, 0);
    if (ret != 0)
    {
	tmp_ref.db_p->err(tmp_ref.db_p, ret, "DB->put");
	error = -dbpf_db_error_to_trove_error(ret);
	goto return_error;
    }

    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Trove KeyVal Write "
                 "of %s\n", (char *)op_p->u.k_write.key.buffer);

    /*
      now that the data is written to disk, update the cache if it's
      an attr keyval we manage.
    */
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

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    return error;
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
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;

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
    int error, ret, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key;

    /* absolutely no need to create the db if we are removing entries */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
	error = ret;
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
	error = -dbpf_db_error_to_trove_error(ret);
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
    return error;
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
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;

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
    q_op_p->op.u.k_iterate.key_array  = key_array;
    q_op_p->op.u.k_iterate.val_array  = val_array;
    q_op_p->op.u.k_iterate.position_p = position_p;
    q_op_p->op.u.k_iterate.count_p    = inout_count_p;

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
 * so we just return that we are done and that there are 0 things read.
 *
 * Otherwise we read and return the position of the last read keyval.
 *
 * In all cases we read using DB_NEXT.  This is ok because it behaves like
 * DB_FIRST (read the first record) when called with an uninitialized
 * cursor (so we just don't initialize the cursor in the TROVE_ITERATE_START
 * case).
 *
 */
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p)
{
    int error, ret, i=0, got_db = 0;
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

    /* TODO: VALIDATE THE HANDLE IN SOME WAY BEFORE DOING THIS? 
     * IT'S DIFFICULT TO KNOW IF THE ERROR IS BECAUSE THE KEYVAL SPACE
     * IS EMPTY OR BECAUSE OF SOME OTHER ERROR.
     *
     * FOR NOW JUST CREATE THE SPACE IF IT ISN'T THERE.  FIX LATER.
     */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
	error = ret;
	goto return_error;
    }
    else
    {
	got_db = 1;
    }

    /* get a cursor */
    ret = tmp_ref.db_p->cursor(tmp_ref.db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
	error = -dbpf_db_error_to_trove_error(ret);
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
	    key.size = key.ulen = op_p->u.k_iterate.key_array[0].buffer_sz;
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
	    error = -dbpf_db_error_to_trove_error(ret);
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
	    error = -dbpf_db_error_to_trove_error(ret);
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
	 *
	 * TODO: FIGURE OUT WHAT IS GOING ON W/KEY AND TRY TO AVOID USING
	 * ANY MEMORY.
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
    /* 'position' points us to the record we just read, or is set to END */

    *op_p->u.k_iterate.count_p = i;

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    /* free the cursor */
    ret = dbc_p->c_close(dbc_p);
    if (ret != 0)
    {
	error = -dbpf_db_error_to_trove_error(ret);
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
    return error;
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
    return -TROVE_ENOSYS;
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
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;

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
    q_op_p->op.u.k_read_list.count     = count;
	
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p)
{
    int i, error, ret, got_db = 0;
    struct open_cache_ref tmp_ref;
    DBT key, data;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
	error = ret;
	goto return_error;
    }
    else
    {
	got_db = 1;
    }

    for (i=0; i < op_p->u.k_read_list.count; i++)
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
	    error = -dbpf_db_error_to_trove_error(ret);
	    goto return_error;
	}

	op_p->u.k_read_list.val_array[i].read_sz = data.size;
    }

    DBPF_DB_SYNC_IF_NECESSARY(op_p, tmp_ref.db_p);

    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    /* TODO: SAVE COUNT? */
    return error;
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
    return -TROVE_ENOSYS;
}

static int dbpf_keyval_flush(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    struct dbpf_collection *coll_p;
    dbpf_queued_op_t *q_op_p;
    
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
			KEYVAL_FLUSH,
			handle,
			coll_p,
			dbpf_keyval_flush_op_svc,
			user_ptr,
			flags,
                        context_id);

    /* initialize the op-specific members */
    /* there are no op-specific members for sync */

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_keyval_flush_op_svc()
 *
 * service a queued keyval flush operation
 */
static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p)
{
    int error, ret, got_db = 0;
    struct open_cache_ref tmp_ref;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_DB, &tmp_ref);
    if (ret < 0)
    {
	error = ret;
	goto return_error;
    }
    else
    {
	got_db = 1;
    }

    if ((ret = tmp_ref.db_p->sync(tmp_ref.db_p, 0)) != 0)
    {
	error = -dbpf_db_error_to_trove_error(ret);
	goto return_error;
    }
	
    dbpf_open_cache_put(&tmp_ref);
    return 1;

 return_error:
    if (got_db)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    return error;
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

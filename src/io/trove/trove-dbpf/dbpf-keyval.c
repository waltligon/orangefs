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
#include "dbpf-keyval.h"

/* TODO: sync flag on all operations?  */

/* TODO: eventually make all the interface functions static */

extern struct dbpf_collection *my_coll_p;

/* Internal function prototypes */
static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);

static int dbpf_keyval_read(
			    TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    TROVE_keyval_s *key_p,
			    TROVE_keyval_s *val_p,
			    TROVE_ds_flags flags,
			    TROVE_vtag_s *vtag, 
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    /* TODO: find the collection */
    coll_p = my_coll_p;

    /* validate the handle, check permissions */

    /* Q: do we want to somehow lock the handle here so that it
     *    doesn't get removed while we're working on it?  To allow
     *    for atomic access?
     */

    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -1;

    /* initialize all the common members */
    dbpf_queued_op_init(
			q_op_p,
			KEYVAL_READ,
			handle,
			coll_p,
			dbpf_keyval_read_op_svc,
			user_ptr);

    /* initialize the op-specific members */
    q_op_p->op.u.k_read.key = *key_p;
    q_op_p->op.u.k_read.val = *val_p;
	
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_keyval_read_op_svc()
 *
 * Service a queued keyval read operation.
 */
static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p)
{
    int ret, got_db = 0;
    DB *db_p;
    DBT key, data;

    /* TODO: move into initial function so we know that the DB is around before
     * we enqueue
     */
    ret = dbpf_keyval_dbcache_try_get(op_p->coll_p->coll_id, op_p->handle, 0, &db_p);
    switch (ret) {
	case DBPF_KEYVAL_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_KEYVAL_DBCACHE_BUSY:
	    return 0;
	case DBPF_KEYVAL_DBCACHE_SUCCESS:
	    got_db = 1;
	    /* drop through */
    }

    /* get keyval */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = op_p->u.k_read.key.buffer;
    key.size = op_p->u.k_read.key.buffer_sz;
    data.data = op_p->u.k_read.val.buffer;
    data.ulen = op_p->u.k_read.val.buffer_sz;
    data.flags = DB_DBT_USERMEM;

    ret = db_p->get(db_p, NULL, &key, &data, 0);
    if (ret != 0) {
	db_p->err(db_p, ret, "DB->get");
	goto return_error;
    }

    /* sync? */

    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);
    return 1;

 return_error:
    if (got_db) dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);
    return -1;
}

/* TODO: switch valsize and valbuffer ordering, maybe use ds_key_s
 * (renamed) for val too?
 */
static int dbpf_keyval_write(
			     TROVE_coll_id coll_id,
			     TROVE_handle handle,
			     TROVE_keyval_s *key_p,
			     TROVE_keyval_s *val_p,
			     TROVE_ds_flags flags,
			     TROVE_vtag_s *vtag,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    /* TODO: find the collection */
    coll_p = my_coll_p;

    /* validate the handle, check permissions */

    /* Q: do we want to somehow lock the handle here so that it
     *    doesn't get removed while we're working on it?  To allow
     *    for atomic access?
     */

    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -1;

    /* initialize all the common members */
    dbpf_queued_op_init(
			q_op_p,
			KEYVAL_WRITE,
			handle,
			coll_p,
			dbpf_keyval_write_op_svc,
			user_ptr);

    /* initialize the op-specific members */
    q_op_p->op.u.k_write.key = *key_p;
    q_op_p->op.u.k_write.val = *val_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
	
    return 0;
}

/* dbpf_keyval_write_op_svc()
 *
 * Service a queued keyval read operation.
 */
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p)
{
    int ret;
    DB *db_p;
    DBT key, data;

    /* TODO: move into initial function so that we know the DB is around
     * before we enqueue.
     */
    ret = dbpf_keyval_dbcache_try_get(op_p->coll_p->coll_id, op_p->handle, 1, &db_p);
    switch (ret) {
	case DBPF_KEYVAL_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_KEYVAL_DBCACHE_BUSY:
	    return 0;
	case DBPF_KEYVAL_DBCACHE_SUCCESS:
	    /* drop through */
            break;
    }

    /* we have a keyval space now, maybe a brand new one. */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = op_p->u.k_write.key.buffer;
    key.size = op_p->u.k_write.key.buffer_sz;
    data.data = op_p->u.k_write.val.buffer;
    data.size = op_p->u.k_write.val.buffer_sz;

    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0) {
	db_p->err(db_p, ret, "DB->put");
	goto return_error;
    }

    /* sync? */


    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);
    return 1;

 return_error:
    return -1;
}

/* dbpf_keyval_remove()
 */
static int dbpf_keyval_remove(
			      TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      TROVE_keyval_s *key_p,
			      TROVE_ds_flags flags,
			      TROVE_vtag_s *vtag,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    /* TODO: find the collection */
    coll_p = my_coll_p;

    /* Q: what happens if someone queues a read/write request on 
     * a deleted keyval? */

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -1;

    /* initialaze common members */
    dbpf_queued_op_init(
			q_op_p,
			KEYVAL_REMOVE_KEY,
			handle,
			coll_p,
			dbpf_keyval_remove_op_svc,
			user_ptr);

    /* initialize op-specific members */
    q_op_p->op.u.k_remove.key = *key_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
	
    return 0;
}

static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p)
{
    int ret;
    DB *db_p;
    DBT key;

    /* absolutely no need to create the db if we are removing entries */
    ret = dbpf_keyval_dbcache_try_get(op_p->coll_p->coll_id, op_p->handle, 0, &db_p);
    switch (ret) {
	case DBPF_KEYVAL_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_KEYVAL_DBCACHE_BUSY:
	    return 0;
	case DBPF_KEYVAL_DBCACHE_SUCCESS:
	    /* drop through */
            break;
    }
    memset (&key, 0, sizeof(key));
    key.data = op_p->u.k_remove.key.buffer;
    key.size = op_p->u.k_remove.key.buffer_sz;
    ret = db_p->del(db_p, NULL, &key, 0);

    if (ret != 0) {
	db_p->err(db_p, ret, "DB->del");
	goto return_error;
    }
    if ((ret=db_p->sync(db_p, 0) )!= 0) {
	db_p->err(db_p, ret, "dbpf_keyval_remove_op_svc\n");
	return -1;
    }
    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);
    return 1;

 return_error:
    return -1;
}

/* dbpf_keyval_validate()
 */
static int dbpf_keyval_validate(
				TROVE_coll_id coll_id,
				TROVE_handle handle,
				TROVE_ds_flags flags,
				TROVE_vtag_s *vtag,
				void* user_ptr,
				TROVE_op_id *out_op_id_p)
{
    return -1;
}

/* dbpf_keyval_iterate()
 */
static int dbpf_keyval_iterate(
			       TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_ds_position *position_p,
			       TROVE_keyval_s *key_array,
			       TROVE_keyval_s *val_array,
			       int *inout_count_p,
			       TROVE_ds_flags flags,
			       TROVE_vtag_s *vtag,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = my_coll_p;
    if (coll_p == NULL) return -1;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -1;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			KEYVAL_ITERATE,
			handle,
			coll_p,
			dbpf_keyval_iterate_op_svc,
			user_ptr);

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
    int ret, i=0;
    db_recno_t recno;
    DB *db_p;
    DBC *dbc_p;
    DBT key, data;

    /* if they passed in that they are at the end, return 0.
     *
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate.position_p == TROVE_ITERATE_END) {
	*op_p->u.k_iterate.count_p = 0;
	return 1;
    }

    /* TODO: VALIDATE THE HANDLE IN SOME WAY BEFORE DOING THIS? 
     * IT'S DIFFICULT TO KNOW IF THE ERROR IS BECAUSE THE KEYVAL SPACE
     * IS EMPTY OR BECAUSE OF SOME OTHER ERROR.
     *
     * FOR NOW JUST CREATE THE SPACE IF IT ISN'T THERE.  FIX LATER.
     */

    ret = dbpf_keyval_dbcache_try_get(op_p->coll_p->coll_id, op_p->handle, 1, &db_p);
    switch (ret) {
	case DBPF_KEYVAL_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_KEYVAL_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_KEYVAL_DBCACHE_SUCCESS:
	    /* drop through */
            break;
    }

    /* get a cursor */
    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0) goto return_error;


    /* we have two choices here: 'seek' to a specific key by either a:
     * specifying a key or b: using record numbers in the db.  record numbers
     * will serialize multiple modification requests. We are going with record
     * numbers for now. i don't know if that's a problem, but it is something
     * to keep in mind if at some point simultaneous modifications to pvfs
     * perform badly.   -- robl */

    if (*op_p->u.k_iterate.position_p != TROVE_ITERATE_START) {
	/* need to position cursor before reading.  note that this will
	 * actually position the cursor over the last thing that was read
	 * on the last call, so we don't need to return what we get back.
	 */

	/* here we make sure that the key is big enough to hold the
	 * position that we need to pass in.
	 */
	memset(&key, 0, sizeof(key));
	if (sizeof(recno) < op_p->u.k_iterate.key_array[0].buffer_sz) {
	    key.data = op_p->u.k_iterate.key_array[0].buffer;
	    key.size = key.ulen = op_p->u.k_iterate.key_array[0].buffer_sz;
	}
	else {
	    key.data = &recno;
	    key.size = key.ulen = sizeof(recno);
	}
	*(TROVE_ds_position *) key.data = *op_p->u.k_iterate.position_p;
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = op_p->u.k_iterate.val_array[0].buffer;
	data.size = data.ulen = op_p->u.k_iterate.val_array[0].buffer_sz;
	data.flags |= DB_DBT_USERMEM;

	/* position the cursor and grab the first key/value pair */
	ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
	if (ret == DB_NOTFOUND) goto return_ok;
	else if (ret != 0) goto return_error;
    }

    for (i=0; i < *op_p->u.k_iterate.count_p; i++)
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
	if (ret == DB_NOTFOUND) goto return_ok;
	else if (ret != 0) goto return_error;
    }
    
return_ok:
    if (ret == DB_NOTFOUND) {
	*op_p->u.k_iterate.position_p = TROVE_ITERATE_END;
    }
    else {
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
	if (ret == DB_NOTFOUND) printf("warning: keyval iterate -- notfound\n");
	else if (ret != 0) printf("warning: keyval iterate -- some other failure @ recno\n");

	*op_p->u.k_iterate.position_p = recno;
    }
    /* 'position' points us to the record we just read, or is set to END */

    *op_p->u.k_iterate.count_p = i;

    /* give up the db reference */
    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);

    /* free the cursor */
    ret = dbc_p->c_close(dbc_p);
    if (ret != 0) goto return_error;
    return 1;
    
return_error:
    fprintf(stderr, "dbpf_keyval_iterate_op_svc: %s\n", db_strerror(ret));
    *op_p->u.k_iterate.count_p = i;
    return -1;
}

static int dbpf_keyval_iterate_keys(
				    TROVE_coll_id coll_id,
				    TROVE_handle handle,
				    TROVE_ds_position *position_p,
				    TROVE_keyval_s *key_array,
				    int *inout_count_p,
				    TROVE_ds_flags flags,
				    TROVE_vtag_s *vtag,
				    void *user_ptr,
				    TROVE_op_id *out_op_id_p)
{
    return -1;
}

static int dbpf_keyval_read_list(
				 TROVE_coll_id coll_id,
				 TROVE_handle handle,
				 TROVE_keyval_s *key_array,
				 TROVE_keyval_s *val_array,
				 int count,
				 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_op_id *out_op_id_p)
{
    return -1;
}

static int dbpf_keyval_write_list(
				  TROVE_coll_id coll_id,
				  TROVE_handle handle,
				  TROVE_keyval_s *key_array,
				  TROVE_keyval_s *val_array,
				  int count,
				  TROVE_ds_flags flags,
				  TROVE_vtag_s *vtag,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    return -1;
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
    dbpf_keyval_write_list
};


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

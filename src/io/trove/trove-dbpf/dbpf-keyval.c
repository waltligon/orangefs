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

#include <trove.h>
#include <trove-internal.h>
#include <dbpf.h>
#include <dbpf-op-queue.h>
#include <dbpf-keyval.h>

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
    int ret;
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

    if ((ret = db_p->get(db_p, NULL, &key, &data, 0)) == 0)
	printf("db: key retrieved.\n");
    else {
	db_p->err(db_p, ret, "DB->get");
	goto return_error;
    }

    /* sync? */


    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);
    return 1;

 return_error:
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
    }

    /* we have a keyval space now, maybe a brand new one. */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = op_p->u.k_write.key.buffer;
    key.size = op_p->u.k_write.key.buffer_sz;
    data.data = op_p->u.k_write.val.buffer;
    data.size = op_p->u.k_write.val.buffer_sz;

    if ((ret = db_p->put(db_p, NULL, &key, &data, 0)) == 0)
	printf("db: key stored.\n");
    else {
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
    }
    memset (&key, 0, sizeof(key));
    key.data = op_p->u.k_remove.key.buffer;
    key.size = op_p->u.k_remove.key.buffer_sz;
    if ( (ret = db_p->del(db_p, NULL, &key, 0)) == 0) {
	printf("db: key removed. \n");
    } else {
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
    q_op_p->op.u.k_iterate.count      = inout_count_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_keyval_iterate_op_svc()
 */
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p)
{
    int ret, i=0;
    DB *db_p;
    DBC *dbc_p;
    DBT key, data;

    ret = dbpf_keyval_dbcache_try_get(op_p->coll_p->coll_id, op_p->handle, 1, &db_p);
    switch (ret) {
	case DBPF_KEYVAL_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_KEYVAL_DBCACHE_BUSY:
	    return 0;
	case DBPF_KEYVAL_DBCACHE_SUCCESS:
	    /* drop through */
    }

    /* grab out key/value pairs */

    /* get a cursor */
    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0) goto return_error;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    /* we have two choices here: 'seek' to a specific key by either a:
     * specifying a key or b: using record numbers in the db.  record numbers
     * will serialize multiple modification requests. We are going with record
     * numbers for now. i don't know if that's a problem, but it is something
     * to keep in mind if at some point simultaneous modifications to pvfs
     * perform badly.   -- robl */

    key.data = op_p->u.k_iterate.key_array[0].buffer;
    key.size = key.ulen = op_p->u.k_iterate.key_array[0].buffer_sz;
    data.data = op_p->u.k_iterate.val_array[0].buffer;
    data.size = data.ulen = op_p->u.k_iterate.val_array[0].buffer_sz;

    *(TROVE_ds_position *)key.data = *(op_p->u.k_iterate.position_p);
    key.flags |= DB_DBT_USERMEM;
    data.flags |= DB_DBT_USERMEM;

    /* position the cursor and grab the first key/value pair */
    ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
    if (ret == DB_NOTFOUND) {
	/* no more pairs: tell caller how many we processed */
	*(op_p->u.k_iterate.count)=0; 
    }
    else if (ret != 0) goto return_error;
    else {
	for (i=1; i < *(op_p->u.k_iterate.count); i++) {
	    key.data = op_p->u.k_iterate.key_array[i].buffer;
	    key.size = key.ulen = op_p->u.k_iterate.key_array[i].buffer_sz;
	    key.flags |= DB_DBT_USERMEM;
	    data.data = op_p->u.k_iterate.val_array[i].buffer;
	    data.size = data.ulen = op_p->u.k_iterate.val_array[i].buffer_sz;
	    data.flags |= DB_DBT_USERMEM;
	    
	    ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
	    if (ret == DB_NOTFOUND) {
		/* no more pairs: tell caller how many we processed */
		*(op_p->u.k_iterate.count)=i; 
	    }
	    else if (ret != 0) goto return_error;
	}

	*(op_p->u.k_iterate.position_p) += i;
    }

    /* 'position' is the record we will read next time through */
    /* XXX: right now, 'posistion' gets set to garbage when we hit the end.
     * well, not exactly 'garbage', but it gets incremented here even in the
     * end-of-database case ( come in, ask for one, read zero (hit end):
     * position still gets incremented ).  It might be helpful (or at least
     * consistent) if posistion always pointed to the 'next' place to access,
     * even if that's one place past the end of the database ... or maybe i'm
     * putting too much weight on 'posistion's value at the end */

    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle);

    /* free the cursor */
    ret = dbc_p->c_close(dbc_p);
    if (ret != 0) goto return_error;
    return 1;

 return_error:
    fprintf(stderr, "dbpf_keyval_iterate_op_svc: %s\n", db_strerror(ret));
    *(op_p->u.k_iterate.count)=i; 
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
 * vim: ts=4
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <malloc.h>
#include <assert.h>

#include <trove.h>
#include <trove-internal.h>
#include <dbpf.h>
#include <dbpf-op-queue.h>
#include <trove-ledger.h>

/* TODO: move both of these into header file? */
extern struct dbpf_collection *my_coll_p;
extern TROVE_handle dbpf_last_handle;

static int dbpf_dspace_iterate_handles_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p);

/* TODO: should this have a ds_attributes with it? */
int dbpf_dspace_create(TROVE_coll_id coll_id,
		       TROVE_handle *handle_p,
		       TROVE_handle bitmask,
		       TROVE_ds_type type,
		       TROVE_keyval_s *hint, /* TODO: What is this? */
		       void *user_ptr,
		       TROVE_op_id *out_op_id_p)
{
    int ret;
    struct dbpf_collection *coll_p;
    struct dbpf_dspace_attr attr;
    TROVE_handle new_handle;
    DBT key, data;
    
    /* TODO: search for collection using coll_id */
    coll_p = my_coll_p;
    if (coll_p == NULL) return -1;

    /* TODO: REDO BUCKETS!!!! */
    new_handle = trove_handle_get(coll_p->free_handles, *handle_p, bitmask);
    
    printf("new handle = %Lu (%Lx).\n", new_handle, new_handle);
    
    attr.coll_id = coll_id;
    attr.type = type;
    attr.ext.uid = -1;
    attr.ext.gid = -1;
    attr.ext.mode = 0;
    attr.ext.ctime = time(NULL);
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = &new_handle;
    key.size = sizeof(new_handle);
    
    /* check to see if handle is already used */
    ret = coll_p->ds_db->get(coll_p->ds_db, NULL, &key, &data, 0);
    if (ret == 0) {
	printf("handle already exists...\n");
	return -1;
    }
    if (ret != DB_NOTFOUND) {
	printf("some other error in dspace create.\n");
	return -1;
    }
    
    memset(&data, 0, sizeof(data));
    data.data = &attr;
    data.size = sizeof(attr);
    
    /* create new dataspace entry */
    ret = coll_p->ds_db->put(coll_p->ds_db, NULL, &key, &data, 0);
    if (ret != 0) {
	return -1;
    }
    
    /* always sync to ensure that data made it to the disk */
    if ((ret = coll_p->ds_db->sync(coll_p->ds_db, 0)) != 0) {
	return -1;
    }
    
    printf("created new dspace with above handle.\n");
    
    *handle_p = new_handle;
    dbpf_last_handle++;
    return 1;
}

/* dbpf_dspace_remove()
 * XXX: maybe make this an op_svc()?
 */
static int dbpf_dspace_remove(TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p)
{
	int ret;
	struct dbpf_collection *coll_p;
	DBT key;

	/* TODO: search for collection using coll_id */
	coll_p = my_coll_p;
	if (coll_p == NULL) return -1;

	/* whereas dspace_create has to do handle-making steps, we already know
	 * the handle we want to wack */

	memset(&key, 0, sizeof(key));
	key.data = &handle;
	key.size = sizeof(handle);

	/* XXX: no steps taken to ensure it's empty... */
	ret = coll_p->ds_db->del(coll_p->ds_db, NULL, &key, 0);
	switch (ret) {
		case 0:
			printf("removed dataspace with handle %Ld\n", handle);
			break;
		case DB_NOTFOUND:
			printf("tried to remove non-existant dataspace\n");
			return -1;
			break;
		default:
			coll_p->ds_db->err(coll_p->ds_db, ret, "dbpf_dspace_remove");
			return -1;
	}

	/* always sync to ensure that data made it to the disk */
	if ( (ret = coll_p->ds_db->sync(coll_p->ds_db, 0)) != 0) {
		coll_p->ds_db->err(coll_p->ds_db, ret, "dbpf_dspace_remove");
		return -1;
	}

	/* return handle to free list */
	trove_handle_put(coll_p->free_handles, handle);

	return 1;

}
int dbpf_dspace_iterate_handles(
                                TROVE_coll_id coll_id,
                                TROVE_ds_position *position_p,
                                TROVE_handle *handle_array,
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
			DSPACE_ITERATE_HANDLES,
			(TROVE_handle) 0, /* handle -- ignored in this case */
			coll_p,
			dbpf_dspace_iterate_handles_op_svc,
			user_ptr);

    /* initialize op-specific members */
    q_op_p->op.u.d_iterate_handles.handle_array = handle_array;
    q_op_p->op.u.d_iterate_handles.position_p   = position_p;
    q_op_p->op.u.d_iterate_handles.count_p      = inout_count_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_dspace_iterate_handles_op_svc()
 */
static int dbpf_dspace_iterate_handles_op_svc(struct dbpf_op *op_p)
{
    int ret = 0, i = 0;
    DB *db_p;
    DBC *dbc_p;
    DBT key, data;
    db_recno_t recno;
    struct dbpf_dspace_attr attr;
    TROVE_handle dummy_handle;

    db_p = op_p->coll_p->ds_db;
    if (db_p == NULL) goto return_error;
 
    if (*op_p->u.d_iterate_handles.position_p == TROVE_ITERATE_END) {
	*op_p->u.d_iterate_handles.count_p = 0;
	return 1;
    }

    /* get a cursor */
    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0) goto return_error;

    /* we have two choices here: 'seek' to a specific key by either a:
     * specifying a key or b: using record numbers in the db.  record numbers
     * will serialize multiple modification requests. We are going with record
     * numbers for now. i don't know if that's a problem, but it is something
     * to keep in mind if at some point simultaneous modifications to pvfs
     * perform badly.   -- robl
     *
     * not sure this is the best thing to be doing, but ok for now... -- rob
     */

    /* an uninitialized cursor will start reading at the beginning
     * of the database (first record) when used with DB_NEXT, so
     * we don't need to position with DB_FIRST.
     */
    if (*op_p->u.d_iterate_handles.position_p != TROVE_ITERATE_START) {
	/* we need to position the cursor before we can read new entries.
	 * we will go ahead and read the first entry as well, so that we
	 * can use the same loop below to read the remainder in this or
	 * the above case.
	 */

	printf("setting position\n");

	/* set position */
	assert(sizeof(recno) < sizeof(dummy_handle));

	dummy_handle = *op_p->u.d_iterate_handles.position_p;
	memset(&key, 0, sizeof(key));
	key.data  = &dummy_handle;
	key.size  = key.ulen = sizeof(dummy_handle);
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &attr;
	data.size = data.ulen = sizeof(attr);
	data.flags |= DB_DBT_USERMEM;

	ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
	if (ret == DB_NOTFOUND) goto return_ok;
	if (ret != 0) goto return_error;

	printf("handle at recno = %Ld\n", dummy_handle);
    }

    /* read handles until we run out of handles or space in buffer */
    for (i = 0; i < *op_p->u.d_iterate_handles.count_p; i++) {
	memset(&key, 0, sizeof(key));
	key.data = &op_p->u.d_iterate_handles.handle_array[i];
	key.size = key.ulen = sizeof(TROVE_handle);
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &attr;
	data.size = data.ulen = sizeof(attr);
	data.flags |= DB_DBT_USERMEM;
	    
	ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
	if (ret == DB_NOTFOUND) goto return_ok;
	else if (ret != 0) {
	    printf("c_get failed on iteration %d\n", i);
	    goto return_error;
	}
    }

return_ok:
    if (ret == DB_NOTFOUND) {
	/* if we ran off the end of the database, return TROVE_ITERATE_END */
	*op_p->u.d_iterate_handles.position_p = TROVE_ITERATE_END;
	printf("returning done!\n");
    }
    else {
	/* get the record number to return.
	 *
	 * note: key field is ignored by c_get in this case
	 */
	memset(&key, 0, sizeof(key));
	key.data  = &dummy_handle;
	key.size  = key.ulen = sizeof(dummy_handle);
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &recno;
	data.size = data.ulen = sizeof(recno);
	data.flags |= DB_DBT_USERMEM;

	ret = dbc_p->c_get(dbc_p, &key, &data, DB_GET_RECNO);
	if (ret == DB_NOTFOUND) printf("iterate -- notfound\n");
	else if (ret != 0) printf("iterate -- some other failure @ recno\n");

	*op_p->u.d_iterate_handles.position_p = recno;
    }
    /* 'position' points us to the record we just read, or is set to END */

    *op_p->u.d_iterate_handles.count_p = i;

    ret = dbc_p->c_close(dbc_p);
    if (ret != 0) return -1;
    return 1;

return_error:
    fprintf(stderr, "dbpf_dspace_iterate_handles_op_svc: %s\n", db_strerror(ret));
    *op_p->u.d_iterate_handles.count_p = i; 
    dbc_p->c_close(dbc_p); /* don't check error -- we're returning an error anyway. */

    return -1;
}

/* dbpf_dspace_verify()
 */
static int dbpf_dspace_verify(TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      TROVE_ds_type *type,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p)
{
    return -1;
}

/* dbpf_dspace_getattr()
 */
static int dbpf_dspace_getattr(TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_ds_attributes_s *ds_attr_p, 
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p)
{
    return -1;
}

/* dbpf_dspace_setattr()
 */
static int dbpf_dspace_setattr(TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_ds_attributes_s *ds_attr_p, 
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
			DSPACE_SETATTR,
			handle,
			coll_p,
			dbpf_dspace_setattr_op_svc,
			user_ptr);

    /* initialize op-specific members */
    q_op_p->op.u.d_setattr.attr_p = ds_attr_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p)
{
    
    
    return -1;
}

/* dbpf_dspace_test()
 *
 * Here's how this works:
 * - Caller gives us an id (we don't really need the coll_id)
 * - Map to queued operation with id_gen_fast_lookup(id)
 * - Call service function that is stored in operation structure
 * ...
 *
 * Returns 0 if not completed, 1 if completed (successfully or with error).
 *
 * The error state of the completed operation is returned via the state_p,
 * more to follow on this...
 *
 * out_count gets the count of completed operations too...
 *
 * Removes completed operations from the queue.
 */
static int dbpf_dspace_test(TROVE_coll_id coll_id,
			    TROVE_op_id id,
			    int *out_count_p,
			    TROVE_vtag_s *vtag,
			    void **returned_user_ptr_p,
			    TROVE_ds_state *state_p)
{
    int ret;
    struct dbpf_queued_op *q_op_p;
    
    /* map to queued operation */
    ret = dbpf_queued_op_try_get(id, &q_op_p);
    switch (ret) {
	case DBPF_QUEUED_OP_INVALID:
	    return -1;
	case DBPF_QUEUED_OP_BUSY:
	    *out_count_p = 0;
	    return 0;
	case DBPF_QUEUED_OP_SUCCESS:
	    /* fall through and process */
    }
    
    /* for now we need to call the service function;
     * there's no thread to do the work yet.
     */
    ret = q_op_p->op.svc_fn(&(q_op_p->op));

    /* if we were really simulating what should eventually happen
     * with a background thread, we would call
     * dbpf_queued_op_put(q_op_p, 1) to return it to the queue
     * and mark completed.  then we would do a try_get
     * and check to see if it is in the OP_COMPLETED state.
     *
     * however, until we get a background thread working on things,
     * this code will do the trick and is a lot faster and shorter.
     */

    if (ret != 0) {
	/* operation is done and we are telling the caller;
	 * ok to pull off queue now.
	 */
	*out_count_p = 1;
	*state_p = (ret == 1) ? 0 : -1; /* TODO: FIX THIS!!! */
	if (returned_user_ptr_p != NULL) {
	    *returned_user_ptr_p = q_op_p->op.user_ptr;
	}
	dbpf_queued_op_put_and_dequeue(q_op_p);
	return 1;
    }
	
    return 0;
}

/* dbpf_dspace_testsome()
 *
 * Returns 0 if nothing completed, 1 if something is completed (successfully
 * or with error).
 *
 * The error state of the completed operation is returned via the state_p,
 * more to follow on this...
 *
 */
static int dbpf_dspace_testsome(
				TROVE_coll_id coll_id,
				TROVE_op_id *ds_id_array,
				int *inout_count_p,
				int *out_index_array,
				TROVE_vtag_s *vtag_array,
				void **returned_user_ptr_array,
				TROVE_ds_state *state_array
				)
{
    int i, out_count = 0, ret, tmp_count;

    for (i=0; i < *inout_count_p; i++) {
	ret = dbpf_dspace_test(coll_id,
			       ds_id_array[i],
			       &tmp_count,
			       &vtag_array[i], /* TODO: this doesn't seem right! */
			       (returned_user_ptr_array != NULL) ? returned_user_ptr_array[i] : NULL,
			       &state_array[out_count]);
	
	if (ret != 0) {
	    /* operation is done and we are telling the caller;
	     * ok to pull off queue now.
	     */
	    out_index_array[out_count] = i;
	    out_count++;
	}    
    }

    *inout_count_p = out_count;
    return (out_count > 0) ? 1 : 0;
}

struct TROVE_dspace_ops dbpf_dspace_ops =
{
    dbpf_dspace_create,
    dbpf_dspace_remove,
    dbpf_dspace_iterate_handles,
    dbpf_dspace_verify,
    dbpf_dspace_getattr,
    dbpf_dspace_setattr,
    dbpf_dspace_test,
    dbpf_dspace_testsome
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4
 */

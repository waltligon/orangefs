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

#include "trove.h"
#include "trove-internal.h"
#include "trove-ledger.h"
#include "trove-handle-mgmt.h"
#include "dbpf.h"
#include "dbpf-dspace.h"
#include "dbpf-bstream.h"
#include "dbpf-keyval.h"
#include "dbpf-op-queue.h"

#define DBPF_FSTAT fstat

static int dbpf_dspace_iterate_handles_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_create_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_verify_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_getattr_op_svc(struct dbpf_op *op_p);

/* dbpf_dspace_create()
 *
 * TODO: should this have a ds_attributes with it?
 */
static int dbpf_dspace_create(TROVE_coll_id coll_id,
			      TROVE_handle_extent_array *extent_array,
			      TROVE_handle *handle_p,
			      TROVE_ds_type type,
			      TROVE_keyval_s *hint, /* TODO: What is this? */
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_context_id context_id,
			      TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    if (!extent_array || (extent_array->extent_count < 1))
	return -TROVE_EINVAL;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			DSPACE_CREATE,
			(handle_p ? *handle_p : (TROVE_handle)0),
			coll_p,
			dbpf_dspace_create_op_svc,
			user_ptr,
			flags);


    /* no op-specific members here */
    q_op_p->op.u.d_create.extent_array.extent_count =
        extent_array->extent_count;
    q_op_p->op.u.d_create.extent_array.extent_array =
        malloc(extent_array->extent_count * sizeof(TROVE_extent));

    if (q_op_p->op.u.d_create.extent_array.extent_array == NULL)
        return -TROVE_ENOMEM;

    memcpy(q_op_p->op.u.d_create.extent_array.extent_array,
           extent_array->extent_array,
           extent_array->extent_count * sizeof(TROVE_extent));

    q_op_p->op.u.d_create.out_handle_p = handle_p;
    q_op_p->op.u.d_create.type         = type;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_dspace_create_op_svc(struct dbpf_op *op_p)
{
    int ret, got_db = 0;
    TROVE_ds_storedattr_s s_attr;
    TROVE_handle new_handle;
    DBT key, data;
    DB *db_p;
    TROVE_extent cur_extent;

    /* NOTE: THIS WOULD BE EASIER IF THE COLL_ID WERE IN THE OP */
    ret = dbpf_dspace_dbcache_try_get(op_p->coll_p->coll_id, 0, &db_p);
    switch (ret) {
	case DBPF_DSPACE_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_DSPACE_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_DSPACE_DBCACHE_SUCCESS:
	    got_db = 1;
	    /* drop through */
	    break;
    }

    cur_extent = op_p->u.d_create.extent_array.extent_array[0];

    /* check if we got a single specific handle */
    if ((op_p->u.d_create.extent_array.extent_count == 1) &&
        (cur_extent.first == cur_extent.last))
    {
        /*
          check if we MUST use the exact handle value specified;
          if caller requests a specific handle, honor it
        */
        if (op_p->flags & TROVE_FORCE_REQUESTED_HANDLE)
        {
            /*
              we should probably handle this error nicely;
              right now, it will fail later (gracefully) if this
              fails since the handle will already exist, but
              since we know it here, handle it here ?
            */
            new_handle = cur_extent.first;
            trove_handle_set_used(op_p->coll_p->coll_id, new_handle);
#if 0
            printf("new_handle was FORCED to be %Ld\n", new_handle);
#endif
        }
        else if (cur_extent.first == 0)
        {
            /*
              if we got zero, the caller doesn't care
              where the handle comes from
            */
            new_handle = trove_handle_alloc(op_p->coll_p->coll_id);
        }
    }
    else
    {
        /*
          otherwise, we have to try to allocate a handle from
          the specified range that we're given
        */
        new_handle = trove_handle_alloc_from_range(
            op_p->coll_p->coll_id,
            &op_p->u.d_create.extent_array);
    }

#if 0
    printf("[%d extents] -- new_handle is %Ld (cur_extent is %Ld - %Ld)\n",
           op_p->u.d_create.extent_array.extent_count,
           new_handle, cur_extent.first, cur_extent.last);
#endif

    /*
      if we got a zero handle, we're either completely out
      of handles -- or else something terrible has happened
    */
    if (new_handle == (TROVE_handle)0)
    {
	printf("Error: handle allocator returned a zero handle.\n");
        return -TROVE_EINVAL;
    }

    s_attr.type = op_p->u.d_create.type;

    memset(&key, 0, sizeof(key));
    key.data = &new_handle;
    key.size = sizeof(new_handle);

    /* ensure that DB doesn't allocate any memory for the data */
    memset(&data, 0, sizeof(data));
    data.data = &s_attr;
    data.size = data.ulen = sizeof(s_attr);
    data.flags |= DB_DBT_USERMEM;

    /* check to see if handle is already used */
    ret = db_p->get(db_p, NULL, &key, &data, 0);
    if (ret == 0) {
	printf("handle already exists...\n");
	return -1;
    }
    if (ret != DB_NOTFOUND) {
	printf("some other error in dspace create.\n");
	return -1;
    }
    
    memset(&data, 0, sizeof(data));
    data.data = &s_attr;
    data.size = sizeof(s_attr);
    
    /* create new dataspace entry */
    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0) {
	goto return_error;
    }
    
    /* sync if requested by the user */
    if (op_p->flags & TROVE_SYNC) {
	if ((ret = db_p->sync(db_p, 0)) != 0) {
	    goto return_error;
	}
    }

    *op_p->u.d_create.out_handle_p = new_handle;
    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return 1;

return_error:
    if (got_db) dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return -1;
}

/* dbpf_dspace_remove()
 */
static int dbpf_dspace_remove(TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_context_id context_id,
			      TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			DSPACE_REMOVE,
			handle,
			coll_p,
			dbpf_dspace_remove_op_svc,
			user_ptr,
			flags);

    /* no op-specific members here */

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_dspace_remove_op_svc()
 */
static int dbpf_dspace_remove_op_svc(struct dbpf_op *op_p)
{
    int error, ret, got_db = 0;
    DBT key;
    DB *db_p;

    /* NOTE: THIS WOULD BE EASIER IF THE COLL_ID WERE IN THE OP */
    ret = dbpf_dspace_dbcache_try_get(op_p->coll_p->coll_id, 0, &db_p);
    switch (ret) {
	case DBPF_DSPACE_DBCACHE_ERROR:
	    error = -1;
	    goto return_error;
	case DBPF_DSPACE_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_DSPACE_DBCACHE_SUCCESS:
	    got_db = 1;
	    /* drop through */
	    break;
    }

    /* whereas dspace_create has to do handle-making steps, we already know
     * the handle we want to wack
     */

    memset(&key, 0, sizeof(key));
    key.data = &op_p->handle;
    key.size = sizeof(TROVE_handle);

    /* XXX: no steps taken to ensure it's empty... */
    ret = db_p->del(db_p, NULL, &key, 0);
    switch (ret) {
	case DB_NOTFOUND:
	    printf("tried to remove non-existant dataspace\n");
	    error = -TROVE_ENOENT;
	    goto return_error;
	default:
	    db_p->err(db_p, ret, "dbpf_dspace_remove");
	    error = -1;
	    goto return_error;
	case 0:
#if 0
	    printf("removed dataspace with handle 0x%08Lx\n", op_p->handle);
#endif
	    /* drop through */
	    break;
    }

    /* sync if requested */
    if (op_p->flags & TROVE_SYNC) {
	if ( (ret = db_p->sync(db_p, 0)) != 0) {
	    db_p->err(db_p, ret, "dbpf_dspace_remove");
	    error = -1;
	    goto return_error;
	}
    }

    /* remove keyval db if it exists
     *
     * NOTE: this is not a fatal error; this might have never been created.
     */
    ret = dbpf_keyval_dbcache_try_remove(op_p->coll_p->coll_id, op_p->handle);
    if (ret == -TROVE_EBUSY) assert(0);

    /* remove bstream file if it exists
     *
     * NOTE: this is not a fatal error; this might have never been created.
     */
    ret = dbpf_bstream_fdcache_try_remove(op_p->coll_p->coll_id, op_p->handle);
    switch (ret) {
	case DBPF_BSTREAM_FDCACHE_BUSY:
	    assert(0);
	case DBPF_BSTREAM_FDCACHE_ERROR:
	case DBPF_BSTREAM_FDCACHE_SUCCESS:
	    break;
    }

    /* return handle to free list */
    trove_handle_free(op_p->coll_p->coll_id,op_p->handle);
    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return 1;

return_error:
    trove_handle_free(op_p->coll_p->coll_id,op_p->handle);
    if (got_db) dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return error;
}

int dbpf_dspace_iterate_handles(TROVE_coll_id coll_id,
                                TROVE_ds_position *position_p,
                                TROVE_handle *handle_array,
                                int *inout_count_p,
                                TROVE_ds_flags flags,
                                TROVE_vtag_s *vtag,
                                void *user_ptr,
			        TROVE_context_id context_id,
                                TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			DSPACE_ITERATE_HANDLES,
			(TROVE_handle) 0, /* handle -- ignored in this case */
			coll_p,
			dbpf_dspace_iterate_handles_op_svc,
			user_ptr,
			flags);

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
    int ret = 0, i = 0, got_db = 0;
    DB *db_p;
    DBC *dbc_p;
    DBT key, data;
    db_recno_t recno;
    TROVE_ds_storedattr_s s_attr;
    TROVE_handle dummy_handle;

    if (*op_p->u.d_iterate_handles.position_p == TROVE_ITERATE_END) {
	/* already hit end of keyval space; return 1 */
	*op_p->u.d_iterate_handles.count_p = 0;
	return 1;
    }

    /* NOTE: THIS WOULD BE EASIER IF THE COLL_ID WERE IN THE OP */
    ret = dbpf_dspace_dbcache_try_get(op_p->coll_p->coll_id, 0, &db_p);
    switch (ret) {
	case DBPF_DSPACE_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_DSPACE_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_DSPACE_DBCACHE_SUCCESS:
	    got_db = 1;
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

#if 0
	printf("setting position\n");
#endif

	/* set position */
	assert(sizeof(recno) < sizeof(dummy_handle));

	dummy_handle = *op_p->u.d_iterate_handles.position_p;
	memset(&key, 0, sizeof(key));
	key.data  = &dummy_handle;
	key.size  = key.ulen = sizeof(dummy_handle);
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &s_attr;
	data.size = data.ulen = sizeof(s_attr);
	data.flags |= DB_DBT_USERMEM;

	ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
	if (ret == DB_NOTFOUND) goto return_ok;
	if (ret != 0) goto return_error;

#if 0
	printf("handle at recno = %Ld\n", dummy_handle);
#endif
    }

    /* read handles until we run out of handles or space in buffer */
    for (i = 0; i < *op_p->u.d_iterate_handles.count_p; i++) {
	memset(&key, 0, sizeof(key));
	key.data = &op_p->u.d_iterate_handles.handle_array[i];
	key.size = key.ulen = sizeof(TROVE_handle);
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &s_attr;
	data.size = data.ulen = sizeof(s_attr);
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
#if 0
	printf("returning done!\n");
#endif
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

    /* sync if requested */
    if (op_p->flags & TROVE_SYNC) {
	if ( (ret = db_p->sync(db_p, 0)) != 0) {
	    db_p->err(db_p, ret, "dbpf_dspace_remove");
	    goto return_error;
	}
    }

    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return 1;

return_error:
    fprintf(stderr, "dbpf_dspace_iterate_handles_op_svc: %s\n", db_strerror(ret));
    *op_p->u.d_iterate_handles.count_p = i; 
    if (got_db) dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return -1;
}

/* dbpf_dspace_verify()
 */
static int dbpf_dspace_verify(TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      TROVE_ds_type *type_p,
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_context_id context_id,
			      TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			DSPACE_VERIFY,
			handle,
			coll_p,
			dbpf_dspace_verify_op_svc,
			user_ptr,
			flags);

    /* initialize op-specific members */
    q_op_p->op.u.d_verify.type_p = type_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_dspace_verify_op_svc(struct dbpf_op *op_p)
{
    int error, ret, got_db = 0;
    DB *db_p;
    DBT key, data;
    TROVE_ds_storedattr_s s_attr;

    ret = dbpf_dspace_dbcache_try_get(op_p->coll_p->coll_id, 0, &db_p);
    switch (ret) {
	case DBPF_DSPACE_DBCACHE_ERROR:
	    error = -1;
	    goto return_error;
	case DBPF_DSPACE_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_DSPACE_DBCACHE_SUCCESS:
	    got_db = 1;
	    /* drop through */
    }

    memset(&key, 0, sizeof(key));
    key.data = &op_p->handle;
    key.size = sizeof(TROVE_handle);

    memset(&data, 0, sizeof(data));
    data.data = &s_attr;
    data.size = data.ulen = sizeof(s_attr);
    data.flags |= DB_DBT_USERMEM;

    /* check to see if dspace handle is used (ie. object exists) */
    ret = db_p->get(db_p, NULL, &key, &data, 0);
    if (ret == 0) {
	/* object does exist */
    }
    else if (ret == DB_NOTFOUND) {
	/* no error in access, but object does not exist */
	error = -TROVE_ENOENT;
	goto return_error;
    }
    else {	/* error in accessing database */
	error = -1;
	goto return_error;
    }

    /* copy type value back into user's memory */
    *op_p->u.d_verify.type_p = s_attr.type;

    /* sync if requested (unusual but supported in semantics) */
    if (op_p->flags & TROVE_SYNC) {
	if ((ret = db_p->sync(db_p, 0)) != 0) {
	    error = -1;
	    goto return_error;
	}
    }

    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return 1; /* done */

return_error:
    if (got_db) dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return error;
}


/* dbpf_dspace_getattr()
 */
static int dbpf_dspace_getattr(TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_ds_attributes_s *ds_attr_p,
			       TROVE_ds_flags flags,
			       void *user_ptr,
			       TROVE_context_id context_id,
			       TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			DSPACE_GETATTR,
			handle,
			coll_p,
			dbpf_dspace_getattr_op_svc,
			user_ptr,
			flags);

    /* initialize op-specific members */
    q_op_p->op.u.d_getattr.attr_p = ds_attr_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_dspace_setattr()
 */
static int dbpf_dspace_setattr(TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_ds_attributes_s *ds_attr_p,
			       TROVE_ds_flags flags,
			       void *user_ptr,
			       TROVE_context_id context_id,
			       TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			DSPACE_SETATTR,
			handle,
			coll_p,
			dbpf_dspace_setattr_op_svc,
			user_ptr,
			flags);

    /* initialize op-specific members */
    q_op_p->op.u.d_setattr.attr_p = ds_attr_p;

#if 0
    printf("storing attributes (1), uid = %d, mode = %d, type = %d\n",
	   (int) ds_attr_p->uid,
	   (int) ds_attr_p->mode,
	   (int) ds_attr_p->type);
#endif

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p)
{
    int ret, got_db = 0;
    DB *db_p;
    DBT key, data;
    TROVE_ds_storedattr_s s_attr;

    /* NOTE: THIS WOULD BE EASIER IF THE COLL_ID WERE IN THE OP */
    ret = dbpf_dspace_dbcache_try_get(op_p->coll_p->coll_id, 0, &db_p);
    switch (ret) {
	case DBPF_DSPACE_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_DSPACE_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_DSPACE_DBCACHE_SUCCESS:
	    /* drop through */
	    break;
    }

    memset(&key, 0, sizeof(key));
    key.data = &op_p->handle;
    key.size = sizeof(TROVE_handle);
    
    memset(&data, 0, sizeof(data));
    data.data = &s_attr;
    data.size = sizeof(s_attr);

    trove_ds_attr_to_stored((*op_p->u.d_setattr.attr_p), s_attr);

#if 0   
    printf("storing attributes (2), uid = %d, mode = %d, type = %d\n", (int) s_attr.uid, (int) s_attr.mode, (int) s_attr.type);
#endif

    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0) {
	db_p->err(db_p, ret, "DB->put");
	goto return_error;
    }

    /* sync if requested */
    if (op_p->flags & TROVE_SYNC) {
	if ((ret = db_p->sync(db_p, 0)) != 0) {
	    goto return_error;
	}
    }

    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return 1; /* done */
    
return_error:
    if (got_db) dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return -1;
}

static int dbpf_dspace_getattr_op_svc(struct dbpf_op *op_p)
{
    int ret, fd, got_db = 0;
    DB *db_p, *kdb_p;
    DBT key, data;
    TROVE_ds_storedattr_s s_attr;
    TROVE_size b_size = 0, k_size = 0;
    struct stat b_stat; /* for grabbing bstream size */
    DB_BTREE_STAT *k_stat_p; /* for grabbing keyval size; assumes DB_RECNUM!!! */

    /* NOTE: THIS WOULD BE EASIER IF THE COLL_ID WERE IN THE OP */
    ret = dbpf_dspace_dbcache_try_get(op_p->coll_p->coll_id, 0, &db_p);
    switch (ret) {
	case DBPF_DSPACE_DBCACHE_ERROR:
	    goto return_error;
	case DBPF_DSPACE_DBCACHE_BUSY:
	    return 0; /* try again later */
	case DBPF_DSPACE_DBCACHE_SUCCESS:
	    got_db = 1;
	    /* drop through */
	    break;
    }

    /* get an fd for the bstream so we can check size */
    ret = dbpf_bstream_fdcache_try_get(op_p->coll_p->coll_id, op_p->handle, 0, &fd);
    switch (ret) {
	case DBPF_BSTREAM_FDCACHE_ERROR:
	    /* TODO: HOW DO WE TELL A REAL ERROR FROM A "HAVEN'T CREATED YET" ERROR? */
	    /* b_size is already set to zero */
	    break;
	case DBPF_BSTREAM_FDCACHE_BUSY:
	    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id); /* release the dspace dbcache entry */
	    return 0; /* try again later */
	case DBPF_BSTREAM_FDCACHE_SUCCESS:
	    ret = DBPF_FSTAT(fd, &b_stat);
	    dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle); /* release the fd right away */
	    if (ret < 0) goto return_error;
	    b_size = (TROVE_size) b_stat.st_size;
	    /* drop through */
	    break;
    }

    ret = dbpf_keyval_dbcache_try_get(op_p->coll_p->coll_id,
				      op_p->handle,
				      0,
				      &kdb_p);
    if (ret == -TROVE_EBUSY) {
	/* release the dspace dbcache entry */
	dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
	return 0; /* try again later */
    }
    else if (ret == 0) {
	    ret = kdb_p->stat(kdb_p,
                              &k_stat_p,
#ifdef HAVE_UNKNOWN_PARAMETER_TO_DB_STAT
                              NULL,
#endif
                              0);

	    dbpf_keyval_dbcache_put(op_p->coll_p->coll_id, op_p->handle); /* release the db right away */
	    if (ret == 0) {
		k_size = (TROVE_size) k_stat_p->bt_ndata;
		free(k_stat_p);
	    }
	    else goto return_error;
	    /* drop through */
    }
    else {
	/* TODO: HOW DO WE TELL A REAL ERROR FROM A "HAVEN'T
	 * CREATED YET" ERROR?
	 */

	/* b_size is already set to zero */
	/* drop through */
    }

    memset(&key, 0, sizeof(key));
    key.data = &op_p->handle;
    key.size = sizeof(TROVE_handle);
    
    memset(&data, 0, sizeof(data));
    data.data = &s_attr;
    data.size = data.ulen = sizeof(s_attr);
    data.flags |= DB_DBT_USERMEM;

    ret = db_p->get(db_p, NULL, &key, &data, 0);
    if (ret != 0) {
	db_p->err(db_p, ret, "DB->get");
	goto return_error;
    }

#if 0
    printf("reading attributes (1), uid = %d, mode = %d, type = %d\n", (int) s_attr.uid, (int) s_attr.mode, (int) s_attr.type);
#endif

    trove_ds_stored_to_attr(s_attr, (*op_p->u.d_setattr.attr_p), b_size, k_size);

    /* sync if requested; permissable under API */
    if (op_p->flags & TROVE_SYNC) {
	if ((ret = db_p->sync(db_p, 0)) != 0) {
	    goto return_error;
	}
    }

    dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
    return 1; /* done */
    
return_error:
    if (got_db) dbpf_dspace_dbcache_put(op_p->coll_p->coll_id);
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
                            TROVE_context_id context_id,
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
	    break;
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
	 *
	 * returns error code from operation in region pointed
	 * to by state_p.
	 */
	*out_count_p = 1;

	*state_p = (ret == 1) ? 0 : ret;

	if (returned_user_ptr_p != NULL) {
	    *returned_user_ptr_p = q_op_p->op.user_ptr;
	}
	dbpf_queued_op_put_and_dequeue(q_op_p);
	dbpf_queued_op_free(q_op_p);
#if 0
	printf("dbpf_dspace_test returning success.\n");
#endif
	return 1;
    }
    else {
	dbpf_queued_op_put(q_op_p, 0);
#if 0
	printf("dbpf_dspace_test returning no progress.\n");
	sleep(1);
#endif
	return 0;
    }
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
			        TROVE_context_id context_id,
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
			       context_id,
			       &tmp_count,
			       &vtag_array[i], /* TODO: this doesn't seem right! */
			       (returned_user_ptr_array != NULL) ?  &returned_user_ptr_array[out_count] : NULL,
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

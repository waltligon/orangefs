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
#include <time.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <assert.h>

#include "pvfs2-internal.h"
#include "gossip.h"
#include "pint-perf-counter.h"
#include "pint-event.h"
/* #include "pint-mem.h" obsolete */
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op.h"
#include "dbpf-thread.h"
#include "dbpf-bstream.h"
#include "dbpf-op-queue.h"
#include "dbpf-attr-cache.h"
#include "dbpf-open-cache.h"

#define TROVE_DEFAULT_DB_PAGESIZE 512

#ifdef __PVFS2_TROVE_THREADED__
#include <pthread.h>
#include "dbpf-thread.h"
#include "pint-perf-counter.h"

#include <stdio.h>
#include <string.h>

extern pthread_cond_t dbpf_op_completed_cond;
extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];
#else
extern struct qlist_head dbpf_op_queue;
extern gen_mutex_t dbpf_op_queue_mutex;
#endif
extern gen_mutex_t dbpf_attr_cache_mutex;

int64_t s_dbpf_metadata_writes = 0, s_dbpf_metadata_reads = 0;

extern TROVE_method_callback global_trove_method_callback;
extern struct TROVE_bstream_ops *bstream_method_table[];

static inline void organize_post_op_statistics(
    enum dbpf_op_type op_type, TROVE_op_id op_id)
{
    switch(op_type)
    {
        case KEYVAL_WRITE:
        case KEYVAL_REMOVE_KEY:
        case KEYVAL_WRITE_LIST:
        case KEYVAL_FLUSH:
        case DSPACE_REMOVE:
        case DSPACE_SETATTR:
            UPDATE_PERF_METADATA_WRITE();
            break;
        case KEYVAL_READ:
        case KEYVAL_READ_LIST:
        case KEYVAL_VALIDATE:
        case KEYVAL_ITERATE:
        case KEYVAL_ITERATE_KEYS:
        case DSPACE_ITERATE_HANDLES:
        case DSPACE_VERIFY:
        case DSPACE_GETATTR:
            UPDATE_PERF_METADATA_READ();
            break;
        case BSTREAM_READ_LIST:
            break;
        case BSTREAM_WRITE_LIST:
            break;
        case DSPACE_CREATE:
            UPDATE_PERF_METADATA_WRITE();
            break;
        case DSPACE_CREATE_LIST:
            UPDATE_PERF_METADATA_WRITE();
            break;
        default:
            break;
    }
}

static int dbpf_dspace_create_store_handle(struct dbpf_collection* coll_p,
                                           TROVE_ds_type type,
                                           TROVE_handle new_handle);
static int dbpf_dspace_iterate_handles_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_create_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_create_list_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_remove_list_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_verify_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_getattr_op_svc(struct dbpf_op *op_p);
static int dbpf_dspace_getattr_list_op_svc(struct dbpf_op *op_p);

static int dbpf_dspace_create(TROVE_coll_id coll_id,
                              TROVE_handle handle,
                              TROVE_handle *out_handle,
                              TROVE_ds_type type,
                              TROVE_keyval_s *hint,
                              TROVE_ds_flags flags,
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
    PINT_event_type event_type;
    PINT_event_id event_id = 0;

    gossip_debug(GOSSIP_TROVE_DEBUG, "trove: dbpf_dspace_create handle: %s\n",
                 PVFS_OID_str(&handle));

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_CREATE,
                                           coll_p,
                                           handle,
                                           dbpf_dspace_create_op_svc,
                                           flags,
                                           NULL,
                                           user_ptr,
                                           context_id,
                                           &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_dspace_create_event_id;
    DBPF_EVENT_START(coll_p,
                     q_op_p,
                     event_type,
                     &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     PINT_HINT_GET_OP_ID(hints));

    /* this array is freed in dbpf-op.c:dbpf_queued_op_free, or
     * in dbpf_queue_or_service in the case of immediate completion */
    op_p->u.d_create.handle = handle;
    op_p->u.d_create.out_handle = out_handle;
    op_p->hints = hints;

    op_p->u.d_create.type = type;

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}

static int dbpf_dspace_create_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    TROVE_handle new_handle = TROVE_HANDLE_NULL;
    TROVE_handle cur_handle;

    cur_handle = op_p->u.d_create.handle;

    /* V3 FORCE doesn't make sense any more either handle is given or not */
    /* if ((op_p->flags & TROVE_FORCE_REQUESTED_HANDLE) */

    if (PVFS_OID_cmp(&cur_handle, &TROVE_HANDLE_NULL))
    {
        /*
         * currently the only way to check for an already
         * used handle is to look into the DB - which we
         * will do later, so we test for that error then
         */
        new_handle = cur_handle;
    }
    else
    {
        PVFS_OID_gen(&new_handle);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: new_handle is %s\n",
                 __func__, PVFS_OID_str(&new_handle));
    /*
     * if we got a zero handle, we're either completely out of handles
     * -- or else something terrible has happened
     */
    if (!PVFS_OID_cmp(&new_handle, &TROVE_HANDLE_NULL))
    {
        gossip_err("%s: Error: handle allocator returned a zero handle.\n",
                   __func__);
        return(-TROVE_ENOSPC);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: creating dspace\n", __func__);
    ret = dbpf_dspace_create_store_handle(op_p->coll_p,
                                          op_p->u.d_create.type,
                                          new_handle);
    if(ret < 0)
    {
        PVFS_OID_init(&new_handle); /* clear out the variable */
        gossip_err("%s: Error: create_store returned error %d. \n",
                    __func__, ret);
        return(ret);
    }

    *op_p->u.d_create.out_handle = new_handle;

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_SUB);

    *op_p->u.d_create.out_handle = new_handle;
    return DBPF_OP_COMPLETE;
}

static int dbpf_dspace_create_list(TROVE_coll_id coll_id,
                                   TROVE_handle *handle_array,
                                   TROVE_handle *out_handle_array,
                                   int count,
                                   TROVE_ds_type type,
                                   TROVE_keyval_s *hint,
                                   TROVE_ds_flags flags,
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
    PINT_event_type event_type;
    PINT_event_id event_id = 0;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: \n", __func__);

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_CREATE,
                                           coll_p,
                                           TROVE_HANDLE_NULL,
                                           dbpf_dspace_create_list_op_svc,
                                           flags,
                                           NULL,
                                           user_ptr,
                                           context_id,
                                           &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_dspace_create_event_id;
    DBPF_EVENT_START(coll_p,
                     q_op_p,
                     event_type,
                     &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     PINT_HINT_GET_OP_ID(hints));

    /* this array is freed in dbpf-op.c:dbpf_queued_op_free, or
     * in dbpf_queue_or_service in the case of immediate completion */
    op_p->u.d_create_list.count = count;
    op_p->u.d_create_list.handle_array =
                    malloc(count * sizeof(TROVE_handle));
    op_p->u.d_create_list.out_handle_array = out_handle_array;

    if (op_p->u.d_create_list.handle_array == NULL)
    {
        return -TROVE_ENOMEM;
    }

    memcpy(op_p->u.d_create_list.handle_array,
           handle_array,
           count * sizeof(TROVE_handle));

    op_p->u.d_create_list.type = type;

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p,
                                 q_op_p,
                                 coll_p,
                                 out_op_id_p,
                                 event_type,
                                 event_id);
}

static int dbpf_dspace_create_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    TROVE_handle new_handle = TROVE_HANDLE_NULL;
    struct dbpf_data key;
    int i;
    int j;

    for(i = 0; i < op_p->u.d_create_list.count; i++)
    {
        new_handle = op_p->u.d_create_list.handle_array[i];

        if (!PVFS_OID_cmp(&new_handle, &TROVE_HANDLE_NULL))
        {
            /*
             * user passed in NULL so allocate a handle
             */
            PVFS_OID_gen(&new_handle);
        }

        /*
         *  if we got a zero handle, we're either completely out of handles
         *  -- or else something terrible has happened
         */
        if (!PVFS_OID_cmp(&new_handle, &TROVE_HANDLE_NULL))
        {
            gossip_err("Error: handle allocator returned a zero handle.\n");
            return(-TROVE_ENOSPC);
        }

        ret = dbpf_dspace_create_store_handle(op_p->coll_p, 
                                              op_p->u.d_create.type,
                                              new_handle);
        if(ret < 0)
        {
            /* release any handles we grabbed so far */
            for(j = 0; j <= i; j++)
            {
                if(PVFS_OID_cmp(&op_p->u.d_create_list.out_handle_array[j],
                                &TROVE_HANDLE_NULL))
                {
                    key.data = &op_p->u.d_create_list.out_handle_array[j];
                    key.len = sizeof(TROVE_handle);
                    dbpf_db_del(op_p->coll_p->ds_db, &key);

                    PVFS_OID_init(&op_p->u.d_create_list.out_handle_array[j]);
                }
            }
            return(ret);
        }
        op_p->u.d_create_list.out_handle_array[i] = new_handle;
    }

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_SUB);

    return DBPF_OP_COMPLETE;
}

static int dbpf_dspace_remove_list(TROVE_coll_id coll_id,
                                   TROVE_handle* handle_array,
                                   TROVE_ds_state *error_array,
                                   int count,
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

    gossip_debug(GOSSIP_TROVE_DEBUG, "trove: dbpf_dspace_remove_list\n");

    dbpf_queued_op_init(q_op_p,
                        DSPACE_REMOVE_LIST,
                        TROVE_HANDLE_NULL,
                        coll_p,
                        dbpf_dspace_remove_list_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.d_remove_list.count = count;
    q_op_p->op.u.d_remove_list.handle_array = handle_array;
    q_op_p->op.u.d_remove_list.error_p = error_array;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_dspace_remove(TROVE_coll_id coll_id,
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

    gossip_debug(GOSSIP_TROVE_DEBUG, "trove: dbpf_dspace_remove handle: %s\n",
                 PVFS_OID_str(&handle));

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_REMOVE,
                                           coll_p,
                                           handle,
                                           dbpf_dspace_remove_op_svc,
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

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int remove_one_handle(TROVE_object_ref ref,
                             struct dbpf_collection *coll_p)
{
    int count = 0;
    int ret = -TROVE_EINVAL;
    struct dbpf_data key;

    key.data = &ref.handle;
    key.len = sizeof(TROVE_handle);

    ret = dbpf_db_del(coll_p->ds_db, &key);
    if (ret == TROVE_ENOENT)
    {
        gossip_err("tried to remove non-existant dataspace\n");
    }
    else if (ret != 0)
    {
        gossip_err("TROVE:DBPF: dbpf_dspace_remove");
        //ret = -ret;
        goto return_error;
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "removed dataspace with handle %s\n",
                     PVFS_OID_str(&ref.handle));
    }

    /* if this attr is in the dbpf attr cache, remove it */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_remove(ref);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    /* remove bstream if it exists.  Not a fatal
     * error if this fails (may not have ever been created)
     */
    ret = dbpf_open_cache_remove(coll_p->coll_id, ref.handle);

    /* remove the keyval entries for this handle if any exist.
     * this way seems a bit messy to me, i.e. we're operating
     * on keyval databases directly here instead of going through
     * the trove keyval interfaces.  It does allow us to perform the cleanup
     * of a handle without having to post more operations though.
     */
    ret = PINT_dbpf_keyval_iterate(coll_p->keyval_db,
                                   ref.handle,
                                   DBPF_ATTRIBUTE_TYPE,
                                   coll_p->pcache,
                                   NULL,
                                   NULL,
                                   &count,
                                   TROVE_ITERATE_START,
                                   PINT_dbpf_dspace_remove_keyval);
    if(ret != 0 && ret != -TROVE_ENOENT)
    {
        goto return_error;
    }

    /* return handle to free list */
    PVFS_OID_init(&ref.handle); /* clear out the variable */
    return 0;

return_error:
    return ret;
}


static int dbpf_dspace_remove_list_op_svc(struct dbpf_op *op_p)
{
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    int ret = -TROVE_EINVAL;
    int i;

    for(i = 0; i < op_p->u.d_remove_list.count; i++)
    {
        ref.handle = op_p->u.d_remove_list.handle_array[i];
        ref.fs_id = op_p->coll_p->coll_id;
        
        /* if error_p is NULL, assume that the caller is ignoring errors */
        if(op_p->u.d_remove_list.error_p)
        {
            op_p->u.d_remove_list.error_p[i] = 
                            remove_one_handle(ref, op_p->coll_p);
        }
        else
        {
                remove_one_handle(ref, op_p->coll_p);
        }
    }

    /* we still do a non-coalesced sync of the keyval db here
     * because we're in a dspace operation
     */
    DBPF_DB_SYNC_IF_NECESSARY(op_p, op_p->coll_p->keyval_db, ret);
    if(ret < 0)
    {
        return(ret);
    }

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_SUB);

    return DBPF_OP_COMPLETE;
}


static int dbpf_dspace_remove_op_svc(struct dbpf_op *op_p)
{
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    int ret = -TROVE_EINVAL;

    ret = remove_one_handle(ref, op_p->coll_p);
    if(ret < 0)
    {
        return(ret);
    }

    /* we still do a non-coalesced sync of the keyval db here
     * because we're in a dspace operation
     */
    DBPF_DB_SYNC_IF_NECESSARY(op_p, op_p->coll_p->keyval_db, ret);
    if(ret < 0)
    {
        return(ret);
    }

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_SUB);

    return DBPF_OP_COMPLETE;
}

int PINT_dbpf_dspace_remove_keyval(dbpf_cursor *dbc,
                                   TROVE_handle handle,
                                   TROVE_keyval_s *key,
                                   TROVE_keyval_s *val)
{
    int ret;
    ret = dbpf_db_cursor_del(dbc);
    if(ret != 0)
    {
        ret = -ret;
    }
    return ret;
}

static int dbpf_dspace_iterate_handles(TROVE_coll_id coll_id,
                                       TROVE_ds_position *position_p,
                                       TROVE_handle *handle_array,
                                       int *inout_count_p,
                                       TROVE_ds_flags flags,
                                       TROVE_vtag_s *vtag,
                                       void *user_ptr,
                                       TROVE_context_id context_id,
                                       TROVE_op_id *out_op_id_p)
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

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_ITERATE_HANDLES,
                                           coll_p,
                                           TROVE_HANDLE_NULL,
                                           dbpf_dspace_iterate_handles_op_svc,
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
    op_p->u.d_iterate_handles.handle_array = handle_array;
    op_p->u.d_iterate_handles.position_p = position_p;
    op_p->u.d_iterate_handles.count_p = inout_count_p;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_dspace_iterate_handles_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    dbpf_cursor *dbc = NULL;
    struct dbpf_data key, data;
    TROVE_handle dummy_handle;
    TROVE_ds_attributes attr;

    if (*op_p->u.d_iterate_handles.position_p == TROVE_ITERATE_END)
    {
        /* already hit end of keyval space; return 1 */
        *op_p->u.d_iterate_handles.count_p = 0;
        return DBPF_OP_COMPLETE;
        //return 1;
    }

    /* get a cursor */
    ret = dbpf_db_cursor(op_p->coll_p->ds_db, &dbc, 1);
    if (ret != 0)
    {
        //ret = -ret;
        gossip_err("failed to get a cursor\n");
        goto return_error;
    }

    /* we have two choices here: 'seek' to a specific key by either a:
     * specifying a key or b: using record numbers in the db.  record
     * numbers will serialize multiple modification requests. We are
     * going with record numbers for now.
     */

    /* an uninitialized cursor will start reading at the beginning of
     * the database (first record) when used with DB_NEXT, so we don't
     * need to position with DB_FIRST.
     */
    if (*op_p->u.d_iterate_handles.position_p != TROVE_ITERATE_START)
    {
        /* we need to position the cursor before we can read new
         * entries.  we will go ahead and read the first entry as
         * well, so that we can use the same loop below to read the
         * remainder in this or the above case.
         */

        memcpy(&dummy_handle,
               op_p->u.d_iterate_handles.position_p,
               sizeof(TROVE_ds_position));
        key.data  = &dummy_handle;
        key.len = sizeof dummy_handle;

        data.data = &attr;
        data.len = sizeof(attr);

        ret = dbpf_db_cursor_get(dbc,
                                 &key,
                                 &data,
                                 DBPF_DB_CURSOR_SET_RANGE,
                                 sizeof dummy_handle);
        if (ret == TROVE_ENOENT)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            //ret = -ret;
            gossip_err("failed to set cursor position: %llu\n",
                       llu(*op_p->u.d_iterate_handles.position_p));
            goto return_error;
        }
    }
    else
    {
        /* start reading at the beginning of the db */
        /* note: key field is ignored by c_get in this case */
        key.data = &dummy_handle;
        key.len =  sizeof(TROVE_handle);

        data.data = &attr;
        data.len = sizeof(attr);

        ret = dbpf_db_cursor_get(dbc,
                                 &key,
                                 &data,
                                 DBPF_DB_CURSOR_FIRST,
                                 sizeof(TROVE_handle));
        if (ret == -TROVE_ENOENT)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            //ret = -ret;
            gossip_err("failed to set cursor position at handle: %s\n",
                   PVFS_OID_str((TROVE_handle *)op_p->u.d_iterate_handles.position_p));
            goto return_error;
        }
    }

    op_p->u.d_iterate_handles.handle_array[i] = dummy_handle;
    ++i;

    key.data = &dummy_handle;
    key.len = sizeof dummy_handle;
    data.data = &attr;
    data.len = sizeof attr;

    while(i < *op_p->u.d_iterate_handles.count_p)
    {
        ret = dbpf_db_cursor_get(dbc,
                                 &key,
                                 &data,
                                 DBPF_DB_CURSOR_NEXT,
                                 sizeof dummy_handle);
        if(ret == -TROVE_ENOENT)
        {
            goto return_ok;
        }
        else if(ret < 0)
        {
            //ret = -ret;
            gossip_err("c_get failed on iteration %d\n", i);
            goto return_error;
        }
        op_p->u.d_iterate_handles.handle_array[i++] = dummy_handle;
    }

    /* get the record number to return.
     *
     * note: key field is ignored by c_get in this case
     */
    key.data = &dummy_handle;
    key.len = sizeof(dummy_handle);

    data.data = &attr;
    data.len = sizeof(attr);

    ret = dbpf_db_cursor_get(dbc,
                             &key,
                             &data,
                             DBPF_DB_CURSOR_NEXT,
                             sizeof dummy_handle);
    if (ret == -TROVE_ENOENT)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "iterate -- notfound\n");
        goto return_ok;
    }
    else if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "iterate -- some other failure @ recno\n");
        //ret = -ret;
        goto return_error;
    }

return_ok:
    if (ret == -TROVE_ENOENT)
    {
        /* if off the end of the database, return TROVE_ITERATE_END */
        *op_p->u.d_iterate_handles.position_p = TROVE_ITERATE_END;
    }
    /* 'position' points to record we just read, or is set to END */

    *op_p->u.d_iterate_handles.count_p = i;

    dbpf_db_cursor_close(dbc);

    return DBPF_OP_COMPLETE;
    //return 1;

return_error:
    *op_p->u.d_iterate_handles.count_p = i;
    PVFS_perror_gossip("dbpf_dspace_iterate_handles_op_svc", ret);

    dbpf_db_cursor_close(dbc);

    return ret;
}

static int dbpf_dspace_verify(TROVE_coll_id coll_id,
                              TROVE_handle handle,
                              TROVE_ds_type *type_p,
                              TROVE_ds_flags flags,
                              void *user_ptr,
                              TROVE_context_id context_id,
                              TROVE_op_id *out_op_id_p,
                              PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    int ret;

    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_VERIFY,
                                           coll_p,
                                           handle,
                                           dbpf_dspace_verify_op_svc,
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
    op_p->u.d_verify.type_p = type_p;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_dspace_verify_op_svc(struct dbpf_op *op_p)
{
    struct dbpf_data key, data;
    TROVE_ds_attributes attr;
    int ret;

    key.data = &op_p->handle;
    key.len = sizeof(TROVE_handle);

    data.data = &attr;
    data.len = sizeof(attr);

    /* check to see if dspace handle is used (ie. object exists) */
    ret = dbpf_db_get(op_p->coll_p->ds_db, &key, &data);
    if (ret)
    {
        /* error in accessing database */
        //ret = -ret;
        goto return_error;
    }

    /* copy type value back into user's memory */
    *op_p->u.d_verify.type_p = attr.type;

    return DBPF_OP_COMPLETE;

return_error:
    return ret;
}

static int dbpf_dspace_getattr(TROVE_coll_id coll_id,
                               TROVE_handle handle,
                               TROVE_ds_attributes_s *ds_attr_p,
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
    TROVE_object_ref ref = {handle, coll_id};
    int ret;
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    gossip_debug(GOSSIP_TROVE_DEBUG, "trove: dbpf_dspace_getattr handle: %s\n",
                 PVFS_OID_str(&handle));

    /* fast path cache hit; skips queueing */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    if (dbpf_attr_cache_ds_attr_fetch_cached_data(ref, ds_attr_p) == 0)
    {
#if 0
        gossip_debug(
            GOSSIP_TROVE_DEBUG, "ATTRIB: retrieved "
            "attributes from CACHE for key %llu\n  uid = %d, mode = %d, "
            "type = %d, dfile_count = %d, dist_size = %d\n",
            llu(handle), (int)ds_attr_p->uid, (int)ds_attr_p->mode,
            (int)ds_attr_p->type, (int)ds_attr_p->dfile_count,
            (int)ds_attr_p->dist_size);
#endif
        gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                     "dspace_getattr fast path attr cache hit on %s\n",
                     PVFS_OID_str(&handle));
        if(ds_attr_p->type == PVFS_TYPE_METAFILE)
        {
            gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                         "(dfile_count=%d, dist_size=%d)",
                         ds_attr_p->u.metafile.dfile_count,
                         ds_attr_p->u.metafile.dist_size);
        }
        else if(ds_attr_p->type == PVFS_TYPE_DATAFILE)
        {
            gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                         "(bstream_size=%lld)\n",
                         lld(ds_attr_p->u.datafile.b_size));
        }
        else if(ds_attr_p->type == PVFS_TYPE_DIRDATA)
        {
            gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                         "(dir_count=%llu)\n",
                         llu(ds_attr_p->u.dirdata.dirent_count));
        }

        UPDATE_PERF_METADATA_READ();
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
        return DBPF_OP_COMPLETE;
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_GETATTR,
                                           coll_p,
                                           handle,
                                           dbpf_dspace_getattr_op_svc,
                                           flags,
                                           NULL,
                                           user_ptr,
                                           context_id,
                                           &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_dspace_getattr_event_id;
    DBPF_EVENT_START(coll_p,
                     q_op_p,
                     event_type,
                     &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints));

   /* initialize op-specific members */
    op_p->u.d_getattr.attr_p = ds_attr_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p,
                                 q_op_p,
                                 coll_p,
                                 out_op_id_p,
                                 event_type,
                                 event_id);
}

static int dbpf_dspace_getattr_list(TROVE_coll_id coll_id,
                                    int nhandles,
                                    TROVE_handle *handle_array,
                                    TROVE_ds_attributes_s *ds_attr_p,
                                    TROVE_ds_state *error_array,
                                    TROVE_ds_flags flags,
                                    void *user_ptr,
                                    TROVE_context_id context_id,
                                    TROVE_op_id *out_op_id_p,
                                    PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    TROVE_object_ref ref;
    int i;
    int cache_hits = 0; 

    gen_mutex_lock(&dbpf_attr_cache_mutex);
    /* go ahead and try to hit attr cache for all handles up front */ 
    for (i = 0; i < nhandles; i++) 
    {
        ref.handle = handle_array[i];
        ref.fs_id = coll_id;

        if (dbpf_attr_cache_ds_attr_fetch_cached_data(ref, &ds_attr_p[i]) == 0)
        {
#if 0
            gossip_debug(
                GOSSIP_TROVE_DEBUG, "ATTRIB: retrieved "
                "attributes from CACHE for key %llu\n  uid = %d, mode = %d, "
                "type = %d, dfile_count = %d, dist_size = %d\n",
                llu(handle), (int)ds_attr_p->uid, (int)ds_attr_p->mode,
                (int)ds_attr_p->type, (int)ds_attr_p->dfile_count,
                (int)ds_attr_p->dist_size);
#endif
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "%s: fast path attr cache hit on %s, "
                         "uid=%d, mode=%d, type=%d\n",
                         __func__,
                         PVFS_OID_str(&handle_array[i]),
                         (int)ds_attr_p[i].uid,
                         (int)ds_attr_p[i].mode,
                         (int)ds_attr_p[i].type);

            if(ds_attr_p[i].type == PVFS_TYPE_METAFILE)
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "\tdfile_count = %d, dist_size = %d\n",
                             ds_attr_p[i].u.metafile.dfile_count,
                             ds_attr_p[i].u.metafile.dist_size);
            }
            else if(ds_attr_p[i].type == PVFS_TYPE_DATAFILE)
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "\tbstream_size = %llu\n",
                             llu(ds_attr_p[i].u.datafile.b_size));
            }
            else if(ds_attr_p[i].type == PVFS_TYPE_DIRDATA)
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "\tcount = %llu\n",
                             llu(ds_attr_p[i].u.dirdata.dirent_count));
            }

            UPDATE_PERF_METADATA_READ();
            error_array[i] = 0;
            cache_hits++;
        }
        else
        {
            /* no hit; mark attr entry so that we can detect that in the
             * service routine
             */
            ds_attr_p[i].type = PVFS_TYPE_NONE;
        }
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    /* All handles hit in the cache, return */
    if (cache_hits == nhandles) 
    {
        gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                     "%s: serviced entirely from attr cache.\n", __func__);
        return 1;
    }

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

    gossip_debug(GOSSIP_TROVE_DEBUG, "trove: dbpf_dspace_getattr_list\n");

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        DSPACE_GETATTR_LIST,
                        TROVE_HANDLE_NULL,
                        coll_p,
                        dbpf_dspace_getattr_list_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.d_getattr_list.count = nhandles;
    q_op_p->op.u.d_getattr_list.handle_array = handle_array;
    q_op_p->op.u.d_getattr_list.attr_p = ds_attr_p;
    q_op_p->op.u.d_getattr_list.error_p = error_array;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}


static int dbpf_dspace_setattr(TROVE_coll_id coll_id,
                               TROVE_handle handle,
                               TROVE_ds_attributes_s *ds_attr_p,
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
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    gossip_debug(GOSSIP_TROVE_DEBUG, "trove: dbpf_dspace_setattr handle: %s\n",
                 PVFS_OID_str(&handle));

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(&op,
                                           &q_op_p,
                                           DSPACE_SETATTR,
                                           coll_p,
                                           handle,
                                           dbpf_dspace_setattr_op_svc,
                                           flags,
                                           NULL,
                                           user_ptr,
                                           context_id,
                                           &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_dspace_setattr_event_id;
    DBPF_EVENT_START(coll_p,
                     q_op_p,
                     event_type,
                     &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints));

   /* initialize op-specific members */
    ds_attr_p->fs_id = coll_id;
    ds_attr_p->handle = handle;
    op_p->u.d_setattr.attr_p = ds_attr_p;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p,
                                 q_op_p,
                                 coll_p,
                                 out_op_id_p,
                                 event_type,
                                 event_id);
}

int dbpf_dspace_attr_set(struct dbpf_collection *coll_p,
                         TROVE_object_ref ref,
                         TROVE_ds_attributes *attr)
{
    int ret;
    struct dbpf_data key, data;

    key.data = &ref.handle;
    key.len = sizeof(TROVE_handle);

    data.data = attr;
    data.len = sizeof(*attr);

    ret = dbpf_db_put(coll_p->ds_db, &key, &data);
    if (ret != 0)
    {
        gossip_err("TROVE:DBPF: dspace dbpf_db_put setattr");
        return ret;
        //return -ret;
    }

    /* now that the disk is updated, update the cache if necessary */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_ds_attr_update_cached_data(ref, attr);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    return 0;
}

int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    ret = dbpf_dspace_attr_set(op_p->coll_p, ref, op_p->u.d_setattr.attr_p);
    if(ret != 0)
    {
        return ret;
    }

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_METADATA_DSPACE_OPS,
                    1,
                    PINT_PERF_SUB);

    return DBPF_OP_COMPLETE;
}

int dbpf_dspace_attr_get(struct dbpf_collection *coll_p,
                         TROVE_object_ref ref,
                         TROVE_ds_attributes *attr)
{
    struct dbpf_data key, data;
    int ret;

    key.data = &ref.handle;
    key.len = sizeof(ref.handle);

    data.data = attr;
    data.len = sizeof(*attr);

    ret = dbpf_db_get(coll_p->ds_db, &key, &data);
    if (ret)
    {
        if (ret != TROVE_ENOENT)
        {
            gossip_err("TROVE:DBPF: dspace dbpf_db_get");
        }
        return ret;
        //return -ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "ATTRIB: retrieved attributes "
                 "from DISK for key %s\n\tuid = %d, mode = %d, type = %d\n",
                 PVFS_OID_str(&ref.handle),
                 (int)attr->uid,
                 (int)attr->mode,
                 (int)attr->type);

    if(attr->type == PVFS_TYPE_METAFILE)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "\tdfile_count = %d, dist_size = %d\n",
                     attr->u.metafile.dfile_count,
                     attr->u.metafile.dist_size);
    }
    else if(attr->type == PVFS_TYPE_DATAFILE)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "\tbstream_size = %llu\n",
                     llu(attr->u.datafile.b_size));
    }
    else if(attr->type == PVFS_TYPE_DIRDATA)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "\tcount = %llu\n",
                     llu(attr->u.dirdata.dirent_count));
    }

    /* add retrieved ds_attr to dbpf_attr cache here */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_insert(ref, attr);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    return 0;
}

static int dbpf_dspace_getattr_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    ret = dbpf_dspace_attr_get(op_p->coll_p, ref, op_p->u.d_getattr.attr_p);
    if(ret < 0)
    {
        return(ret);
    }

    return DBPF_OP_COMPLETE;
    //return 1;
}

static int dbpf_dspace_getattr_list_op_svc(struct dbpf_op *op_p)
{
    int i;
    TROVE_object_ref ref;

    for (i = 0; i < op_p->u.d_getattr_list.count; i++)
    {
        if(op_p->u.d_getattr_list.attr_p[i].type != PVFS_TYPE_NONE)
        {
            /* we already serviced this one from the cache at post time;
             * skip to the next element
             */
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                "dbpf_dspace_getattr_list_op_svc() skipping "
                "element %d resolved from cache.\n", i);
            continue;
        }

        ref.handle = op_p->u.d_getattr_list.handle_array[i];
        ref.fs_id = op_p->coll_p->coll_id;

        op_p->u.d_getattr_list.error_p[i] = dbpf_dspace_attr_get(
                                            op_p->coll_p,
                                            ref,
                                            &op_p->u.d_getattr_list.attr_p[i]);
    }

    return DBPF_OP_COMPLETE;
    //return 1;
}

/*
  FIXME: it's possible to have a non-threaded version of this, but
  it's not implemented right now
*/
static int dbpf_dspace_cancel(TROVE_coll_id coll_id,
                              TROVE_op_id id,
                              TROVE_context_id context_id)
{
    int ret = -TROVE_ENOSYS;
#ifdef __PVFS2_TROVE_THREADED__
    int state = 0;
    dbpf_queued_op_t *cur_op = NULL;
#endif

    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_dspace_cancel called for "
                 "id %llu.\n", llu(id));

#ifdef __PVFS2_TROVE_THREADED__

    cur_op = id_gen_fast_lookup(id);
    if (cur_op == NULL)
    {
        gossip_err("Invalid operation to test against\n");
        return -TROVE_EINVAL;
    }

    /*
     * for bstream ops, call the bstream cancel instead.  for other ops,
     * there's not much we can do other than let the op
     * complete normally
     */
    if(cur_op->op.type >= BSTREAM_OP_TYPE &&
       cur_op->op.type < KEYVAL_OP_TYPE)
    {
        int method_id = global_trove_method_callback(coll_id);
        if(method_id < 0)
        {
            return -TROVE_EINVAL;
        }

        if(bstream_method_table[method_id]->bstream_cancel)
        {
            return bstream_method_table[method_id]->bstream_cancel(
                coll_id, id, context_id);
        }
        else
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "Trove cancellation is not supported "
                         "for this operation type; ignoring.\n");
            return(0);

        }
    }

    /* check the state of the current op to see if it's completed */
    gen_mutex_lock(&cur_op->mutex);
    state = cur_op->op.state;
    gen_mutex_unlock(&cur_op->mutex);

    gossip_debug(GOSSIP_TROVE_DEBUG, "got cur_op %p\n", cur_op);

    switch(state)
    {
        case OP_QUEUED:
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "op %p is queued: handling\n", cur_op);

                /* dequeue and complete the op in canceled state */
                cur_op->op.state = OP_IN_SERVICE;
                dbpf_queued_op_put_and_dequeue(cur_op);
                assert(cur_op->op.state == OP_DEQUEUED);

                cur_op->state = 0;
                /* this is a macro defined in dbpf-thread.h */
                dbpf_queued_op_complete(cur_op, OP_CANCELED);

                gossip_debug(GOSSIP_TROVE_DEBUG, "op %p is canceled\n", cur_op);
                ret = 0;
            }
            break;
        case OP_IN_SERVICE:
            gossip_debug(GOSSIP_TROVE_DEBUG, "op is in service: ignoring "
                         "operation type %d\n", cur_op->op.type);
            ret = 0;
            break;
        case OP_COMPLETED:
        case OP_CANCELED:
            /* easy cancelation case; do nothing */
            gossip_debug(GOSSIP_TROVE_DEBUG, "op is completed: ignoring\n");
            ret = 0;
            break;
        default:
            gossip_err("Invalid dbpf_op state found (%d)\n", state);
            gossip_err("   from op type: %d\n", cur_op->op.type);
            assert(0);
    }
#endif
    return ret;
}


/* dbpf_dspace_test()
 *
 * Returns TROVE_OP_BUSY(0) if not completed, or TROVE_OP_COMPLETE(1)
 * if completed (successfully or with error).
 *
 * The error state of the completed operation is returned via the
 * state_p, more to follow on this...
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
                            TROVE_ds_state *state_p,
                            int max_idle_time_ms)
{
    int ret = -TROVE_EINVAL;
    dbpf_queued_op_t *cur_op = NULL;
#ifdef __PVFS2_TROVE_THREADED__
    int state = 0;
    gen_mutex_t *context_mutex = NULL;
#endif

    *out_count_p = 0;
#ifdef __PVFS2_TROVE_THREADED__
    assert(dbpf_completion_queue_array[context_id]);
    context_mutex = &dbpf_completion_queue_array_mutex[context_id];
    assert(context_mutex);
    cur_op = id_gen_fast_lookup(id);
    if (cur_op == NULL)
    {
        gossip_err("%s: Invalid operation %ld to test against\n", __func__, id);
        return ret;
    }

    gen_mutex_lock(context_mutex);

    /* check the state of the current op to see if it's completed */
    /* where is state changed? */
    gen_mutex_lock(&cur_op->mutex);
    state = cur_op->op.state;
    gen_mutex_unlock(&cur_op->mutex);

    /* this appears to ignore errors the comments say should be treated
     * as complete, unless we expect the op to do that translation
     */
    /* if the op is not completed, wait for up to max_idle_time_ms */
    if ((state != OP_COMPLETED) && (state != OP_CANCELED))
    {
        struct timeval base;
        struct timespec wait_time;

        /* replace with func call */
        /* compute how long to wait */
        gettimeofday(&base, NULL);
        wait_time.tv_sec = base.tv_sec + (max_idle_time_ms / 1000);
        wait_time.tv_nsec = base.tv_usec * 1000 + 
                        ((max_idle_time_ms % 1000) * 1000000);
        if (wait_time.tv_nsec > 1000000000)
        {
            wait_time.tv_nsec = wait_time.tv_nsec - 1000000000;
            wait_time.tv_sec++;
        }

        ret = pthread_cond_timedwait(&dbpf_op_completed_cond,
                                     context_mutex,
                                     &wait_time);

        if (ret == ETIMEDOUT)
        {
            goto op_not_completed;
        }
        else
        {
            /* some op completed, check if it's the one we're testing */
            gen_mutex_lock(&cur_op->mutex);
            state = cur_op->op.state;
            gen_mutex_unlock(&cur_op->mutex);

            if ((state == OP_COMPLETED) || (state == OP_CANCELED))
            {
                goto op_completed;
            }
            goto op_not_completed;
        }
    }
    else
    {
    op_completed:
        assert(!dbpf_op_queue_empty(dbpf_completion_queue_array[context_id]));

        /* pull the op out of the context specific completion queue */
        dbpf_op_queue_remove(cur_op);
        gen_mutex_unlock(context_mutex);

        *out_count_p = 1;
        *state_p = cur_op->state;
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: op %ld state set to %s\n",
                         __func__, cur_op->op.id, dbpf_op_type_to_str(cur_op->state));

        if (returned_user_ptr_p != NULL)
        {
            *returned_user_ptr_p = cur_op->op.user_ptr;
        }

        organize_post_op_statistics(cur_op->op.type, cur_op->op.id);
        dbpf_queued_op_free(cur_op);
        return TROVE_OP_COMPLETE;
    }

op_not_completed:
    gen_mutex_unlock(context_mutex);
    return TROVE_OP_BUSY;
    //return 0;

#else

    /* map to queued operation */
    ret = dbpf_queued_op_try_get(id, &cur_op);
    switch (ret)
    {
        case DBPF_QUEUED_OP_INVALID:
            return -TROVE_EINVAL;
        case DBPF_QUEUED_OP_BUSY:
            *out_count_p = 0;
            return TROVE_OP_BUSY;
            //return 0;
        case DBPF_QUEUED_OP_SUCCESS:
            break;
    }
    
    ret = cur_op->op.svc_fn(&(cur_op->op));

    if (ret != DBPF_OP_BUSY)
    {
        /* operation is done and we are telling the caller;
         * ok to pull off queue now.
         *
         * returns error code from operation in region pointed
         * to by state_p.
         */
        *out_count_p = 1;

        *state_p = (ret == 1) ? 0 : ret;

        if (returned_user_ptr_p != NULL)
        {
            *returned_user_ptr_p = cur_op->op.user_ptr;
        }

        organize_post_op_statistics(cur_op->op.type, cur_op->op.id);
        dbpf_queued_op_put_and_dequeue(cur_op);
        dbpf_queued_op_free(cur_op);
        return DBPF_OP_COMPLETE;
        //return 1;
    }

    dbpf_queued_op_put(cur_op, 0);
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "dbpf_dspace_test returning no progress.\n");
    usleep((max_idle_time_ms * 1000));
    return DBPF_OP_BUSY;
    //return 0;
#endif
}

static int dbpf_dspace_testcontext(TROVE_coll_id coll_id,
                                   TROVE_op_id *ds_id_array,
                                   int *inout_count_p,
                                   TROVE_ds_state *state_array,
                                   void** user_ptr_array,
                                   int max_idle_time_ms,
                                   TROVE_context_id context_id)
{
    int ret = 0;
    dbpf_queued_op_t *cur_op = NULL;
#ifdef __PVFS2_TROVE_THREADED__
    int out_count = 0, limit = *inout_count_p;
    gen_mutex_t *context_mutex = NULL;
    void **user_ptr_p = NULL;

    assert(dbpf_completion_queue_array[context_id]);

    context_mutex = &dbpf_completion_queue_array_mutex[context_id];

    assert(inout_count_p);
    *inout_count_p = 0;

    /*
      check completion queue for any completed ops and return
      them in the provided ds_id_array (up to inout_count_p).
      otherwise, cond_timedwait for max_idle_time_ms.

      we will only sleep if there is nothing to do; otherwise 
      we return whatever we find ASAP
    */
    gen_mutex_lock(context_mutex);
    if (dbpf_op_queue_empty(dbpf_completion_queue_array[context_id]))
    {
        struct timeval base;
        struct timespec wait_time;

        /* compute how long to wait */
        gettimeofday(&base, NULL);
        wait_time.tv_sec = base.tv_sec + (max_idle_time_ms / 1000);
        wait_time.tv_nsec = base.tv_usec * 1000 + 
                        ((max_idle_time_ms % 1000) * 1000000);
        if (wait_time.tv_nsec > 1000000000)
        {
            wait_time.tv_nsec = wait_time.tv_nsec - 1000000000;
            wait_time.tv_sec++;
        }

        ret = pthread_cond_timedwait(&dbpf_op_completed_cond,
                                     context_mutex,
                                     &wait_time);

        if (ret == ETIMEDOUT)
        {
            /* we timed out without being awoken- this means there is
             * no point in checking the completion queue, we should just
             * return
             */
            gen_mutex_unlock(context_mutex);
            *inout_count_p = 0;
            return(0);
        }
    }

    while(!dbpf_op_queue_empty(dbpf_completion_queue_array[context_id]) &&
          (out_count < limit))
    {
        cur_op = dbpf_op_queue_shownext(
                        dbpf_completion_queue_array[context_id]);
        assert(cur_op);

        /* pull the op out of the context specific completion queue */
        dbpf_op_queue_remove(cur_op);

        state_array[out_count] = cur_op->state;

        user_ptr_p = &user_ptr_array[out_count];
        if (user_ptr_p != NULL)
        {
            *user_ptr_p = cur_op->op.user_ptr;
        }
        ds_id_array[out_count] = cur_op->op.id;

        if(cur_op->event_type == trove_dbpf_read_event_id ||
           cur_op->event_type == trove_dbpf_write_event_id)
        {
            DBPF_EVENT_END(cur_op->event_type, cur_op->event_id);
        }
        organize_post_op_statistics(cur_op->op.type, cur_op->op.id);
        dbpf_queued_op_free(cur_op);

        out_count++;
    }
    gen_mutex_unlock(context_mutex);

    *inout_count_p = out_count;
    ret = 0;
#else
    /*
      without threads, we're going to just test the
      first operation we can get our hands on in the
      operation queue.  so we grab the trove id of the
      next op from the op queue and test it for
      completion here.
    */
    gen_mutex_lock(&dbpf_op_queue_mutex);
    cur_op = dbpf_op_queue_shownext(&dbpf_op_queue);
    gen_mutex_unlock(&dbpf_op_queue_mutex);

    if (cur_op)
    {
        ret = dbpf_dspace_test(coll_id,
                               cur_op->op.id,
                               context_id,
                               inout_count_p,
                               NULL,
                               &(user_ptr_array[0]),
                               &(state_array[0]),
                               max_idle_time_ms);
        if (ret == 1)
        {
            ds_id_array[0] = cur_op->op.id;
        }
    }
    else
    {
        /*
          if there's no op to service, just return after wasting time
          away for now
        */
        *inout_count_p = 0;
        usleep((max_idle_time_ms * 1000));
    }
#endif
    return ret;
}

/* dbpf_dspace_testsome()
 *
 * Returns 0 if nothing completed, 1 if something is completed
 * (successfully or with error).
 *
 * The error state of the completed operation is returned via the
 * state_p.
 */
static int dbpf_dspace_testsome(TROVE_coll_id coll_id,
                                TROVE_context_id context_id,
                                TROVE_op_id *ds_id_array,
                                int *inout_count_p,
                                int *out_index_array,
                                TROVE_vtag_s *vtag_array,
                                void **returned_user_ptr_array,
                                TROVE_ds_state *state_array,
                                int max_idle_time_ms)
{
    int i = 0, out_count = 0, ret = 0;
#ifdef __PVFS2_TROVE_THREADED__
    int state = 0, wait = 0;
    dbpf_queued_op_t *cur_op = NULL;
    gen_mutex_t *context_mutex = NULL;
    void **returned_user_ptr_p = NULL;

    assert(dbpf_completion_queue_array[context_id]);

    context_mutex = &dbpf_completion_queue_array_mutex[context_id];

    gen_mutex_lock(context_mutex);
  scan_for_completed_ops:
#endif

    assert(inout_count_p);
    for (i = 0; i < *inout_count_p; i++)
    {
#ifdef __PVFS2_TROVE_THREADED__
        cur_op = id_gen_fast_lookup(ds_id_array[i]);
        if (cur_op == NULL)
        {
            gen_mutex_unlock(context_mutex);
            gossip_err("Invalid operation to testsome against\n");
            return -TROVE_EINVAL;
        }

        /* check the state of the current op to see if it's completed */
        gen_mutex_lock(&cur_op->mutex);
        state = cur_op->op.state;
        gen_mutex_unlock(&cur_op->mutex);

        if ((state == OP_COMPLETED) || (state == OP_CANCELED))
        {
            assert(!dbpf_op_queue_empty(
                       dbpf_completion_queue_array[context_id]));

            /* pull the op out of the context specific completion queue */
            dbpf_op_queue_remove(cur_op);

            state_array[out_count] = cur_op->state;

            returned_user_ptr_p = &returned_user_ptr_array[out_count];
            if (returned_user_ptr_p != NULL)
            {
                *returned_user_ptr_p = cur_op->op.user_ptr;
            }
            organize_post_op_statistics(cur_op->op.type, cur_op->op.id);
            dbpf_queued_op_free(cur_op);
        }
        ret = (((state == OP_COMPLETED) ||
                (state == OP_CANCELED)) ? 1 : 0);
#else
        int tmp_count = 0;

        ret = dbpf_dspace_test(coll_id,
                               ds_id_array[i],
                               context_id,
                               &tmp_count,
                               &vtag_array[i],
                               ((returned_user_ptr_array != NULL) ?
                                &returned_user_ptr_array[out_count] : NULL),
                               &state_array[out_count],
                               max_idle_time_ms);
#endif
        if (ret != TROVE_OP_BUSY)
        {
            /* operation is done and we are telling the caller;
             * ok to pull off queue now.
             */
            out_index_array[out_count] = i;
            out_count++;
        }
    }

#ifdef __PVFS2_TROVE_THREADED__
    /* if no op completed, wait for up to max_idle_time_ms */
    if ((wait == 0) && (out_count == 0) && (max_idle_time_ms > 0))
    {
        struct timeval base;
        struct timespec wait_time;

        /* compute how long to wait */
        gettimeofday(&base, NULL);
        wait_time.tv_sec = base.tv_sec + (max_idle_time_ms / 1000);
        wait_time.tv_nsec = base.tv_usec * 1000 + 
                        ((max_idle_time_ms % 1000) * 1000000);
        if (wait_time.tv_nsec > 1000000000)
        {
            wait_time.tv_nsec = wait_time.tv_nsec - 1000000000;
            wait_time.tv_sec++;
        }

        ret = pthread_cond_timedwait(&dbpf_op_completed_cond,
                                     context_mutex,
                                     &wait_time);

        if (ret != ETIMEDOUT)
        {
            /*
              since we were signaled awake (rather than timed out
              while sleeping), we're going to rescan ops here for
              completion.  if nothing completes the second time
              around, we're giving up and won't be back here.
            */
            wait = 1;

            goto scan_for_completed_ops;
        }
    }

    gen_mutex_unlock(context_mutex);
#endif

    *inout_count_p = out_count;
    return ((out_count > 0) ? 1 : 0); // WTF!!!
}

/* dbpf_dspace_create_store_handle()
 *
 * records persisent record of new dspace within trove
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int dbpf_dspace_create_store_handle(struct dbpf_collection* coll_p,
                                           TROVE_ds_type type,
                                           TROVE_handle new_handle)
{
    int ret = -TROVE_EINVAL;
    TROVE_ds_attributes attr;
    struct dbpf_data key, data;
    TROVE_object_ref ref = {TROVE_HANDLE_NULL, coll_p->coll_id};
    char filename[PATH_MAX + 1] = {0};

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: create dspace record.\n",
                 __func__);

    memset(&attr, 0, sizeof(attr));
    attr.type = type;

    key.data = &new_handle;
    key.len = sizeof(TROVE_handle);

    data.data = &attr;
    data.len = sizeof(attr);

    /* check to see if handle is already used */
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: check for existing record - should fail.\n", __func__);
    ret = dbpf_db_get(coll_p->ds_db, &key, &data);
    //if (ret == 0)
    if (ret == TROVE_SUCCESS)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: handle (%s) already exists.\n",
                     __func__, PVFS_OID_str(&new_handle));
        return(-TROVE_EEXIST);
    }
    else if ((ret != -TROVE_ENOENT))
    {
        gossip_err("%s: error in dspace create (db_p->get failed).\n",
                   __func__);
        //ret = -ret;
        return(ret);
    }
    gossip_debug(GOSSIP_TROVE_DEBUG,"%s: no existing record (%s) found\n",
                 __func__, PVFS_OID_str(&new_handle));
    
    /* check for old bstream files (these should not exist, but it is
     * possible if the db gets out of sync with the rest of the collection
     * somehow
     */
    DBPF_GET_BSTREAM_FILENAME(filename,
                              PATH_MAX,
                              my_storage_p->data_path,
                              coll_p->coll_id,
                              new_handle);

    ret = access(filename, F_OK);
    if(ret == TROVE_SUCCESS)
    {
        char new_filename[PATH_MAX+1];
        memset(new_filename, 0, PATH_MAX+1);

        gossip_err("%s: Warning: found old bstream file %s; "
                   "moving to stranded-bstreams.\n", 
                   __func__, filename);
        
        DBPF_GET_STRANDED_BSTREAM_FILENAME(new_filename,
                                           PATH_MAX,
                                           my_storage_p->data_path, 
                                           coll_p->coll_id,
                                           new_handle);
        /* an old file exists.  Move it to the stranded subdirectory */
        ret = rename(filename, new_filename);
        if(ret != TROVE_SUCCESS)
        {
            ret = -trove_errno_to_trove_error(errno);
            gossip_err("%s: Error: trove failed to rename stranded bstream:"
                       " %s\n", __func__, filename);
            return(ret);
        }
    }
     
    data.data = &attr;
    data.len = sizeof(attr);
    
    /* create new dataspace entry */
    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: creating record.\n", __func__);
    ret = dbpf_db_put(coll_p->ds_db, &key, &data);
    if (ret != 0)
    {
        gossip_err("%s: error in dspace create (db_p->put failed).\n",
                   __func__);
        //ret = -ret;
        return(ret);
    }

    /* add retrieved ds_attr to dbpf_attr cache here */
    ref.handle = new_handle;
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_insert(ref, &attr);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Success!\n", __func__);
    return(TROVE_SUCCESS);
}

struct TROVE_dspace_ops dbpf_dspace_ops =
{
    dbpf_dspace_create,
    dbpf_dspace_create_list,
    dbpf_dspace_remove,
    dbpf_dspace_remove_list,
    dbpf_dspace_iterate_handles,
    dbpf_dspace_verify,
    dbpf_dspace_getattr,
    dbpf_dspace_getattr_list,
    dbpf_dspace_setattr,
    dbpf_dspace_cancel,
    dbpf_dspace_test,
    dbpf_dspace_testsome,
    dbpf_dspace_testcontext
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

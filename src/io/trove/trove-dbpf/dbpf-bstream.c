/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <assert.h>
#include <errno.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-bstream.h"
#include "dbpf-attr-cache.h"
#include "pint-event.h"
#include "dbpf-open-cache.h"

#define AIOCB_ARRAY_SZ 8

/* Internal prototypes */
static inline int dbpf_bstream_rw_list(TROVE_coll_id coll_id,
				       TROVE_handle handle,
				       char **mem_offset_array, 
				       TROVE_size *mem_size_array,
				       int mem_count,
				       TROVE_offset *stream_offset_array, 
				       TROVE_size *stream_size_array,
				       int stream_count,
				       TROVE_size *out_size_p,
				       TROVE_ds_flags flags, 
				       TROVE_vtag_s *vtag,
				       void *user_ptr,
				       TROVE_context_id context_id,
				       TROVE_op_id *out_op_id_p,
				       int opcode);
static int dbpf_bstream_read_at_op_svc(struct dbpf_op *op_p);
static int dbpf_bstream_write_at_op_svc(struct dbpf_op *op_p);
#ifndef __PVFS2_TROVE_AIO_THREADED__
static int dbpf_bstream_rw_list_op_svc(struct dbpf_op *op_p);
#endif
static int dbpf_bstream_flush_op_svc(struct dbpf_op *op_p);
static int dbpf_bstream_resize_op_svc(struct dbpf_op *op_p);

#ifdef __PVFS2_TROVE_AIO_THREADED__
#include "dbpf-thread.h"

extern pthread_cond_t dbpf_op_completed_cond;
extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t *dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];

static void aio_progress_notification(sigval_t sig)
{
    dbpf_queued_op_t *cur_op = NULL;
    struct dbpf_op *op_p = NULL;
    int ret, i, aiocb_inuse_count, state = 0, error_code = 0;
    struct aiocb *aiocb_p = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = {0};
    gen_mutex_t *context_mutex = NULL;

    cur_op = (dbpf_queued_op_t *)sig.sival_ptr;
    assert(cur_op);

    op_p = &cur_op->op;
    assert(op_p);

    gossip_debug(
        GOSSIP_TROVE_DEBUG," --- aio_progress_notification called "
        "with %p (handle %Lu)\n", sig.sival_ptr, Lu(op_p->handle));

    aiocb_p = op_p->u.b_rw_list.aiocb_array;
    assert(aiocb_p);

    gen_mutex_lock(&cur_op->mutex);
    state = cur_op->op.state;
    gen_mutex_unlock(&cur_op->mutex);

    assert(state != OP_COMPLETED);

    /*
      we should iterate through the ops here to determine the
      error/return value of the op based on individual request
      error/return values.  they're ignored for now, however.
    */
    for (i = 0; i < op_p->u.b_rw_list.aiocb_array_count; i++)
    {
        if (aiocb_p[i].aio_lio_opcode == LIO_NOP)
        {
            continue;
        }

        /* aio_error gets the "errno" value of the individual op */
        ret = aio_error(&aiocb_p[i]);
        if (ret == 0)
        {
            /* aio_return gets the return value of the individual op */
            ret = aio_return(&aiocb_p[i]);

            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "  aio_return() says %d\n", ret);

            /* mark as a NOP so we ignore it from now on */
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }
        else
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "error %d (%s) from "
                         "aio_error/aio_return on block %d; "
                         "skipping\n", ret, strerror(ret), i);

            error_code = ret;
            goto final_threaded_aio_cleanup;
        }
    }

    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLPOSTED)
    {
        error_code = 0;

      final_threaded_aio_cleanup:
        gossip_debug(GOSSIP_TROVE_DEBUG, " aio_progress_notification: "
                     "op completed\n");

        if (op_p->flags & TROVE_SYNC)
        {
            if ((ret = DBPF_SYNC(op_p->u.b_rw_list.fd)) != 0)
            {
                error_code = -trove_errno_to_trove_error(ret);
            }
        }

        /* this is a macro defined in dbpf-thread.h */
        move_op_to_completion_queue(
            cur_op, error_code,
            ((error_code == ECANCELED) ? OP_CANCELED : OP_COMPLETED));
        return;
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "*** issuing more aio requests "
                     "(state is %d)\n",op_p->u.b_rw_list.list_proc_state);

        /* no operations currently in progress; convert and post some more */
        op_p->u.b_rw_list.aiocb_array_count  = AIOCB_ARRAY_SZ;
        op_p->u.b_rw_list.aiocb_array        = aiocb_p;

        /* convert listio arguments into aiocb structures */
        aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
        ret = dbpf_bstream_listio_convert(
            op_p->u.b_rw_list.fd,
            op_p->u.b_rw_list.opcode,
            op_p->u.b_rw_list.mem_offset_array,
            op_p->u.b_rw_list.mem_size_array,
            op_p->u.b_rw_list.mem_array_count,
            op_p->u.b_rw_list.stream_offset_array,
            op_p->u.b_rw_list.stream_size_array,
            op_p->u.b_rw_list.stream_array_count,
            aiocb_p,
            &aiocb_inuse_count,
            &op_p->u.b_rw_list.lio_state);

        if (ret == 1)
        {
            op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLCONVERTED;
        }

        op_p->u.b_rw_list.sigev.sigev_notify = SIGEV_THREAD;
        op_p->u.b_rw_list.sigev.sigev_notify_attributes = NULL;
        op_p->u.b_rw_list.sigev.sigev_notify_function =
            aio_progress_notification;
        op_p->u.b_rw_list.sigev.sigev_value.sival_ptr = (void *)cur_op;

        /*
          if we didn't use the entire array, mark the unused ones with
          LIO_NOPs
        */
        for(i = aiocb_inuse_count;
            i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
            /* for simplicity just mark these as NOPs and we'll ignore them */
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }

        for(i = 0; i < aiocb_inuse_count; i++)
        {
            aiocb_ptr_array[i] = &aiocb_p[i];
        }

        assert(cur_op == op_p->u.b_rw_list.sigev.sigev_value.sival_ptr);

        ret = lio_listio(LIO_NOWAIT, aiocb_ptr_array,
                         aiocb_inuse_count, &op_p->u.b_rw_list.sigev);
        if (ret != 0)
        {
            gossip_lerr("lio_listio() returned %d\n", ret);
            return;
        }
        if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
        {
            op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
        }
    }
}
#endif /* __PVFS2_TROVE_AIO_THREADED__ */

static int dbpf_bstream_read_at(TROVE_coll_id coll_id,
				TROVE_handle handle,
				void *buffer,
				TROVE_size *inout_size_p,
				TROVE_offset offset,
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
        return -1;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -1;
    }
    
    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			BSTREAM_READ_AT,
			handle,
			coll_p,
			dbpf_bstream_read_at_op_svc,
			user_ptr,
			flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_read_at.offset =  offset;
    q_op_p->op.u.b_read_at.size   = *inout_size_p;
    q_op_p->op.u.b_read_at.buffer =  buffer;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_bstream_read_at_op_svc()
 *
 * Returns 1 on completion, -1 on error, 0 on not done.
 */
static int dbpf_bstream_read_at_op_svc(struct dbpf_op *op_p)
{
    int ret, got_fd = 0;
    struct open_cache_ref tmp_ref;

    ret = dbpf_open_cache_get(op_p->coll_p->coll_id,
	op_p->handle,
	0,
	DBPF_OPEN_FD,
	&tmp_ref);
    if(ret < 0)
    {
	goto return_error;
    }
    got_fd = 1;

    ret = DBPF_LSEEK(tmp_ref.fd, op_p->u.b_read_at.offset, SEEK_SET);
    if (ret < 0)
    {
        goto return_error;
    }
    
    ret = DBPF_READ(tmp_ref.fd, op_p->u.b_read_at.buffer,
                    op_p->u.b_read_at.size);
    if (ret < 0)
    {
        goto return_error;
    }
    
    if (op_p->flags & TROVE_SYNC)
    {
	if ((ret = DBPF_SYNC(tmp_ref.fd)) != 0)
        {
	    goto return_error;
	}
    }

    dbpf_open_cache_put(&tmp_ref);

    gossip_debug(GOSSIP_TROVE_DEBUG, "read %d bytes.\n", ret);

    /* TODO: any way to return partial success? */
    return 1;
   
 return_error:
    if (got_fd)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    /* TODO: shouldn't this return a real error code? */
    return -1;
}

static int dbpf_bstream_write_at(TROVE_coll_id coll_id,
				 TROVE_handle handle,
				 void *buffer,
				 TROVE_size *inout_size_p,
				 TROVE_offset offset,
				 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_context_id context_id,
				 TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;
    
    /* find the collection */
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -1;
    }
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -1;
    }
    
    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			BSTREAM_WRITE_AT,
			handle,
			coll_p,
			dbpf_bstream_write_at_op_svc,
			user_ptr,
			flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_write_at.offset =  offset;
    q_op_p->op.u.b_write_at.size   = *inout_size_p;
    q_op_p->op.u.b_write_at.buffer =  buffer;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_bstream_write_at_op_svc(struct dbpf_op *op_p)
{
    int ret, got_fd = 0;
    struct open_cache_ref tmp_ref;

    /* grab the FD (also increments a reference count) */
    ret = dbpf_open_cache_get(op_p->coll_p->coll_id,
	op_p->handle, 1, DBPF_OPEN_FD, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    got_fd = 1;
    
    ret = DBPF_LSEEK(tmp_ref.fd, op_p->u.b_write_at.offset, SEEK_SET);
    if (ret < 0)
    {
        goto return_error;
    }
    
    ret = DBPF_WRITE(tmp_ref.fd, op_p->u.b_write_at.buffer,
                     op_p->u.b_write_at.size);
    if (ret < 0)
    {
        goto return_error;
    }

    if (op_p->flags & TROVE_SYNC)
    {
	if ((ret = DBPF_SYNC(tmp_ref.fd)) != 0)
        {
	    goto return_error;
	}
    }
    dbpf_open_cache_put(&tmp_ref);

    gossip_debug(GOSSIP_TROVE_DEBUG, "wrote %d bytes.\n", ret);

    /* TODO: any way to return partial success? */
    return 1;
    
 return_error:
    if (got_fd)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    /* TODO: shouldn't this return a real error code? */
    return -1;
}

static int dbpf_bstream_flush(TROVE_coll_id coll_id,
                              TROVE_handle handle,
                              TROVE_ds_flags flags,
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
			BSTREAM_FLUSH,
			handle,
			coll_p,
			dbpf_bstream_flush_op_svc,
			user_ptr,
			flags,
                        context_id);
    
    /* initialize the op-specific members */
    /* there are none for flush */
   
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    
    return 0;
}

/* returns 1 on completion, -1 on error, 0 on not done */
static int dbpf_bstream_flush_op_svc(struct dbpf_op *op_p)
{
    int ret, error, got_fd = 0;
    struct open_cache_ref tmp_ref;

    /* grab the FD (also increments a reference count) */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 0, DBPF_OPEN_FD, &tmp_ref);
    if(ret < 0)
    {
	error = -trove_errno_to_trove_error(ret);
	goto return_error;
    }
    got_fd = 1;

    ret = DBPF_SYNC(tmp_ref.fd);
    if (ret != 0)
    {
	error = -trove_errno_to_trove_error(errno);
	goto return_error;
    }
    dbpf_open_cache_put(&tmp_ref);
    return 1;

return_error:
    if (got_fd)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    return error;
}

static int dbpf_bstream_resize(TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_size *inout_size_p,
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
			BSTREAM_RESIZE,
			handle,
			coll_p,
			dbpf_bstream_resize_op_svc,
			user_ptr,
			flags,
                        context_id);
    
    /* initialize the op-specific members */
    q_op_p->op.u.b_resize.size = *inout_size_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

/* dbpf_bstream_flush_op_svc
 * returns 1 on completion, -1 on error, 0 on not done
 */
static int dbpf_bstream_resize_op_svc(struct dbpf_op *op_p)
{
    int ret, error, got_fd = 0;
    struct open_cache_ref tmp_ref;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    /* grab the FD (also increments a reference count) */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle, 1, DBPF_OPEN_FD, &tmp_ref);
    if(ret < 0)
    {
	error = -trove_errno_to_trove_error(ret);
	goto return_error;
    }
    got_fd = 1;

    ret = DBPF_RESIZE(tmp_ref.fd, op_p->u.b_resize.size);
    if ( ret != 0)
    {
	error = -trove_errno_to_trove_error(errno);
	goto return_error;
    }
    dbpf_open_cache_put(&tmp_ref);

    /* adjust size in cached attribute element, if present */
    dbpf_attr_cache_ds_attr_update_cached_data_bsize(
        ref,  op_p->u.b_resize.size);

    return 1;

return_error:
    if (got_fd)
    {
	dbpf_open_cache_put(&tmp_ref);
    }
    return error;
}

static int dbpf_bstream_validate(TROVE_coll_id coll_id,
				 TROVE_handle handle,
			       	 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_context_id context_id,
				 TROVE_op_id *out_op_id_p)
{
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_read_list(TROVE_coll_id coll_id,
				  TROVE_handle handle,
				  char **mem_offset_array, 
				  TROVE_size *mem_size_array,
				  int mem_count,
				  TROVE_offset *stream_offset_array, 
				  TROVE_size *stream_size_array,
				  int stream_count,
				  TROVE_size *out_size_p,
				  TROVE_ds_flags flags, 
				  TROVE_vtag_s *vtag,
				  void *user_ptr,
				  TROVE_context_id context_id,
				  TROVE_op_id *out_op_id_p)
{
    return dbpf_bstream_rw_list(coll_id,
				handle,
				mem_offset_array, 
				mem_size_array,
				mem_count,
				stream_offset_array, 
				stream_size_array,
				stream_count,
				out_size_p,
				flags, 
				vtag,
				user_ptr,
				context_id,
				out_op_id_p,
				LIO_READ);
}

static int dbpf_bstream_write_list(TROVE_coll_id coll_id,
				   TROVE_handle handle,
				   char **mem_offset_array, 
				   TROVE_size *mem_size_array,
				   int mem_count,
				   TROVE_offset *stream_offset_array, 
				   TROVE_size *stream_size_array,
				   int stream_count,
				   TROVE_size *out_size_p,
				   TROVE_ds_flags flags, 
				   TROVE_vtag_s *vtag,
				   void *user_ptr,
				   TROVE_context_id context_id,
				   TROVE_op_id *out_op_id_p)
{
    return dbpf_bstream_rw_list(coll_id,
				handle,
				mem_offset_array, 
				mem_size_array,
				mem_count,
				stream_offset_array, 
				stream_size_array,
				stream_count,
				out_size_p,
				flags, 
				vtag,
				user_ptr,
				context_id,
				out_op_id_p,
				LIO_WRITE);
}

/* dbpf_bstream_rw_list()
 *
 * Handles queueing of both read and write list operations
 *
 * opcode parameter should be LIO_READ or LIO_WRITE
 */
static inline int dbpf_bstream_rw_list(TROVE_coll_id coll_id,
				       TROVE_handle handle,
				       char **mem_offset_array, 
				       TROVE_size *mem_size_array,
				       int mem_count,
				       TROVE_offset *stream_offset_array, 
				       TROVE_size *stream_size_array,
				       int stream_count,
				       TROVE_size *out_size_p,
				       TROVE_ds_flags flags, 
				       TROVE_vtag_s *vtag,
				       void *user_ptr,
				       TROVE_context_id context_id,
				       TROVE_op_id *out_op_id_p,
				       int opcode)
{
    int ret;
    dbpf_queued_op_t *q_op_p;
    struct dbpf_collection *coll_p;
    enum dbpf_op_type tmp_type;
    int event_type;
#ifdef __PVFS2_TROVE_AIO_THREADED__
    struct dbpf_op *op_p = NULL;
    int i, aiocb_inuse_count;
    struct aiocb *aiocb_p = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = {0};
#endif

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

    if (opcode == LIO_READ)
    {
	tmp_type = BSTREAM_READ_LIST;
	event_type = PVFS_EVENT_TROVE_READ_LIST;
    }
    else
    {
	tmp_type = BSTREAM_WRITE_LIST;
	event_type = PVFS_EVENT_TROVE_WRITE_LIST;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			tmp_type,
			handle,
			coll_p,
#ifdef __PVFS2_TROVE_AIO_THREADED__
                        NULL,
#else
			dbpf_bstream_rw_list_op_svc,
#endif
			user_ptr,
			flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.b_rw_list.fd                  = -1;
    q_op_p->op.u.b_rw_list.opcode              = opcode;
    q_op_p->op.u.b_rw_list.mem_array_count     = mem_count;
    q_op_p->op.u.b_rw_list.mem_offset_array    = mem_offset_array;
    q_op_p->op.u.b_rw_list.mem_size_array      = mem_size_array;
    q_op_p->op.u.b_rw_list.stream_array_count  = stream_count;
    q_op_p->op.u.b_rw_list.stream_offset_array = stream_offset_array;
    q_op_p->op.u.b_rw_list.stream_size_array   = stream_size_array;
    q_op_p->op.u.b_rw_list.aiocb_array_count   = 0;
    q_op_p->op.u.b_rw_list.aiocb_array         = NULL;

    /* initialize list processing state (more op-specific members) */
    q_op_p->op.u.b_rw_list.lio_state.mem_ct          = 0;
    q_op_p->op.u.b_rw_list.lio_state.stream_ct       = 0;
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_size    = mem_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_off     = mem_offset_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_size = stream_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_off  = stream_offset_array[0];

    q_op_p->op.u.b_rw_list.list_proc_state = LIST_PROC_INITIALIZED;

    /*
      get an FD so we can access the file; last
      parameter controls file creation
    */
    ret = dbpf_open_cache_get(
        coll_id, handle, (opcode == LIO_WRITE) ? 1 : 0, DBPF_OPEN_FD,
	&q_op_p->op.u.b_rw_list.open_ref);
    if (ret < 0)
    {
	dbpf_queued_op_free(q_op_p);
	gossip_ldebug(GOSSIP_TROVE_DEBUG,
                      "warning: useless error value\n");
        return -trove_errno_to_trove_error(ret);
    }
    q_op_p->op.u.b_rw_list.fd = q_op_p->op.u.b_rw_list.open_ref.fd;

    /*
      if we're doing an i/o write, remove the cached
      attribute for this handle if it's present
    */
    if (opcode == LIO_WRITE)
    {
        TROVE_object_ref ref = {handle, coll_id};
        dbpf_attr_cache_remove(ref);
    }

#ifndef __PVFS2_TROVE_AIO_THREADED__

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    DBPF_EVENT_START(event_type, *out_op_id_p);

#else
    op_p = &q_op_p->op;

    /*
      instead of queueing the op like most other trove operations,
      we're going to issue the system aio calls here to begin
      being serviced immediately.  We'll check progress in the 
      aio_progress_notification callback method
    */
    aiocb_p = (struct aiocb *)malloc(AIOCB_ARRAY_SZ*sizeof(struct aiocb));
    if (aiocb_p == NULL)
    {
	dbpf_open_cache_put(&q_op_p->op.u.b_rw_list.open_ref);
        return -TROVE_ENOMEM;
    }

    /* initialize, paying particular attention to set the opcode to NOP.
     */
    memset(aiocb_p, 0, AIOCB_ARRAY_SZ*sizeof(struct aiocb));
    for(i = 0; i < AIOCB_ARRAY_SZ; i++)
    {
        aiocb_p[i].aio_lio_opcode = LIO_NOP;
        aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
    }

    op_p->u.b_rw_list.aiocb_array_count  = AIOCB_ARRAY_SZ;
    op_p->u.b_rw_list.aiocb_array        = aiocb_p;
    op_p->u.b_rw_list.list_proc_state    = LIST_PROC_INPROGRESS;

    /* convert listio arguments into aiocb structures */
    aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
    ret = dbpf_bstream_listio_convert(
        op_p->u.b_rw_list.fd,
        op_p->u.b_rw_list.opcode,
        op_p->u.b_rw_list.mem_offset_array,
        op_p->u.b_rw_list.mem_size_array,
        op_p->u.b_rw_list.mem_array_count,
        op_p->u.b_rw_list.stream_offset_array,
        op_p->u.b_rw_list.stream_size_array,
        op_p->u.b_rw_list.stream_array_count,
        aiocb_p,
        &aiocb_inuse_count,
        &op_p->u.b_rw_list.lio_state);

    if (ret == 1)
    {
        op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLCONVERTED;
    }

    op_p->u.b_rw_list.sigev.sigev_notify = SIGEV_THREAD;
    op_p->u.b_rw_list.sigev.sigev_notify_attributes = NULL;
    op_p->u.b_rw_list.sigev.sigev_notify_function =
        aio_progress_notification;
    op_p->u.b_rw_list.sigev.sigev_value.sival_ptr = (void *)q_op_p;

    /*
      if we didn't use the entire array, mark
      the unused ones with LIO_NOPs
    */
    for(i = aiocb_inuse_count; i < op_p->u.b_rw_list.aiocb_array_count; i++)
    {
        /* for simplicity just mark these as NOPs and we'll ignore them */
        aiocb_p[i].aio_lio_opcode = LIO_NOP;
    }

    for(i = 0; i < aiocb_inuse_count; i++)
    {
        aiocb_ptr_array[i] = &aiocb_p[i];
    }

    assert(q_op_p == op_p->u.b_rw_list.sigev.sigev_value.sival_ptr);

    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
    {
        op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
    }

    gen_mutex_lock(&q_op_p->mutex);
    q_op_p->op.state = OP_IN_SERVICE;
    gen_mutex_unlock(&q_op_p->mutex);

    id_gen_fast_register(&q_op_p->op.id, q_op_p);
    *out_op_id_p = q_op_p->op.id;
    DBPF_EVENT_START(event_type, *out_op_id_p);

    ret = lio_listio(LIO_NOWAIT, aiocb_ptr_array,
                     aiocb_inuse_count, &op_p->u.b_rw_list.sigev);
    if (ret != 0)
    {
        gossip_lerr("lio_listio() returned %d\n", ret);
	dbpf_open_cache_put(&q_op_p->op.u.b_rw_list.open_ref);
        return -trove_errno_to_trove_error(errno);
    }
    gossip_debug(GOSSIP_TROVE_DEBUG, " +++ lio_listio posted %p "
                 "(handle %Lu) and returned %d\n", q_op_p,
                 Lu(handle), ret);
#endif

    return 0;
}


/* dbpf_bstream_rw_list_op_svc()
 *
 * This function is used to service both read_list and write_list
 * operations.  State maintained in the struct dbpf_op (pointed to by
 * op_p) keeps up with which type of operation this is via the
 * "opcode" field in the b_rw_list member.
 *
 * NOTE: This method will NEVER be called if
 * __PVFS2_TROVE_AIO_THREADED__ is defined.  Instead, progress is
 * monitored and pushed using aio_progress_notification callback
 * method.
 *
 * Assumptions:
 * - FD has been retrieved from open cache, is refct'd so it won't go
 *   away
 * - lio_state in the op is valid
 * - opcode is set to LIO_READ or LIO_WRITE (corresponding to a
 *   read_list or write_list, respectively)
 *
 * This function is responsible for alloating and deallocating the
 * space reserved for the aiocb array.
 *
 * Outline:
 * - look to see if we have an aiocb array
 *   - if we don't, allocate one
 *   - if we do, then check on progress of each operation (having
 *     array implies that we have put some things in service)
 *     - if we got an error, ???
 *     - if op is finished, mark w/NOP
 *
 * - look to see if there are unfinished but posted operations
 *   - if there are, return 0
 *   - if not, and we are in the LIST_PROC_ALLPOSTED state,
 *     then we're done!
 *   - otherwise convert some more elements and post them.
 * 
 */
#ifndef __PVFS2_TROVE_AIO_THREADED__
static int dbpf_bstream_rw_list_op_svc(struct dbpf_op *op_p)
{
    int ret, i, aiocb_inuse_count, op_in_progress_count = 0;
    struct aiocb *aiocb_p, *aiocb_ptr_array[AIOCB_ARRAY_SZ];

    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_INITIALIZED)
    {
	/* first call; need to allocate aiocb array and ptr array */
	aiocb_p = (struct aiocb *)malloc(
            AIOCB_ARRAY_SZ*sizeof(struct aiocb));

	if (aiocb_p == NULL)
        {
	    return -TROVE_ENOMEM;
	}

	/* initialize, paying particular attention to set the opcode
	 * to NOP.
	 *
	 * Also, it appears to be important to do all this SIGEV_NONE
	 * stuff, although some man pages seem to indicate that it is
	 * not.
	 */
	memset(aiocb_p, 0, AIOCB_ARRAY_SZ*sizeof(struct aiocb));
	for (i=0; i < AIOCB_ARRAY_SZ; i++)
        {
	    aiocb_p[i].aio_lio_opcode            = LIO_NOP;
	    aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	}

	op_p->u.b_rw_list.aiocb_array_count  = AIOCB_ARRAY_SZ;
	op_p->u.b_rw_list.aiocb_array        = aiocb_p;
	op_p->u.b_rw_list.list_proc_state    = LIST_PROC_INPROGRESS;
	op_p->u.b_rw_list.sigev.sigev_notify = SIGEV_NONE;
    }
    else
    {
	/* operations potentially in progress */
	aiocb_p = op_p->u.b_rw_list.aiocb_array;

	/* check to see how we're progressing on previous operations */
	for (i=0; i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
	    if (aiocb_p[i].aio_lio_opcode == LIO_NOP)
            {
                continue;
            }

            /* gets the "errno" value of the individual op */
	    ret = aio_error(&aiocb_p[i]);
	    if (ret == 0)
            {
		/*
                  this particular operation completed w/out error.
                  gets the return value of the individual op
                */
		ret = aio_return(&aiocb_p[i]);

		/* mark as a NOP so we ignore it from now on */
		aiocb_p[i].aio_lio_opcode = LIO_NOP;
	    }
	    else if (ret != EINPROGRESS)
            {
		gossip_debug(GOSSIP_TROVE_DEBUG, "error %d (%s) from "
                             "aio_error/aio_return on block %d; "
                             "skipping\n", ret, strerror(ret), i);

                ret = -trove_errno_to_trove_error(ret);
                goto final_aio_cleanup;
	    }
	    else
            {
		/* otherwise the operation is still in progress; skip it */
		op_in_progress_count++;
	    }
	}
    }

    /* if we're not done with the last set of operations, break out */
    if (op_in_progress_count > 0)
    {
        return 0;
    }
    else if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLPOSTED)
    {
	/* we've posted everything, and it all completed */
        ret = 1;

      final_aio_cleanup:
	return ret;
    }
    else
    {
	/*
          no operations currently in progress; convert and post some
          more
        */
	aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
	ret = dbpf_bstream_listio_convert(
            op_p->u.b_rw_list.fd,
            op_p->u.b_rw_list.opcode,
            op_p->u.b_rw_list.mem_offset_array,
            op_p->u.b_rw_list.mem_size_array,
            op_p->u.b_rw_list.mem_array_count,
            op_p->u.b_rw_list.stream_offset_array,
            op_p->u.b_rw_list.stream_size_array,
            op_p->u.b_rw_list.stream_array_count,
            aiocb_p,
            &aiocb_inuse_count,
            &op_p->u.b_rw_list.lio_state);

	if (ret == 1)
        {
            op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLCONVERTED;
        }
	
	/*
          if we didn't use the entire array, mark the unused ones with
          LIO_NOPs
        */
	for (i = aiocb_inuse_count;
             i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
	    /* just mark these as NOPs and we'll ignore them */
	    aiocb_p[i].aio_lio_opcode = LIO_NOP;
	}

	for (i=0; i < aiocb_inuse_count; i++)
        {
	    aiocb_ptr_array[i] = &aiocb_p[i];
	}

	ret = lio_listio(LIO_NOWAIT, aiocb_ptr_array,
                         aiocb_inuse_count, &op_p->u.b_rw_list.sigev);
	if (ret != 0)
        {
	    gossip_lerr("lio_listio() returned %d\n", ret);
	    return -trove_errno_to_trove_error(errno);
	}

	if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
        {
	    op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
        }
	return 0;
    }
}
#endif

struct TROVE_bstream_ops dbpf_bstream_ops =
{
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    dbpf_bstream_read_list,
    dbpf_bstream_write_list,
    dbpf_bstream_flush
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

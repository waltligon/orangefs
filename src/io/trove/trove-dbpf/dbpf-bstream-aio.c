/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifdef __PVFS2_USE_AIO__
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-bstream.h"
#include "dbpf-op-queue.h"
#include "id-generator.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "dbpf-attr-cache.h"
#include "pvfs2-event.h"
#include "pint-event.h"

/* bstream-aio functions */

int dbpf_bstream_listio_convert(
                int fd,
                int op_type,
                char **mem_offset_array,
                TROVE_size *mem_size_array,
                int mem_count,
                TROVE_offset *stream_offset_array,
                TROVE_size *stream_size_array,
                int stream_count,
                struct aiocb *aiocb_array,
                int *aiocb_count,
                struct bstream_listio_state *lio_state
                );

/* dbpf_bstream_listio_convert()
 *
 * Returns 0 if there are still pieces of the input list to convert,
 * and 1 if processing is complete.
 *
 * Stored state in lio_state so that processing can
 * continue on subsequent calls.
 */

extern gen_mutex_t dbpf_attr_cache_mutex;

#define AIOCB_ARRAY_SZ 64

#define DBPF_MAX_IOS_IN_PROGRESS  16
static int s_dbpf_ios_in_progress = 0;

static int issue_or_delay_io_operation(
    dbpf_queued_op_t * cur_op,
    struct aiocb **aiocb_ptr_array,
    int aiocb_inuse_count,
    struct sigevent *sig,
    int dec_first);
static void start_delayed_ops_if_any(
    int dec_first);

static char *list_proc_state_strings[] = {
    "LIST_PROC_INITIALIZED",
    "LIST_PROC_INPROGRESS ",
    "LIST_PROC_ALLCONVERTED",
    "LIST_PROC_ALLPOSTED",
};

static inline int dbpf_bstream_rw_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p,
    int opcode);

static int dbpf_bstream_read_at_op_svc(
    struct dbpf_op *op_p);
static int dbpf_bstream_write_at_op_svc(
    struct dbpf_op *op_p);
#ifndef __PVFS2_TROVE_AIO_THREADED__
static int dbpf_bstream_rw_list_op_svc(
    struct dbpf_op *op_p);
#endif
static int dbpf_bstream_flush_op_svc(
    struct dbpf_op *op_p);
static int dbpf_bstream_resize_op_svc(
    struct dbpf_op *op_p);

#include "dbpf-thread.h"
#include "pvfs2-internal.h"

extern pthread_cond_t dbpf_op_completed_cond;
extern gen_mutex_t *dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];

#ifdef __PVFS2_TROVE_AIO_THREADED__

static void aio_progress_notification(
    union sigval sig)
{
    dbpf_queued_op_t *cur_op = NULL;
    struct dbpf_op *op_p = NULL;
    int ret, i, aiocb_inuse_count, state = 0;
    struct aiocb *aiocb_p = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = { 0 };

    cur_op = (dbpf_queued_op_t *) sig.sival_ptr;
    assert(cur_op);

    op_p = &cur_op->op;
    assert(op_p);

    gossip_debug(GOSSIP_TROVE_DEBUG, " --- aio_progress_notification called "
                 "with handle %llu (%p)\n", llu(op_p->handle), cur_op);

    aiocb_p = op_p->u.b_rw_list.aiocb_array;
    assert(aiocb_p);

    state = dbpf_op_get_status(cur_op);

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

            gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s complete: "
                         "aio_return() says %d [fd = %d]\n",
                         __func__,
                         ((op_p->type == BSTREAM_WRITE_LIST) ||
                          (op_p->type == BSTREAM_WRITE_AT) ?
                          "WRITE" : "READ"), ret, op_p->u.b_rw_list.fd);

            *(op_p->u.b_rw_list.out_size_p) += ret;

            /* mark as a NOP so we ignore it from now on */
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }
        else
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "error %d (%s) from "
                         "aio_error/aio_return on block %d; "
                         "skipping\n", ret, strerror(ret), i);

            ret = -trove_errno_to_trove_error(ret);
            goto final_threaded_aio_cleanup;
        }
    }

    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLPOSTED)
    {
        ret = 0;

      final_threaded_aio_cleanup:
        if ((op_p->type == BSTREAM_WRITE_AT) ||
            (op_p->type == BSTREAM_WRITE_LIST))
        {
            DBPF_AIO_SYNC_IF_NECESSARY(op_p, op_p->u.b_rw_list.fd, ret);
        }

        dbpf_open_cache_put(&op_p->u.b_rw_list.open_ref);
        op_p->u.b_rw_list.fd = -1;

        gossip_debug(GOSSIP_TROVE_DEBUG, "*** starting delayed ops if any "
                     "(state is %s)\n",
                     list_proc_state_strings[op_p->u.b_rw_list.
                                             list_proc_state]);

        dbpf_move_op_to_completion_queue(cur_op, ret,
                                         ((ret ==
                                           -TROVE_ECANCEL) ? OP_CANCELED :
                                          OP_COMPLETED));

        start_delayed_ops_if_any(1);
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "*** issuing more aio requests "
                     "(state is %s)\n",
                     list_proc_state_strings[op_p->u.b_rw_list.
                                             list_proc_state]);

        /* no operations in progress; convert and post some more */
        op_p->u.b_rw_list.aiocb_array_count = AIOCB_ARRAY_SZ;
        op_p->u.b_rw_list.aiocb_array = aiocb_p;

        /* convert listio arguments into aiocb structures */
        aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
        ret = dbpf_bstream_listio_convert(op_p->u.b_rw_list.fd,
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
        op_p->u.b_rw_list.sigev.sigev_value.sival_ptr = (void *) cur_op;

        /* mark the unused with LIO_NOPs */
        for (i = aiocb_inuse_count;
             i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
            /* mark these as NOPs and we'll ignore them */
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }

        for (i = 0; i < aiocb_inuse_count; i++)
        {
            aiocb_ptr_array[i] = &aiocb_p[i];
        }

        assert(cur_op == op_p->u.b_rw_list.sigev.sigev_value.sival_ptr);

        if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
        {
            op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
        }

        ret =
            issue_or_delay_io_operation(cur_op, aiocb_ptr_array,
                                        aiocb_inuse_count,
                                        &op_p->u.b_rw_list.sigev, 1);

        if (ret)
        {
            gossip_lerr("issue_or_delay_io_operation() returned " "%d\n", ret);
        }
    }
}
#endif /* __PVFS2_TROVE_AIO_THREADED__ */

static void start_delayed_ops_if_any(
    int dec_first)
{
    int ret = 0;
    dbpf_queued_op_t *cur_op = NULL;
    int i = 0, aiocb_inuse_count = 0;
    struct aiocb *aiocbs = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = { 0 };

    gen_mutex_lock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
    if (dec_first)
    {
        s_dbpf_ios_in_progress--;
    }
    gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF I/O ops in progress: %d\n",
                 s_dbpf_ios_in_progress);
                 
    assert(& dbpf_op_queue[OP_QUEUE_IO]);

    if (!dbpf_op_queue_empty(& dbpf_op_queue[OP_QUEUE_IO]))
    {
        cur_op = dbpf_op_pop_front_nolock(& dbpf_op_queue[OP_QUEUE_IO]);
        assert(cur_op);
#ifndef __PVFS2_TROVE_AIO_THREADED__
        assert(cur_op->op.state == OP_INTERNALLY_DELAYED);
#endif
        assert((cur_op->op.type == BSTREAM_READ_AT) ||
               (cur_op->op.type == BSTREAM_READ_LIST) ||
               (cur_op->op.type == BSTREAM_WRITE_AT) ||
               (cur_op->op.type == BSTREAM_WRITE_LIST));

        assert(s_dbpf_ios_in_progress < (DBPF_MAX_IOS_IN_PROGRESS + 1));

        gossip_debug(GOSSIP_TROVE_DEBUG, "starting delayed I/O "
                     "operation %p (%d in progress)\n", cur_op,
                     s_dbpf_ios_in_progress);

        aiocbs = cur_op->op.u.b_rw_list.aiocb_array;
        assert(aiocbs);

        for (i = 0; i < AIOCB_ARRAY_SZ; i++)
        {
            if (aiocbs[i].aio_lio_opcode != LIO_NOP)
            {
                aiocb_inuse_count++;
            }
        }

        for (i = 0; i < aiocb_inuse_count; i++)
        {
            aiocb_ptr_array[i] = &aiocbs[i];
        }

        if (gossip_debug_enabled(GOSSIP_TROVE_DEBUG))
        {

            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "lio_listio called with %d following aiocbs:\n",
                         aiocb_inuse_count);
            for (i = 0; i < aiocb_inuse_count; i++)
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "aiocb_ptr_array[%d]: fd: %d, off: %lld, "
                             "bytes: %d, buf: %p, type: %d\n",
                             i,
                             aiocb_ptr_array[i]->aio_fildes,
                             lld(aiocb_ptr_array[i]->aio_offset),
                             (int) aiocb_ptr_array[i]->aio_nbytes,
                             aiocb_ptr_array[i]->aio_buf,
                             (int) aiocb_ptr_array[i]->aio_lio_opcode);
            }
        }

        ret = lio_listio(LIO_NOWAIT, aiocb_ptr_array, aiocb_inuse_count,
                         &cur_op->op.u.b_rw_list.sigev);

        if (ret != 0)
        {
            gossip_lerr("lio_listio() returned %d\n", ret);
            dbpf_open_cache_put(&cur_op->op.u.b_rw_list.open_ref);
            goto error_exit;
        }
        s_dbpf_ios_in_progress++;

        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: lio_listio posted %p "
                     "(handle %llu, ret %d))\n", __func__, cur_op,
                     llu(cur_op->op.handle), ret);

#ifndef __PVFS2_TROVE_AIO_THREADED__
        /*
           to continue making progress on this previously delayed I/O
           operation, we need to re-add it back to the normal dbpf
           operation queue so that the calling thread can continue to
           call the service method (state flag is updated as well)
         */
        dbpf_queued_op_queue_nolock(cur_op, & dbpf_op_queue[OP_QUEUE_IO]);
        
#endif
    }
  error_exit:
    gen_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
}

static int issue_or_delay_io_operation(
    dbpf_queued_op_t * cur_op,
    struct aiocb **aiocb_ptr_array,
    int aiocb_inuse_count,
    struct sigevent *sig,
    int dec_first)
{
    int ret = -TROVE_EINVAL, op_delayed = 0;
    int i;
    assert(cur_op);

    gen_mutex_lock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
    if (dec_first)
    {
        s_dbpf_ios_in_progress--;
    }
    if (s_dbpf_ios_in_progress < DBPF_MAX_IOS_IN_PROGRESS)
    {
        s_dbpf_ios_in_progress++;
    }
    else
    {
        assert(& dbpf_op_queue[OP_QUEUE_IO]);
        dbpf_op_queue_add(& dbpf_op_queue[OP_QUEUE_IO], cur_op);

        op_delayed = 1;
#ifndef __PVFS2_TROVE_AIO_THREADED__
        /*
           setting this state flag tells the caller not to re-add this
           operation to the normal dbpf-op queue because it will be
           started automatically (internally) on completion of other
           I/O operations
         */
        dbpf_op_change_status(cur_op, OP_INTERNALLY_DELAYED);
#endif

        gossip_debug(GOSSIP_TROVE_DEBUG, "delayed I/O operation %p "
                     "(%d already in progress)\n",
                     cur_op, s_dbpf_ios_in_progress);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF I/O ops in progress: %d\n",
                 s_dbpf_ios_in_progress);

    gen_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);

    if (!op_delayed)
    {
        if (gossip_debug_enabled(GOSSIP_TROVE_DEBUG))
        {

            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "lio_listio called with the following aiocbs:\n");
            for (i = 0; i < aiocb_inuse_count; i++)
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "aiocb_ptr_array[%d]: fd: %d, "
                             "off: %lld, bytes: %d, buf: %p, type: %d\n",
                             i, aiocb_ptr_array[i]->aio_fildes,
                             lld(aiocb_ptr_array[i]->aio_offset),
                             (int) aiocb_ptr_array[i]->aio_nbytes,
                             aiocb_ptr_array[i]->aio_buf,
                             (int) aiocb_ptr_array[i]->aio_lio_opcode);
            }
        }

        ret = lio_listio(LIO_NOWAIT, aiocb_ptr_array, aiocb_inuse_count, sig);
        if (ret != 0)
        {
            s_dbpf_ios_in_progress--;
            gossip_lerr("lio_listio() returned %d\n", ret);
            dbpf_open_cache_put(&cur_op->op.u.b_rw_list.open_ref);
            return -trove_errno_to_trove_error(errno);
        }
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: lio_listio posted %p "
                     "(handle %llu, ret %d)\n", __func__, cur_op,
                     llu(cur_op->op.handle), ret);
    }
    return 0;
}

static int dbpf_bstream_read_at(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    void *buffer,
    TROVE_size * inout_size_p,
    TROVE_offset offset,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
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
                        BSTREAM_READ_AT,
                        handle,
                        coll_p,
                        dbpf_bstream_read_at_op_svc,
                        user_ptr, flags, context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_read_at.offset = offset;
    q_op_p->op.u.b_read_at.size = *inout_size_p;
    q_op_p->op.u.b_read_at.buffer = buffer;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p, &dbpf_op_queue[OP_QUEUE_IO]);

    return 0;
}

/* dbpf_bstream_read_at_op_svc()
 *
 * Returns 1 on completion, -TROVE_errno on error, 0 on not done.
 */
static int dbpf_bstream_read_at_op_svc(
    struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_fd = 0;
    struct open_cache_ref tmp_ref;

    ret = dbpf_open_cache_get(op_p->coll_p->coll_id, op_p->handle, 0, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    got_fd = 1;

    ret = DBPF_LSEEK(tmp_ref.fd, op_p->u.b_read_at.offset, SEEK_SET);
    if (ret < 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto return_error;
    }

    ret = DBPF_READ(tmp_ref.fd, op_p->u.b_read_at.buffer,
                    op_p->u.b_read_at.size);
    if (ret < 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto return_error;
    }

    dbpf_open_cache_put(&tmp_ref);

    gossip_debug(GOSSIP_TROVE_DEBUG, "read %d bytes.\n", ret);

    return 1;

  return_error:
    if (got_fd)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
}

static int dbpf_bstream_write_at(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    void *buffer,
    TROVE_size * inout_size_p,
    TROVE_offset offset,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
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
                        BSTREAM_WRITE_AT,
                        handle,
                        coll_p,
                        dbpf_bstream_write_at_op_svc,
                        user_ptr, flags, context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_write_at.offset = offset;
    q_op_p->op.u.b_write_at.size = *inout_size_p;
    q_op_p->op.u.b_write_at.buffer = buffer;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p, &dbpf_op_queue[OP_QUEUE_IO]);

    return 0;
}

static int dbpf_bstream_write_at_op_svc(
    struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_fd = 0;
    struct open_cache_ref tmp_ref;
    TROVE_object_ref ref = { op_p->handle, op_p->coll_p->coll_id };

    ret = dbpf_open_cache_get(op_p->coll_p->coll_id, op_p->handle, 1, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    got_fd = 1;

    ret = DBPF_LSEEK(tmp_ref.fd, op_p->u.b_write_at.offset, SEEK_SET);
    if (ret < 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto return_error;
    }

    ret = DBPF_WRITE(tmp_ref.fd, op_p->u.b_write_at.buffer,
                     op_p->u.b_write_at.size);
    if (ret < 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto return_error;
    }

    /* remove cached attribute for this handle if it's present */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_remove(ref);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    DBPF_ERROR_SYNC_IF_NECESSARY(op_p, tmp_ref.fd);

    dbpf_open_cache_put(&tmp_ref);

    gossip_debug(GOSSIP_TROVE_DEBUG, "wrote %d bytes.\n", ret);

    return 1;

  return_error:
    if (got_fd)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
}

static int dbpf_bstream_flush(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
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
                        BSTREAM_FLUSH,
                        handle,
                        coll_p,
                        dbpf_bstream_flush_op_svc, user_ptr, flags, context_id);

    *out_op_id_p = dbpf_queued_op_queue(q_op_p, &dbpf_op_queue[OP_QUEUE_IO]);
    return 0;
}

/* returns 1 on completion, -TROVE_errno on error, 0 on not done */
static int dbpf_bstream_flush_op_svc(
    struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_fd = 0;
    struct open_cache_ref tmp_ref;

    ret = dbpf_open_cache_get(op_p->coll_p->coll_id, op_p->handle, 1, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    got_fd = 1;

    ret = DBPF_SYNC(tmp_ref.fd);
    if (ret != 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto return_error;
    }
    dbpf_open_cache_put(&tmp_ref);
    return 1;

  return_error:
    if (got_fd)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
}

static int dbpf_bstream_resize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size * inout_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
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
                        BSTREAM_RESIZE,
                        handle,
                        coll_p,
                        dbpf_bstream_resize_op_svc,
                        user_ptr, flags, context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_resize.size = *inout_size_p;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p, &dbpf_op_queue[OP_QUEUE_IO]);

    return 0;
}

static int dbpf_bstream_resize_op_svc(
    struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_fd = 0;
    struct open_cache_ref tmp_ref;
    TROVE_object_ref ref = { op_p->handle, op_p->coll_p->coll_id };

    ret = dbpf_open_cache_get(op_p->coll_p->coll_id, op_p->handle, 1, &tmp_ref);
    if (ret < 0)
    {
        goto return_error;
    }
    got_fd = 1;

    ret = DBPF_RESIZE(tmp_ref.fd, op_p->u.b_resize.size);
    if (ret != 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto return_error;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "  RESIZED bstream %llu [fd = %d] "
                 "to %lld \n", llu(op_p->handle), tmp_ref.fd,
                 lld(op_p->u.b_resize.size));

    dbpf_open_cache_put(&tmp_ref);

    /* adjust size in cached attribute element, if present */
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_ds_attr_update_cached_data_bsize(ref,
                                                     op_p->u.b_resize.size);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    return 1;

  return_error:
    if (got_fd)
    {
        dbpf_open_cache_put(&tmp_ref);
    }
    return ret;
}

static int dbpf_bstream_validate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_read_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
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
                                user_ptr, context_id, out_op_id_p, LIO_READ);
}

static int dbpf_bstream_write_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
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
                                user_ptr, context_id, out_op_id_p, LIO_WRITE);
}

/* dbpf_bstream_rw_list()
 *
 * Handles queueing of both read and write list operations
 *
 * opcode parameter should be LIO_READ or LIO_WRITE
 */
static inline int dbpf_bstream_rw_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p,
    int opcode)
{
    int ret = -TROVE_EINVAL;
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    enum dbpf_op_type tmp_type;
    int event_type;
    int i = 0;
#ifdef __PVFS2_TROVE_AIO_THREADED__
    struct dbpf_op *op_p = NULL;
    int aiocb_inuse_count = 0;
    struct aiocb *aiocb_p = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = { 0 };
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
    dbpf_queued_op_init(q_op_p, tmp_type, handle, coll_p,
#ifdef __PVFS2_TROVE_AIO_THREADED__
                        NULL,
#else
                        dbpf_bstream_rw_list_op_svc,
#endif
                        user_ptr, flags, context_id);

    DBPF_EVENT_START(event_type, q_op_p->op.id);

    if (gossip_debug_enabled(GOSSIP_TROVE_DEBUG))
    {
        PVFS_size count_mem = 0, count_stream = 0;
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "dbpf_bstream_rw_list: mem_count: %d, stream_count: %d\n",
                     mem_count, stream_count);
        for (i = 0; i < mem_count; ++i)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "dbpf_bstream_rw_list: mem_offset: %p, mem_size: %Ld\n",
                         mem_offset_array[i], lld(mem_size_array[i]));
            count_mem += mem_size_array[i];
        }

        for (i = 0; i < stream_count; ++i)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "dbpf_bstream_rw_list: "
                         "stream_offset: %Ld, stream_size: %Ld\n",
                         lld(stream_offset_array[i]),
                         lld(stream_size_array[i]));
            count_stream += stream_size_array[i];
        }

        if (count_mem != count_stream)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "dbpf_bstream_rw_list: "
                         "mem_count: %Ld != stream_count: %Ld\n",
                         lld(count_mem), lld(count_stream));
        }
    }

    /* initialize op-specific members */
    q_op_p->op.u.b_rw_list.fd = -1;
    q_op_p->op.u.b_rw_list.opcode = opcode;
    q_op_p->op.u.b_rw_list.mem_array_count = mem_count;
    q_op_p->op.u.b_rw_list.mem_offset_array = mem_offset_array;
    q_op_p->op.u.b_rw_list.mem_size_array = mem_size_array;
    q_op_p->op.u.b_rw_list.stream_array_count = stream_count;
    q_op_p->op.u.b_rw_list.stream_offset_array = stream_offset_array;
    q_op_p->op.u.b_rw_list.stream_size_array = stream_size_array;

    /* initialize the out size to 0 */
    *out_size_p = 0;
    q_op_p->op.u.b_rw_list.out_size_p = out_size_p;
    q_op_p->op.u.b_rw_list.aiocb_array_count = 0;
    q_op_p->op.u.b_rw_list.aiocb_array = NULL;
#ifndef __PVFS2_TROVE_AIO_THREADED__
    q_op_p->op.u.b_rw_list.queued_op_ptr = (void *) q_op_p;
#endif

    /* initialize list processing state (more op-specific members) */
    q_op_p->op.u.b_rw_list.lio_state.mem_ct = 0;
    q_op_p->op.u.b_rw_list.lio_state.stream_ct = 0;
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_size = mem_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_off = mem_offset_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_size = stream_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_off = stream_offset_array[0];

    q_op_p->op.u.b_rw_list.list_proc_state = LIST_PROC_INITIALIZED;

    ret = dbpf_open_cache_get(coll_id, handle, (opcode == LIO_WRITE) ? 1 : 0,
                              &q_op_p->op.u.b_rw_list.open_ref);
    if (ret < 0)
    {
        dbpf_queued_op_free(q_op_p);
        gossip_ldebug(GOSSIP_TROVE_DEBUG,
                      "warning: useless error value: %d\n", ret);
        return ret;
    }
    q_op_p->op.u.b_rw_list.fd = q_op_p->op.u.b_rw_list.open_ref.fd;

    /*
       if we're doing an i/o write, remove the cached attribute for
       this handle if it's present
     */
    if (opcode == LIO_WRITE)
    {
        TROVE_object_ref ref = { handle, coll_id };
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        dbpf_attr_cache_remove(ref);
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }

#ifndef __PVFS2_TROVE_AIO_THREADED__

    *out_op_id_p = dbpf_queued_op_queue(q_op_p, & dbpf_op_queue[OP_QUEUE_IO]);

#else
    op_p = &q_op_p->op;

    /*
       instead of queueing the op like most other trove operations,
       we're going to issue the system aio calls here to begin being
       serviced immediately.  We'll check progress in the
       aio_progress_notification callback method; this array is freed
       in dbpf-op.c:dbpf_queued_op_free
     */
    aiocb_p = (struct aiocb *) malloc((AIOCB_ARRAY_SZ * sizeof(struct aiocb)));
    if (aiocb_p == NULL)
    {
        dbpf_open_cache_put(&q_op_p->op.u.b_rw_list.open_ref);
        return -TROVE_ENOMEM;
    }

    memset(aiocb_p, 0, (AIOCB_ARRAY_SZ * sizeof(struct aiocb)));
    for (i = 0; i < AIOCB_ARRAY_SZ; i++)
    {
        aiocb_p[i].aio_lio_opcode = LIO_NOP;
        aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
    }

    op_p->u.b_rw_list.aiocb_array_count = AIOCB_ARRAY_SZ;
    op_p->u.b_rw_list.aiocb_array = aiocb_p;
    op_p->u.b_rw_list.list_proc_state = LIST_PROC_INPROGRESS;

    /* convert listio arguments into aiocb structures */
    aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
    ret = dbpf_bstream_listio_convert(op_p->u.b_rw_list.fd,
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
    op_p->u.b_rw_list.sigev.sigev_notify_function = aio_progress_notification;
    op_p->u.b_rw_list.sigev.sigev_value.sival_ptr = (void *) q_op_p;

    /* mark unused with LIO_NOPs */
    for (i = aiocb_inuse_count; i < op_p->u.b_rw_list.aiocb_array_count; i++)
    {
        aiocb_p[i].aio_lio_opcode = LIO_NOP;
    }

    for (i = 0; i < aiocb_inuse_count; i++)
    {
        aiocb_ptr_array[i] = &aiocb_p[i];
    }

    assert(q_op_p == op_p->u.b_rw_list.sigev.sigev_value.sival_ptr);

    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
    {
        op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
    }

    dbpf_op_change_status(q_op_p, OP_IN_SERVICE);

    id_gen_safe_register(&q_op_p->op.id, q_op_p);
    *out_op_id_p = q_op_p->op.id;

    ret =
        issue_or_delay_io_operation(q_op_p, aiocb_ptr_array, aiocb_inuse_count,
                                    &op_p->u.b_rw_list.sigev, 0);

    if (ret)
    {
        return ret;
    }
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
static int dbpf_bstream_rw_list_op_svc(
    struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, i = 0, aiocb_inuse_count = 0;
    int op_in_progress_count = 0;
    struct aiocb *aiocb_p = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ];

    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_INITIALIZED)
    {
        /*
           first call; need to allocate aiocb array and ptr array;
           this array is freed in dbpf-op.c:dbpf_queued_op_free
         */
        aiocb_p = malloc(AIOCB_ARRAY_SZ * sizeof(struct aiocb));
        if (aiocb_p == NULL)
        {
            return -TROVE_ENOMEM;
        }

        memset(aiocb_p, 0, AIOCB_ARRAY_SZ * sizeof(struct aiocb));
        for (i = 0; i < AIOCB_ARRAY_SZ; i++)
        {
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
            aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
        }

        op_p->u.b_rw_list.aiocb_array_count = AIOCB_ARRAY_SZ;
        op_p->u.b_rw_list.aiocb_array = aiocb_p;
        op_p->u.b_rw_list.list_proc_state = LIST_PROC_INPROGRESS;
        op_p->u.b_rw_list.sigev.sigev_notify = SIGEV_NONE;
    }
    else
    {
        /* operations potentially in progress */
        aiocb_p = op_p->u.b_rw_list.aiocb_array;

        /* check to see how we're progressing on previous operations */
        for (i = 0; i < op_p->u.b_rw_list.aiocb_array_count; i++)
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

                gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s complete: "
                             "aio_return() ret %d (fd %d)\n",
                             __func__,
                             ((op_p->type == BSTREAM_WRITE_LIST) ||
                              (op_p->type == BSTREAM_WRITE_AT) ?
                              "WRITE" : "READ"), ret, op_p->u.b_rw_list.fd);

                /* aio_return doesn't seem to return bytes read/written if 
                 * sigev_notify == SIGEV_NONE, so we set the out size 
                 * from what's requested.  For reads we just leave as zero,
                 * which ends up being OK,
                 * since the amount read (if past EOF its less than requested)
                 * is determined from the bstream size.
                 */
                if (op_p->type == BSTREAM_WRITE_LIST ||
                    op_p->type == BSTREAM_WRITE_AT)
                {
                    *(op_p->u.b_rw_list.out_size_p) += aiocb_p[i].aio_nbytes;
                }

                /* mark as a NOP so we ignore it from now on */
                aiocb_p[i].aio_lio_opcode = LIO_NOP;
            }
            else if (ret != EINPROGRESS)
            {
                gossip_err("%s: aio_error on block %d, skipping: %s\n",
                           __func__, i, strerror(ret));
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
        if ((op_p->type == BSTREAM_WRITE_AT) ||
            (op_p->type == BSTREAM_WRITE_LIST))
        {
            DBPF_AIO_SYNC_IF_NECESSARY(op_p, op_p->u.b_rw_list.fd, ret);
        }

        dbpf_open_cache_put(&op_p->u.b_rw_list.open_ref);
        op_p->u.b_rw_list.fd = -1;

        start_delayed_ops_if_any(1);
        return ret;
    }
    else
    {
        /* no operations in progress; convert and post some more */
        aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
        ret = dbpf_bstream_listio_convert(op_p->u.b_rw_list.fd,
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

        /* mark unused with LIO_NOPs */
        for (i = aiocb_inuse_count;
             i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }

        for (i = 0; i < aiocb_inuse_count; i++)
        {
            aiocb_ptr_array[i] = &aiocb_p[i];
        }

        if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
        {
            op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
        }

        /*
           we use a reverse mapped ptr for I/O operations in order to
           access the queued op from the op.  this is only useful for
           the delayed io operation scheme.  it's initialized in
           dbpf_bstream_rw_list
         */
        assert(op_p->u.b_rw_list.queued_op_ptr);

        ret = issue_or_delay_io_operation((dbpf_queued_op_t *) op_p->u.
                                          b_rw_list.queued_op_ptr,
                                          aiocb_ptr_array, aiocb_inuse_count,
                                          &op_p->u.b_rw_list.sigev, 1);

        if (ret)
        {
            return ret;
        }
        return 0;
    }
}
#endif
 
int dbpf_bstream_listio_convert(
    int fd,
    int op_type,
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    struct aiocb *aiocb_array,
    int *aiocb_count_p,
    struct bstream_listio_state *lio_state)
{
    int mct, sct, act = 0;
    int oom = 0, oos = 0;
    TROVE_size cur_mem_size = 0;
    char *cur_mem_off = NULL;
    TROVE_size cur_stream_size = 0;
    TROVE_offset cur_stream_off = 0;
    struct aiocb *cur_aiocb_ptr = NULL;

    if (lio_state == NULL)
    {
	mct = 0;
	sct = 0;
	cur_mem_size = mem_size_array[0];
	cur_mem_off = mem_offset_array[0];
	cur_stream_size = stream_size_array[0];
	cur_stream_off = stream_offset_array[0];
    }
    else
    {
	mct = lio_state->mem_ct;
	sct = lio_state->stream_ct;
	cur_mem_size = lio_state->cur_mem_size;
	cur_mem_off = lio_state->cur_mem_off;
	cur_stream_size = lio_state->cur_stream_size;
	cur_stream_off = lio_state->cur_stream_off;
    }
    cur_aiocb_ptr = aiocb_array;

    /* _POSIX_AIO_LISTIO_MAX */

    while (act < *aiocb_count_p)
    {
        assert(fd > 0);
	cur_aiocb_ptr->aio_fildes = fd;
	cur_aiocb_ptr->aio_offset = cur_stream_off;
	cur_aiocb_ptr->aio_buf = cur_mem_off;
	cur_aiocb_ptr->aio_reqprio = 0;
	cur_aiocb_ptr->aio_lio_opcode = op_type;
	cur_aiocb_ptr->aio_sigevent.sigev_notify = SIGEV_NONE;

        /*
          determine if we're either out of memory (oom) regions, or
          out of stream (oos) regions
        */
        /* in many (all?) cases mem_count is 1, so oom will end up being 1 */
        oom = (((mct + 1) < mem_count) ? 0 : 1);
        oos = (((sct + 1) < stream_count) ? 0 : 1);

	if (cur_mem_size == cur_stream_size)
        {
	    /* consume both mem and stream regions */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

            if (!oom)
            {
                cur_mem_size = mem_size_array[++mct];
                cur_mem_off  = mem_offset_array[mct];
            }
	    else
	    {
		cur_mem_size = 0;
	    }
            if (!oos)
            {
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	    else 
	    {
		cur_stream_size = 0;
	    }
	}
	else if (cur_mem_size < cur_stream_size)
        {
	    /* consume mem region and update stream region */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

	    cur_stream_size -= cur_mem_size;
	    cur_stream_off  += cur_mem_size;

            if (!oom)
            {
                cur_mem_size = mem_size_array[++mct];
                cur_mem_off  = mem_offset_array[mct];
            }
	    else
	    {
		cur_mem_size = 0;
	    }
	}
	else /* cur_mem_size > cur_stream_size */
        {
	    /* consume stream region and update mem region */
	    cur_aiocb_ptr->aio_nbytes = cur_stream_size;

	    cur_mem_size -= cur_stream_size;
	    cur_mem_off  += cur_stream_size;

            if (!oos)
            {
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	    else
	    {
		cur_stream_size = 0;
	    }
	}
	cur_aiocb_ptr = &aiocb_array[++act];

        /* process until there are no bytes left in the current piece */
        if ((oom && cur_mem_size == 0) || (oos && cur_stream_size == 0))
        {
            break;
        }
    }

    /* return the number actually used */
    *aiocb_count_p = act;

    /* until we've consumed everything we have, we're not finished */
    if (!oom || !oos)
    {
	/* haven't processed all of list regions */
	if (lio_state != NULL)
        {
            /* if we ran out of streams, increment mem_ct */
	    lio_state->mem_ct = (!oos) ? mct : ++mct;
            /* if we ran out of mems, increment stream_ct */
	    lio_state->stream_ct = (!oom) ? sct : ++sct;
	    lio_state->cur_mem_size = cur_mem_size;
	    lio_state->cur_mem_off = cur_mem_off;
	    lio_state->cur_stream_size = cur_stream_size;
	    lio_state->cur_stream_off = cur_stream_off;
	}
	return 0;
    }
    return 1;
}

#if 0
static void aiocb_print(struct aiocb *ptr)
{
    static char lio_write[] = "LIO_WRITE";
    static char lio_read[] = "LIO_READ";
    static char lio_nop[] = "LOP_NOP";
    static char sigev_none[] = "SIGEV_NONE";
    static char invalid_value[] = "invalid value";
    char *opcode, *sigev;

    opcode = (ptr->aio_lio_opcode == LIO_WRITE) ? lio_write :
	(ptr->aio_lio_opcode == LIO_READ) ? lio_read :
	(ptr->aio_lio_opcode == LIO_NOP) ? lio_nop : invalid_value;
    sigev = ((ptr->aio_sigevent.sigev_notify == SIGEV_NONE) ?
             sigev_none : invalid_value);

    gossip_debug(GOSSIP_TROVE_DEBUG, "aio_fildes = %d, aio_offset = %d, "
                 "aio_buf = %n, aio_nbytes = %zu, aio_reqprio = %d, "
                 "aio_lio_opcode = %s, aio_sigevent.sigev_notify = %s\n",
                 ptr->aio_fildes, (int)ptr->aio_offset,
                 ptr->aio_buf, ptr->aio_nbytes,
                 ptr->aio_reqprio, opcode, sigev);
}
#endif


struct TROVE_bstream_ops dbpf_bstream_ops = {
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    dbpf_bstream_read_list,
    dbpf_bstream_write_list,
    dbpf_bstream_flush
};
#endif /* use AIO */
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

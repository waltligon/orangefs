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
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
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
#include "dbpf-sync.h"

#include "dbpf-alt-aio.h"

extern gen_mutex_t dbpf_attr_cache_mutex;

#define AIOCB_ARRAY_SZ 64

extern int TROVE_max_concurrent_io;
static int s_dbpf_ios_in_progress = 0;
static dbpf_op_queue_p s_dbpf_io_ready_queue = NULL;
static gen_mutex_t s_dbpf_io_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t dbpf_update_size_lock = GEN_MUTEX_INITIALIZER;

static struct dbpf_aio_ops aio_ops;

static int issue_or_delay_io_operation(
    dbpf_queued_op_t *cur_op, struct aiocb **aiocb_ptr_array,
    int aiocb_inuse_count, struct sigevent *sig, int dec_first);
static void start_delayed_ops_if_any(int dec_first);

#ifdef __PVFS2_TROVE_AIO_THREADED__
static char *list_proc_state_strings[] __attribute__((unused)) = {
    "LIST_PROC_INITIALIZED",
    "LIST_PROC_INPROGRESS ",
    "LIST_PROC_ALLCONVERTED",
    "LIST_PROC_ALLPOSTED",
};
#endif

#ifndef __PVFS2_TROVE_AIO_THREADED__
static int dbpf_bstream_rw_list_op_svc(struct dbpf_op *op_p);
#endif
static int dbpf_bstream_flush_op_svc(struct dbpf_op *op_p);

#ifdef __PVFS2_TROVE_AIO_THREADED__
#include "dbpf-thread.h"
#include "pvfs2-internal.h"

extern pthread_cond_t dbpf_op_completed_cond;
extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];

static void aio_progress_notification(union sigval sig)
{
    dbpf_queued_op_t *cur_op = NULL;
    struct dbpf_op *op_p = NULL;
    int ret, i, aiocb_inuse_count, state = 0;
    struct aiocb *aiocb_p = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = {0};
    PVFS_size eor = -1;
    int j;
    TROVE_ds_attributes attr;
    TROVE_object_ref ref;
    int sync_required = 0;

    cur_op = (dbpf_queued_op_t *)sig.sival_ptr;
    assert(cur_op);

    op_p = &cur_op->op;
    assert(op_p);

    gossip_debug(
        GOSSIP_TROVE_DEBUG," --- aio_progress_notification called "
        "with handle %llu (%p)\n", llu(op_p->handle), cur_op);

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
        ret = op_p->u.b_rw_list.aio_ops->aio_error(&aiocb_p[i]);
        if (ret == 0)
        {
            /* aio_return gets the return value of the individual op */
            ret = op_p->u.b_rw_list.aio_ops->aio_return(&aiocb_p[i]);

            gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s complete: "
                         "aio_return() says %d [fd = %d]\n",
                         __func__, dbpf_op_type_to_str(op_p->type),
                         ret, op_p->u.b_rw_list.fd);

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
            DBPF_AIO_SYNC_IF_NECESSARY(
                op_p, op_p->u.b_rw_list.fd, ret);

            /* TODO: need similar logic for non-threaded aio case too */

            /* calculate end of request */
            for(j=0; j<op_p->u.b_rw_list.stream_array_count; j++)
            {
                if(eor < op_p->u.b_rw_list.stream_offset_array[j] + 
                    op_p->u.b_rw_list.stream_size_array[j])
                {
                    eor = op_p->u.b_rw_list.stream_offset_array[j] + 
                        op_p->u.b_rw_list.stream_size_array[j];
                }
            }

            ref.fs_id = op_p->coll_p->coll_id;
            ref.handle = op_p->handle;

            gen_mutex_lock(&dbpf_update_size_lock);
            ret = dbpf_dspace_attr_get(op_p->coll_p, ref, &attr);
            if(ret != 0)
            {
                gen_mutex_unlock(&dbpf_update_size_lock);
                goto error_in_cleanup;
            }

            if(eor > attr.u.datafile.b_size)
            {
                /* set the size of the file */
                attr.u.datafile.b_size = eor;
                ret = dbpf_dspace_attr_set(op_p->coll_p, ref, &attr);
                if(ret != 0)
                {
                    gen_mutex_unlock(&dbpf_update_size_lock);
                    goto error_in_cleanup;
                }
                if(op_p->flags & TROVE_SYNC)
                {
                    sync_required = 1;
                }
            }
            gen_mutex_unlock(&dbpf_update_size_lock);
        }

error_in_cleanup:

        dbpf_open_cache_put(&op_p->u.b_rw_list.open_ref);
        op_p->u.b_rw_list.fd = -1;
        
	cur_op->state = ret;
        /* this is a macro defined in dbpf-thread.h */

        if(sync_required)
        {
            int outcount;

            gossip_debug(GOSSIP_TROVE_DEBUG, 
                "aio updating size for handle %llu\n", llu(ref.handle));

            /* If we updated the size, then convert cur_op into a setattr.
             * Note that we are not actually going to perform a setattr.
             * We just want the coalescing path to treat it like a setattr
             * so that the size update is synced before we complete.
             */

            /* We need to free the aiocb_array in this case, since the
             * dbpf_queued_op_free function won't know to do that anymore
             */
            free(cur_op->op.u.b_rw_list.aiocb_array);
            cur_op->op.u.b_rw_list.aiocb_array = NULL;

            dbpf_queued_op_init(cur_op,
                                DSPACE_SETATTR,
                                ref.handle,
                                cur_op->op.coll_p,
                                dbpf_dspace_setattr_op_svc,
                                cur_op->op.user_ptr,
                                TROVE_SYNC,
                                cur_op->op.context_id);
            cur_op->op.state = OP_IN_SERVICE;
            dbpf_sync_coalesce(cur_op, 0, &outcount);
        }
        else
        {
            dbpf_queued_op_complete(cur_op, OP_COMPLETED);
        }

        gossip_debug(GOSSIP_TROVE_DEBUG, "*** starting delayed ops if any "
                     "(state is %s)\n", 
                     list_proc_state_strings[
                     op_p->u.b_rw_list.list_proc_state]);

        start_delayed_ops_if_any(1);

    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "*** issuing more aio requests "
                     "(state is %s)\n", 
                     list_proc_state_strings[
                     op_p->u.b_rw_list.list_proc_state]);

        /* no operations in progress; convert and post some more */
        op_p->u.b_rw_list.aiocb_array_count = AIOCB_ARRAY_SZ;
        op_p->u.b_rw_list.aiocb_array = aiocb_p;

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

        /* mark the unused with LIO_NOPs */
        for(i = aiocb_inuse_count;
            i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
            /* mark these as NOPs and we'll ignore them */
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }

        for(i = 0; i < aiocb_inuse_count; i++)
        {
            aiocb_ptr_array[i] = &aiocb_p[i];
        }

        assert(cur_op == op_p->u.b_rw_list.sigev.sigev_value.sival_ptr);

        if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
        {
            op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;
        }

        ret = issue_or_delay_io_operation(
            cur_op, aiocb_ptr_array, aiocb_inuse_count,
            &op_p->u.b_rw_list.sigev, 1);

        if (ret)
        {
            gossip_lerr("issue_or_delay_io_operation() returned "
                        "%d\n", ret);
        }
    }
}
#endif /* __PVFS2_TROVE_AIO_THREADED__ */

static void start_delayed_ops_if_any(int dec_first)
{
    int ret = 0;
    dbpf_queued_op_t *cur_op = NULL;
    int i = 0, aiocb_inuse_count = 0;
    struct aiocb *aiocbs = NULL, *aiocb_ptr_array[AIOCB_ARRAY_SZ] = {0};

    gen_mutex_lock(&s_dbpf_io_mutex);
    if (dec_first)
    {
        s_dbpf_ios_in_progress--;
    }
    gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF I/O ops in progress: %d\n",
        s_dbpf_ios_in_progress);

    if (s_dbpf_io_ready_queue == NULL)
    {
        s_dbpf_io_ready_queue = dbpf_op_queue_new();
    }
    assert(s_dbpf_io_ready_queue);

    if (!dbpf_op_queue_empty(s_dbpf_io_ready_queue))
    {
        cur_op = dbpf_op_queue_shownext(s_dbpf_io_ready_queue);
        assert(cur_op);
#ifndef __PVFS2_TROVE_AIO_THREADED__
        assert(cur_op->op.state == OP_INTERNALLY_DELAYED);
#endif
        assert((cur_op->op.type == BSTREAM_READ_AT) ||
               (cur_op->op.type == BSTREAM_READ_LIST) ||
               (cur_op->op.type == BSTREAM_WRITE_AT) ||
               (cur_op->op.type == BSTREAM_WRITE_LIST));
        dbpf_op_queue_remove(cur_op);

        gossip_debug(GOSSIP_TROVE_DEBUG, "starting delayed I/O "
                     "operation %p (%d in progress)\n", cur_op,
                     s_dbpf_ios_in_progress);

        aiocbs = cur_op->op.u.b_rw_list.aiocb_array;
        assert(aiocbs);

        for(i = 0; i < AIOCB_ARRAY_SZ; i++)
        {
            if (aiocbs[i].aio_lio_opcode != LIO_NOP)
            {
                aiocb_inuse_count++;
            }
        }

        for(i = 0; i < aiocb_inuse_count; i++)
        {
            aiocb_ptr_array[i] = &aiocbs[i];
        }

        if(gossip_debug_enabled(GOSSIP_TROVE_DEBUG))
        {

            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "lio_listio called with %d following aiocbs:\n", 
                         aiocb_inuse_count);
            for(i=0; i<aiocb_inuse_count; i++)
            {
                gossip_debug(
                    GOSSIP_TROVE_DEBUG,
                    "aiocb_ptr_array[%d]: fd: %d, off: %lld, "
                    "bytes: %d, buf: %p, type: %d\n",
                    i, 
                    aiocb_ptr_array[i]->aio_fildes,
                    lld(aiocb_ptr_array[i]->aio_offset),
                    (int)aiocb_ptr_array[i]->aio_nbytes,
                    aiocb_ptr_array[i]->aio_buf,
                    (int)aiocb_ptr_array[i]->aio_lio_opcode);
            }
        }

        ret = cur_op->op.u.b_rw_list.aio_ops->lio_listio(
            LIO_NOWAIT, aiocb_ptr_array, aiocb_inuse_count,
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
        dbpf_queued_op_queue_nolock(cur_op);
#endif
    }
  error_exit:
    gen_mutex_unlock(&s_dbpf_io_mutex);
}

static int issue_or_delay_io_operation(
    dbpf_queued_op_t *cur_op, struct aiocb **aiocb_ptr_array,
    int aiocb_inuse_count, struct sigevent *sig, int dec_first)
{
    int ret = -TROVE_EINVAL, op_delayed = 0;
    int i;
    assert(cur_op);

    gen_mutex_lock(&s_dbpf_io_mutex);
    if (dec_first)
    {
        s_dbpf_ios_in_progress--;
    }
    if (s_dbpf_ios_in_progress < TROVE_max_concurrent_io)
    {
        s_dbpf_ios_in_progress++;
    }
    else
    {
        if (s_dbpf_io_ready_queue == NULL)
        {
            s_dbpf_io_ready_queue = dbpf_op_queue_new();
            if (!s_dbpf_io_ready_queue)
            {
                return -TROVE_ENOMEM;
            }
        }
        assert(s_dbpf_io_ready_queue);
        dbpf_op_queue_add(s_dbpf_io_ready_queue, cur_op);

        op_delayed = 1;
#ifndef __PVFS2_TROVE_AIO_THREADED__
        /*
          setting this state flag tells the caller not to re-add this
          operation to the normal dbpf-op queue because it will be
          started automatically (internally) on completion of other
          I/O operations
        */
        gen_mutex_lock(&cur_op->mutex);
        cur_op->op.state = OP_INTERNALLY_DELAYED;
        gen_mutex_unlock(&cur_op->mutex);
#endif

        gossip_debug(GOSSIP_TROVE_DEBUG, "delayed I/O operation %p "
                     "(%d already in progress)\n",
                     cur_op, s_dbpf_ios_in_progress);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF I/O ops in progress: %d\n",
        s_dbpf_ios_in_progress);

    gen_mutex_unlock(&s_dbpf_io_mutex);

    if (!op_delayed)
    {
        if(gossip_debug_enabled(GOSSIP_TROVE_DEBUG))
        {

            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "lio_listio called with the following aiocbs:\n");
            for(i=0; i<aiocb_inuse_count; i++)
            {
                gossip_debug(GOSSIP_TROVE_DEBUG,
                             "aiocb_ptr_array[%d]: fd: %d, "
                             "off: %lld, bytes: %d, buf: %p, type: %d\n",
                             i, aiocb_ptr_array[i]->aio_fildes,
                             lld(aiocb_ptr_array[i]->aio_offset),
                             (int)aiocb_ptr_array[i]->aio_nbytes,
                             aiocb_ptr_array[i]->aio_buf,
                             (int)aiocb_ptr_array[i]->aio_lio_opcode);

            }
        }

        ret = cur_op->op.u.b_rw_list.aio_ops->lio_listio(
            LIO_NOWAIT, aiocb_ptr_array,
            aiocb_inuse_count, sig);
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

int dbpf_bstream_read_at(TROVE_coll_id coll_id,
                         TROVE_handle handle,
                         void *buffer,
                         TROVE_size *inout_size_p,
                         TROVE_offset offset,
                         TROVE_ds_flags flags,
                         TROVE_vtag_s *vtag,
                         void *user_ptr,
                         TROVE_context_id context_id,
                         TROVE_op_id *out_op_id_p,
                         PVFS_hint  hints)
{
    return -TROVE_ENOSYS;
}

int dbpf_bstream_write_at(TROVE_coll_id coll_id,
                          TROVE_handle handle,
                          void *buffer,
                          TROVE_size *inout_size_p,
                          TROVE_offset offset,
                          TROVE_ds_flags flags,
                          TROVE_vtag_s *vtag,
                          void *user_ptr,
                          TROVE_context_id context_id,
                          TROVE_op_id *out_op_id_p,
                          PVFS_hint  hints)
{
    return -TROVE_ENOSYS;
}

int dbpf_bstream_flush(TROVE_coll_id coll_id,
                       TROVE_handle handle,
                       TROVE_ds_flags flags,
                       void *user_ptr,
                       TROVE_context_id context_id,
                       TROVE_op_id *out_op_id_p,
                       PVFS_hint  hints)
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
                        dbpf_bstream_flush_op_svc,
                        user_ptr,
                        flags,
                        context_id);
    q_op_p->op.hints = hints;
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    return 0;
}

/* returns 1 on completion, -TROVE_errno on error, 0 on not done */
static int dbpf_bstream_flush_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, got_fd = 0;
    struct open_cache_ref tmp_ref;

    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle,
        DBPF_FD_BUFFERED_WRITE, &tmp_ref);
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

int dbpf_bstream_validate(TROVE_coll_id coll_id,
                          TROVE_handle handle,
                          TROVE_ds_flags flags,
                          TROVE_vtag_s *vtag,
                          void *user_ptr,
                          TROVE_context_id context_id,
                          TROVE_op_id *out_op_id_p,
                          PVFS_hint  hints)
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
                                  TROVE_op_id *out_op_id_p,
                                  PVFS_hint  hints)
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
                                LIO_READ,
                                &aio_ops,
                                hints);
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
                                   TROVE_op_id *out_op_id_p,
                                   PVFS_hint  hints)
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
                                LIO_WRITE,
                                &aio_ops,
                                hints);
}

/* dbpf_bstream_rw_list()
 *
 * Handles queueing of both read and write list operations
 *
 * opcode parameter should be LIO_READ or LIO_WRITE
 */
inline int dbpf_bstream_rw_list(TROVE_coll_id coll_id,
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
                                int opcode,
                                struct dbpf_aio_ops * aio_ops,
                                PVFS_hint  hints)
{
    int ret = -TROVE_EINVAL;
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    enum dbpf_op_type tmp_type;
    PINT_event_type event_type;
    int i;
    PVFS_size count_mem;
#ifdef __PVFS2_TROVE_AIO_THREADED__
    struct dbpf_op *op_p = NULL;
    int aiocb_inuse_count = 0;
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
        event_type = trove_dbpf_read_event_id;
    }
    else
    {
        tmp_type = BSTREAM_WRITE_LIST;
        event_type = trove_dbpf_write_event_id;
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

    if(PINT_EVENT_ENABLED)
    {
        count_mem = 0;
        for(i = 0; i < mem_count; ++i)
        {
            count_mem += mem_size_array[i];
        }
    }

    q_op_p->event_type = event_type;

    PINT_EVENT_START(event_type, dbpf_pid, NULL, &q_op_p->event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     PINT_HINT_GET_HANDLE(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints),
                     count_mem);

    if(gossip_debug_enabled(GOSSIP_TROVE_DEBUG))
    {
        PVFS_size count_stream = 0;
        count_mem = 0;
        gossip_debug(GOSSIP_TROVE_DEBUG, 
                     "dbpf_bstream_rw_list: mem_count: %d, stream_count: %d\n",
                     mem_count,
                     stream_count);
        for(i = 0; i < mem_count; ++i)
        {
            gossip_debug(
                GOSSIP_TROVE_DEBUG,
                "dbpf_bstream_rw_list: mem_offset: %p, mem_size: %Ld\n",
                mem_offset_array[i], lld(mem_size_array[i]));
            count_mem += mem_size_array[i];
        }

        for(i = 0; i < stream_count; ++i)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "dbpf_bstream_rw_list: "
                         "stream_offset: %Ld, stream_size: %Ld\n",
                         lld(stream_offset_array[i]), lld(stream_size_array[i]));
            count_stream += stream_size_array[i];
        }
        
        if(count_mem != count_stream)
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
    q_op_p->op.hints = hints;
    q_op_p->op.u.b_rw_list.aio_ops = aio_ops;

    /* initialize the out size to 0 */
    *out_size_p = 0;
    q_op_p->op.u.b_rw_list.out_size_p = out_size_p;
    q_op_p->op.u.b_rw_list.aiocb_array_count = 0;
    q_op_p->op.u.b_rw_list.aiocb_array = NULL;
#ifndef __PVFS2_TROVE_AIO_THREADED__
    q_op_p->op.u.b_rw_list.queued_op_ptr = (void *)q_op_p;
#endif

    /* initialize list processing state (more op-specific members) */
    q_op_p->op.u.b_rw_list.lio_state.mem_ct = 0;
    q_op_p->op.u.b_rw_list.lio_state.stream_ct = 0;
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_size = mem_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_off = mem_offset_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_size =
        stream_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_off =
        stream_offset_array[0];

    q_op_p->op.u.b_rw_list.list_proc_state = LIST_PROC_INITIALIZED;

    ret = dbpf_open_cache_get(
        coll_id, handle, 
        (opcode == LIO_WRITE) ? DBPF_FD_BUFFERED_WRITE : DBPF_FD_BUFFERED_READ, 
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
        TROVE_object_ref ref = {handle, coll_id};
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        dbpf_attr_cache_remove(ref);
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }

#ifndef __PVFS2_TROVE_AIO_THREADED__

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

#else
    op_p = &q_op_p->op;

    /*
      instead of queueing the op like most other trove operations,
      we're going to issue the system aio calls here to begin being
      serviced immediately.  We'll check progress in the
      aio_progress_notification callback method; this array is freed
      in dbpf-op.c:dbpf_queued_op_free
    */
    aiocb_p = (struct aiocb *)malloc(
        (AIOCB_ARRAY_SZ * sizeof(struct aiocb)));
    if (aiocb_p == NULL)
    {
        dbpf_open_cache_put(&q_op_p->op.u.b_rw_list.open_ref);
        return -TROVE_ENOMEM;
    }

    memset(aiocb_p, 0, (AIOCB_ARRAY_SZ * sizeof(struct aiocb)));
    for(i = 0; i < AIOCB_ARRAY_SZ; i++)
    {
        aiocb_p[i].aio_lio_opcode = LIO_NOP;
        aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
    }

    op_p->u.b_rw_list.aiocb_array_count = AIOCB_ARRAY_SZ;
    op_p->u.b_rw_list.aiocb_array = aiocb_p;
    op_p->u.b_rw_list.list_proc_state = LIST_PROC_INPROGRESS;

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

    /* mark unused with LIO_NOPs */
    for(i = aiocb_inuse_count;
        i < op_p->u.b_rw_list.aiocb_array_count; i++)
    {
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

    ret = issue_or_delay_io_operation(
        q_op_p, aiocb_ptr_array, aiocb_inuse_count,
        &op_p->u.b_rw_list.sigev, 0);

    if (ret)
    {
        return ret;
    }
#endif
    return 0;
}

static int dbpf_bstream_cancel(
    TROVE_coll_id coll_id,
    TROVE_op_id cancel_id,
    TROVE_context_id context_id)
{
    dbpf_queued_op_t *cur_op = NULL;
    int state, ret;

    cur_op = id_gen_fast_lookup(cancel_id);
    if (cur_op == NULL)
    {
        gossip_err("Invalid operation to test against\n");
        return -TROVE_EINVAL;
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

            gossip_debug(
                GOSSIP_TROVE_DEBUG, "op %p is canceled\n", cur_op);
            ret = 0;
        }
        break;
        case OP_IN_SERVICE:
        {
            /*
              for bstream i/o op, try an aio_cancel.  for other ops,
              there's not much we can do other than let the op
              complete normally
            */
            if (((cur_op->op.type == BSTREAM_READ_LIST) ||
                (cur_op->op.type == BSTREAM_WRITE_LIST)) &&
                (cur_op->op.u.b_rw_list.aio_ops != NULL))
            {
#if 0
                ret = aio_cancel(cur_op->op.u.b_rw_list.fd,
                                 cur_op->op.u.b_rw_list.aiocb_array);
#endif
                ret = cur_op->op.u.b_rw_list.aio_ops->aio_cancel(
                    cur_op->op.u.b_rw_list.fd,
                    cur_op->op.u.b_rw_list.aiocb_array);
                gossip_debug(
                    GOSSIP_TROVE_DEBUG, "aio_cancel returned %s\n",
                    ((ret == AIO_CANCELED) ? "CANCELED" :
                     "NOT CANCELED"));
                /*
                  NOTE: the normal aio notification method takes care
                  of completing the op and moving it to the completion
                  queue
                */
            }
            else
            {
                gossip_debug(
                    GOSSIP_TROVE_DEBUG, "op is in service: ignoring "
                    "operation type %d\n", cur_op->op.type);
            }
            ret = 0;
        }
        break;
        case OP_COMPLETED:
        case OP_CANCELED:
            /* easy cancelation case; do nothing */
            gossip_debug(
                GOSSIP_TROVE_DEBUG, "op is completed: ignoring\n");
            ret = 0;
            break;
        default:
            gossip_err("Invalid dbpf_op state found (%d)\n", state);
            assert(0);
    }
    return ret;
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
        for(i = 0; i < AIOCB_ARRAY_SZ; i++)
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
        for(i = 0; i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
            if (aiocb_p[i].aio_lio_opcode == LIO_NOP)
            {
                continue;
            }

            /* gets the "errno" value of the individual op */
            ret = op_p->u.b_rw_list.aio_ops->aio_error(&aiocb_p[i]);
            if (ret == 0)
            {
                /*
                  this particular operation completed w/out error.
                  gets the return value of the individual op
                */
                ret = op_p->u.b_rw_list.aio_ops->aio_return(&aiocb_p[i]);

                gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s complete: "
                             "aio_return() ret %d (fd %d)\n",
                             __func__,
                             ((op_p->type == BSTREAM_WRITE_LIST) ||
                              (op_p->type == BSTREAM_WRITE_AT) ?
                              "WRITE" : "READ"), ret,
                             op_p->u.b_rw_list.fd);

                /* aio_return doesn't seem to return bytes read/written if 
                 * sigev_notify == SIGEV_NONE, so we set the out size 
                 * from what's requested.  For reads we just leave as zero,
                 * which ends up being OK,
                 * since the amount read (if past EOF its less than requested)
                 * is determined from the bstream size.
                 */
                if(op_p->type == BSTREAM_WRITE_LIST || 
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
        
        /* mark unused with LIO_NOPs */
        for(i = aiocb_inuse_count;
            i < op_p->u.b_rw_list.aiocb_array_count; i++)
        {
            aiocb_p[i].aio_lio_opcode = LIO_NOP;
        }

        for(i = 0; i < aiocb_inuse_count; i++)
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

        ret = issue_or_delay_io_operation(
            (dbpf_queued_op_t *)op_p->u.b_rw_list.queued_op_ptr,
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

inline int dbpf_pread(int fd, void *buf, size_t count, off_t offset)
{
    int ret = 0;
    int ret_size = 0;

    do
    {
        ret = pread(fd, ((char *)buf) + ret_size, 
                    count - ret_size, offset + ret_size);
        if (ret)
        {
            ret_size += ret;
        }
    } while( (ret == -1 && errno == EINTR) || (ret_size < count && ret > 0) );

    if(ret < 0)
    {
        return ret;
    }
    return ret_size;
}

inline int dbpf_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    int ret = 0;
    int ret_size = 0;
    do
    {
        ret = pwrite(fd, ((char *)buf) + ret_size, 
                     count - ret_size, offset + ret_size);
        if (ret)
        {
            ret_size += ret;
        }
    } while( (ret == -1 && errno == EINTR)  || (ret_size < count && ret > 0) );

    if(ret < 0)
    {
        return -trove_errno_to_trove_error(errno);
    }
    return ret_size;
}

static struct dbpf_aio_ops aio_ops =
{
    aio_read,
    aio_write,
    lio_listio,
    aio_error,
    aio_return,
    aio_cancel,
    aio_suspend,
    aio_fsync
};

static int dbpf_bstream_resize_op_svc(struct dbpf_op *op_p)
{
    int ret;
    TROVE_ds_attributes attr;
    TROVE_object_ref ref;
    dbpf_queued_op_t *q_op_p;
    struct open_cache_ref open_ref;
    PVFS_size tmpsize;

    q_op_p = (dbpf_queued_op_t *)op_p->u.b_resize.queued_op_ptr;

    ref.fs_id = op_p->coll_p->coll_id;
    ref.handle = op_p->handle;

    gen_mutex_lock(&dbpf_update_size_lock);
    ret = dbpf_dspace_attr_get(op_p->coll_p, ref, &attr);
    if(ret != 0)
    {
        gen_mutex_unlock(&dbpf_update_size_lock);
        return ret;
    }

    tmpsize = op_p->u.b_resize.size;
    attr.u.datafile.b_size = tmpsize;

    ret = dbpf_dspace_attr_set(op_p->coll_p, ref, &attr);
    if(ret < 0)
    {
        gen_mutex_unlock(&dbpf_update_size_lock);
        return ret;
    }
    gen_mutex_unlock(&dbpf_update_size_lock);

    /* setup op for sync coalescing */
    dbpf_queued_op_init(q_op_p,
                        DSPACE_SETATTR,
                        ref.handle,
                        q_op_p->op.coll_p,
                        dbpf_dspace_setattr_op_svc,
                        q_op_p->op.user_ptr,
                        TROVE_SYNC,
                        q_op_p->op.context_id);
    q_op_p->op.state = OP_IN_SERVICE;

    /* truncate file after attributes are set */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle,
        DBPF_FD_BUFFERED_WRITE,
        &open_ref);
    if(ret < 0)
    {
        return ret;
    }

    ret = DBPF_RESIZE(open_ref.fd, tmpsize);
    if(ret < 0)
    {
        return(ret);
    }

    dbpf_open_cache_put(&open_ref);

    return DBPF_OP_COMPLETE;
}

int dbpf_bstream_resize(TROVE_coll_id coll_id,
                        TROVE_handle handle,
                        TROVE_size *inout_size_p,
                        TROVE_ds_flags flags,
                        TROVE_vtag_s *vtag,
                        void *user_ptr,
                        TROVE_context_id context_id,
                        TROVE_op_id *out_op_id_p,
                        PVFS_hint hints)
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
                        user_ptr,
                        flags,
                        context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_resize.size = *inout_size_p;
    q_op_p->op.u.b_resize.queued_op_ptr = q_op_p;
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

struct TROVE_bstream_ops dbpf_bstream_ops =
{
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    dbpf_bstream_read_list,
    dbpf_bstream_write_list,
    dbpf_bstream_flush,
    dbpf_bstream_cancel
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

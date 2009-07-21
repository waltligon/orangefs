/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <string.h>
#include "pvfs2-types.h"
#include "pvfs2-internal.h"
#include "pint-worker-threaded-queues.h"
#include "pint-queue.h"
#include "pint-worker.h"
#include "pint-mgmt.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "quicklist.h"
#include <pthread.h>


/* How long we wait for a queue to be added before timing out.
 * Note that its necessary to timeout and check that a request to
 * stop the thread has not been sent.  Right now we set this to
 * 10 millisecs (value in microsecs) 
 */
#define WAIT_FOR_QUEUE_INTERVAL 1e5

#define DEFAULT_TIMEOUT 1e4

static int PINT_worker_queue_thread_start(
    struct PINT_worker_thread_entry * tentry);

static int PINT_worker_queue_thread_stop(
    struct PINT_worker_thread_entry * tentry);

static int threaded_queues_init(struct PINT_manager_s *manager,
                                PINT_worker_inst *inst,
                                PINT_worker_attr_t *attr)
{
    struct PINT_worker_threaded_queues_s *w;
    int ret = 0;
    int i;

    w = &inst->threaded;

    w->attr = attr->u.threaded;
    gen_mutex_init(&w->mutex);
    gen_cond_init(&w->cond);
    INIT_QLIST_HEAD(&w->queues);
    INIT_QLIST_HEAD(&w->inuse_queues);

    w->manager = manager;

    w->threads = malloc(sizeof(struct PINT_worker_thread_entry) *
                        w->attr.thread_count);
    if(!w->threads)
    {
        ret = -PVFS_ENOMEM;
        gen_cond_destroy(&w->cond);
        goto exit;
    }

    for(i = 0; i < w->attr.thread_count; ++i)
    {
        w->threads[i].worker = w;
        ret = PINT_worker_queue_thread_start(&w->threads[i]);
        if(ret < 0)
        {
            /* stop the other threads */
            for(; i >= 0; --i)
            {
                PINT_worker_queue_thread_stop(&w->threads[i]);
            }
            free(w->threads);
            gen_cond_destroy(&w->cond);
        }
    }

exit:
    return ret;
}

static int threaded_queues_destroy(struct PINT_manager_s *manager,
                                   PINT_worker_inst *inst)
{
    struct PINT_worker_threaded_queues_s *w;
    struct PINT_worker_thread_entry * tentry;
    int i, count;

    w = &inst->threaded;

    gen_mutex_lock(&w->mutex);
    count = w->attr.thread_count;
    gen_mutex_unlock(&w->mutex);

    /* stop all threads */
    for(i = 0; i < w->attr.thread_count; ++i)
    {
        gen_mutex_lock(&w->mutex);
        tentry = &w->threads[i];
        gen_mutex_unlock(&w->mutex);

        PINT_worker_queue_thread_stop(tentry);
    }

    free(w->threads);
    gen_cond_destroy(&w->cond);
    gen_mutex_unlock(&w->mutex);

    return 0;
}

static int threaded_queues_queue_add(struct PINT_manager_s *manager,
                                     PINT_worker_inst *inst,
                                     PINT_queue_id queue_id)
{
    struct PINT_worker_threaded_queues_s *w;
    struct PINT_queue_s *queue;

    w = &inst->threaded;

    queue = id_gen_fast_lookup(queue_id);
    gen_mutex_lock(&w->mutex);

    assert(queue->link.next == NULL && queue->link.prev == NULL);

    qlist_add_tail(&queue->link, &w->queues);
    PINT_queue_add_producer(queue_id, w);
    PINT_queue_add_consumer(queue_id, w);

    /* send a signal to one thread waiting for a queue to be added */
    gen_cond_signal(&w->cond);
    gen_mutex_unlock(&w->mutex);

    return 0;
}

static int threaded_queues_queue_remove(struct PINT_manager_s *manager,
                                        PINT_worker_inst *inst,
                                        PINT_queue_id queue_id)
{
    struct PINT_worker_threaded_queues_s *w;
    struct PINT_queue_s *queue;
    struct timespec timeout;

    w = &inst->threaded;

    gen_mutex_lock(&w->mutex);
    w->remove_requested = 1;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    /* we wait for 10 millisecs -- long enough for the queue to
     * be added back to the unused list */
    while(!qlist_exists(&w->queues, &queue->link))
    {
        /* assume that operations are being pulled off presently
         * and it just needs to be added back to the
         * list of queues, which we will wait for
         */
        timeout.tv_sec = time(NULL);
        timeout.tv_nsec = 1e6;
        gen_cond_timedwait(&w->cond, &w->mutex, &timeout);
    }

    /* now we're ensured that its there, so pluck it off */
    qlist_del(&queue->link);
    PINT_queue_remove_producer(queue_id, w);
    PINT_queue_remove_consumer(queue_id, w);

    memset(&queue->link, 0, sizeof(queue->link));

    w->remove_requested = 0;
    gen_cond_broadcast(&w->cond);
    gen_mutex_unlock(&w->mutex);

    return 0;
}

static int threaded_queues_post(struct PINT_manager_s *manager,
                                PINT_worker_inst *inst,
                                PINT_queue_id id,
                                PINT_operation_t *operation)
{
    struct PINT_worker_threaded_queues_s *w;
    int ret;
    PINT_worker_id wid;
    struct PINT_queue_s *queue;
    PINT_queue_id queue_id;

    w = &inst->threaded;

    gen_mutex_lock(&w->mutex);

    id_gen_fast_register(&wid, w);

    /* if the queue_id matches the worker_id, then assume that there's
     * only one queue maintained by this worker and use that
     */
    if(id == 0)
    {
        /* a dirty hack to check that the list of queues only has one element */
        if(w->queues.next->next != NULL)
        {
            gossip_err("%s: no queue id was specified and there's more than "
                       "one queue being managed by this worker\n", __func__);
            gen_mutex_unlock(&w->mutex);
            return -PVFS_EINVAL;
        }

        /* there must be only one queue, so just use that */
        queue = qlist_entry(&w->queues.next, struct PINT_queue_s, link);
        queue_id = queue->id;
    }
    else
    {
        /* its not the worker id, so it must be an id for one of our queues */
        queue = id_gen_fast_lookup(id);

        /* verify that this is a queue id we know about */
        if(!qlist_exists(&w->queues, &queue->link) &&
           !qlist_exists(&w->inuse_queues, &queue->link))
        {
            gen_mutex_unlock(&w->mutex);
            return -PVFS_EINVAL;
        }
        queue_id = id;
    }

    /* at this point we should have a valid queue_id that's managed
     * by this worker.
     */
    ret = PINT_queue_push(queue_id, &operation->qentry);
    if(ret < 0)
    {
        gossip_err("%s: failed to push op onto queue: %llu\n",
                   __func__, llu(queue_id));
        gen_mutex_unlock(&w->mutex);
        return ret;
    }

    gossip_debug(GOSSIP_MGMT_DEBUG,
                 "%s: post op to worker (threaded queues) queue: %llu\n",
                 __func__,
                 llu(queue_id));

    gen_mutex_unlock(&w->mutex);
    return PINT_MGMT_OP_POSTED;
}

static int threaded_queues_cancel(struct PINT_manager_s *manager,
                                  PINT_worker_inst *inst,
                                  PINT_queue_id queue_id,
                                  PINT_operation_t *op)
{
    struct PINT_worker_threaded_queues_s *w;
    struct PINT_queue_s *queue;

    w = &inst->threaded;

    gen_mutex_lock(&w->mutex);

    /* if the queue_id matches the worker_id, then assume that there's
     * only one queue maintained by this worker and use that
     */
    if(queue_id == 0)
    {
        /* a dirty hack to check that the list of queues only has one element */
        if(w->queues.next->next != NULL)
        {
            gossip_err("%s: no queue id was specified and there's more than "
                       "one queue being managed by this worker\n", __func__);
            gen_mutex_unlock(&w->mutex);
            return -PVFS_EINVAL;
        }

        /* there must be only one queue, so just use that */
        queue = qlist_entry(&w->queues.next, struct PINT_queue_s, link);
        queue_id = queue->id;
    }
    else
    {
        /* its not the worker id, so it must be an id for one of our queues */
        queue = id_gen_fast_lookup(queue_id);

        /* verify that this is a queue id we know about */
        if(!qlist_exists(&w->queues, &queue->link) &&
           !qlist_exists(&w->inuse_queues, &queue->link))
        {
            gen_mutex_unlock(&w->mutex);
            return -PVFS_EINVAL;
        }
    }

    return PINT_queue_remove(queue_id, &op->qentry);
}

struct PINT_worker_impl PINT_worker_threaded_queues_impl =
{
    "THREADED",
    threaded_queues_init,
    threaded_queues_destroy,
    threaded_queues_queue_add,
    threaded_queues_queue_remove,
    threaded_queues_post,

    /* the threaded queues impl doesn't implement the do_work callback
     * because work is done in the threads
     */
    NULL,

    threaded_queues_cancel
};

static void *PINT_worker_queues_thread_function(void * ptr)
{
    struct PINT_worker_thread_entry *thread;
    struct PINT_worker_threaded_queues_s *worker;
    struct PINT_manager_s *manager;
    struct PINT_queue_s *queue;
    PINT_operation_t *op;
    int op_count;
    int timeout;
    PINT_queue_entry_t **qentries = NULL;
    int i = 0;
    int ret, service_time, error;
    struct timespec wait_interval;

    wait_interval.tv_sec = (WAIT_FOR_QUEUE_INTERVAL / 1e6);
    wait_interval.tv_nsec =
        (WAIT_FOR_QUEUE_INTERVAL - (wait_interval.tv_sec * 1e6)) * 1e3;

    thread = (struct PINT_worker_thread_entry *)ptr;

    gen_mutex_lock(&thread->mutex);
    worker = thread->worker;
    manager = worker->manager;
    gen_mutex_unlock(&thread->mutex);

    gen_mutex_lock(&worker->mutex);
    op_count = worker->attr.ops_per_queue;
    timeout = worker->attr.timeout;
    if(timeout == 0)
    {
        timeout = DEFAULT_TIMEOUT;
    }
    gen_mutex_unlock(&worker->mutex);

    qentries = malloc(sizeof(PINT_queue_entry_t *) * op_count);
    if(!qentries)
    {
        ret = -PVFS_ENOMEM;
        gen_mutex_unlock(&thread->mutex);
        goto free_ops;
    }

    gen_mutex_lock(&thread->mutex);
    thread->running = 1;
    while(thread->running)
    {
        /* unlock the thread mutex to allow someone else
         * to set the running field to zero
         */
        gen_mutex_unlock(&thread->mutex);

        gen_mutex_lock(&worker->mutex);

        if(worker->remove_requested)
        {
            gen_cond_wait(&worker->cond, &worker->mutex);
            gen_mutex_unlock(&worker->mutex);

            /* lock the mutex again before checking the running field */
            gen_mutex_lock(&thread->mutex);
            continue;
        }

        if(!qlist_empty(&worker->queues))
        {
            queue = qlist_entry(
                worker->queues.next, struct PINT_queue_s, link);

            /* take the queue off the head of the list so
             * that we can put it back on the tail.  This
             * allows the threads to work on the queues in
             * a round-robin fashion
             */
            qlist_del(&queue->link);
            qlist_add_tail(&queue->link, &worker->inuse_queues);
            gen_mutex_unlock(&worker->mutex);

            op_count = worker->attr.ops_per_queue;
            /* now we wait for operations to get put on the queue
             * and service them when they do
             */
            ret = PINT_queue_timedwait(
                queue->id, &op_count, qentries, timeout);
            if(ret < 0 && ret != -PVFS_ETIMEDOUT)
            {
                goto thread_failed;
            }

            /* add the queue back to the end of the list */
            gen_mutex_lock(&worker->mutex);
            qlist_del(&queue->link);
            qlist_add_tail(&queue->link, &worker->queues);

            /* Don't signal another thread unless there is actually more
             * work left in this queue.  Note that it is safe to check the
             * count here because new operations cannot be submitted to the
             * queue while worker->mutex is held (see
             * threaded_queues_post()).  PINT_queue_insert() will do its own
             * signalling as needed later.
             */
            if(PINT_queue_count(queue->id) > 0)
            {
                gen_cond_signal(&worker->cond);
            }
            gen_mutex_unlock(&worker->mutex);

            if(op_count > 0)
            {
                for(i = 0; i < op_count; ++i)
                {
                    op = PINT_op_from_qentry(qentries[i]);
                    /* service the operation */
                    ret = PINT_manager_service_op(
                        manager, op, &service_time, &error);
                    if(ret < 0)
                    {
                        /* fatal if we can't service an operation */
                        goto free_ops;
                    }

                    ret = PINT_manager_complete_op(
                        manager, op, error);
                    if(ret < 0)
                    {
                        /* fatal if we can't complete an op */
                        goto free_ops;
                    }
                }
            }
        }
        else
        {
            /* no queues in the list, wait for addition */

            /* we set a timeout of 1 second, long enough to not peg the cpu, but
             * short enough to check thread cancellation */
            struct timespec empty_timeout;
            empty_timeout.tv_sec = time(NULL) + 1;
            empty_timeout.tv_nsec = 0;

            ret = gen_cond_timedwait(&worker->cond, &worker->mutex, &empty_timeout);
            if(ret != 0 && ret != ETIMEDOUT)
            {
                gossip_lerr("gen_cond_timedwait failed with error: %s\n",
                            strerror(ret));
            }
            else
            {
                gen_mutex_unlock(&worker->mutex);
            }
        }
        /* lock the mutex again before checking the running field */
        gen_mutex_lock(&thread->mutex);
    }

    gen_mutex_unlock(&thread->mutex);

    /* must have been external request to stop thread */
    ret = 0;

free_ops:
    if(qentries)
    {
        free(qentries);
    }

thread_failed:
    thread->error = ret;
    return NULL;
}

static int PINT_worker_queue_thread_start(
    struct PINT_worker_thread_entry * tentry)
{
    int ret = 0;

    gen_mutex_init(&tentry->mutex);
    ret = pthread_create(&tentry->thread_id, NULL,
                         PINT_worker_queues_thread_function, tentry);
    if(ret < 0)
    {
        /* convert to PVFS error */
        return PVFS_errno_to_error(ret);
    }
    return 0;
}

static int PINT_worker_queue_thread_stop(
    struct PINT_worker_thread_entry * tentry)
{
    int ret;
    void *ptr;
    struct PINT_worker_threaded_queues_s *w;

    gen_mutex_lock(&tentry->mutex);
    w = tentry->worker;
    tentry->running = 0;
    gen_mutex_unlock(&tentry->mutex);

    gen_mutex_lock(&w->mutex);
    gen_cond_broadcast(&w->cond);
    gen_mutex_unlock(&w->mutex);

    ret = pthread_join(tentry->thread_id, &ptr);
    if(ret < 0)
    {
        return PVFS_errno_to_error(ret);
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include "pvfs2-internal.h"
#include "pint-worker-queues.h"
#include "pint-queue.h"
#include "pint-worker.h"
#include "pint-mgmt.h"
#include "pvfs2-debug.h"
#include "gossip.h"

static int queues_init(struct PINT_manager_s *manager,
                       PINT_worker_inst *inst,
                       PINT_worker_attr_t *attr)
{
    struct PINT_worker_queues_s *w;
    int ret = 0;

    w = &inst->queues;

    w->attr = attr->u.queues;
    INIT_QLIST_HEAD(&w->queues);
    gen_mutex_init(&w->mutex);
    gen_cond_init(&w->cond);

    w->qentries = malloc(sizeof(PINT_queue_entry_t) * w->attr.ops_per_queue);
    if(!w->qentries)
    {
        ret = -PVFS_ENOMEM;
        goto error_exit;
    }

    return 0;

error_exit:
    gen_cond_destroy(&w->cond);

    return ret;
}

static int queues_destroy(struct PINT_manager_s *manager,
                          PINT_worker_inst *inst)
{
    struct PINT_worker_queues_s *w;
    w = &inst->queues;

    free(w->qentries);
    gen_cond_destroy(&w->cond);
    return 0;
}

static int queues_queue_add(struct PINT_manager_s *manager,
                            PINT_worker_inst * inst,
                            PINT_queue_id queue_id)
{
    struct PINT_worker_queues_s *w;
    struct PINT_queue_s *queue;

    w = &inst->queues;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&w->mutex);
    qlist_add_tail(&queue->link, &w->queues);
    PINT_queue_add_producer(queue_id, w);
    PINT_queue_add_consumer(queue_id, w);
    gen_cond_signal(&w->cond);
    gen_mutex_unlock(&w->mutex);

    return 0;
}

static int queues_queue_remove(struct PINT_manager_s *manager,
                               PINT_worker_inst *inst,
                               PINT_queue_id queue_id)
{
    struct PINT_worker_queues_s *w;
    struct PINT_queue_s *queue;

    w = &inst->queues;

    gen_mutex_lock(&w->mutex);

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    /* make sure its actually in there at the moment */
    while(!qlist_exists(&w->queues, &queue->link))
    {
        /* assume that operations are being pulled off presently
         * and it just needs to be added back to the
         * list of queues, which we will wait for
         */
        gen_cond_wait(&w->cond, &w->mutex);
    }

    /* now we're sure that its there, so pluck it off */
    qlist_del(&queue->link);
    PINT_queue_remove_producer(queue_id, w);
    PINT_queue_remove_consumer(queue_id, w);
    gen_mutex_unlock(&w->mutex);

    return 0;
}

static int queues_post(struct PINT_manager_s *manager,
                       PINT_worker_inst *inst,
                       PINT_queue_id id,
                       PINT_operation_t *operation)
{
    struct PINT_worker_queues_s *w;
    int ret;
    PINT_worker_id wid;
    struct PINT_queue_s *queue;
    PINT_queue_id queue_id;

    w = &inst->queues;

    gen_mutex_lock(&w->mutex);

    if(qlist_empty(&w->queues))
    {
        gossip_err("%s: cannot post an operation without first adding queues "
                   "to the queue worker\n", __func__);
        gen_mutex_unlock(&w->mutex);
        return -PVFS_EINVAL;
    }

    id_gen_fast_register(&wid, w);

    /* if the queue_id is zero, then assume that there's
     * only one queue maintained by this worker and use that
     */
    if(id == 0)
    {
        /* a dirty hack to check that the list of queues only has one element */
        if(w->queues.next->next != &w->queues)
        {
            gossip_err("%s: no queue id was specified and there's more than "
                       "one queue being managed by this worker\n", __func__);
            gen_mutex_unlock(&w->mutex);
            return -PVFS_EINVAL;
        }

        /* there must be only one queue, so just use that */
        queue = qlist_entry(w->queues.next, struct PINT_queue_s, link);
        queue_id = queue->id;

    }
    else
    {
        /* its not the worker id, so it must be an id for one of our queues */
        queue = id_gen_fast_lookup(id);

        /* verify that this is a queue id we know about */
        if(!qlist_exists(&w->queues, &queue->link))
        {
            gen_mutex_unlock(&w->mutex);
            gossip_err("%s: failed to find a valid queue matching the "
                       "queue id passed in\n", __func__);
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
        gen_mutex_unlock(&w->mutex);
        gossip_err("%s: failed to push op onto queue: %llu\n",
                   __func__, llu(queue_id));
        return ret;
    }

    gossip_debug(GOSSIP_MGMT_DEBUG,
                 "%s: post op to worker (queues) queue: %llu\n",
                 __func__, llu(queue_id));

    gen_mutex_unlock(&w->mutex);
    return PINT_MGMT_OP_POSTED;
}

static int queues_do_work(struct PINT_manager_s *manager,
                          PINT_worker_inst *inst,
                          PINT_context_id context_id,
                          PINT_operation_t *op,
                          int microsecs)
{
    struct PINT_worker_queues_s *w;
    struct timeval start, now;
    struct PINT_queue_s *queue;
    int count;
    int i, j, ret;
    PINT_queue_entry_t *qentry;
    int service_time, error;

    w = &inst->queues;

    gettimeofday(&start, NULL);
    gen_mutex_lock(&w->mutex);

    if(qlist_empty(&w->queues))
    {
        /* no queues!  just return zero */
        gen_mutex_unlock(&w->mutex);
        return 0;
    }

    if(op->id != 0)
    {
        /* find the op in one of the queues */
        qlist_for_each_entry(queue, &w->queues, link)
        {
            ret = PINT_queue_search_and_remove(
                queue->id,
                PINT_op_queue_find_op_id_callback,
                &op->id,
                &qentry);
            if(ret == -PVFS_ENOENT)
            {
                continue;
            }
            else if(ret < 0)
            {
                gen_mutex_unlock(&w->mutex);
                return 0;
            }

            op = PINT_op_from_qentry(qentry);
            /* must have removed it. */
            break;
        }

        if(!op)
        {
            gen_mutex_unlock(&w->mutex);
            return -PVFS_EINVAL;
        }

        /* service */
        ret = PINT_manager_service_op(manager, op, &service_time, &error);
        if(ret < 0)
        {
            gen_mutex_unlock(&w->mutex);
            return ret;
        }

        ret = PINT_manager_complete_op(manager, op, error);
        gen_mutex_unlock(&w->mutex);
        return ret;
    }

    /* no specific op was specified to do work on, so we just
     * iterate through the queues doing work until the timeout
     * expires
     */
    while(1)
    {
        queue = qlist_entry(w->queues.next, struct PINT_queue_s, link);

        /* remove it from the list so that we can operate on the queues
         * in a round-robin fashion.
         */
        qlist_del(&queue->link);

        count = w->attr.ops_per_queue;

        /* service as many operations as specified in the attributes
        */
        if(w->attr.timeout == 0)
        {
            ret = PINT_queue_wait(queue->id, &count, &w->qentries);

        }
        else
        {
            ret = PINT_queue_timedwait(
                queue->id, &count, &w->qentries, w->attr.timeout);
        }

        if(ret < 0)
        {
            /* fatal error, couldn't get operations off the queue */
            goto exit;
        }

        for(i = 0; i < count; ++i)
        {
            op = PINT_op_from_qentry(&w->qentries[i]);

            /* service! */
            ret = PINT_manager_service_op(
                manager, op, &service_time, &error);
            if(ret < 0)
            {
                /* failed to notify the manager that this operation
                 * had been serviced.  Put the queue back on the list
                 * of queues.
                 */
                goto exit;
            }

            ret = PINT_manager_complete_op(manager, op, error);
            if(ret < 0)
            {
                goto exit;
            }

            gettimeofday(&now, NULL);

            if(microsecs > 0 &&
               ((now.tv_sec * 1e6 + now.tv_usec) -
                (start.tv_sec * 1e6 + start.tv_usec)) > microsecs)
            {
                /* went past timeout.  put the remaining operations back
                 * on the queue and return
                 */
                break;
            }
        }

        if(i < count)
        {
            /* push all the un-serviced operations back onto the
             * queue.  We push them onto the front since they were
             * removed from the front.
             */
            for(j = (count - 1); j > i; --j)
            {
                ret = PINT_queue_push_front(queue->id, &w->qentries[j]);
                if(ret < 0)
                {
                    goto exit;
                }
            }
        }

        qlist_add_tail(&queue->link, &w->queues);
    }

exit:
    /* put the queue back on the list of queues */
    qlist_add_tail(&queue->link, &w->queues);
    gen_mutex_unlock(&w->mutex);
    return ret;
};

struct PINT_worker_impl PINT_worker_queues_impl =
{
    "QUEUES",
    queues_init,
    queues_destroy,
    queues_queue_add,
    queues_queue_remove,
    queues_post,
    queues_do_work,
    NULL
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

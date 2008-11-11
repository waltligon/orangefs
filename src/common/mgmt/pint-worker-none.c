
#include "pint-worker-none.h"
#include "pint-queue.h"

static int PINT_worker_queues_init(PINT_worker_inst *inst,
                                   PINT_worker_attr_t *attr)
{
    int ret = 0;
    inst->queues.attr = attr->u.queues;
    INIT_QLIST_HEAD(inst->queues.queues);
    gen_mutex_init(&inst->queues.mutex);
    gen_cond_init(&inst->queues.cond);

    inst->queues.ops = malloc(sizeof(PINT_operation_t) *
                            inst->queues.attr.ops_per_queue);
    if(!inst->queues.ops)
    {
        ret = -PVFS_ENOMEM;
        goto error_exit;
    }

error_exit:
    gen_cond_destroy(&inst->queues.cond);
    return ret;
}

static int PINT_worker_queues_destroy(PINT_worker_inst *inst)
{
    free(inst->queues.ops);
    gen_cond_destroy(&inst->queues.cond);
    return 0;
}

static int PINT_worker_queues_queue_add(PINT_worker_inst * inst,
                                        PINT_queue_id queue_id)
{
    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&inst->queues.mutex);
    qlist_add_tail(&queue->link, &inst->queues.queues);
    gen_mutex_unlock(&inst->queues.mutex);

    return 0;
}

static int PINT_worker_queues_queue_remove(PINT_worker_inst *inst,
                                           PINT_queue_id queue_id)
{
    struct PINT_queue_s *queue;

    gen_mutex_lock(&inst->queues.mutex);

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    /* make sure its actually in there at the moment */
    while(!qlist_exists(&inst->queues.queues, queue->link))
    {
        /* assume that operations are being pulled off presently
         * and it just needs to be added back to the
         * list of queues, which we will wait for
         */
        gen_mutex_condwait(&inst->queues.cond,
                           &inst->queues.mutex);
    }

    /* now we're sure that its there, so pluck it off */
    qlist_del(&queue->link);
    gen_mutex_unlock(&inst->queues.mutex);

    return 0;
}

static int PINT_worker_queues_post(PINT_worker_inst *inst,
                                   PINT_queue_id queue_id,
                                   PINT_operation_t *operation)
{
    gen_mutex_lock(&inst->queues.mutex);

    /* the queue id is in the entry->id field */
    ret = PINT_queue_push(queue_id, operation);
    if(ret < 0)
    {
        gossip_err("%s: failed to push op onto queue: %p\n",
                   __func__, queue_id);
        return ret;
    }

    gossip_debug(PINT_MGMT_DEBUG,
                 "%s: post op to worker (queues) queue: %p\n",
                 __func__,
                 queue_id);

    gen_mutex_unlock(&inst->queues.mutex);

    return PINT_MGMT_OP_POSTED;
}

static int PINT_worker_queues_do_work(PINT_manager_t manager,
                                      PINT_worker_inst *inst,
                                      int microsecs)
{
    struct timeval start, now;
    struct PINT_queue_s *queue;
    int count;
    int i, j;

    assert(microsecs > 0);

    gettimeofday(&start, NULL);
    gen_mutex_lock(&inst->queues.mutex);

    if(qlist_empty(&inst->queues.queues))
    {
        /* no queues!  just return zero */
        return 0;
    }

    while(1)
    {
        queue = qlist_entry(inst->queues.queues.next, struct PINT_queue_s, link);

        /* remove it from the list so that we can operate on the queues
         * in a round-robin fashion.
         */
        qlist_del(queue->link);

        count = inst->queues.attr.ops_per_queue;

        /* service as many operations as specified in the attributes
        */
        if(inst->queues.attr.timeout == 0)
        {
            ret = PINT_queue_wait(queue->id,
                                  &count,
                                  inst->queues.ops);
        }
        else
        {
            ret = PINT_queue_timedwait(queue->id,
                                       &count,
                                       inst->queues.ops,
                                       inst->queues.attr.timeout);
        }

        if(ret < 0)
        {
            /* fatal error, couldn't get operations off the queue */
            goto exit;
        }

        for(i = 0; i < count; ++i)
        {
            /* service! */
            error = inst->queues.ops[i].operation(
                inst->queues.ops[i].operation_ptr,
                inst->queues.ops[i].hint);

            ret = PINT_manager_serviced(manager, &inst->queues.ops[i], error);
            if(ret < 0)
            {
                /* failed to notify the manager that this operation
                 * had been serviced.  Put the queue back on the list
                 * of queues.
                 */
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
                ret = PINT_queue_push_front(
                    queue_id, inst->queues.ops[j]);
                if(ret < 0)
                {
                    goto exit;
                }
            }
        }

        qlist_add_tail(&queue->link, &inst->queues.queues);
    }

exit:
    /* put the queue back on the list of queues */
    qlist_add_tail(&queue->link, &inst->queues.queues);
    gen_mutex_unlock(&inst->queues.mutex);
    return ret;
}

struct PINT_worker_impl PINT_worker_queues_impl
{
    "NONE",
    PINT_worker_queues_init,
    PINT_worker_queues_destroy,
    PINT_worker_queues_queue_add,
    PINT_worker_queues_queue_remove,
    PINT_worker_queues_post,
    PINT_worker_queues_do_work
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

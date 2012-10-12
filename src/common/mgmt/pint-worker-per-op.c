/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "pint-worker-per-op.h"
#include "pint-worker.h"
#include "pint-mgmt.h"
#include "gossip.h"

struct PINT_manager_s;

struct PINT_worker_per_op_thread_s
{
    pthread_t id;
    struct PINT_manager_s *manager;
    PINT_operation_t *operation;
    struct PINT_worker_per_op_s *worker;
};

static void *PINT_worker_per_op_thread_function(void * ptr);

static int per_op_init(struct PINT_manager_s *manager,
                       PINT_worker_inst *inst,
                       PINT_worker_attr_t *attr)
{
    struct PINT_worker_per_op_s *w;
    w = &inst->per_op;

    w->attr = attr->u.per_op;
    w->service_count = 0;
    return 0;
}

static int per_op_destroy(struct PINT_manager_s *manager,
                          PINT_worker_inst *inst)
{
    struct PINT_worker_per_op_s *w;
    w = &inst->per_op;

    if(w->service_count)
    {
        return -PVFS_EBUSY;
    }

    return 0;
}

static int per_op_post(struct PINT_manager_s *manager,
                       PINT_worker_inst *inst,
                       PINT_queue_id queue_id,
                       PINT_operation_t *operation)
{
    struct PINT_worker_per_op_s *w;
    int ret;
    struct PINT_worker_per_op_thread_s *thread;
    pthread_attr_t attr;

    w = &inst->per_op;

    /* no queues in per-op */
    assert(queue_id == 0);

    thread = malloc(sizeof(struct PINT_worker_per_op_thread_s));
    if(!thread)
    {
        return -PVFS_ENOMEM;
    }

    thread->manager = manager;
    thread->operation = operation;
    thread->worker = w;

    ret = pthread_attr_init(&attr);
    if(ret != 0)
    {
        return PVFS_get_errno_mapping(ret);
    }

    /* set the thread to detached.  Once the operation finishes
     * this thread will exit
     */
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if(ret != 0)
    {
        pthread_attr_destroy(&attr);
        return PVFS_get_errno_mapping(ret);
    }

    /* create the thread */
    ret = pthread_create(&thread->id,
                         &attr,
                         PINT_worker_per_op_thread_function,
                         thread);
    if(ret != 0)
    {
        return PVFS_errno_to_error(ret);
    }

    return PINT_MGMT_OP_POSTED;
}

static void *PINT_worker_per_op_thread_function(void * ptr)
{
    int ret, service_time, error;
    struct PINT_worker_per_op_thread_s *thread;

    thread = (struct PINT_worker_per_op_thread_s *)ptr;

    thread->worker->service_count++;

    ret = PINT_manager_service_op(
        thread->manager, thread->operation, &service_time, &error);
    if(ret < 0)
    {
        gossip_err("%s: failed to service operation: %llu\n",
                   __func__, llu(thread->operation->id));
    }

    ret = PINT_manager_complete_op(
        thread->manager, thread->operation, error);
    if(ret < 0)
    {
        gossip_err("%s: failed to complete operation: %llu\n",
                   __func__, llu(thread->operation->id));
    }

    thread->worker->service_count--;

    return NULL;
}

struct PINT_worker_impl PINT_worker_per_op_impl =
{
    "PER_OP",
    per_op_init,
    per_op_destroy,

    /* the per-op worker doesn't use queues, so the queue_add and
     * queue_remove callbacks aren't implemented
     */
    NULL,
    NULL,

    per_op_post,

    /* the per-op impl doesn't implement the do_work callback
     * because work is done in the threads
     */
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

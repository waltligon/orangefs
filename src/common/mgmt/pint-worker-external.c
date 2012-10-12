/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pint-worker-external.h"
#include "pint-queue.h"
#include "pint-mgmt.h"
#include "pint-worker.h"
#include "pvfs2-internal.h"

static int external_init(struct PINT_manager_s *manager,
                         PINT_worker_inst *inst,
                         PINT_worker_attr_t *attr)
{
    inst->external.attr = attr->u.external;
    inst->external.posted = 0;

    gen_mutex_init(&inst->external.mutex);
    return PINT_queue_create(&inst->external.wait_queue, NULL);
}

static int external_destroy(struct PINT_manager_s *manager,
                            PINT_worker_inst *inst)
{
    return PINT_queue_destroy(inst->external.wait_queue);
}

static int external_post(struct PINT_manager_s *manager,
                         PINT_worker_inst *inst,
                         PINT_queue_id queue_id,
                         PINT_operation_t *operation)
{
    int ret;
    gen_mutex_lock(&inst->external.mutex);

    /* put in wait queue if we've exceeded max posts.  If max posts is 0,
     * we never queue
     */
    if(inst->external.attr.max_posts > 0 &&
       inst->external.posted >= inst->external.attr.max_posts)
    {
        ret = PINT_queue_push(inst->external.wait_queue, &operation->qentry);
    }
    else
    {
        ret = inst->external.attr.post(&operation->id,
                                       inst->external.attr.external_ptr,
                                       operation);
        inst->external.posted++;
    }
    gen_mutex_unlock(&inst->external.mutex);
    return ret;
}

struct PINT_worker_impl PINT_worker_external_impl =
{
    "EXTERNAL",
    external_init,
    external_destroy,

    /* the external worker doesn't use queues */
    NULL,
    NULL,

    external_post,
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

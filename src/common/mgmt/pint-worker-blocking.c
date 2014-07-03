/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include "pint-worker-blocking.h"
#include "pint-worker.h"
#include "pint-mgmt.h"

static int blocking_post(struct PINT_manager_s *manager,
                         PINT_worker_inst *inst,
                         PINT_queue_id queue_id,
                         PINT_operation_t *operation)
{
    int ret;
    int service_time, error;

    assert(queue_id == 0);

    ret = PINT_manager_service_op(manager, operation, &service_time, &error);
    if(0 == ret && 0 == error)
    {
        return PINT_MGMT_OP_COMPLETED;
    }
    return error;
}

struct PINT_worker_impl PINT_worker_blocking_impl =
{
    "BLOCKING",

    /* init and destroy are null because the blocking worker
     * doesn't do anything besides service and return in the post
     */
    NULL,
    NULL,

    /* the blocking worker doesn't use queues, so the queue_add
     * and queue_remove callbacks aren't implemented
     */
    NULL,
    NULL,

    blocking_post,

    /* the blocking impl doesn't implement the do_work callback
     * since the work is done in the post
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

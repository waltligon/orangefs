/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_WORKER_QUEUES_H
#define PINT_WORKER_QUEUES_H

#include "pint-op.h"
#include "pint-queue.h"

typedef struct
{
    /* The number of operations that should be serviced before moving on
     * to the next queue
     */
    int ops_per_queue;

    /* The time to wait (in microsecs) for new ops to be added to a queue.
     * 0 means no timeout.
     */
    int timeout;

} PINT_worker_queues_attr_t;

struct PINT_worker_queues_s
{
    PINT_worker_queues_attr_t attr;
    PINT_op_id *ids;
    PINT_service_callout *callouts;
    void **service_ptrs;
    PVFS_hint **hints;
    struct qlist_head queues;
    PINT_queue_entry_t *qentries;
    gen_mutex_t mutex;
    gen_cond_t cond;
};

struct PINT_worker_impl PINT_worker_queues_impl;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


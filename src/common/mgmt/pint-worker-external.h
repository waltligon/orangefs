/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_WORKER_EXTERNAL_H
#define PINT_WORKER_EXTERNAL_H

#include "pint-op.h"
#include "pint-queue.h"

/* should return PINT_MGMT_OP_COMPLETE or PINT_MGMT_OP_POSTED
 * or some negative value on error
 */
typedef int (*PINT_worker_external_post_callout) (
    PINT_op_id *op_id,
    void *external_ptr,
    PINT_operation_t *operation);

typedef int (*PINT_worker_external_test_callout) (
    PINT_op_id *op_ids,
    void *external_ptr,
    int *count,
    PINT_operation_t *operation);

typedef struct
{
    PINT_worker_external_post_callout post;
    PINT_worker_external_test_callout test;
    void *external_ptr;

    int max_posts;
} PINT_worker_external_attr_t;

struct PINT_worker_external_s
{
    PINT_worker_external_attr_t attr;
    PINT_queue_id wait_queue;
    int posted;
    gen_mutex_t mutex;
};

struct PINT_worker_impl PINT_worker_external_impl;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

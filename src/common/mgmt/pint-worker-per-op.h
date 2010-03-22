/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_WORKER_PER_OP_H
#define PINT_WORKER_PER_OP_H

#include "pint-op.h"

typedef struct
{
    /* Max number of threads that be started/servicing operations.  Once
     * this value is reached, calls to post new operations will return
     * EAGAIN */
    int max_threads;

} PINT_worker_per_op_attr_t;

struct PINT_worker_per_op_s
{
    PINT_worker_per_op_attr_t attr;
    int service_count;
};

struct PINT_worker_impl PINT_worker_per_op_impl;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

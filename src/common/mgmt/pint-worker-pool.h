/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_WORKER_POOL_H
#define PINT_WORKER_POOL_H

#include "pint-op.h"

typedef struct
{
    int max_threads;

} PINT_worker_pool_attr_t;

struct PINT_worker_pool_s
{
    PINT_worker_pool_attr_t attr;
};
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

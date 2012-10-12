/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pint-worker-pool.h"
#include "pint-worker.h"
#include "pint-mgmt.h"

#include "pvfs2-types.h"

struct PINT_worker_impl PINT_worker_pool_impl =
{
    "POOL",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
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

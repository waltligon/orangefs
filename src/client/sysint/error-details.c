/*
 * (C) 2004 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>

#include "pvfs2-types.h"

/* PVFS2 error details functions
 *
 * The error details system is used for returning server-specific errors
 * for cases where one or more errors have occurred when talking to
 * a collection of servers.
 *
 * The PVFS_error_details structure is defined in pvfs2-types.h.
 */

/* PVFS_error_details_new(count)
 *
 * Q: SHOULD WE RETURN AN INTEGER ERROR INSTEAD?
 */
PVFS_error_details *PVFS_error_details_new(int count)
{
    PVFS_error_details *details;
    int sz;

    sz = sizeof(PVFS_error_details) +
	(count-1) * sizeof(PVFS_error_server);

    details = (PVFS_error_details *) malloc(sz);
    if (details != NULL)
    {
	details->count_allocated = count;
    }

    return details;
}

void PVFS_error_details_free(PVFS_error_details *details)
{
    free(details);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is an example of a flowproto implementation */

#include <errno.h>

#include "gossip.h"
#include "flow.h"
#include "flowproto-support.h"

/* interface prototypes */
int fp_multiqueue_initialize(int flowproto_id);

int fp_multiqueue_finalize(void);

int fp_multiqueue_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

int fp_multiqueue_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

int fp_multiqueue_post(flow_descriptor * flow_d);

int fp_multiqueue_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms);

int fp_multiqueue_service(flow_descriptor * flow_d);

char fp_multiqueue_name[] = "fp_multiqueue";

struct flowproto_ops fp_multiqueue_ops = {
    fp_multiqueue_name,
    fp_multiqueue_initialize,
    fp_multiqueue_finalize,
    fp_multiqueue_getinfo,
    fp_multiqueue_setinfo,
    fp_multiqueue_post,
    fp_multiqueue_find_serviceable,
    fp_multiqueue_service
};

int fp_multiqueue_initialize(int flowproto_id)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_finalize(void)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_post(flow_descriptor * flow_d)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_service(flow_descriptor * flow_d)
{
    return (-PVFS_ENOSYS);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

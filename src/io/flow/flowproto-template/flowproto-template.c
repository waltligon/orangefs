/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is an example of a flowproto implementation */

#include <errno.h>

#include "gossip.h"
#include "src/io/flow/flowproto-support.h"

/* interface prototypes */
int template_flowproto_initialize(int flowproto_id);

int template_flowproto_finalize(void);

int template_flowproto_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

int template_flowproto_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

int template_flowproto_post(flow_descriptor * flow_d);

int template_flowproto_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms);

int template_flowproto_service(flow_descriptor * flow_d);

char template_flowproto_name[] = "template_flowproto";

struct flowproto_ops flowproto_template_ops = {
    template_flowproto_name,
    template_flowproto_initialize,
    template_flowproto_finalize,
    template_flowproto_getinfo,
    template_flowproto_setinfo,
    template_flowproto_post,
    template_flowproto_find_serviceable,
    template_flowproto_service
};

int template_flowproto_initialize(int flowproto_id)
{
    return (0);
}

int template_flowproto_finalize(void)
{
    return (0);
}

int template_flowproto_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-ENOSYS);
}

int template_flowproto_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-ENOSYS);
}

int template_flowproto_post(flow_descriptor * flow_d)
{
    return (-ENOSYS);
}

int template_flowproto_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms)
{
    return (-ENOSYS);
}

int template_flowproto_service(flow_descriptor * flow_d)
{
    return (-ENOSYS);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

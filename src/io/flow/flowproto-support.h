/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header contains information that is only relevant to flow
 * protocols
 */

#ifndef __FLOWPROTO_SUPPORT_H
#define __FLOWPROTO_SUPPORT_H

#include "flow.h"

/* flow protocol interface */
struct flowproto_ops
{
    char *flowproto_name;
    int (*flowproto_initialize) (int flowproto_id);
    int (*flowproto_finalize) (void);
    int (*flowproto_getinfo) (flow_descriptor * flow_d,
			      int option,
			      void *parameter);
    int (*flowproto_setinfo) (flow_descriptor * flow_d,
			      int option,
			      void *parameter);
    int (*flowproto_post) (flow_descriptor * flow_d);
    int (*flowproto_cancel) (flow_descriptor * flow_d);
};

/* used to query protocols to determine which endpoint pairs are
 * supported 
 */
struct flowproto_type_support
{
    int src_endpoint_id;
    int dest_endpoint_id;
};

#endif /* __FLOWPROTO_SUPPORT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

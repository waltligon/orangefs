/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* functions for handling queues of flows that the flow interface is
 * managing
 */

#ifndef __FLOW_QUEUE_H
#define __FLOW_QUEUE_H

#include "quicklist.h"
#include "flow.h"

typedef struct qlist_head *flow_queue_p;

flow_queue_p flow_queue_new(void);
void flow_queue_cleanup(flow_queue_p fqp);
void flow_queue_add(flow_queue_p fqp,
		    struct flow_descriptor *flow_d);
void flow_queue_remove(struct flow_descriptor *desc);
int flow_queue_empty(flow_queue_p fqp);
struct flow_descriptor *flow_queue_shownext(flow_queue_p fqp);

#endif /* __FLOW_QUEUE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

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

#include <quicklist.h>
#include <flow.h>

typedef struct qlist_head* flow_queue_p;

flow_queue_p flow_queue_new(void);
void flow_queue_cleanup(flow_queue_p fqp);
void flow_queue_add(flow_queue_p fqp, struct flow_descriptor* flow_d);
void flow_queue_remove(struct flow_descriptor* desc);
int flow_queue_empty(flow_queue_p fqp);
struct flow_descriptor* flow_queue_shownext(flow_queue_p fqp);
flow_descriptor* flow_queue_search(flow_queue_p fqp, flow_descriptor*
	flow_d);
int flow_queue_search_multi(flow_queue_p fqp, int incount, 
	flow_descriptor** flow_array, int* outcount, int* index_array);

#endif /* __FLOW_QUEUE_H */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* functions for handling queues of flows that the flow interface is
 * managing
 */

#include "gossip.h"
#include "quicklist.h"
#include "flow-queue.h"
#include <errno.h>

/* flow_queue_new()
 *
 * creates a new flow queue
 *
 * returns pointer to queue on success, NULL on failure
 */
flow_queue_p flow_queue_new(void)
{
    struct qlist_head *tmp_flow_queue = NULL;

    tmp_flow_queue = (struct qlist_head *) malloc(sizeof(struct qlist_head));
    if (tmp_flow_queue)
    {
	INIT_QLIST_HEAD(tmp_flow_queue);
    }
    return (tmp_flow_queue);
}

/* flow_queue_cleanup()
 *
 * destroys an existing flow queue
 *
 * no return value
 */
void flow_queue_cleanup(flow_queue_p fqp)
{
    struct flow_descriptor *tmp_flow_d = NULL;

    do
    {
	tmp_flow_d = flow_queue_shownext(fqp);
	if (tmp_flow_d)
	{
	    flow_queue_remove(tmp_flow_d);
	    /* TODO: what to do here? release flow? */
	}
    } while (tmp_flow_d);

    free(fqp);
    fqp = NULL;
    return;
}

/* flwo_queue_add()
 *
 * adds a flow to an existing queue
 *
 * no return value
 */
void flow_queue_add(flow_queue_p fqp,
		    struct flow_descriptor *flow_d)
{
    qlist_add_tail(&(flow_d->sched_queue_link), fqp);
    return;
}

/* flow_queue_remove()
 *
 * removes a flow from a queue
 *
 * no return value
 */
void flow_queue_remove(struct flow_descriptor *flow_d)
{
    qlist_del(&(flow_d->sched_queue_link));
    return;
}

/* flow_queue_empty()
 *
 * checks to see if a queue is empty
 *
 * returns 1 if empty, 0 otherwise
 */
int flow_queue_empty(flow_queue_p fqp)
{
    return (qlist_empty(fqp));
}

/* flow_queue_shownext()
 *
 * returns a pointer to the next element in the queue
 *
 * returns pointer on success, NULL on failure
 */
struct flow_descriptor *flow_queue_shownext(flow_queue_p fqp)
{
    flow_descriptor *flow_d = NULL;
    if (fqp->next == fqp)
    {
	return (NULL);
    }
    flow_d = qlist_entry(fqp->next, struct flow_descriptor, sched_queue_link);
    return (flow_d);
}

/* flow_queue_search()
 *
 * searches a queue for a particular flow (does _not_ lock queue)
 *
 * returns pointer to flow descriptor on success, NULL on failure
 */
flow_descriptor *flow_queue_search(flow_queue_p fqp,
				   flow_descriptor * flow_d)
{
    struct qlist_head *tmp_link = NULL;
    flow_descriptor *tmp_flow = NULL;

    qlist_for_each(tmp_link, fqp)
    {
	tmp_flow = qlist_entry(tmp_link, struct flow_descriptor,
			       sched_queue_link);
	if (tmp_flow == flow_d)
	{
	    return (flow_d);
	}
    }

    return (NULL);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

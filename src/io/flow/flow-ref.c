/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <flow-ref.h>

/* flow_ref_new()
 *
 * creates a new linked list to keep up with flow protocol
 *
 * returns pointer to flow reference list on success, NULL on failure
 */
flow_ref_p flow_ref_new(void)
{
	struct qlist_head* tmp_frp = NULL;

	tmp_frp = (struct qlist_head*)malloc(sizeof(struct qlist_head));
	if(tmp_frp)
	{
		INIT_QLIST_HEAD(tmp_frp);
	}

	return(tmp_frp);
}

/* flow_ref_add()
 *
 * adds a new entry to the flow reference list
 * 
 * no return value
 */
int flow_ref_add(flow_ref_p frp, PVFS_endpoint_type src_endpoint, 
	PVFS_endpoint_type dest_endpoint, int flowproto_id)
{
	struct flow_ref_entry* tmp_entry = NULL;

	tmp_entry = (struct flow_ref_entry*)malloc(sizeof(struct
		flow_ref_entry));
	if(!tmp_entry)
	{
		return(-ENOMEM);
	}

	tmp_entry->src_endpoint = src_endpoint;
	tmp_entry->dest_endpoint = dest_endpoint;
	tmp_entry->flowproto_id = flowproto_id;

	qlist_add(&(tmp_entry->flow_ref_link), frp);

	return(0);
}

/* flow_ref_search()
 *
 * searches for a flow reference entry based on src and dest endpoints
 *
 * returns pointer to entry if found, NULL otherwise
 */
struct flow_ref_entry* flow_ref_search(flow_ref_p frp, 
	PVFS_endpoint_type src_endpoint, PVFS_endpoint_type dest_endpoint)
{
	flow_ref_p tmp_link = NULL;
	flow_ref_p tmp_next_link = NULL;
	struct flow_ref_entry* tmp_entry = NULL;

	tmp_link = frp->next;
	tmp_next_link = tmp_link->next;
	while(tmp_link != frp)
	{
		tmp_entry = qlist_entry(tmp_link, struct flow_ref_entry,
			flow_ref_link);
		if(tmp_entry->src_endpoint == src_endpoint &&
			tmp_entry->dest_endpoint == dest_endpoint)
		{
			return(tmp_entry);
		}
		tmp_link = tmp_next_link;
		tmp_next_link = tmp_link->next;
	}

	return(NULL);
}

/* flow_ref_remove()
 *
 * removes a flow reference entry
 *
 * no return value
 */
void flow_ref_remove(struct flow_ref_entry* entry)
{
	qlist_del(&(entry->flow_ref_link));
	return;
}


/* flow_ref_cleanup()
 *
 * destroys a flow reference linked list and all of its entries
 *
 * no return value
 */
void flow_ref_cleanup(flow_ref_p frp)
{
	flow_ref_p iterator = NULL;
	flow_ref_p scratch = NULL;
	struct flow_ref_entry* tmp_entry = NULL;

	qlist_for_each_safe(iterator, scratch, frp)
	{
		tmp_entry = qlist_entry(iterator, struct flow_ref_entry,
			flow_ref_link);
		free(tmp_entry);
	}

	free(frp);
	frp = NULL;
	return;
}


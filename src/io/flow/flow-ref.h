/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header contains contains the interface for keeping up with
 * flow references (which handle mapping incoming flows to the correct
 * protocol)
 */

#ifndef __FLOW_REF_H
#define __FLOW_REF_H

#include <quicklist.h>
#include <flow.h>

/* struct used to map pairs of endpoints to particular flow protocols */
struct flow_ref_entry
{
	PVFS_endpoint_type src_endpoint;
	PVFS_endpoint_type dest_endpoint;
	int flowproto_id;
	struct qlist_head flow_ref_link;
};

typedef struct qlist_head* flow_ref_p;

flow_ref_p flow_ref_new(void);
int flow_ref_add(flow_ref_p frp, PVFS_endpoint_type src_endpoint, 
	PVFS_endpoint_type dest_endpoint, int flowproto_id);
struct flow_ref_entry* flow_ref_search(flow_ref_p frp, 
	PVFS_endpoint_type src_endpoint, PVFS_endpoint_type dest_endpoint);
void flow_ref_remove(struct flow_ref_entry* entry);
void flow_ref_cleanup(flow_ref_p frp);

#endif /* __FLOW_REF_H */

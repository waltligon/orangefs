/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Used by GM to maintain collection of known method addresses */

#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "bmi-gm-addressing.h"
#include "bmi-gm-addr-list.h"
#include "gossip.h"

#include<gm.h>

/* gm_addr_add()
 *
 * adds a gm method address to a quicklist
 *
 * no return value
 */
void gm_addr_add(struct qlist_head *head,
		 method_addr_p map)
{
    struct gm_addr *gm_addr_data = NULL;
    gm_addr_data = map->method_data;
    qlist_add(&(gm_addr_data->gm_addr_list), head);
    return;
}

/* gm_addr_del()
 *
 * deletes a gm method address from a quicklist
 *
 * no return value
 */
void gm_addr_del(method_addr_p map)
{
    struct gm_addr *gm_addr_data = NULL;
    gm_addr_data = map->method_data;
    qlist_del(&(gm_addr_data->gm_addr_list));
    return;
}

/* gm_addr_search()
 *
 * searches for a particular gm method address within a quicklist by
 * comparing gm node id's
 *
 * returns pointer to method address on success, NULL on failure
 */
method_addr_p gm_addr_search(struct qlist_head * head,
			     unsigned int node_id)
{
    struct qlist_head *tmp_entry = NULL;
    struct gm_addr *gm_addr_data = NULL;

    qlist_for_each(tmp_entry, head)
    {
	gm_addr_data = qlist_entry(tmp_entry, struct gm_addr,
				   gm_addr_list);
	if (gm_addr_data->node_id == node_id)
	{
	    /* pointer magic :) we know that the method addr structure and
	     * gm_addr structure are adjacent and contiguous.
	     */
	    return ((method_addr_p) ((unsigned long) gm_addr_data -
				     (unsigned long) sizeof(struct
							    method_addr)));
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
 * vim: ts=8 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Used by GM to maintain collection of known method addresses */

#include<quicklist.h>
#include<bmi-method-support.h>

#ifndef __BMI_GM_ADDR_LIST_H
#define __BMI_GM_ADDR_LIST_H

void gm_addr_add(struct qlist_head* head, method_addr_p map);
void gm_addr_del(method_addr_p map);
method_addr_p gm_addr_search(struct qlist_head* head, unsigned int
	node_id);

#endif /* __BMI_GM_ADDR_LIST_H */

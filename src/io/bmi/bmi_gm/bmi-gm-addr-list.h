/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Used by GM to maintain collection of known method addresses */

#include "quicklist.h"
#include "bmi-method-support.h"

#ifndef __BMI_GM_ADDR_LIST_H
#define __BMI_GM_ADDR_LIST_H

#define bmi_gm_errno_to_pvfs bmi_errno_to_pvfs

void gm_addr_add(struct qlist_head *head,
		 bmi_method_addr_p map);
void gm_addr_del(bmi_method_addr_p map);
bmi_method_addr_p gm_addr_search(struct qlist_head *head,
			     unsigned int node_id,
                             unsigned int port_id);



#endif /* __BMI_GM_ADDR_LIST_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

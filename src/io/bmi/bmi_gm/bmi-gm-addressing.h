/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * tcp specific host addressing information
 */

#ifndef __BMI_GM_ADDRESSING_H
#define __BMI_GM_ADDRESSING_H

#include <bmi-types.h>
#include <quicklist.h>
#include <op-list.h>

#include<gm.h>

/*****************************************************************
 * Information specific to gm
 */

enum
{
	/* unit and port number we will be using on every machine */
	/* TODO: this should be configurable */
	BMI_GM_UNIT_NUM = 0,
	BMI_GM_PORT_NUM = 5,
};

struct gm_addr
{
	struct qlist_head gm_addr_list;
	unsigned int node_id;
	op_list_p send_queue;
	op_list_p handshake_queue;
};


/*****************************************************************
 * function prototypes
 */

#endif /* __BMI_GM_ADDRESSING_H */

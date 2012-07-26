/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * gm specific host addressing information
 */

#ifndef __BMI_GM_ADDRESSING_H
#define __BMI_GM_ADDRESSING_H

#include "bmi-types.h"
#include "quicklist.h"
#include "op-list.h"

#include<gm.h>

/*****************************************************************
 * Information specific to gm
 */

/* mask of ports that are off limits to user applications (see GM FAQ) */
#define BMI_GM_MAX_PORTS 8

/* TODO: this should be configurable */
#define BMI_GM_UNIT_NUM  0

struct gm_addr
{
    struct qlist_head gm_addr_list;
    unsigned int node_id;
    unsigned int port_id;
    BMI_addr_t bmi_addr;
    op_list_p send_queue;
    op_list_p handshake_queue;
};


/*****************************************************************
 * function prototypes
 */

#endif /* __BMI_GM_ADDRESSING_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * tcp specific host addressing information
 */

#ifndef __BMI_TCP_ADDRESSING_H
#define __BMI_TCP_ADDRESSING_H

#include "bmi-types.h"

/*****************************************************************
 * Information specific to tcp/ip
 */

typedef int32_t bmi_sock_t;	/* tcp/ip socket */
typedef int32_t bmi_port_t;	/* tcp/ip port */

/* this contains TCP/IP addressing information- it is filled in as
 * connections are made */
struct tcp_addr
{
    method_addr_p map;		/* points back to generic address */
    char *hostname;
    char *ipaddr;
    bmi_port_t port;
    bmi_sock_t socket;
    /* flag that indicates this address represents a
     * server port on which connections may be accepted */
    int server_port;
    /* reference count of pending send operations to this address */
    int write_ref_count;
    /* is the socket connected yet? */
    int not_connected;
    /* socket collection link */
    struct qlist_head sc_link;
};

/*****************************************************************
 * function prototypes
 */

void tcp_forget_addr(method_addr_p map,
		     int dealloc_flag);
method_addr_p alloc_tcp_method_addr(void);

#endif /* __BMI_TCP_ADDRESSING_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

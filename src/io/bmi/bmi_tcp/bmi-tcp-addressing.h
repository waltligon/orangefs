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

#include <netinet/in.h>
#include "bmi-types.h"
#include "quickhash.h"

/*****************************************************************
 * Information specific to tcp/ip
 */

/*
  max number of sequential zero reads to allow; usually indicates a
  dead connection, but it's used for checking several times to be sure
*/
#define BMI_TCP_ZERO_READ_LIMIT  10

/* wait no more than 10 seconds for a partial BMI header to arrive on a
 * socket once we have detected part of it.
 */
#define BMI_TCP_HEADER_WAIT_SECONDS 10

/* peer name types */
#define BMI_TCP_PEER_IP 1
#define BMI_TCP_PEER_HOSTNAME 2

#ifdef USE_TRUSTED

struct tcp_allowed_connection_s {
    int                 port_enforce;
    unsigned long       ports[2];
    int                 network_enforce;
    int                 network_count;
    struct in_addr      *network;
    struct in_addr      *netmask;
};

#endif


/* this contains TCP/IP addressing information- it is filled in as
 * connections are made */
struct tcp_addr
{
    bmi_method_addr_p map;		/* points back to generic address */ \
    BMI_addr_t bmi_addr;
    /* stores error code for addresses that are broken for some reason */
    int addr_error;		
    char *hostname;
    int port;
    int socket;
    /* flag that indicates this address represents a
     * server port on which connections may be accepted */
    int server_port;
    /* reference count of pending send operations to this address */
    int write_ref_count;
    /* is the socket connected yet? */
    int not_connected;
    /* socket collection link */
    struct qlist_head sc_link;
    int sc_index;
    /* count of the number of sequential zero read operations */
    int zero_read_limit;
    /* timer for how long we wait on incomplete headers to arrive */
    int short_header_timer;
    /* flag used to determine if we can reconnect this address after failure */
    int dont_reconnect;
    char* peer;
    int peer_type;
    uint32_t addr_hash; /* hash of string identifier */
    struct qhash_head hash_link;
    bmi_method_addr_p parent;
};


/*****************************************************************
 * function prototypes
 */

#define bmi_tcp_errno_to_pvfs bmi_errno_to_pvfs

void tcp_forget_addr(bmi_method_addr_p map,
		     int dealloc_flag,
		     int error_code);
bmi_method_addr_p alloc_tcp_method_addr(void);

#endif /* __BMI_TCP_ADDRESSING_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

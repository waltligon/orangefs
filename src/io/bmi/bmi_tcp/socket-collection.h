/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * This file contains the visible data structures and function interface
 * for a socket collection library.  This library can maintain lists of
 * sockets and perform polling operations on them.
 */

/*
 * NOTE:  I am making read bits implicit in the implementation.  A poll
 * will always check to see if there is data to be read on a socket.
 */

#ifndef __SOCKET_COLLECTION_H
#define __SOCKET_COLLECTION_H

#include <bmi-method-support.h>
#include <bmi-tcp-addressing.h>
#include <quicklist.h>

typedef struct qlist_head* socket_collection_p;

enum{
	SC_READ_BIT = 1,
	SC_WRITE_BIT = 2,
	SC_ERROR_BIT = 4
};

socket_collection_p socket_collection_init(bmi_sock_t new_server_socket);
void socket_collection_add(socket_collection_p scp, method_addr_p map);
void socket_collection_remove(socket_collection_p scp, method_addr_p map);
void socket_collection_add_write_bit(socket_collection_p scp, 
	method_addr_p map);
void socket_collection_remove_write_bit(socket_collection_p scp, 
	method_addr_p map);
void socket_collection_finalize(socket_collection_p scp);
int socket_collection_testglobal(socket_collection_p scp, int incount, 
	int* outcount, method_addr_p* maps, bmi_flag_t* status, int
	wait_metric);

#endif /* __SOCKET_COLLECTION_H */

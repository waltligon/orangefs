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

#include <assert.h>
#include "bmi-method-support.h"
#include "bmi-tcp-addressing.h"
#include "quicklist.h"
#include "gen-locks.h"

struct socket_collection
{
    gen_mutex_t mutex;

    struct pollfd* pollfd_array;
    method_addr_p* addr_array;
    int array_max;
    int array_count;

    gen_mutex_t queue_mutex;
    struct qlist_head remove_queue;
    struct qlist_head add_queue;

    int server_socket;
};
typedef struct socket_collection* socket_collection_p;

enum
{
    SC_READ_BIT = 1,
    SC_WRITE_BIT = 2,
    SC_ERROR_BIT = 4
};

socket_collection_p BMI_socket_collection_init(int new_server_socket);
void BMI_socket_collection_queue(socket_collection_p scp,
			   method_addr_p map, struct qlist_head* queue);
#define BMI_socket_collection_add(s, m) \
do { \
    gen_mutex_lock(&((s)->queue_mutex)); \
    BMI_socket_collection_queue(s, m, &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

#define BMI_socket_collection_remove(s, m) \
do { \
    gen_mutex_lock(&((s)->queue_mutex)); \
    BMI_socket_collection_queue(s, m, &((s)->remove_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

#define BMI_socket_collection_add_write_bit(s, m) \
do { \
    gen_mutex_lock(&((s)->queue_mutex)); \
    struct tcp_addr* tcp_data = (m)->method_data; \
    tcp_data->write_ref_count++; \
    BMI_socket_collection_queue((s),(m), &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

#define BMI_socket_collection_remove_write_bit(s, m) \
do { \
    gen_mutex_lock(&((s)->queue_mutex)); \
    struct tcp_addr* tcp_data = (m)->method_data; \
    tcp_data->write_ref_count--; \
    assert(tcp_data->write_ref_count > -1); \
    BMI_socket_collection_queue((s),(m), &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

void BMI_socket_collection_finalize(socket_collection_p scp);
int BMI_socket_collection_testglobal(socket_collection_p scp,
				 int incount,
				 int *outcount,
				 method_addr_p * maps,
				 int * status,
				 int poll_timeout,
				 gen_mutex_t* external_mutex);

#endif /* __SOCKET_COLLECTION_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

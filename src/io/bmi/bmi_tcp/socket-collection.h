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
    struct pollfd* pollfd_array;
    bmi_method_addr_p* addr_array;
    int array_max;
    int array_count;

    gen_mutex_t queue_mutex;
    struct qlist_head remove_queue;
    struct qlist_head add_queue;

    int server_socket;
    int pipe_fd[2];
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
			   bmi_method_addr_p map, struct qlist_head* queue);

/* the bmi_tcp code may try to add a socket to the collection before
 * it is fully connected, just ignore in this case
 */
/* write a byte on the pipe_fd[1] so that poll breaks out in case it is idling */
#define BMI_socket_collection_add(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    if(tcp_data->socket > -1){ \
        char c; \
	gen_mutex_lock(&((s)->queue_mutex)); \
	BMI_socket_collection_queue(s, m, &((s)->add_queue)); \
	gen_mutex_unlock(&((s)->queue_mutex)); \
        write(s->pipe_fd[1], &c, 1);\
    } \
} while(0)

#define BMI_socket_collection_remove(s, m) \
do { \
    char c;\
    gen_mutex_lock(&((s)->queue_mutex)); \
    BMI_socket_collection_queue(s, m, &((s)->remove_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
    write(s->pipe_fd[1], &c, 1);\
} while(0)

/* we _must_ have a valid socket at this point if we want to write data */
#define BMI_socket_collection_add_write_bit(s, m) \
do { \
    char c;\
    struct tcp_addr* tcp_data = (m)->method_data; \
    assert(tcp_data->socket > -1); \
    gen_mutex_lock(&((s)->queue_mutex)); \
    tcp_data->write_ref_count++; \
    BMI_socket_collection_queue((s),(m), &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
    write(s->pipe_fd[1], &c, 1);\
} while(0)

#define BMI_socket_collection_remove_write_bit(s, m) \
do { \
    char c;\
    struct tcp_addr* tcp_data = (m)->method_data; \
    gen_mutex_lock(&((s)->queue_mutex)); \
    tcp_data->write_ref_count--; \
    assert(tcp_data->write_ref_count > -1); \
    BMI_socket_collection_queue((s),(m), &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
    write(s->pipe_fd[1], &c, 1);\
} while(0)

void BMI_socket_collection_finalize(socket_collection_p scp);
int BMI_socket_collection_testglobal(socket_collection_p scp,
				 int incount,
				 int *outcount,
				 bmi_method_addr_p * maps,
				 int * status,
				 int poll_timeout);

#endif /* __SOCKET_COLLECTION_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

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

#ifndef __SOCKET_COLLECTION_EPOLL_H
#define __SOCKET_COLLECTION_EPOLL_H

#include <assert.h>
#include <sys/epoll.h>

#include "bmi-method-support.h"
#include "bmi-tcp-addressing.h"
#include "quicklist.h"
#include "gen-locks.h"

#define BMI_EPOLL_MAX_PER_CYCLE 16

struct socket_collection
{
    gen_mutex_t mutex;

    int epfd;
    
    gen_mutex_t queue_mutex;
    struct qlist_head add_queue;
    struct qlist_head remove_queue;

    struct epoll_event event_array[BMI_EPOLL_MAX_PER_CYCLE];

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
			   bmi_method_addr_p map, struct qlist_head* queue);

/* the bmi_tcp code may try to add a socket to the collection before
 * it is fully connected, just ignore in this case
 */
/* TODO: maybe optimize later; with epoll it is safe to add a new descriptor
 * while a poll is in progress, so we could skip lock and queue in some
 * cases.
 */
#ifndef __PVFS2_JOB_THREADED__

#define BMI_socket_collection_add(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    if(tcp_data->socket > -1){ \
	gen_mutex_lock(&((s)->queue_mutex)); \
	BMI_socket_collection_queue(s, m, &((s)->add_queue)); \
	gen_mutex_unlock(&((s)->queue_mutex)); \
    } \
} while(0)

#define BMI_socket_collection_remove(s, m) \
do { \
    gen_mutex_lock(&((s)->queue_mutex)); \
    BMI_socket_collection_queue(s, m, &((s)->remove_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

/* we _must_ have a valid socket at this point if we want to write data */
#define BMI_socket_collection_add_write_bit(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    assert(tcp_data->socket > -1); \
    gen_mutex_lock(&((s)->queue_mutex)); \
    tcp_data->write_ref_count++; \
    BMI_socket_collection_queue((s),(m), &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

#define BMI_socket_collection_remove_write_bit(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    gen_mutex_lock(&((s)->queue_mutex)); \
    tcp_data->write_ref_count--; \
    assert(tcp_data->write_ref_count > -1); \
    BMI_socket_collection_queue((s),(m), &((s)->add_queue)); \
    gen_mutex_unlock(&((s)->queue_mutex)); \
} while(0)

#else

#define BMI_socket_collection_add(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    if(tcp_data->socket > -1){ \
        struct epoll_event event;\
        memset(&event, 0, sizeof(event));\
        event.events = EPOLLIN|EPOLLERR|EPOLLHUP;\
        event.data.ptr = tcp_data->map;\
        epoll_ctl(s->epfd, EPOLL_CTL_ADD, tcp_data->socket, &event);\
    } \
} while(0)

#define BMI_socket_collection_remove(s, m) \
do { \
    struct epoll_event event;\
    struct tcp_addr* tcp_data = (m)->method_data; \
    tcp_data->write_ref_count = 0; \
    memset(&event, 0, sizeof(event));\
    event.events = 0;\
    event.data.ptr = tcp_data->map;\
    epoll_ctl(s->epfd, EPOLL_CTL_DEL, tcp_data->socket, &event);\
} while(0)

/* we _must_ have a valid socket at this point if we want to write data */
#define BMI_socket_collection_add_write_bit(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    struct epoll_event event;\
    assert(tcp_data->socket > -1); \
    tcp_data->write_ref_count++; \
    memset(&event, 0, sizeof(event));\
    event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLOUT;\
    event.data.ptr = tcp_data->map;\
    epoll_ctl(s->epfd, EPOLL_CTL_MOD, tcp_data->socket, &event);\
} while(0)

#define BMI_socket_collection_remove_write_bit(s, m) \
do { \
    struct tcp_addr* tcp_data = (m)->method_data; \
    struct epoll_event event;\
    tcp_data->write_ref_count--; \
    assert(tcp_data->write_ref_count > -1); \
    if (tcp_data->write_ref_count == 0) { \
        memset(&event, 0, sizeof(event));\
        event.events = EPOLLIN|EPOLLERR|EPOLLHUP;\
        event.data.ptr = tcp_data->map;\
        epoll_ctl(s->epfd, EPOLL_CTL_MOD, tcp_data->socket, &event);\
    }\
} while(0)

#endif

void BMI_socket_collection_finalize(socket_collection_p scp);
int BMI_socket_collection_testglobal(socket_collection_p scp,
				 int incount,
				 int *outcount,
				 bmi_method_addr_p * maps,
				 int * status,
				 int poll_timeout,
				 gen_mutex_t* external_mutex);

#endif /* __SOCKET_COLLECTION_EPOLL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

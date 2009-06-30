/* 
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * this is an implementation of a socket collection library.  It can be
 * used to maintain a dynamic list of sockets and perform polling
 * operations.
 */

/*
 * NOTE:  I am making read bits implicit in the implementation.  A poll
 * will always check to see if there is data to be read on a socket.
 */

#include <sys/poll.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#include "gossip.h"
#include "socket-collection-epoll.h"
#include "bmi-method-support.h"
#include "bmi-tcp-addressing.h"
#include "gen-locks.h"

/* errors that can occur on a poll socket */
#define ERRMASK (EPOLLERR|EPOLLHUP)

/* hint to kernel about how many sockets we expect to poll over */
#define EPOLL_CREATE_SIZE 128

/* socket_collection_init()
 * 
 * creates a new socket collection.  It also acquires the server socket
 * from the caller if it is available.  Passing in a negative value
 * indicates that this is being used on a client node and there is no
 * server socket.
 *
 * returns a pointer to the collection on success, NULL on failure.
 */
socket_collection_p BMI_socket_collection_init(int new_server_socket)
{
    struct epoll_event event;
    socket_collection_p tmp_scp = NULL;
    int ret = -1;

    tmp_scp = (struct socket_collection*) malloc(sizeof(struct
	socket_collection));
    if(!tmp_scp)
    {
	return(NULL);
    }

    memset(tmp_scp, 0, sizeof(struct socket_collection));

    tmp_scp->epfd = epoll_create(EPOLL_CREATE_SIZE);
    if(tmp_scp->epfd < 0)
    {
        gossip_err("Error: epoll_create() failure: %s.\n", strerror(errno));
        free(tmp_scp);
        return(NULL);
    }

    tmp_scp->server_socket = new_server_socket;

    if(new_server_socket > -1)
    {
        memset(&event, 0, sizeof(event));
        event.events = (EPOLLIN|EPOLLERR|EPOLLHUP);
        event.data.ptr = NULL;
        ret = epoll_ctl(tmp_scp->epfd, EPOLL_CTL_ADD, new_server_socket,
            &event);
        if(ret < 0 && errno != EEXIST)
        {
            gossip_err("Error: epoll_ctl() failure: %s.\n", strerror(errno));
            free(tmp_scp);
            return(NULL);
        }
    }

    return (tmp_scp);
}

/* socket_collection_finalize()
 *
 * destroys a socket collection.  IMPORTANT:  It DOES NOT destroy the
 * addresses contained within the collection, nor does it terminate
 * connections.  This must be handled elsewhere.
 *
 * no return values.
 */
void BMI_socket_collection_finalize(socket_collection_p scp)
{
    free(scp);
    return;
}


/* socket_collection_testglobal()
 *
 * this function is used to poll to see if any of the new sockets are
 * available for work.  The array of method addresses and array of
 * status fields must be passed into the function by the caller.
 * incount specifies the size of these arrays.  outcount
 * specifies the number of ready addresses.
 *
 * returns 0 on success, -errno on failure.
 */
int BMI_socket_collection_testglobal(socket_collection_p scp,
				 int incount,
				 int *outcount,
				 bmi_method_addr_p * maps,
				 int * status,
				 int poll_timeout)
{
    struct tcp_addr* tcp_addr_data = NULL;
    int ret = -1;
    int old_errno;
    int tmp_count;
    int i;

    /* init the outgoing arguments for safety */
    *outcount = 0;
    memset(maps, 0, (sizeof(bmi_method_addr_p) * incount));
    memset(status, 0, (sizeof(int) * incount));

    if(incount == 0)
    {
        return(0);
    }

    /* actually do the epoll_wait() here */
    do
    {
        tmp_count = incount;
        if(tmp_count > BMI_EPOLL_MAX_PER_CYCLE)
            tmp_count = BMI_EPOLL_MAX_PER_CYCLE;

        ret = epoll_wait(scp->epfd, scp->event_array, tmp_count,
            poll_timeout);

    } while(ret < 0 && errno == EINTR);
    old_errno = errno;

    if(ret < 0)
    {
	return(-old_errno);
    }

    /* nothing ready, just return */
    if(ret == 0)
    {
	return(0);
    }

    tmp_count = ret;

    for(i=0; i<tmp_count; i++)
    {
        assert(scp->event_array[i].events);

        if(scp->event_array[i].events & ERRMASK)
            status[*outcount] |= SC_ERROR_BIT;
        if(scp->event_array[i].events & POLLIN)
            status[*outcount] |= SC_READ_BIT;
        if(scp->event_array[i].events & POLLOUT)
            status[*outcount] |= SC_WRITE_BIT;

        if(scp->event_array[i].data.ptr == NULL)
        {
            /* server socket */
            maps[*outcount] = alloc_tcp_method_addr();
            /* TODO: handle this */
            assert(maps[*outcount]);
            tcp_addr_data = (maps[*outcount])->method_data;
            tcp_addr_data->server_port = 1;
            tcp_addr_data->socket = scp->server_socket;
            tcp_addr_data->port = -1;
        }
        else
        {
            /* normal case */
            maps[*outcount] = scp->event_array[i].data.ptr;
        }

        *outcount = (*outcount) + 1;
    }

    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

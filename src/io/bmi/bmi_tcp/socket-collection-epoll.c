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

    gen_mutex_init(&tmp_scp->mutex);
    gen_mutex_init(&tmp_scp->queue_mutex);

    INIT_QLIST_HEAD(&tmp_scp->remove_queue);
    INIT_QLIST_HEAD(&tmp_scp->add_queue);
    tmp_scp->server_socket = new_server_socket;

    if(new_server_socket > -1)
    {
        event.events = (EPOLLIN|EPOLLERR|EPOLLHUP);
        event.data.ptr = NULL;
        ret = epoll_ctl(tmp_scp->epfd, EPOLL_CTL_ADD, new_server_socket,
            &event);
        if(ret < 0)
        {
            gossip_err("Error: epoll_ctl() failure: %s.\n", strerror(errno));
#if 0
            gen_mutex_destroy(&tmp_scp->mutex);
            gen_mutex_destroy(&tmp_scp->queue_mutex);
#endif
            free(tmp_scp);
            return(NULL);
        }
    }

    return (tmp_scp);
}

/* socket_collection_queue()
 * 
 * queues a tcp method_addr for addition or removal from the collection.
 *
 * returns 0 on success, -errno on failure.
 */
void BMI_socket_collection_queue(socket_collection_p scp,
			   method_addr_p map, struct qlist_head* queue)
{
    struct qlist_head* iterator = NULL;
    struct qlist_head* scratch = NULL;
    struct tcp_addr* tcp_addr_data = NULL;

    /* make sure that this address isn't already slated for addition/removal */
    qlist_for_each_safe(iterator, scratch, &scp->remove_queue)
    {
	tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
	if(tcp_addr_data->map == map)
	{
	    qlist_del(&tcp_addr_data->sc_link);
	    break;
	}
    }
    qlist_for_each_safe(iterator, scratch, &scp->add_queue)
    {
	tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
	if(tcp_addr_data->map == map)
	{
	    qlist_del(&tcp_addr_data->sc_link);
	    break;
	}
    }

    /* add it on to the appropriate queue */
    tcp_addr_data = map->method_data;
    /* add to head, we are likely to access it again soon */
    qlist_add(&tcp_addr_data->sc_link, queue);

    return;
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
#if 0
    gen_mutex_destroy(&scp->mutex);
    gen_mutex_destroy(&scp->queue_mutex);
#endif
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
				 method_addr_p * maps,
				 int * status,
				 int poll_timeout,
				 gen_mutex_t* external_mutex)
{
    struct qlist_head* iterator = NULL;
    struct qlist_head* scratch = NULL;
    struct tcp_addr* tcp_addr_data = NULL;
    int ret = -1;
    int old_errno;
    int tmp_count;
    int i;
    int skip_flag;
    struct epoll_event event;

    /* init the outgoing arguments for safety */
    *outcount = 0;
    memset(maps, 0, (sizeof(method_addr_p) * incount));
    memset(status, 0, (sizeof(int) * incount));

    gen_mutex_lock(&scp->mutex);

    gen_mutex_lock(&scp->queue_mutex);

    /* look for addresses slated for removal */
    qlist_for_each_safe(iterator, scratch, &scp->remove_queue)
    {
	tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
	qlist_del(&tcp_addr_data->sc_link);
        
        /* take out of the epoll set */
        if(tcp_addr_data->sc_index > -1)
        {
            event.events = 0;
            event.data.ptr = tcp_addr_data->map;
            ret = epoll_ctl(scp->epfd, EPOLL_CTL_DEL, tcp_addr_data->socket,
                &event);

            if(ret < 0)
            {
                /* TODO: error handling */
                gossip_lerr("Error: epoll_ctl() failure: %s\n",
                    strerror(errno));
                assert(0);
            }

	    tcp_addr_data->sc_index = -1;
	    tcp_addr_data->write_ref_count = 0;
        }
    }

    /* look for addresses slated for addition */
    qlist_for_each_safe(iterator, scratch, &scp->add_queue)
    {
	tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
	qlist_del(&tcp_addr_data->sc_link);
	if(tcp_addr_data->sc_index > -1)
	{
	    /* update existing entry */
            event.data.ptr = tcp_addr_data->map;
            event.events = (EPOLLIN|EPOLLERR|EPOLLHUP);
	    if(tcp_addr_data->write_ref_count > 0)
                event.events |= EPOLLOUT;
            ret = epoll_ctl(scp->epfd, EPOLL_CTL_MOD, tcp_addr_data->socket,
                &event);

            if(ret < 0)
            {
                /* TODO: error handling */
                gossip_lerr("Error: epoll_ctl() failure: %s\n",
                    strerror(errno));
                assert(0);
            }
	}
	else
	{
	    /* new entry */
            tcp_addr_data->sc_index = 1;

            event.data.ptr = tcp_addr_data->map;
            event.events = (EPOLLIN|EPOLLERR|EPOLLHUP);
	    if(tcp_addr_data->write_ref_count > 0)
                event.events |= EPOLLOUT;
            ret = epoll_ctl(scp->epfd, EPOLL_CTL_ADD, tcp_addr_data->socket,
                &event);
            if(ret < 0)
            {
                /* TODO: error handling */
                gossip_lerr("Error: epoll_ctl() failure: %s\n",
                    strerror(errno));
                assert(0);
            }
	}
    }
    gen_mutex_unlock(&scp->queue_mutex);

    /* actually do the poll() work */
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
	gen_mutex_unlock(&scp->mutex);
	return(-old_errno);
    }

    /* nothing ready, just return */
    if(ret == 0)
    {
	gen_mutex_unlock(&scp->mutex);
	return(0);
    }

    tmp_count = ret;

    for(i=0; i<tmp_count; i++)
    {
        assert(scp->event_array[i].events);
        skip_flag = 0;

        /* make sure this addr hasn't been removed */
        gen_mutex_lock(&scp->queue_mutex);
        qlist_for_each_safe(iterator, scratch, &scp->remove_queue)
        {
            tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
            if(tcp_addr_data->map == scp->event_array[i].data.ptr)
            {
                skip_flag = 1;
                break;
            }
        }
        gen_mutex_unlock(&scp->queue_mutex);
        if(skip_flag)
            continue;

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

    gen_mutex_unlock(&scp->mutex);

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

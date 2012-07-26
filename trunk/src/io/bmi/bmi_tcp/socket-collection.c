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
#include <stdio.h>
#include <sys/poll.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "gossip.h"
#include "socket-collection.h"
#include "bmi-method-support.h"
#include "bmi-tcp-addressing.h"
#include "gen-locks.h"

/* errors that can occur on a poll socket */
#define ERRMASK (POLLERR+POLLHUP+POLLNVAL)

#define POLLFD_ARRAY_START 32
#define POLLFD_ARRAY_INC 32

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

    socket_collection_p tmp_scp = NULL;

    tmp_scp = (struct socket_collection*) malloc(sizeof(struct
	socket_collection));
    if(!tmp_scp)
    {
	return(NULL);
    }

    memset(tmp_scp, 0, sizeof(struct socket_collection));

    gen_mutex_init(&tmp_scp->queue_mutex);

    tmp_scp->pollfd_array = (struct
	pollfd*)malloc(POLLFD_ARRAY_START*sizeof(struct pollfd));
    if(!tmp_scp->pollfd_array)
    {
	free(tmp_scp);
	return(NULL);
    }
    tmp_scp->addr_array =
	(bmi_method_addr_p*)malloc(POLLFD_ARRAY_START*sizeof(bmi_method_addr_p));
    if(!tmp_scp->addr_array)
    {
	free(tmp_scp->pollfd_array);
	free(tmp_scp);
        return NULL;
    }
    if (pipe(tmp_scp->pipe_fd) < 0)
    {
        perror("pipe failed:");
        free(tmp_scp->addr_array);
	free(tmp_scp->pollfd_array);
	free(tmp_scp);
        return NULL;
    }

    tmp_scp->array_max = POLLFD_ARRAY_START;
    tmp_scp->array_count = 0;
    INIT_QLIST_HEAD(&tmp_scp->remove_queue);
    INIT_QLIST_HEAD(&tmp_scp->add_queue);
    tmp_scp->server_socket = new_server_socket;

    if(new_server_socket > -1)
    {
	tmp_scp->pollfd_array[tmp_scp->array_count].fd = new_server_socket;
	tmp_scp->pollfd_array[tmp_scp->array_count].events = POLLIN;
	tmp_scp->addr_array[tmp_scp->array_count] = NULL;
	tmp_scp->array_count++;
    }

    /* Add the pipe_fd[0] fd to the poll in set always */
    tmp_scp->pollfd_array[tmp_scp->array_count].fd = tmp_scp->pipe_fd[0];
    tmp_scp->pollfd_array[tmp_scp->array_count].events = POLLIN;
    tmp_scp->addr_array[tmp_scp->array_count] = NULL;
    tmp_scp->array_count++;

    return (tmp_scp);
}

/* socket_collection_queue()
 * 
 * queues a tcp method_addr for addition or removal from the collection.
 *
 * returns 0 on success, -errno on failure.
 */
void BMI_socket_collection_queue(socket_collection_p scp,
			   bmi_method_addr_p map, struct qlist_head* queue)
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
    free(scp->pollfd_array);
    free(scp->addr_array);
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
    struct qlist_head* iterator = NULL;
    struct qlist_head* scratch = NULL;
    struct tcp_addr* tcp_addr_data = NULL;
    struct tcp_addr* shifted_tcp_addr_data = NULL;
    struct pollfd* tmp_pollfd_array = NULL;
    bmi_method_addr_p* tmp_addr_array = NULL;
    int ret = -1;
    int old_errno;
    int tmp_count;
    int i;
    int skip_flag;
    int pipe_notify = 0;
    struct timeval start, end;
    int allowed_poll_time = poll_timeout;

    gettimeofday(&start, NULL);
do_again:
    /* init the outgoing arguments for safety */
    *outcount = 0;
    memset(maps, 0, (sizeof(bmi_method_addr_p) * incount));
    memset(status, 0, (sizeof(int) * incount));

    gen_mutex_lock(&scp->queue_mutex);

    /* look for addresses slated for removal */
    qlist_for_each_safe(iterator, scratch, &scp->remove_queue)
    {
	tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
	qlist_del(&tcp_addr_data->sc_link);
	/* take out of poll array, shift last entry into its place */
	if(tcp_addr_data->sc_index > -1)
	{
	    scp->pollfd_array[tcp_addr_data->sc_index] = 
		scp->pollfd_array[scp->array_count-1];
	    scp->addr_array[tcp_addr_data->sc_index] = 
		scp->addr_array[scp->array_count-1];
	    shifted_tcp_addr_data =
		scp->addr_array[tcp_addr_data->sc_index]->method_data;
	    shifted_tcp_addr_data->sc_index = tcp_addr_data->sc_index;
	    scp->array_count--;
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
#if 0
	    gossip_err("HELLO: updating addr: %p, index: %d, ref: %d.\n",
		scp->addr_array[tcp_addr_data->sc_index],
		tcp_addr_data->sc_index,
		tcp_addr_data->write_ref_count);
#endif
	    scp->pollfd_array[tcp_addr_data->sc_index].events = POLLIN;
	    if(tcp_addr_data->write_ref_count > 0)
		scp->pollfd_array[tcp_addr_data->sc_index].events |= POLLOUT;
	}
	else
	{
	    /* new entry */
	    if(scp->array_count == scp->array_max)
	    {
		/* we must enlarge the poll arrays */
		tmp_pollfd_array = (struct pollfd*)malloc(
		    (scp->array_max+POLLFD_ARRAY_INC)*sizeof(struct pollfd)); 
		/* TODO: handle this */
		assert(tmp_pollfd_array);
		tmp_addr_array = (bmi_method_addr_p*)malloc(
		    (scp->array_max+POLLFD_ARRAY_INC)*sizeof(bmi_method_addr_p)); 
		/* TODO: handle this */
		assert(tmp_addr_array);
		memcpy(tmp_pollfd_array, scp->pollfd_array,
		    scp->array_max*sizeof(struct pollfd));
		free(scp->pollfd_array);
		scp->pollfd_array = tmp_pollfd_array;
		memcpy(tmp_addr_array, scp->addr_array,
		    scp->array_max*sizeof(bmi_method_addr_p));
		free(scp->addr_array);
		scp->addr_array = tmp_addr_array;
		scp->array_max = scp->array_max+POLLFD_ARRAY_INC;
	    }
	    /* add into pollfd array */
	    tcp_addr_data->sc_index = scp->array_count;
	    scp->array_count++;
	    scp->addr_array[tcp_addr_data->sc_index] = tcp_addr_data->map;
	    scp->pollfd_array[tcp_addr_data->sc_index].fd =
		tcp_addr_data->socket;
	    scp->pollfd_array[tcp_addr_data->sc_index].events = POLLIN;
	    if(tcp_addr_data->write_ref_count > 0)
		scp->pollfd_array[tcp_addr_data->sc_index].events |= POLLOUT;
	}
    }
    gen_mutex_unlock(&scp->queue_mutex);

    /* actually do the poll() work */
    do
    {
	ret = poll(scp->pollfd_array, scp->array_count, allowed_poll_time);
    } while(ret < 0 && errno == EINTR);
    old_errno = errno;

    if(ret < 0)
    {
	return(bmi_tcp_errno_to_pvfs(-old_errno));
    }

    /* nothing ready, just return */
    if(ret == 0)
    {
	return(0);
    }

    tmp_count = ret;

    for(i=0; i<scp->array_count; i++)
    {
	/* short out if we hit count limit */
	if(*outcount == incount || *outcount == tmp_count)
	{
	    break;
	}
        /* make sure we dont return the pipe fd as being ready */
        if (scp->pollfd_array[i].fd == scp->pipe_fd[0])
        {
            if (scp->pollfd_array[i].revents) {
                char c;
                /* drain the pipe */
                read(scp->pipe_fd[0], &c, 1);
                pipe_notify = 1;
            }
            continue;
        }
	/* anything ready on this socket? */
	if (scp->pollfd_array[i].revents)
	{
	    skip_flag = 0;

	    /* make sure that this addr hasn't been removed */
	    gen_mutex_lock(&scp->queue_mutex);
	    qlist_for_each_safe(iterator, scratch, &scp->remove_queue)
	    {
		tcp_addr_data = qlist_entry(iterator, struct tcp_addr, sc_link);
		if(tcp_addr_data->map == scp->addr_array[i])
		{
		    skip_flag = 1;
		    break;
		}
	    }
	    gen_mutex_unlock(&scp->queue_mutex);
	    if(skip_flag)
		continue;

	    if(scp->pollfd_array[i].revents & ERRMASK)
		status[*outcount] |= SC_ERROR_BIT;
	    if(scp->pollfd_array[i].revents & POLLIN)
		status[*outcount] |= SC_READ_BIT;
	    if(scp->pollfd_array[i].revents & POLLOUT)
		status[*outcount] |= SC_WRITE_BIT;

	    if(scp->addr_array[i] == NULL)
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
		maps[*outcount] = scp->addr_array[i];
	    }

	    *outcount = (*outcount) + 1;
	}
    }

    /* Under the following conditions (i.e. all of them must be true) we go back to redoing poll
     * a) There were no outstanding sockets/fds that had data
     * b) There was a pipe notification that our socket sets have changed
     * c) we havent exhausted our allotted time
     */
    if (*outcount == 0 && pipe_notify == 1)
    {
        gettimeofday(&end, NULL);
        timersub(&end, &start, &end);
        allowed_poll_time -= (end.tv_sec * 1000 + end.tv_usec/1000);
        if (allowed_poll_time > 0)
            goto do_again;
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

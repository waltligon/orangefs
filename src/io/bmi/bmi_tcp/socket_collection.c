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

/* NOTE: also note that this code is not re-entrant.  It is written
 * assuming that only one thread (method) will be accessing it at any
 * given time.
 */

#include <sys/poll.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gossip.h>
#include <socket_collection.h>
#include <bmi_method_support.h>
#include <bmi_tcp_addressing.h>

/* number of sockets to poll at a time */
#define SC_POLL_SIZE 128 

/* errors that can occur on a poll socket */
#define ERRMASK (POLLERR+POLLHUP+POLLNVAL)

/* used to keep up with the server socket if we have one. */
static bmi_sock_t server_socket = -1;	

/* internal function prototypes */
static method_addr_p socket_collection_search_addr(socket_collection_p 
	scp, method_addr_p map);
static method_addr_p socket_collection_shownext(
	socket_collection_p scp);

static struct pollfd big_poll_fds[SC_POLL_SIZE];
static method_addr_p big_poll_addr[SC_POLL_SIZE];

/*********************************************************************
 * public function implementations
 */

/*
 * socket_collection_init()
 * 
 * creates a new socket collection.  It also acquires the server socket
 * from the caller if it is available.  Passing in a negative value
 * indicates that this is being used on a client node and there is no
 * server socket.
 *
 * returns a pointer to the collection on success, NULL on failure.
 */
socket_collection_p socket_collection_init(bmi_sock_t new_server_socket){

	socket_collection_p tmp_scp = NULL;

	if(new_server_socket > 0){
		server_socket = new_server_socket;
	}

	tmp_scp = (struct qlist_head*)malloc(sizeof(struct qlist_head));
	if(tmp_scp)
	{
		INIT_QLIST_HEAD(tmp_scp);
	}
	
	return(tmp_scp);
}

/*
 * socket_collection_add()
 * 
 * adds a tcp method_addr to the collection.  It checks to see if the
 * addr is already present in the list first.
 *
 * returns 0 on success, -errno on failure.
 */
void socket_collection_add(socket_collection_p scp, method_addr_p map){

	method_addr_p tmp_map = NULL;
	struct tcp_addr* tcp_data = NULL;

	/* see if it is already in the collection first */
	tmp_map = socket_collection_search_addr(scp, map);

	if(tmp_map){
		/* we already have it */
		return;
	}

	tcp_data = map->method_data;
	tcp_data->write_ref_count = 0;

	/* NOTE: adding to head on purpose.  Probably we will access
	 * this socket soon after adding
	 */
	qlist_add(&(tcp_data->sc_link), scp);
	return;
}


/*
 * socket_collection_remove()
 *
 * removes a tcp method_addr from the collection.
 *
 * returns 0 on success, -errno on failure.
 */
void socket_collection_remove(socket_collection_p scp, method_addr_p
	map){

	struct tcp_addr* tcp_data = map->method_data;

	qlist_del(&(tcp_data->sc_link));

	return;
}


/*
 * socket_collection_add_write_bit()
 *
 * indicates that a poll operation should check for the write condition
 * on this particular socket.  This may be called several times for one
 * socket (method_addr).
 * 
 * returns 0 on success, -errno on failure.
 */
void socket_collection_add_write_bit(socket_collection_p scp, 
	method_addr_p map){

	struct tcp_addr* tcp_data = map->method_data;
	tcp_data->write_ref_count++;
	
	return;
}

/*
 * socket_collection_remove_write_bit()
 *
 * indicates that the given socket no longer needs to be polled for the
 * write condition.  This may also be called multiple times for one
 * address (all instances must be removed before it is no longer polled
 * for the write condition)
 *
 * returns 0 on success, -errno on failure.
 */
void socket_collection_remove_write_bit(socket_collection_p scp, 
	method_addr_p map){

	struct tcp_addr* tcp_data = map->method_data;
	tcp_data->write_ref_count--;
	if(tcp_data->write_ref_count < 0)
	{
		tcp_data->write_ref_count = 0;
	}
	return;
}


/*
 * socket_collection_finalize()
 *
 * destroys a socket collection.  IMPORTANT:  It DOES NOT destroy the
 * addresses contained within the collection, nor does it terminate
 * connections.  This must be handled elsewhere.
 *
 * no return values.
 */
void socket_collection_finalize(socket_collection_p scp){

	/* not much to do here */
	free(scp);
}

/*
 * socket_collection_testglobal()
 *
 * this function is used to poll to see if any of the new sockets are
 * available for work.  The array of method addresses and array of
 * status fields must be passed into the function by the caller.
 * incount specifies the size of these arrays.  outcount
 * specifies the number of ready addresses.
 *
 * returns 0 on success, -errno on failure.
 */
int socket_collection_testglobal(socket_collection_p scp, 
	int incount, int* outcount, method_addr_p* maps, bmi_flag_t* status,
	int wait_metric){

	int num_to_poll = 0;
	int max_to_poll = SC_POLL_SIZE;
	struct tcp_addr* tcp_data = NULL;
	method_addr_p tmp_map = NULL;
	int ret = -1;
	int num_handled = 0;
	int i=0;

	if((incount < 1) || !(outcount) || !(maps) || !(status))
	{
		return(-EINVAL);
	}

	/* init the outgoing arguments for safety */
	*outcount = 0;
	memset(maps, 0, (sizeof(method_addr_p) * incount));
	memset(status, 0, (sizeof(bmi_flag_t) * incount));

	/* paranoia */
	memset(big_poll_fds, 0, (sizeof(struct pollfd) * SC_POLL_SIZE));
	memset(big_poll_addr, 0, (sizeof(method_addr_p) * SC_POLL_SIZE));

	/* leave room for server socket if needed */
	if(server_socket >= 0)
	{
		max_to_poll--;
	}

	/* put a sentinal in the first poll field */
	big_poll_fds[0].fd = -1;
	big_poll_fds[1].fd = -1;
	num_to_poll = 0;
		
	/* add the server socket if we have one */
	if(server_socket >= 0)
	{
		big_poll_fds[num_to_poll].fd = server_socket;
		big_poll_fds[num_to_poll].events = POLLIN;
		num_to_poll++;
	}

	while(num_to_poll < max_to_poll &&
		(tmp_map = socket_collection_shownext(scp)) &&
		((struct tcp_addr*)(tmp_map->method_data))->socket !=
		big_poll_fds[0].fd &&
		((struct tcp_addr*)(tmp_map->method_data))->socket !=
		big_poll_fds[1].fd)
	{
		/* remove the job; add it back at the end of the queue */
		socket_collection_remove(scp, tmp_map);
		tcp_data = tmp_map->method_data;
		if(tcp_data->socket < 0)
		{
			gossip_lerr("Error: found bad socket in socket collection.\n");
			gossip_lerr("Error: not handle properly....\n");
			/* TODO: handle this better */
			return(-EINVAL);
		}
		big_poll_fds[num_to_poll].fd = tcp_data->socket;
		if(tcp_data->write_ref_count > 0)
		{
			big_poll_fds[num_to_poll].events += POLLOUT;
		}
		big_poll_fds[num_to_poll].events += POLLIN;
		big_poll_addr[num_to_poll] = tmp_map;
		num_to_poll++;
		qlist_add_tail(&(tcp_data->sc_link), scp);
	}

	/* we should be all set now to perform the poll operation */
	do
	{
		ret = poll(big_poll_fds, num_to_poll, wait_metric);
	} while(ret < 0 && errno == EINTR);
		
	/* look for poll error */
	if(ret < 0)
	{
		return(-errno);
	}

	/* short out if nothing is ready */
	if(ret == 0)
	{
		return(0);
	}
	
	if(ret <= incount)
	{
		*outcount = ret;
	}
	else
	{
		*outcount = incount;
	}

	num_handled = 0;
	for(i=0; i<num_to_poll; i++)
	{
		/* short out if we have handled as many as the caller wanted */
		if(num_handled == *outcount)
		{
			break;
		}

		/* anything ready on this socket? */
		if(big_poll_fds[i].revents)
		{
			/* error case */
			if(big_poll_fds[i].revents & ERRMASK)
			{
				gossip_lerr("Error: poll error value: 0x%x\n",
					big_poll_fds[i].revents);
				gossip_lerr("Error: on socket: %d\n", big_poll_fds[i].fd);
				status[num_handled] += SC_ERROR_BIT;
			}
			if(big_poll_fds[i].revents & POLLIN)
			{
				status[num_handled] += SC_READ_BIT;
			}
			if(big_poll_fds[i].revents & POLLOUT)
			{
				status[num_handled] += SC_WRITE_BIT;
			}
			
			if(big_poll_addr[i] == NULL)
			{
				/* server socket */
				maps[num_handled] = alloc_tcp_method_addr();
				if(!(maps[num_handled]))
				{
					/* TODO: handle better? */
					return(-ENOMEM);
				}
				tcp_data = (maps[num_handled])->method_data;
				tcp_data->server_port = 1;
				tcp_data->socket = server_socket;
				tcp_data->port = -1;
			}
			else
			{
				/* "normal" socket */
				maps[num_handled] = big_poll_addr[i];
			}
			num_handled++;
		}
	}

	return(0);
}


/*********************************************************************
 * internal utility functions
 */

/*
 * socket_collection_search_addr()
 * 
 * searches a socket collection to find an entry that matches the
 * given method addr.
 *
 * returns a pointer to the method_addr on success, NULL on failure
 */
static method_addr_p socket_collection_search_addr(socket_collection_p 
	scp, method_addr_p map){

	struct tcp_addr* tmp_entry = NULL;
	socket_collection_p tmp_link = NULL;

	qlist_for_each(tmp_link, scp)
	{
		tmp_entry = qlist_entry(tmp_link, struct tcp_addr, sc_link);
		if(tmp_entry->map == map)
		{
			return(map);
		}
	}
	return(NULL);
}

/* socket_collection_shownext()
 *
 * returns a pointer to the next item in the collection
 *
 * returns pointer to method address on success, NULL on failure
 */
static method_addr_p socket_collection_shownext(
	socket_collection_p scp)
{
	struct tcp_addr* tcp_data = NULL;

	if(scp->next == scp)
	{
		return(NULL);
	}
	tcp_data = qlist_entry(scp->next, struct tcp_addr, sc_link);
	return(tcp_data->map);
}

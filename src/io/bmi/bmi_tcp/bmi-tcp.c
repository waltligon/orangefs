/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* TCP/IP implementation of a BMI method */

#include<errno.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>

#include<bmi-method-support.h>
#include<bmi-method-callback.h>
#include<bmi-tcp-addressing.h>
#include<socket-collection.h>
#include<op-list.h>
#include<gossip.h>
#include<sockio.h>
#include<sys/poll.h>
#include<netinet/tcp.h>
#include<quickhash.h>

/* function prototypes */
int BMI_tcp_initialize(method_addr_p listen_addr, bmi_flag_t
	method_id, bmi_flag_t init_flags);
int BMI_tcp_finalize(void);
int BMI_tcp_set_info(int option, void* inout_parameter);
int BMI_tcp_get_info(int option, void* inout_parameter);
void* BMI_tcp_memalloc(bmi_size_t size, bmi_flag_t send_recv);
int BMI_tcp_memfree(void* buffer, bmi_size_t size, bmi_flag_t send_recv);
int BMI_tcp_post_send(bmi_op_id_t* id, method_addr_p dest, void* buffer,
	bmi_size_t size, bmi_flag_t buffer_flag, bmi_msg_tag_t tag, 
	void* user_ptr);
int BMI_tcp_post_sendunexpected(bmi_op_id_t* id, method_addr_p dest, 
	void* buffer, bmi_size_t size, bmi_flag_t buffer_flag, bmi_msg_tag_t
	tag, void* user_ptr);
int BMI_tcp_post_recv(bmi_op_id_t* id, method_addr_p src, void* buffer,
	bmi_size_t expected_size, bmi_size_t* actual_size, bmi_flag_t 
	buffer_flag, bmi_msg_tag_t tag, void* user_ptr);
int BMI_tcp_test(bmi_op_id_t id, int* outcount, bmi_error_code_t*
	error_code, bmi_size_t* actual_size, void** user_ptr,
	int timeout_ms);
int BMI_tcp_testsome(int incount, bmi_op_id_t* id_array, int* outcount,
	int* index_array, bmi_error_code_t* error_code_array, bmi_size_t*
	actual_size_array, void** user_ptr_array, int timeout_ms);
int BMI_tcp_testunexpected(int incount, int* outcount, struct
	method_unexpected_info* info, int timeout_ms);
method_addr_p BMI_tcp_method_addr_lookup(const char* id_string);
int BMI_tcp_post_send_list(bmi_op_id_t* id, method_addr_p dest,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t total_size, bmi_flag_t buffer_flag, bmi_msg_tag_t 
	tag, void* user_ptr);
int BMI_tcp_post_recv_list(bmi_op_id_t* id, method_addr_p src,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t total_expected_size, bmi_size_t* total_actual_size,
	bmi_flag_t buffer_flag, bmi_msg_tag_t tag, void* user_ptr);
int BMI_tcp_post_sendunexpected_list(bmi_op_id_t* id, method_addr_p dest,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t total_size, bmi_flag_t buffer_flag, bmi_msg_tag_t tag,
	void* user_ptr);

char BMI_tcp_method_name[] = "bmi_tcp";

/* structure internal to tcp for use as a message header */
struct tcp_msg_header{
	bmi_flag_t mode;      /* eager, rendezvous, etc. */
	bmi_msg_tag_t tag;    /* user specified message tag */
	bmi_size_t size;      /* length of trailing message */
};

/* tcp private portion of operation structure */
struct tcp_op{
	struct tcp_msg_header env;  /* envelope for this message */
	int tcp_op_state;
	int free_lists_flag;
};
enum
{
	BMI_TCP_INPROGRESS = 1,
	BMI_TCP_BUFFERING = 2
};

/* internal utility functions */
static int tcp_server_init(void);
static void dealloc_tcp_method_addr(method_addr_p map);
static int tcp_sock_init(method_addr_p my_method_addr);
static int enqueue_operation(op_list_p target_list, bmi_flag_t
	send_recv, method_addr_p map, void** buffer_list, bmi_size_t*
	size_list, int list_count, bmi_size_t amt_complete, bmi_size_t
	env_amt_complete, int add_write_bit_flag, bmi_op_id_t* id,
	int tcp_op_state, struct tcp_msg_header header, 
	void* user_ptr, int free_lists_flag, bmi_size_t actual_size,
	bmi_size_t expected_size);
static int tcp_cleanse_addr(method_addr_p map);
static int tcp_shutdown_addr(method_addr_p map);
static int test_done_unexpected(int incount, int* outcount, struct
	method_unexpected_info* info);
static int test_done_some(int incount, bmi_op_id_t* id_array,
	int* outcount, int* index_array, bmi_error_code_t*
	error_code_array, bmi_size_t* actual_size_array, void**
	user_ptr_array);
static int test_done(bmi_op_id_t id, int* outcount,
	bmi_error_code_t* error_code, bmi_size_t* actual_size, void**
	user_ptr);
static int tcp_do_work(int wait_metric);
static int tcp_do_work_error(method_addr_p map);
static int tcp_do_work_recv(method_addr_p map);
static int tcp_do_work_send(method_addr_p map);
static int work_on_recv_op(method_op_p my_method_op);
static int work_on_send_op(method_op_p my_method_op, int*
	blocked_flag);
static int tcp_accept_init(int* socket);
static method_op_p alloc_tcp_method_op(void);
static void dealloc_tcp_method_op(method_op_p old_op);
static int handle_new_connection(method_addr_p map);
static int BMI_tcp_post_send_generic(bmi_op_id_t* id, method_addr_p dest, 
	void** buffer_list, bmi_size_t* size_list, int list_count, 
	bmi_flag_t buffer_flag, struct tcp_msg_header my_header, void* 
	user_ptr, int free_lists_flag);
static int hash_op_id(void* id, int table_size);
static int hash_op_id_compare(void* key, struct qlist_head* link);
static int tcp_post_recv_generic(bmi_op_id_t* id, method_addr_p src,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t expected_size, bmi_size_t* actual_size,
	bmi_flag_t buffer_flag, bmi_msg_tag_t tag, void* user_ptr,
	int free_lists_flag);

/* exported method interface */
struct bmi_method_ops bmi_tcp_ops = 
{
	BMI_tcp_method_name,
	BMI_tcp_initialize,
	BMI_tcp_finalize,
	BMI_tcp_set_info,
	BMI_tcp_get_info,
	BMI_tcp_memalloc,
	BMI_tcp_memfree,
	BMI_tcp_post_send,
	BMI_tcp_post_sendunexpected,
	BMI_tcp_post_recv,
	BMI_tcp_test,
	BMI_tcp_testsome,
	BMI_tcp_testunexpected,
	BMI_tcp_method_addr_lookup,
	BMI_tcp_post_send_list,
	BMI_tcp_post_recv_list,
	BMI_tcp_post_sendunexpected_list
};

/* module parameters */
static method_params_st tcp_method_params;

/* op_list_array indices */
enum
{
	NUM_INDICES = 6,
	IND_SEND = 0,
	IND_RECV = 1,
	IND_RECV_INFLIGHT = 2,
	IND_RECV_EAGER_DONE_BUFFERING = 3,
	IND_COMPLETE = 4,              /* MAKE SURE THESE COME LAST */
	IND_COMPLETE_RECV_UNEXP = 5,   /* MAKE SURE THESE COME LAST */
};

/* internal operation lists */
static op_list_p op_list_array[7] = {NULL, NULL, NULL, NULL,
	NULL, NULL, NULL};

/* internal socket collection */
static socket_collection_p tcp_socket_collection_p = NULL;

/* tunable parameters */
enum
{
	/* amount of pending connections we'll allow */
	TCP_BACKLOG = 256, 	
	/* amount of work to be done during a test.  This roughly 
	 * translates into the number of sockets that we will perform
	 * nonblocking operations on during one function call.
	 */
	TCP_WORK_METRIC = 128,
	/* amount of time we will wait around within the wait functions if no
	 * work is immediately available (in milliseconds)
	 */
	TCP_WAIT_METRIC = 10
};

/* TCP message modes */
enum
{
	TCP_MODE_IMMED = 1,  /* not used for TCP/IP */
	TCP_MODE_UNEXP = 2,
	TCP_MODE_EAGER = 4,
	TCP_MODE_REND = 8
};

/* Allowable sizes for each mode */
enum
{
	TCP_MODE_IMMED_LIMIT = 0,
	TCP_MODE_EAGER_LIMIT = 16384, /* 16K */
	TCP_MODE_REND_LIMIT = 16777216, /* 16M */
	TCP_MODE_UNEXP_LIMIT = 16384 /* 16K */
};

/* hash table for completed operations */
struct qhash_table* completion_hash;

/*************************************************************************
 * Visible Interface 
 */

/* BMI_tcp_initialize()
 *
 * Initializes the tcp method.  Must be called before any other tcp
 * method functions.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_initialize(method_addr_p listen_addr, bmi_flag_t
	method_id, bmi_flag_t init_flags)
{

	int ret = -1;
	int tmp_errno = -ENOSYS;
	struct tcp_addr* tcp_addr_data = NULL;
	int i = 0;

	gossip_ldebug(BMI_DEBUG_TCP, "Initializing TCP/IP module.\n");

	/* check args */
	if((init_flags & BMI_INIT_SERVER) && !listen_addr)
	{
		gossip_lerr("Error: bad parameters given to TCP/IP module.\n");
		return(-EINVAL);
	}

	/* zero out our parameter structure and fill it in */
	memset(&tcp_method_params, 0, sizeof(struct method_params));
	tcp_method_params.method_id = method_id;
	tcp_method_params.mode_immed_limit = TCP_MODE_IMMED_LIMIT;
	tcp_method_params.mode_eager_limit = TCP_MODE_EAGER_LIMIT;
	tcp_method_params.mode_rend_limit = TCP_MODE_REND_LIMIT;
	tcp_method_params.mode_unexp_limit = TCP_MODE_UNEXP_LIMIT;
	tcp_method_params.method_flags = init_flags;

	if(init_flags & BMI_INIT_SERVER)
	{
		/* hang on to our local listening address if needed */
		tcp_method_params.listen_addr = listen_addr;
		/* and initialize server functions */
		ret = tcp_server_init();
		if(ret < 0)
		{
			tmp_errno = ret;
			gossip_lerr("Error: tcp_server_init() failure.\n");
			goto initialize_failure;
		}
	}

	/* set up the operation lists */
	for(i=0; i<NUM_INDICES; i++)
	{
		op_list_array[i] = op_list_new();
		if(!op_list_array[i])
		{
			tmp_errno = -ENOMEM;
			goto initialize_failure;
		}
	}

	/* setup hash table for completed operations */
	completion_hash = qhash_init(hash_op_id_compare, hash_op_id,
		1021);
	if(!completion_hash)
	{
		tmp_errno = -ENOMEM;
		goto initialize_failure;
	}

	/* set up the socket collection */
	if(tcp_method_params.method_flags & BMI_INIT_SERVER)
	{
		tcp_addr_data = tcp_method_params.listen_addr->method_data;
		tcp_socket_collection_p = socket_collection_init(tcp_addr_data->socket);
	}
	else
	{
		tcp_socket_collection_p = socket_collection_init(-1);
	}

	if(!tcp_socket_collection_p)
	{
		tmp_errno = -ENOMEM;
		goto initialize_failure;
	}

	gossip_ldebug(BMI_DEBUG_TCP, "TCP/IP module successfully initialized.\n");
	return(0);

initialize_failure:

	/* cleanup data structures and bail out */
	for(i=0; i< NUM_INDICES; i++)
	{
		if(op_list_array[i])
		{
			op_list_cleanup(op_list_array[i]);
		}
	}
	if(completion_hash)
	{
		qhash_finalize(completion_hash);
	}
	if(tcp_socket_collection_p)
	{
		socket_collection_finalize(tcp_socket_collection_p);
	}
	return(tmp_errno);
}


/* BMI_tcp_finalize()
 * 
 * Shuts down the tcp method.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_finalize(void)
{
	int i = 0;
	
	/* note that this forcefully shuts down operations */
	for(i=0; i<NUM_INDICES; i++)
	{
		if(op_list_array[i])
		{
			op_list_cleanup(op_list_array[i]);
			op_list_array[i] = NULL;
		}
	}

	qhash_finalize(completion_hash);

	/* get rid of socket collection */
	if(tcp_socket_collection_p)
	{
		socket_collection_finalize(tcp_socket_collection_p);
		tcp_socket_collection_p = NULL;
	}

	/* NOTE: we are trusting the calling BMI layer to deallocate 
	 * all of the method addresses (this will close any open sockets)
	 */
	gossip_ldebug(BMI_DEBUG_TCP, "TCP/IP module finalized.\n");
	return(0);
}


/*
 * BMI_tcp_method_addr_lookup()
 *
 * resolves the string representation of an address into a method
 * address structure.  
 *
 * returns a pointer to method_addr on success, NULL on failure
 */
method_addr_p BMI_tcp_method_addr_lookup(const char* id_string)
{
	char* tcp_string = NULL;
	char* delim = NULL;
	char* hostname = NULL;
	method_addr_p new_addr = NULL;
	struct tcp_addr* tcp_addr_data = NULL;
	int ret = -1;
	char local_tag[] = "NULL";

	tcp_string = string_key("tcp", id_string);
	if(!tcp_string){
		/* the string doesn't even have our info */
		return(NULL);
	}

	/* start breaking up the method information */
	/* for normal tcp, it is simply hostname:port */
	if((delim = index(tcp_string, ':')) == NULL){
		gossip_lerr("Error: malformed tcp address.\n");
		free(tcp_string);
		return(NULL);
	}

	/* looks ok, so let's build the method addr structure */
	new_addr = alloc_tcp_method_addr();
	if(!new_addr){
		free(tcp_string);
		return(NULL);
	}
	tcp_addr_data = new_addr->method_data;

	ret = sscanf((delim+1), "%d", &(tcp_addr_data->port));
	if(ret != 1){
		gossip_lerr("Error: malformed tcp address.\n");
		dealloc_method_addr(new_addr);
		free(tcp_string);
		return(NULL);
	}

	hostname = (char*)malloc((delim - tcp_string + 1));
	if(!hostname){
		dealloc_method_addr(new_addr);
		free(tcp_string);
		return(NULL);
	}
	strncpy(hostname, tcp_string, (delim - tcp_string));
	hostname[delim - tcp_string] = '\0';

	tcp_addr_data->hostname = hostname;

	if(strcmp(hostname, local_tag) == 0)
	{
		new_addr->local_addr = 1;
	}

	free(tcp_string);
	return(new_addr);
}


/* BMI_tcp_memalloc()
 * 
 * Allocates memory that can be used in native mode by tcp.
 *
 * returns 0 on success, -errno on failure
 */
void* BMI_tcp_memalloc(bmi_size_t size, bmi_flag_t send_recv)
{
	/* we really don't care what flags the caller uses, TCP/IP has no
	 * preferences about how the memory should be configured.
	 */

	return(malloc((size_t)size));
}


/* BMI_tcp_memfree()
 * 
 * Frees memory that was allocated with BMI_tcp_memalloc()
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_memfree(void* buffer, bmi_size_t size, bmi_flag_t send_recv)
{
	/* NOTE: I am not going to bother to check to see if it is really our
	 * buffer.  This function trusts the caller.
	 * We also could care less whether it was a send or recv buffer.
	 */
	if(buffer)
	{
		free(buffer);
	}

	return(0);
}

/* BMI_tcp_set_info()
 * 
 * Pass in optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_set_info(int option, void* inout_parameter)
{

	int ret = -1;
	method_addr_p tmp_addr = NULL;

	switch(option){

		case BMI_DROP_ADDR:
			if(inout_parameter == NULL){
				ret = -EINVAL;
			}
			else{
				tmp_addr = (method_addr_p)inout_parameter;
				/* take it out of the socket collection */
				tcp_forget_addr(tmp_addr, 1);
				ret = 0;
			}
			break;

		default:
			gossip_ldebug(BMI_DEBUG_TCP, "TCP hint %d not implemented.\n", 
				option);
			ret = 0;
			break;
	}

	return(ret);
}

/* BMI_tcp_get_info()
 * 
 * Query for optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_get_info(int option, void* inout_parameter)
{
	int ret = -1;

	switch(option)
	{
		case BMI_CHECK_MAXSIZE:
			*((int*)inout_parameter) = TCP_MODE_REND_LIMIT;
			ret = 0;
		default:
			gossip_ldebug(BMI_DEBUG_TCP, "TCP hint %d not implemented.\n", 
				option);
			ret = 0;
			break;
	}

	return(ret);
}


/* BMI_tcp_post_send()
 * 
 * Submits send operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_tcp_post_send(bmi_op_id_t* id, method_addr_p dest, void* buffer,
	bmi_size_t size, bmi_flag_t buffer_flag, bmi_msg_tag_t tag, 
	void* user_ptr)
{
	struct tcp_msg_header my_header;
	void** buffer_list = NULL;
	bmi_size_t* size_list = NULL;
	
	/* clear the id field for safety */
	*id = 0;

	/* fill in the TCP-specific message header */
	if(size > TCP_MODE_REND_LIMIT)
	{
		return(-EINVAL);
	}

	/* allocate list I/O descriptions */
	/* NOTE: these will be freed by dealloc_tcp_method_op() */
	buffer_list = (void**)malloc(sizeof(void*));
	if(!buffer_list)
	{
		return(-ENOMEM);
	}
	buffer_list[0] = buffer;

	size_list = (bmi_size_t*)malloc(sizeof(bmi_size_t));
	if(!size_list)
	{
		free(buffer_list);
		return(-ENOMEM);
	}
	size_list[0] = size;

	if (size <= TCP_MODE_EAGER_LIMIT)
	{
		my_header.mode = TCP_MODE_EAGER;
	}
	else 
	{
		my_header.mode = TCP_MODE_REND;
	}
	my_header.tag = tag;
	my_header.size = size;

	return(BMI_tcp_post_send_generic(id, dest, buffer_list,
		size_list, 1, buffer_flag, my_header, user_ptr, 1));
}


/* BMI_tcp_post_sendunexpected()
 * 
 * Submits unexpected send operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_tcp_post_sendunexpected(bmi_op_id_t* id, method_addr_p dest, 
	void* buffer, bmi_size_t size, bmi_flag_t buffer_flag, bmi_msg_tag_t
	tag, void* user_ptr)
{
	struct tcp_msg_header my_header;
	void** buffer_list = NULL;
	bmi_size_t* size_list = NULL;

	/* clear the id field for safety */
	*id = 0;

	if(size > TCP_MODE_EAGER_LIMIT)
	{
		return(-EINVAL);
	}

	/* allocate list I/O descriptions */
	/* NOTE: these will be freed by dealloc_tcp_method_op() */
	buffer_list = (void**)malloc(sizeof(void*));
	if(!buffer_list)
	{
		return(-ENOMEM);
	}
	buffer_list[0] = buffer;

	size_list = (bmi_size_t*)malloc(sizeof(bmi_size_t));
	if(!size_list)
	{
		free(buffer_list);
		return(-ENOMEM);
	}
	size_list[0] = size;

	my_header.mode = TCP_MODE_UNEXP;
	my_header.tag = tag;
	my_header.size = size;

	return(BMI_tcp_post_send_generic(id, dest, buffer_list,
		size_list, 1, buffer_flag, my_header, user_ptr, 1));
}



/* BMI_tcp_post_recv()
 * 
 * Submits recv operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_tcp_post_recv(bmi_op_id_t* id, method_addr_p src, void* buffer,
	bmi_size_t expected_size, bmi_size_t* actual_size, bmi_flag_t 
	buffer_flag, bmi_msg_tag_t tag, void* user_ptr)
{
	int ret = -1;
	void** buffer_list = NULL;
	bmi_size_t* size_list = NULL;

	/* A few things could happen here:
	 * a) rendez. recv with sender not ready yet
	 * b) rendez. recv with sender waiting
	 * c) eager recv, data not available yet
	 * d) eager recv, some/all data already here
	 * e) rendez. recv with sender in eager mode
	 *
	 * b or d could lead to completion without polling.
	 * we don't look for unexpected messages here.
	 */

	if(expected_size > TCP_MODE_REND_LIMIT)
	{
		return(-EINVAL);
	}

	/* allocate list I/O descriptions */
	/* NOTE: these will be freed by dealloc_tcp_method_op() */
	buffer_list = (void**)malloc(sizeof(void*));
	if(!buffer_list)
	{
		return(-ENOMEM);
	}
	buffer_list[0] = buffer;

	size_list = (bmi_size_t*)malloc(sizeof(bmi_size_t));
	if(!size_list)
	{
		free(buffer_list);
		return(-ENOMEM);
	}
	size_list[0] = expected_size;

	ret = tcp_post_recv_generic(id, src, buffer_list, size_list,
		1, expected_size, actual_size, buffer_flag, tag,
		user_ptr, 1);

	return(ret);
}


/* BMI_tcp_test()
 * 
 * Checks to see if a particular message has completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_test(bmi_op_id_t id, int* outcount, bmi_error_code_t* 
	error_code, bmi_size_t* actual_size, void** user_ptr, 
	int wait_time)
{
	int ret = -1;

	/* do some ``real work'' here */
	ret = tcp_do_work(wait_time);
	if(ret < 0)
	{
		return(ret);
	}

	ret = test_done(id, outcount, error_code, actual_size, user_ptr);

	return(ret);
}

/* BMI_tcp_testsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testsome(int incount, bmi_op_id_t* id_array, 
	int* outcount, int* index_array, bmi_error_code_t* error_code_array,
	bmi_size_t* actual_size_array, void** user_ptr_array, int wait_time)
{
	int ret = -1;

	/* do some ``real work'' here */
	ret = tcp_do_work(wait_time);
	if(ret < 0)
	{
		return(ret);
	}

	ret = test_done_some(incount, id_array, outcount, index_array,
		error_code_array, actual_size_array, user_ptr_array);

	return(ret);
}


/* BMI_tcp_testunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testunexpected(int incount, int* outcount, struct
	method_unexpected_info* info, int wait_time)
{
	int ret = -1;

	/* do some ``real work'' here */
	ret = tcp_do_work(wait_time);
	if(ret < 0)
	{
		return(ret);
	}

	ret = test_done_unexpected(incount, outcount, info);

	return(ret);
}


/* BMI_tcp_post_send_list()
 *
 * same as the BMI_tcp_post_send() function, except that it sends
 * from an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_send_list(bmi_op_id_t* id, method_addr_p dest,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t total_size, bmi_flag_t buffer_flag, bmi_msg_tag_t 
	tag, void* user_ptr)
{
	struct tcp_msg_header my_header;
	
	/* clear the id field for safety */
	*id = 0;

	/* fill in the TCP-specific message header */
	if(total_size > TCP_MODE_REND_LIMIT)
	{
		return(-EINVAL);
	}

	if (total_size <= TCP_MODE_EAGER_LIMIT)
	{
		my_header.mode = TCP_MODE_EAGER;
	}
	else 
	{
		my_header.mode = TCP_MODE_REND;
	}
	my_header.tag = tag;
	my_header.size = total_size;

	return(BMI_tcp_post_send_generic(id, dest, buffer_list,
		size_list, list_count, buffer_flag, my_header, user_ptr, 0));
}

/* BMI_tcp_post_recv_list()
 *
 * same as the BMI_tcp_post_recv() function, except that it recvs
 * into an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_recv_list(bmi_op_id_t* id, method_addr_p src,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t total_expected_size, bmi_size_t* total_actual_size,
	bmi_flag_t buffer_flag, bmi_msg_tag_t tag, void* user_ptr)
{
	int ret = -1;

	if(total_expected_size > TCP_MODE_REND_LIMIT)
	{
		return(-EINVAL);
	}

	ret = tcp_post_recv_generic(id, src, buffer_list, size_list,
		list_count, total_expected_size, total_actual_size, 
		buffer_flag, tag, user_ptr, 0);

	return(ret);
}


/* BMI_tcp_post_sendunexpected_list()
 *
 * same as the BMI_tcp_post_sendunexpected() function, except that 
 * it sends from an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_sendunexpected_list(bmi_op_id_t* id, method_addr_p dest,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t total_size, bmi_flag_t buffer_flag, bmi_msg_tag_t tag,
	void* user_ptr)
{
	struct tcp_msg_header my_header;

	/* clear the id field for safety */
	*id = 0;

	if(total_size > TCP_MODE_EAGER_LIMIT)
	{
		return(-EINVAL);
	}

	my_header.mode = TCP_MODE_UNEXP;
	my_header.tag = tag;
	my_header.size = total_size;

	return(BMI_tcp_post_send_generic(id, dest, buffer_list,
		size_list, list_count, buffer_flag, my_header, user_ptr, 0));
}

/* tcp_forget_addr()
 *
 * completely removes a tcp method address from use, and aborts any
 * operations that use the address.  If the
 * dealloc_flag is set, the memory used by the address will be
 * deallocated as well.
 *
 * no return value
 */
void tcp_forget_addr(method_addr_p map, int dealloc_flag)
{
	tcp_shutdown_addr(map);
	if(tcp_socket_collection_p)
	{
		socket_collection_remove(tcp_socket_collection_p, map);
	}
	tcp_cleanse_addr(map);
	if(dealloc_flag)
	{
		dealloc_method_addr(map);
	}
	return;
};

/******************************************************************
 * Internal support functions
 */


/*
 * dealloc_tcp_method_addr()
 *
 * destroys method address structures generated by the TCP/IP module.
 *
 * no return value
 */
static void dealloc_tcp_method_addr(method_addr_p map){

	struct tcp_addr* tcp_addr_data = NULL;

	tcp_addr_data = map->method_data;
	/* close the socket, as long as it is not the one we are listening on
	 * as a server.
	 */
	if(!tcp_addr_data->server_port)
	{
		if(tcp_addr_data->socket > -1)
		{
			close(tcp_addr_data->socket);
		}
	}
	
	dealloc_method_addr(map);

	return;
}


/*
 * alloc_tcp_method_addr()
 *
 * creates a new method address with defaults filled in for TCP/IP.
 *
 * returns pointer to struct on success, NULL on failure
 */
method_addr_p alloc_tcp_method_addr(void){

	struct method_addr* my_method_addr = NULL;
	struct tcp_addr* tcp_addr_data = NULL;

	my_method_addr = alloc_method_addr(tcp_method_params.method_id, sizeof(struct
		tcp_addr));
	if(!my_method_addr)
	{
		return(NULL);
	}

	/* note that we trust the alloc_method_addr() function to have zeroed
	 * out the structures for us already 
	 */

	tcp_addr_data = my_method_addr->method_data;
	tcp_addr_data->socket = -1;
	tcp_addr_data->port = -1;
	tcp_addr_data->map = my_method_addr;
	
	return(my_method_addr);
}


/*
 * tcp_server_init()
 *
 * this function is used to prepare a node to recieve incoming
 * connections if it is initialized in a server configuration.   
 *
 * returns 0 on succes, -errno on failure
 */
static int tcp_server_init(void){

	int oldfl = 0; /* old socket flags */
	struct tcp_addr* tcp_addr_data = NULL;
	int tmp_errno = EINVAL;

	/* create a socket */
	tcp_addr_data = tcp_method_params.listen_addr->method_data;
	if((tcp_addr_data->socket = new_sock()) < 0)
	{
		tmp_errno = errno;
		gossip_lerr("Error: new_sock: %s\n", strerror(tmp_errno));
		return(-tmp_errno);
	}

	/* set it to non-blocking operation */
	oldfl = fcntl(tcp_addr_data->socket, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK))
	{
		fcntl(tcp_addr_data->socket, F_SETFL, oldfl | O_NONBLOCK);
	}

	/* setup for a fast restart to avoid bind addr in use errors */
	set_sockopt(tcp_addr_data->socket, SO_REUSEADDR, 1);

	/* bind it to the appropriate port */
	if(bind_sock(tcp_addr_data->socket, tcp_addr_data->port) < 0)
	{
		tmp_errno = errno;
		gossip_lerr("Error: bind_sock: %s\n", strerror(tmp_errno));
		return(-tmp_errno);
	}

	/* go ahead and listen to the socket */
	if(listen(tcp_addr_data->socket, TCP_BACKLOG) != 0)
	{
		tmp_errno = errno;
		gossip_lerr("Error: listen: %s\n", strerror(tmp_errno));
		return(-tmp_errno);
	}

	return(0);
}


/* find_recv_inflight()
 *
 * checks to see if there is a recv operation in flight (when in flight
 * means that some of the data or envelope has been read) for a 
 * particular address. 
 *
 * returns pointer to operation on success, NULL if nothing found.
 */
static method_op_p find_recv_inflight(method_addr_p map)
{
	struct op_list_search_key key;
	method_op_p query_op = NULL;

	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = map;
	key.method_addr_yes = 1;

	query_op = op_list_search(op_list_array[IND_RECV_INFLIGHT], &key);
	
	return(query_op);
}


/* tcp_sock_init()
 *
 * this is an internal function which is used to build up a TCP/IP
 * connection in the situation of a client side operation.
 * addressing information to determine which fields need to be set.
 * If the connection is already established then it does no work.
 *
 * NOTE: this is safe to call repeatedly.  However, always check the
 * value of the not_connected field in the tcp address before using the
 * address.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_sock_init(method_addr_p my_method_addr){

	int oldfl = 0; /* socket flags */
	int ret = -1;
	struct pollfd poll_conn;
	struct tcp_addr* tcp_addr_data = my_method_addr->method_data;
	int tmp_errno = 0;

	/* check for obvious problems */
	if(!my_method_addr){
		return(-EINVAL);
	}
	if(my_method_addr->method_type != tcp_method_params.method_id){
		return(-EINVAL);
	}
	if(tcp_addr_data->server_port){
		return(-EINVAL);
	}

	/* is there already a socket? */
	if(tcp_addr_data->socket > -1){
	/* check to see if we still need to work on the connect.. */
		if(tcp_addr_data->not_connected){
			/* this is a little weird, but we complete the nonblocking
			 * connection by polling */
			poll_conn.fd = tcp_addr_data->socket;
			poll_conn.events = POLLOUT;
			ret = poll(&poll_conn, 1, 2);
			if((ret < 0)||(poll_conn.revents & POLLERR)){
				tmp_errno = errno;
				gossip_lerr("Error: poll: %s\n", strerror(tmp_errno));
				return(-tmp_errno);
			}
			if(poll_conn.revents & POLLOUT){
				tcp_addr_data->not_connected = 0;
			}
		}
		/* return.  the caller should check the "not_connected" flag to
		 * see if the socket is usable yet. */
		return(0);
	}

	/* at this point there is no socket.  try to build it */
	if(tcp_addr_data->port < 1){
		return(-EINVAL);
	}

	/* make a socket */
	if((tcp_addr_data->socket = new_sock())<0){
		tmp_errno = errno;
		return(-tmp_errno);
	}

	/* set it to non-blocking operation */
	oldfl = fcntl(tcp_addr_data->socket, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK)){
		 fcntl(tcp_addr_data->socket, F_SETFL, oldfl | O_NONBLOCK);   
	}

	/* turn of Nagle's algorithm */
	if(set_tcpopt(tcp_addr_data->socket, TCP_NODELAY, 1) < 0)
	{
		tmp_errno = errno;
		gossip_lerr("Error: failed to set TCP_NODELAY option.\n");
		close(tcp_addr_data->socket);
		return(-tmp_errno);
	}

	/* connect_sock will work with both ipaddr and hostname :) */
	if(tcp_addr_data->hostname){
		gossip_ldebug(BMI_DEBUG_TCP, "Connect: socket=%d, hostname=%s, port=%d\n",
			tcp_addr_data->socket, tcp_addr_data->hostname, 
			tcp_addr_data->port);
		ret = connect_sock(tcp_addr_data->socket, tcp_addr_data->hostname,
			tcp_addr_data->port);
	}
	else if(tcp_addr_data->ipaddr){
		gossip_ldebug(BMI_DEBUG_TCP, "Connect: socket=%d, ip=%s, port=%d\n",
			tcp_addr_data->socket, tcp_addr_data->ipaddr, 
			tcp_addr_data->port);
		ret = connect_sock(tcp_addr_data->socket, 
			tcp_addr_data->ipaddr, tcp_addr_data->port);
	}
	else{
		return(-EINVAL);	
	}

	if(ret < 0){
		if(errno == EINPROGRESS){
			tcp_addr_data->not_connected = 1;
			/* this will have to be connected later with a poll */
		}
		else{
			gossip_lerr("Error: connect_sock: %s\n", strerror(errno));
			return(-errno);
		}
	}

	return(0);
}


/* enqueue_operation()
 *
 * creates a new operation based on the arguments to the function.  It
 * then makes sure that the address is added to the socket collection,
 * and the operation is added to the appropriate operation queue.
 *
 * Damn, what a big prototype!
 *
 * returns 0 on success, -errno on failure
 */
static int enqueue_operation(op_list_p target_list, bmi_flag_t
	send_recv, method_addr_p map, void** buffer_list, bmi_size_t*
	size_list, int list_count, bmi_size_t amt_complete, bmi_size_t
	env_amt_complete, int add_write_bit_flag, bmi_op_id_t* id,
	int tcp_op_state, struct tcp_msg_header header, 
	void* user_ptr, int free_lists_flag, bmi_size_t actual_size,
	bmi_size_t expected_size)
{
	method_op_p new_method_op = NULL;
	int bit_added = 0;
	struct tcp_op* tcp_op_data = NULL;

	/* allocate the operation structure */
	new_method_op = alloc_tcp_method_op();
	if(!new_method_op)
	{
		return(-ENOMEM);
	}

	*id = new_method_op->op_id;

	/* set the fields */
	new_method_op->send_recv = send_recv;
	new_method_op->addr = map;
	new_method_op->user_ptr = user_ptr;
	/* this is on purpose; we want to use the buffer_list all of
	 * the time, no special case for one contig buffer
	 */
	new_method_op->buffer = NULL;
	new_method_op->actual_size = actual_size;
	new_method_op->expected_size = expected_size;
	new_method_op->send_recv = send_recv;
	new_method_op->amt_complete = amt_complete;
	new_method_op->env_amt_complete = env_amt_complete;
	new_method_op->msg_tag = header.tag;
	new_method_op->mode = header.mode;
	new_method_op->buffer_list = buffer_list;
	new_method_op->size_list = size_list;
	new_method_op->list_count = list_count;
	if(amt_complete > size_list[0])
	{
		/* we don't handle this yet */
		gossip_lerr("Error: completed more than one segment before queue.\n");
		gossip_lerr("Error: not supported.\n");
		free(new_method_op);
		return(-EINVAL);
	}
	if(amt_complete == size_list[0])
	{
		/* first segment is done */
		new_method_op->list_index = 1;
		new_method_op->cur_index_complete = 0;
	}
	else
	{
		/* more to send on first segment */
		new_method_op->list_index = 0;
		new_method_op->cur_index_complete = amt_complete;
	}
	tcp_op_data = new_method_op->method_data;
	tcp_op_data->tcp_op_state = tcp_op_state;
	tcp_op_data->env = header;
	tcp_op_data->free_lists_flag = free_lists_flag;

	/* add the socket to poll on */
	socket_collection_add(tcp_socket_collection_p, map);

	if(add_write_bit_flag)
	{
		socket_collection_add_write_bit(tcp_socket_collection_p, map);
		bit_added = 1;
	}

	/* keep up with the operation */
	op_list_add(target_list, new_method_op);

	return(0);
}


/* tcp_post_recv_generic()
 *
 * does the real work of posting an operation - works for both
 * eager and rendezvous messages
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
static int tcp_post_recv_generic(bmi_op_id_t* id, method_addr_p src,
	void** buffer_list, bmi_size_t* size_list, int list_count,
	bmi_size_t expected_size, bmi_size_t* actual_size,
	bmi_flag_t buffer_flag, bmi_msg_tag_t tag, void* user_ptr,
	int free_lists_flag)
{	
	method_op_p query_op = NULL;
	int ret = -1;
	int tmp_errno = 0;
	void* working_buf = NULL;
	struct tcp_addr* tcp_addr_data = NULL;
	struct tcp_op* tcp_op_data = NULL;
	struct tcp_msg_header bogus_header;
	bmi_size_t cur_recv_size = 0;
	bmi_size_t max_recv_size = 0;
	struct op_list_search_key key;
	int copy_size = 0;
	bmi_size_t total_copied = 0;
	int i;

	/* lets make sure that the message hasn't already been fully
	 * buffered in eager mode before doing anything else
	 */
	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = src;
	key.method_addr_yes = 1;
	key.msg_tag = tag;
	key.msg_tag_yes = 1;

	query_op = op_list_search(
		op_list_array[IND_RECV_EAGER_DONE_BUFFERING], &key);
	if(query_op)
	{
		/* make sure it isn't too big */
		if(query_op->actual_size > expected_size)
		{
			gossip_lerr("Error: message ordering violation;\n");
			gossip_lerr("Error: message too large for next buffer.\n");
			return(-EPROTO);
		}

		/* whoohoo- it is already done! */
		/* copy buffer out to list segments; handle short case */
		for(i=0; i<query_op->list_count; i++)
		{
			copy_size = query_op->size_list[i];
			if(copy_size + total_copied > query_op->actual_size)
			{
				copy_size = query_op->actual_size - total_copied;
			}
			memcpy(buffer_list[i], (void*)((char*)query_op->buffer +
				total_copied), copy_size);
			total_copied += copy_size;
			if(total_copied == query_op->actual_size)
			{
				break;
			}
		}
		/* copy out to correct memory regions */
		(*actual_size) = query_op->actual_size;
		free(query_op->buffer);
		*id = 0;
		op_list_remove(query_op);
		dealloc_tcp_method_op(query_op);
		return(1);
	}

	/* look for a message that is already being received */
	query_op = op_list_search(op_list_array[IND_RECV_INFLIGHT], &key);
	if(query_op)
	{
		tcp_op_data = query_op->method_data;
	}

	/* see if it is being buffered into a temporary memory region */
	if(query_op && tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
	{
		/* make sure it isn't too big */
		if(query_op->actual_size > expected_size)
		{
			gossip_lerr("Error: message ordering violation;\n");
			gossip_lerr("Error: message too large for next buffer.\n");
			return(-EPROTO);
		}

		/* copy what we have so far into the correct buffers */
		total_copied = 0;
		for(i=0; i<query_op->list_count; i++)
		{
			copy_size = query_op->size_list[i];
			if(copy_size + total_copied > query_op->amt_complete)
			{
				copy_size = query_op->amt_complete - total_copied;
			}
			if(copy_size > 0)
			{
				memcpy(buffer_list[i], (void*)((char*)query_op->buffer +
					total_copied), copy_size);
			}
			total_copied += copy_size;
			if(total_copied == query_op->amt_complete)
			{
				query_op->list_index = i;
				query_op->cur_index_complete = copy_size;
				break;
			}
		}

		/* see if we ended on a buffer boundary */
		if(query_op->cur_index_complete ==
			query_op->size_list[query_op->list_index])
		{
			query_op->list_index++;
			query_op->cur_index_complete = 0;
		}

		/* release the old buffer */
		if(query_op->buffer)
		{
			free(query_op->buffer);
		}

		*id = query_op->op_id;
		tcp_op_data = query_op->method_data;
		tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;

		query_op->buffer_list = buffer_list;
		query_op->size_list = size_list;
		query_op->list_count = list_count;
		query_op->user_ptr = user_ptr;
		tcp_op_data->free_lists_flag = free_lists_flag;

		if(query_op->amt_complete < query_op->actual_size)
		{
			/* try to recv some more data */
			tcp_addr_data = query_op->addr->method_data;
			max_recv_size = query_op->actual_size-query_op->amt_complete;
			cur_recv_size = query_op->size_list[query_op->list_index]
				- query_op->cur_index_complete;
			if(cur_recv_size > max_recv_size)
			{
				cur_recv_size = max_recv_size;
			}
			working_buf =
				(void*)(query_op->buffer_list[query_op->list_index] + 
				query_op->cur_index_complete);
			ret = nbrecv(tcp_addr_data->socket, working_buf,
				cur_recv_size);
			if(ret < 0){
				tmp_errno = errno;
				gossip_lerr("Error: nbrecv: %s\n", strerror(tmp_errno));
				tcp_forget_addr(query_op->addr, 0);
				return(0);
			}

			query_op->amt_complete += ret;
			query_op->cur_index_complete += ret;
		}

		if(query_op->amt_complete == query_op->actual_size){
			/* we are done */
			op_list_remove(query_op);
			*id = 0;
			(*actual_size) = query_op->actual_size;
			dealloc_tcp_method_op(query_op);
			return(1);
		}
		else{
			/* update indices for next time through */
			if(query_op->cur_index_complete ==
				query_op->size_list[query_op->list_index])
			{
				query_op->cur_index_complete = 0;
				query_op->list_index++;
			}
			/* there is still more work to do */
			tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
			return(0);
		}
	}

	/* NOTE: if the message was in flight, but not buffering, then
	 * that means that it has already matched an earlier receive
	 * post or else is an unexpected message that doesn't require a
	 * matching receive post - at any rate it shouldn't be handled
	 * here
	 */

	/* if we hit this point we must enqueue */
	if(expected_size <= TCP_MODE_EAGER_LIMIT)
	{
		bogus_header.mode = TCP_MODE_EAGER;
	}
	else
	{
		bogus_header.mode = TCP_MODE_REND;
	}
	bogus_header.tag = tag;
	ret = enqueue_operation(op_list_array[IND_RECV],
		BMI_OP_RECV, src, buffer_list, size_list, list_count,
		0, 0, 0, id, BMI_TCP_INPROGRESS, bogus_header, user_ptr,
		free_lists_flag, 0, expected_size);
	/* just for safety; this field isn't valid to the caller anymore */
	(*actual_size) = 0;  
	if(ret >= 0)
	{
		/* go ahead and try to do some work while we are in this
		 * function since we appear to be backlogged.  Make sure that
		 * we do not wait in the poll, however.
		 */
		ret = tcp_do_work(0);
	}
	return(ret);
}


/* tcp_cleanse_addr()
 *
 * finds all active operations matching the given address, places them
 * in an error state, and moves them to the completed queue.
 *
 * NOTE: this function does not shut down the address.  That should be
 * handled separately
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_cleanse_addr(method_addr_p map)
{
	int i = 0;
	struct op_list_search_key key;
	method_op_p query_op = NULL;

	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = map;
	key.method_addr_yes = 1;
	
	/* NOTE: we know the completed queues are the last 2 indices! */
	for(i=0; i<(NUM_INDICES-2); i++)
	{
		if(op_list_array[i])
		{
			while((query_op = op_list_search(op_list_array[i], &key)))
			{
				op_list_remove(query_op);
				query_op->error_code = -EPROTO;
				if(query_op->mode == TCP_MODE_UNEXP && query_op->send_recv
					== BMI_OP_RECV)
				{
					op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP], query_op);
				}
				else
				{
					op_list_add(op_list_array[IND_COMPLETE], query_op);
					qhash_add(completion_hash, &(query_op->op_id),
						&(query_op->hash_link));
				}
			}
		}
	}

	return(0);
}


/* tcp_shutdown_addr()
 *
 * closes connections associated with a tcp method address
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_shutdown_addr(method_addr_p map)
{

	struct tcp_addr* tcp_addr_data = map->method_data;
	close(tcp_addr_data->socket);
	tcp_addr_data->socket = -1;
	tcp_addr_data->not_connected = 1;

	return(0);
}


/* test_done_unexpected()
 * 
 * nonblocking check to see if any unexpected operations have completed.
 * The ops may be in the complete or error state.
 *
 * returns 0 on success, -errno on failure
 */
static int test_done_unexpected(int incount, int* outcount, struct
	method_unexpected_info* info)
{
	method_op_p query_op = NULL;
	struct op_list_search_key key;

	*outcount = 0;

	/* search for done or error unexpected ops */
	memset(&key, 0, sizeof(struct op_list_search_key));
	/* TODO: this could be optimized since we don't have any search
	 * paramters.
	 */

	/* go through the completed list as long as we are finding stuff and 
	 * we have room in the info array for it
	 */
	while((*outcount < incount) &&
		(query_op = op_list_search(op_list_array[IND_COMPLETE_RECV_UNEXP], &key)))
	{
		info[*outcount].error_code = query_op->error_code;
		info[*outcount].addr = query_op->addr;
		info[*outcount].buffer = query_op->buffer;
		info[*outcount].size = query_op->actual_size;
		info[*outcount].tag = query_op->msg_tag;
		op_list_remove(query_op);
		dealloc_tcp_method_op(query_op);
		(*outcount)++;
	}
	return(0);
};


/* test_done()
 * 
 * nonblocking check to see if a certain operation has
 * completed.  
 *
 * returns 0 on success, -errno on failure
 */
static int test_done(bmi_op_id_t id, int* outcount, 
	bmi_error_code_t* error_code, bmi_size_t* actual_size, void** user_ptr)
{
	method_op_p query_op = NULL;
	struct qlist_head* hash_link;

	*outcount = 0;

	hash_link = qhash_search(completion_hash, &(id));
	if(hash_link)
	{
		query_op = qlist_entry(hash_link, struct method_op,
			hash_link);
	}
	else
	{
		query_op = NULL;
	}

	if(query_op)
	{
		op_list_remove(query_op);
		qlist_del(&(query_op->hash_link));
		if(user_ptr != NULL)
		{
			(*user_ptr) = query_op->user_ptr;
		}
		(*error_code) = query_op->error_code;
		(*actual_size) = query_op->actual_size;
		dealloc_tcp_method_op(query_op);
		(*outcount)++;
	}

	return(0);
}


/* test_done_some()
 * 
 * nonblocking check to see if a certain set of operations have
 * completed.  Indicates which ones completed using the index array.
 *
 * returns 0 on success, -errno on failure
 */
static int test_done_some(int incount, bmi_op_id_t* id_array, int* outcount, 
	int* index_array, bmi_error_code_t* error_code_array, bmi_size_t*
	actual_size_array, void** user_ptr_array)
{
	method_op_p query_op = NULL;
	int i = 0;
	struct qlist_head* hash_link;

	*outcount = 0;

	for(i=0; i<incount; i++)
	{
		if(id_array[i])
		{
			hash_link = qhash_search(completion_hash,
				&(id_array[i]));
			if(hash_link)
			{
				query_op = qlist_entry(hash_link, struct method_op,
					hash_link);
			}
			else
			{
				query_op = NULL;
			}

			if(query_op)
			{
				op_list_remove(query_op);
				qlist_del(&(query_op->hash_link));
				error_code_array[*outcount] = query_op->error_code;
				actual_size_array[*outcount] = query_op->actual_size;
				index_array[*outcount] = i;
				if(user_ptr_array != NULL)
				{
					user_ptr_array[*outcount] = query_op->user_ptr;
				}
				dealloc_tcp_method_op(query_op);
				(*outcount)++;
			}
		}
	}
	
	return(0);
}


/* tcp_do_work()
 *
 * this is the function that actually does communication work during
 * BMI_tcp_testXXX and BMI_tcp_waitXXX functions.  The amount of work 
 * that it does is tunable.
 *
 * returns 0 on success, -errno on failure.
 */
static int tcp_do_work(int wait_metric)
{
	int ret = -1;
	method_addr_p addr_array[TCP_WORK_METRIC];
	bmi_flag_t status_array[TCP_WORK_METRIC];
	int socket_count = 0;
	int i = 0;

	/* now we need to poll and see what to work on */
	ret = socket_collection_testglobal(tcp_socket_collection_p,
		TCP_WORK_METRIC, &socket_count, addr_array, status_array,
		wait_metric);
	if(ret < 0)
	{
		return(ret);
	}

	/* do different kinds of work depending on results */
	for(i=0; i<socket_count; i++)
	{
		if(status_array[i] & SC_ERROR_BIT)
		{
			ret = tcp_do_work_error(addr_array[i]);
			if(ret < 0)
			{
				return(ret);
			}
		}
		else
		{
			if(status_array[i] & SC_WRITE_BIT)
			{
				ret = tcp_do_work_send(addr_array[i]);
				if(ret < 0)
				{
					return(ret);
				}
			}
			if(status_array[i] & SC_READ_BIT)
			{
				ret = tcp_do_work_recv(addr_array[i]);
				if(ret < 0)
				{
					return(ret);
				}
			}
		}
	}
	return(0);
}


/* tcp_do_work_send()
 *
 * does work on a TCP address that is ready to send data.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_send(method_addr_p map)
{
	method_op_p active_method_op = NULL;
	struct op_list_search_key key;
	int blocked_flag = 0;
	int ret = 0;

	while(blocked_flag == 0 && ret == 0)
	{
		/* what we want to do here is find the first operation in the send
		 * queue for this address.
		 */
		memset(&key, 0, sizeof(struct op_list_search_key));
		key.method_addr = map;
		key.method_addr_yes = 1;
		active_method_op = op_list_search(op_list_array[IND_SEND], &key);
		if(!active_method_op){
			/* ran out of queued sends to work on */
			return(0);
		}

		ret = work_on_send_op(active_method_op, &blocked_flag);
	}

	return(ret);
}


/* handle_new_connection()
 *
 * this function should be called only on special tcp method addresses
 * that represent local server ports.  It will attempt to accept a new
 * connection and create a new method address for the remote host.
 *
 * side effect: destroys the temporary method_address that is passed in
 * to it.
 *
 * returns 0 on success, -errno on failure
 */
static int handle_new_connection(method_addr_p map)
{
	struct tcp_addr* tcp_addr_data = NULL;
	int accepted_socket = -1;
	method_addr_p new_addr = NULL;
	int ret = -1;

	ret = tcp_accept_init(&accepted_socket);
	if(ret < 0)
	{
		return(ret);
	}
	if(accepted_socket < 0)
	{
		/* guess it wasn't ready after all */
		return(0);
	}

	/* ok, we have a new socket.  what now?  Probably simplest
	 * thing to do is to create a new method_addr, add it to the
	 * socket collection, and return.  It will get caught the next
	 * time around */
	new_addr = alloc_tcp_method_addr();
	if(!new_addr){
		return(-ENOMEM);
	}
	gossip_ldebug(BMI_DEBUG_TCP, "Assigning socket %d to new method addr.\n",
		accepted_socket);
	tcp_addr_data = new_addr->method_data;
	tcp_addr_data->socket = accepted_socket;
	/* register this address with the method control layer */
	ret = bmi_method_addr_reg_callback(new_addr);
	if(ret < 0)
	{
		tcp_shutdown_addr(new_addr);
		dealloc_tcp_method_addr(new_addr);
		dealloc_tcp_method_addr(map);
		return(ret);
	}
	socket_collection_add(tcp_socket_collection_p, new_addr);

	dealloc_tcp_method_addr(map);
	return(0);

}


/* tcp_do_work_recv()
 * 
 * does work on a TCP address that is ready to recv data.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_recv(method_addr_p map)
{

	method_op_p active_method_op = NULL;
	int ret = -1;
	void* new_buffer = NULL;
	struct op_list_search_key key;
	struct tcp_msg_header new_header;
	struct tcp_addr* tcp_addr_data = map->method_data;
	int header_size = sizeof(struct tcp_msg_header);
	struct tcp_op* tcp_op_data = NULL;

	/* figure out if this is a new connection */
	if(tcp_addr_data->server_port)
	{
		/* just try to accept connection- no work yet */
		return(handle_new_connection(map));
	}

	/* look for a recv for this address that is already in flight */
	active_method_op = find_recv_inflight(map);
	/* see if we found one in progress... */
	if(active_method_op)
	{
		tcp_op_data = active_method_op->method_data;
		if(active_method_op->mode == TCP_MODE_REND &&
			tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
		{
			/* we must wait for recv post */
			return(0);
		}
		else
		{
			return(work_on_recv_op(active_method_op));
		}
	}

	/* let's see if a the entire header is ready to be received.  If so
	 * we will go ahead and pull it.  Otherwise, we will try again later.
	 * It isn't worth the complication of reading only a partial message
	 * header - we really want it atomically
	 */
	ret = nbpeek(tcp_addr_data->socket, header_size);
	if(ret < 0)
	{
		tcp_forget_addr(map, 0);
		return(0);
	}
	if(ret < header_size)
	{
		/* header not ready yet */
		return(0);
	}

	gossip_ldebug(BMI_DEBUG_TCP, "Reading header for new op.\n");
	/* NOTE: we only allow a blocking call here because we peeked to see
	 * if this amount of data was ready above.  
	 */
	ret = brecv(tcp_addr_data->socket, &new_header, header_size);
	if(ret < header_size){
		gossip_lerr("Error: brecv: %s\n", strerror(errno));
		tcp_forget_addr(map, 0);
		return(0);
	}

	/* so we have the header. now what?  These are the possible
	 * scenarios:
	 * a) unexpected message
	 * b) eager message for which a recv has been posted
	 * c) eager message for which a recv has not been posted
	 * d) rendezvous messsage for which a recv has been posted
	 * e) rendezvous messsage for which a recv has not been posted
	 * f) eager message for which a rend. recv has been posted
	 */
	
	/* TODO: check the magic number of the header.  If it is bad, goto
	 * bottom of function and trash socket.
	 */

	gossip_ldebug(BMI_DEBUG_TCP, "Received new message; mode: %d.\n", 
		(int)new_header.mode);

	if(new_header.mode == TCP_MODE_UNEXP)
	{
		/* allocate the operation structure */
		active_method_op = alloc_tcp_method_op();
		if(!active_method_op)
		{
			tcp_forget_addr(map, 0);
			return(-ENOMEM);
		}
		/* create data buffer */
		new_buffer = malloc(new_header.size);
		if(!new_buffer)
		{
			dealloc_tcp_method_op(active_method_op);
			tcp_forget_addr(map, 0);
			return(-ENOMEM);
		}

		/* set the fields */
		active_method_op->send_recv = BMI_OP_RECV;
		active_method_op->addr = map;
		active_method_op->actual_size = new_header.size;
		active_method_op->expected_size = 0;
		active_method_op->amt_complete = 0;
		active_method_op->env_amt_complete = header_size;
		active_method_op->msg_tag = new_header.tag;
		active_method_op->buffer = new_buffer;
		active_method_op->mode = TCP_MODE_UNEXP;
		active_method_op->buffer_list = &(active_method_op->buffer);
		active_method_op->size_list =
			&(active_method_op->actual_size);
		active_method_op->list_count = 1;
		tcp_op_data = active_method_op->method_data;
		tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
		tcp_op_data->env = new_header;

		op_list_add(op_list_array[IND_RECV_INFLIGHT],
			active_method_op);
		/* grab some data if we can */
		return(work_on_recv_op(active_method_op));
	}

	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = map;
	key.method_addr_yes = 1;
	key.msg_tag = new_header.tag;
	key.msg_tag_yes = 1;

	/* look for a match within the posted operations */
	active_method_op = op_list_search(op_list_array[IND_RECV], &key);

	if(active_method_op)
	{
		/* make sure it isn't too big */
		if(new_header.size > active_method_op->expected_size)
		{
			gossip_lerr("Error: message ordering violation;\n");
			gossip_lerr("Error: message too large for next buffer.\n");
			gossip_lerr("Error: incoming size: %ld, expected size: %ld\n",
				(long)new_header.size, (long)active_method_op->expected_size);
			/* TODO: return error here or do something else? */
			return(-EPROTO);
		}

		/* we found a match.  go work on it and return */
		op_list_remove(active_method_op);
		active_method_op->env_amt_complete = header_size;
		active_method_op->actual_size = new_header.size;
		op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);
		return(work_on_recv_op(active_method_op));
	}

	/* no match anywhere.  Start a new operation */
	/* allocate the operation structure */
	active_method_op = alloc_tcp_method_op();
	if(!active_method_op)
	{
		tcp_forget_addr(map, 0);
		return(-ENOMEM);
	}

	if(new_header.mode == TCP_MODE_EAGER)
	{
		/* create data buffer for eager messages */
		new_buffer = malloc(new_header.size);
		if(!new_buffer)
		{
			dealloc_tcp_method_op(active_method_op);
			tcp_forget_addr(map, 0);
			return(-ENOMEM);
		}
	}
	else
	{
		new_buffer = NULL;
	}

	/* set the fields */
	active_method_op->send_recv = BMI_OP_RECV;
	active_method_op->addr = map;
	active_method_op->actual_size = new_header.size;
	active_method_op->expected_size = 0;
	active_method_op->amt_complete = 0;
	active_method_op->env_amt_complete = header_size;
	active_method_op->msg_tag = new_header.tag;
	active_method_op->buffer = new_buffer;
	active_method_op->mode = new_header.mode;
	active_method_op->buffer_list = &(active_method_op->buffer);
	active_method_op->size_list =
		&(active_method_op->actual_size);
	active_method_op->list_count = 1;
	tcp_op_data = active_method_op->method_data;
	tcp_op_data->tcp_op_state = BMI_TCP_BUFFERING;
	tcp_op_data->env = new_header;

	op_list_add(op_list_array[IND_RECV_INFLIGHT],
		active_method_op);

	/* grab some data if we can */
	if(new_header.mode == TCP_MODE_EAGER)
	{
		return(work_on_recv_op(active_method_op));
	}
	
	return(0);
}


/*
 * work_on_send_op()
 *
 * used to perform work on a send operation.  this is called by the poll
 * function.
 * 
 * sets blocked_flag if no more work can be done on socket without
 * blocking
 * returns 0 on success, -errno on failure.
 */
static int work_on_send_op(method_op_p my_method_op, int*
	blocked_flag)
{
	int ret = -1;
	void* working_buf = NULL;
	struct tcp_addr* tcp_addr_data = my_method_op->addr->method_data;
	struct tcp_op* tcp_op_data = my_method_op->method_data;
	int msg_header_size = sizeof(struct tcp_msg_header);

	*blocked_flag = 1;

	/* make sure that the connection is done before we continue */
	if(tcp_addr_data->not_connected)
	{
		ret = tcp_sock_init(my_method_op->addr);
		if(ret < 0){
			tcp_forget_addr(my_method_op->addr, 0);
			return(0);
		}
		if(tcp_addr_data->not_connected){
			/* try again later- still could not connect */
			tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
			return(0);
		}
	}

	/* ok- have we sent the message envelope yet? */
	if(my_method_op->env_amt_complete < msg_header_size){
		working_buf = (void*)((long)(&(tcp_op_data->env)) + 
			(long)my_method_op->env_amt_complete);
		ret = nbsend(tcp_addr_data->socket, working_buf, 
			(msg_header_size - my_method_op->env_amt_complete));
		if(ret < 0){
			gossip_lerr("Error: nbsend: %s\n", strerror(errno));
			tcp_forget_addr(my_method_op->addr, 0);
			return(0);
		}
		my_method_op->env_amt_complete += ret;
	}

	/* if we didn't finish the envelope, just leave the op in the queue
	 * for later.
	 */
	if(my_method_op->env_amt_complete < msg_header_size)
	{
		tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
		return(0);
	}

	if(my_method_op->actual_size != 0)
	{
		/* now let's try to send some actual data */
		working_buf =
			(void*)(my_method_op->buffer_list[my_method_op->list_index] + 
			my_method_op->cur_index_complete);
		ret = nbsend(tcp_addr_data->socket, working_buf,
			(my_method_op->size_list[my_method_op->list_index] -
			my_method_op->cur_index_complete));
		if(ret < 0){
			gossip_lerr("Error: nbsend: %s\n", strerror(errno));
			tcp_forget_addr(my_method_op->addr, 0);
			return(0);
		}
	}
	else
	{
		ret = 0;
	}

	gossip_ldebug(BMI_DEBUG_TCP, "Sent: %d bytes of data.\n", ret);
	my_method_op->amt_complete += ret;
	my_method_op->cur_index_complete += ret;

	if(my_method_op->amt_complete == my_method_op->actual_size){
		/* we are done */
		my_method_op->error_code = 0;
		socket_collection_remove_write_bit(tcp_socket_collection_p,
			my_method_op->addr);
		op_list_remove(my_method_op);
		op_list_add(op_list_array[IND_COMPLETE], my_method_op);
		qhash_add(completion_hash, &(my_method_op->op_id),
			&(my_method_op->hash_link));
		*blocked_flag = 0;
	}
	else{
		/* update list indices for next time through */
		if(my_method_op->cur_index_complete ==
			my_method_op->size_list[my_method_op->list_index])
		{
			my_method_op->cur_index_complete = 0;
			my_method_op->list_index++;
		}
		/* there is still more work to do */
		tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
	}

	return(0);
}


/*
 * work_on_recv_op()
 *
 * used to perform work on a recv operation.  this is called by the poll
 * function.
 * NOTE: this function assumes the method header has already been read.
 *
 * returns 0 on success, -errno on failure.
 */
static int work_on_recv_op(method_op_p my_method_op){

	void* working_buf = NULL;
	int ret = -1;
	struct tcp_addr* tcp_addr_data = my_method_op->addr->method_data;
	struct tcp_op* tcp_op_data = my_method_op->method_data;
	bmi_size_t cur_recv_size = 0;
	bmi_size_t max_recv_size = 0;

	if(my_method_op->actual_size != 0)
	{
		/* now let's try to recv some actual data */
		max_recv_size = my_method_op->actual_size -
			my_method_op->amt_complete;
		cur_recv_size =
			my_method_op->size_list[my_method_op->list_index] 
			- my_method_op->cur_index_complete;
		if(cur_recv_size > max_recv_size)
		{
			cur_recv_size = max_recv_size;
		}
		working_buf =
		(void*)(my_method_op->buffer_list[my_method_op->list_index] +
			my_method_op->cur_index_complete);
		ret = nbrecv(tcp_addr_data->socket, working_buf,
			cur_recv_size);
		if(ret < 0){
			gossip_lerr("Error: nbrecv: %s\n", strerror(errno));
			tcp_forget_addr(my_method_op->addr, 0);
			return(0);
		}
	}
	else
	{
		ret = 0;
	}

	my_method_op->amt_complete += ret;
	my_method_op->cur_index_complete += ret;

	if(my_method_op->amt_complete == my_method_op->actual_size)
	{
		/* we are done */
		op_list_remove(my_method_op);
		if(tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
		{
			/* queue up to wait on matching post recv */
			op_list_add(op_list_array[IND_RECV_EAGER_DONE_BUFFERING], my_method_op);
		}
		else
		{
			my_method_op->error_code = 0;
			if(my_method_op->mode == TCP_MODE_UNEXP)
			{
				op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP], my_method_op);
			}
			else
			{
				op_list_add(op_list_array[IND_COMPLETE], my_method_op);
				qhash_add(completion_hash, &(my_method_op->op_id),
					&(my_method_op->hash_link));
			}
		}
	}

	/* update indices for next time through */
	if(my_method_op->cur_index_complete ==
		my_method_op->size_list[my_method_op->list_index])
	{
		my_method_op->cur_index_complete = 0;
		my_method_op->list_index++;
	}

	return(0);

}


/* tcp_do_work_error()
 * 
 * handles a tcp address that has indicated an error during polling.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_error(method_addr_p map)
{
	struct tcp_addr* tcp_addr_data = NULL;

	tcp_addr_data = map->method_data;

	if(tcp_addr_data->server_port){
		/* Ignore this and hope it goes away... we don't want to loose
		 * our local socket */
		dealloc_tcp_method_addr(map);
		gossip_lerr("Warning: error polling on server socket.\n");
		return(0);
	}

	tcp_forget_addr(map, 0);

	return(0);
}

/* 
 * tcp_accept_init()
 * 
 * used to establish a connection from the server side.  Attempts an
 * accept call and provides the socket if it succeeds.
 *
 * returns 0 on success, -errno on failure.
 */
static int tcp_accept_init(int* socket)
{

	int ret = -1;
	int tmp_errno = 0;
	struct tcp_addr* tcp_addr_data = tcp_method_params.listen_addr->method_data;

	/* do we have a socket on this end yet? */
	if(tcp_addr_data->socket < 0){
		ret = tcp_server_init();
		if(ret < 0){
			return(ret);
		}
	}

	*socket = accept(tcp_addr_data->socket, NULL, 0);

	if(*socket < 0){
		if((errno == EAGAIN) || 
			(errno == EWOULDBLOCK) ||
			(errno == ENETDOWN) ||
			(errno == EPROTO) ||
			(errno == ENOPROTOOPT) ||
			(errno == EHOSTDOWN) ||
			(errno == ENONET) ||
			(errno == EHOSTUNREACH) ||
			(errno == EOPNOTSUPP) ||
			(errno == ENETUNREACH))
		{
			/* try again later */
			return(0);
		}
		else{
			gossip_lerr("Error: accept: %s\n", strerror(errno));
			return(-errno);
		}
	}

	/* we accepted a new connection.  turn off Nagle's algorithm. */
	if(set_tcpopt(*socket, TCP_NODELAY, 1) < 0)
	{
		tmp_errno = errno;
		gossip_lerr("Error: failed to set TCP_NODELAY option.\n");
		close(*socket);
		return(-tmp_errno);
	}
	return(0);
}
	

/* alloc_tcp_method_op()
 *
 * creates a new method op with defaults filled in for tcp.
 *
 * returns pointer to structure on success, NULL on failure
 */
static method_op_p alloc_tcp_method_op(void)
{
	method_op_p my_method_op = NULL;

	my_method_op = alloc_method_op(sizeof(struct tcp_op));

	/* we trust alloc_method_op to zero it out */

	return(my_method_op);
}


/* dealloc_tcp_method_op()
 *
 * destroys an existing tcp method op, freeing segment lists if
 * needed
 *
 * no return value
 */
static void dealloc_tcp_method_op(method_op_p old_op)
{
	struct tcp_op* tcp_op_data = old_op->method_data;

	if(tcp_op_data->free_lists_flag)
	{
		free(old_op->buffer_list);
		free(old_op->size_list);
	}
	dealloc_method_op(old_op);
	return;
}

/* BMI_tcp_post_send_generic()
 * 
 * Submits send operations (low level).
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
static int BMI_tcp_post_send_generic(bmi_op_id_t* id, method_addr_p dest, 
	void** buffer_list, bmi_size_t* size_list, int list_count, 
	bmi_flag_t buffer_flag, struct tcp_msg_header my_header, 
	void* user_ptr, int free_lists_flag)
{
	struct tcp_addr* tcp_addr_data = dest->method_data;
	method_op_p query_op = NULL;
	int ret = -1;
	int tmp_errno = 0;
	bmi_size_t amt_complete = 0;
	struct op_list_search_key key;
	int msg_header_size = sizeof(struct tcp_msg_header);

	/* Three things can happen here:
	 * a) another op is already in queue for the address, so we just
	 * queue up
	 * b) we can send the whole message and return
	 * c) we send part of the message and queue the rest
	 */

	/* NOTE: on the post_send side of an operation, it doesn't really
	 * matter whether the op is going to be eager or rendezvous.  It is
	 * handled the same way (except for how the header is filled in).
	 * The difference is in the recv processing for TCP.
	 */

	/* NOTE: we also don't care what the buffer_flag says, TCP could care
	 * less what buffers it is using.
	 */

	/* the first thing we must do is find out if another send is queued
	 * up for this address so that we don't mess up our ordering.	 */
	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = dest;
	key.method_addr_yes = 1;
	query_op = op_list_search(op_list_array[IND_SEND], &key);
	if(query_op)
	{
		/* queue up operation */
		ret = enqueue_operation(op_list_array[IND_SEND], BMI_OP_SEND, 
			dest, buffer_list, size_list, list_count, 0, 0, 1, id, 
			BMI_TCP_INPROGRESS, my_header, user_ptr, free_lists_flag,
			my_header.size, 0);
		if(ret >= 0)
		{
			/* go ahead and try to do some work while we are in this
			 * function since we appear to be backlogged.  Make sure that
			 * we do not wait in the poll, however.
			 */
			ret = tcp_do_work(0);
		}
		return(ret);
	}

	/* make sure the connection is established */
	ret = tcp_sock_init(dest);
	if(ret < 0){
		gossip_lerr("Error: tcp_sock_init() failure.\n");
		return(ret);
	}

	tcp_addr_data = dest->method_data;
	if(tcp_addr_data->not_connected)
	{
		/* if the connection is not completed, queue up for later work */
		ret = enqueue_operation(op_list_array[IND_SEND], BMI_OP_SEND, 
			dest, buffer_list, size_list, list_count, 0, 0, 1, id, 
			BMI_TCP_INPROGRESS, my_header, user_ptr, free_lists_flag,
			my_header.size, 0);
		return(ret);
	}

	/* send the message header first */
	tcp_addr_data = dest->method_data;
	ret = nbsend(tcp_addr_data->socket, &my_header, msg_header_size);
	if(ret < 0){
		tmp_errno = errno;
		gossip_lerr("Error: nbsend: %s\n", strerror(tmp_errno));
		tcp_forget_addr(dest, 0);
		return(-tmp_errno);
	}
	if(ret < msg_header_size)
	{
		/* header send not completed */
		ret = enqueue_operation(op_list_array[IND_SEND], BMI_OP_SEND, 
			dest, buffer_list, size_list, list_count, 0, ret, 1, id, 
			BMI_TCP_INPROGRESS, my_header, user_ptr, free_lists_flag,
			my_header.size, 0);
		return(ret);
	}		

	/* we finished sending the header */

	if(my_header.size != 0)
	{
		/* try to send some actual message data */
		ret = nbsend(tcp_addr_data->socket, buffer_list[0], 
			size_list[0]);
		if(ret < 0){
			tmp_errno = errno;
			gossip_lerr("Error: nbsend: %s\n", strerror(tmp_errno));
			tcp_forget_addr(dest, 0);
			return(-tmp_errno);
		}
	}
	else
	{
		ret = 0;
	}

	gossip_ldebug(BMI_DEBUG_TCP, "Sent: %d bytes of data.\n", ret);
	amt_complete = ret;
	if(amt_complete == my_header.size)
	{
		/* we are already done */
		return(1);
	}

	/* queue up the remainder */
	ret = enqueue_operation(op_list_array[IND_SEND], BMI_OP_SEND,
		dest, buffer_list, size_list, list_count, amt_complete, 
		sizeof(struct tcp_msg_header), 1, id, BMI_TCP_INPROGRESS, 
		my_header, user_ptr, free_lists_flag, my_header.size, 0);

	return(ret);
}


/* hash_op_id()
 *
 * hashes on an operation id for use in a quickhash table
 *
 * returns index into table
 */
static int hash_op_id(void* id, int table_size)
{
	/* TODO: this needs to be updated to do the right thing on 64
	 * bit architectures as well.  Right now it assumes it only
	 * needs to look at 32 bits of the id.
	 */
	uint32_t key = 0; 
	bmi_op_id_t* real_id = id;
	key += (*(real_id));
	return (((key >> 3) * (uint32_t)2654435761U)%(uint32_t)table_size);
}

/* hash_op_id_compare()
 *
 * performs a comparison of a hash table entry to a given key
 * (used for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_op_id_compare(void* key, struct qlist_head* link)
{
	struct method_op* my_op;
	bmi_op_id_t* real_id = key;

	my_op = qlist_entry(link, struct method_op, hash_link);
	if(my_op->op_id == *real_id)
	{
		return(1);
	}

	return(0);
}

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* TCP/IP implementation of a BMI method */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <sys/uio.h>
#include <time.h>

#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "bmi-tcp-addressing.h"
#include "socket-collection.h"
#include "op-list.h"
#include "gossip.h"
#include "sockio.h"
#include "bmi-byteswap.h"
#include "id-generator.h"
#include "pint-event.h"
#include "gen-locks.h"

#define BMI_EVENT_START(__op, __id) \
 PINT_event_timestamp(PVFS_EVENT_API_BMI, __op, 0, __id, \
 PVFS_EVENT_FLAG_START)

#define BMI_EVENT_END(__op, __size, __id) \
 PINT_event_timestamp(PVFS_EVENT_API_BMI, __op, __size, __id, \
 PVFS_EVENT_FLAG_END)

static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;

/* function prototypes */
int BMI_tcp_initialize(method_addr_p listen_addr,
		       int method_id,
		       int init_flags);
int BMI_tcp_finalize(void);
int BMI_tcp_set_info(int option,
		     void *inout_parameter);
int BMI_tcp_get_info(int option,
		     void *inout_parameter);
void *BMI_tcp_memalloc(bmi_size_t size,
		       enum bmi_op_type send_recv);
int BMI_tcp_memfree(void *buffer,
		    bmi_size_t size,
		    enum bmi_op_type send_recv);
int BMI_tcp_post_send(bmi_op_id_t * id,
		      method_addr_p dest,
		      const void *buffer,
		      bmi_size_t size,
		      enum bmi_buffer_type buffer_type,
		      bmi_msg_tag_t tag,
		      void *user_ptr,
		      bmi_context_id context_id);
int BMI_tcp_post_sendunexpected(bmi_op_id_t * id,
				method_addr_p dest,
				const void *buffer,
				bmi_size_t size,
				enum bmi_buffer_type buffer_type,
				bmi_msg_tag_t tag,
				void *user_ptr,
				bmi_context_id context_id);
int BMI_tcp_post_recv(bmi_op_id_t * id,
		      method_addr_p src,
		      void *buffer,
		      bmi_size_t expected_size,
		      bmi_size_t * actual_size,
		      enum bmi_buffer_type buffer_type,
		      bmi_msg_tag_t tag,
		      void *user_ptr,
		      bmi_context_id context_id);
int BMI_tcp_test(bmi_op_id_t id,
		 int *outcount,
		 bmi_error_code_t * error_code,
		 bmi_size_t * actual_size,
		 void **user_ptr,
		 int max_idle_time_ms,
		 bmi_context_id context_id);
int BMI_tcp_testsome(int incount,
		     bmi_op_id_t * id_array,
		     int *outcount,
		     int *index_array,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time_ms,
		     bmi_context_id context_id);
int BMI_tcp_testunexpected(int incount,
			   int *outcount,
			   struct method_unexpected_info *info,
			   int max_idle_time_ms);
int BMI_tcp_testcontext(int incount,
		     bmi_op_id_t * out_id_array,
		     int *outcount,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time_ms,
		     bmi_context_id context_id);
method_addr_p BMI_tcp_method_addr_lookup(const char *id_string);
int BMI_tcp_post_send_list(bmi_op_id_t * id,
			   method_addr_p dest,
			   const void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id);
int BMI_tcp_post_recv_list(bmi_op_id_t * id,
			   method_addr_p src,
			   void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_expected_size,
			   bmi_size_t * total_actual_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id);
int BMI_tcp_post_sendunexpected_list(bmi_op_id_t * id,
				     method_addr_p dest,
				     const void *const *buffer_list,
				     const bmi_size_t *size_list,
				     int list_count,
				     bmi_size_t total_size,
				     enum bmi_buffer_type buffer_type,
				     bmi_msg_tag_t tag,
				     void *user_ptr,
				     bmi_context_id context_id);
int BMI_tcp_open_context(bmi_context_id context_id);
void BMI_tcp_close_context(bmi_context_id context_id);
int BMI_tcp_cancel(bmi_op_id_t id, bmi_context_id context_id);

char BMI_tcp_method_name[] = "bmi_tcp";

/* size of encoded message header */
#define TCP_ENC_HDR_SIZE 24

/* structure internal to tcp for use as a message header */
struct tcp_msg_header
{
    uint32_t magic_nr;          /* magic number */
    uint32_t mode;		/* eager, rendezvous, etc. */
    bmi_msg_tag_t tag;		/* user specified message tag */
    bmi_size_t size;		/* length of trailing message */
    char enc_hdr[TCP_ENC_HDR_SIZE];  /* encoded version of header info */
};

#define BMI_TCP_ENC_HDR(hdr)						\
    do {								\
	*((uint32_t*)&((hdr).enc_hdr[0])) = htobmi32((hdr).magic_nr);	\
	*((uint32_t*)&((hdr).enc_hdr[4])) = htobmi32((hdr).mode);	\
	*((uint64_t*)&((hdr).enc_hdr[8])) = htobmi64((hdr).tag);	\
	*((uint64_t*)&((hdr).enc_hdr[16])) = htobmi64((hdr).size);	\
    } while(0)						    

#define BMI_TCP_DEC_HDR(hdr)						\
    do {								\
	(hdr).magic_nr = bmitoh32(*((uint32_t*)&((hdr).enc_hdr[0])));	\
	(hdr).mode = bmitoh32(*((uint32_t*)&((hdr).enc_hdr[4])));	\
	(hdr).tag = bmitoh64(*((uint64_t*)&((hdr).enc_hdr[8])));	\
	(hdr).size = bmitoh64(*((uint64_t*)&((hdr).enc_hdr[16])));	\
    } while(0)						    

/* enumerate states that we care about */
enum bmi_tcp_state
{
    BMI_TCP_INPROGRESS,
    BMI_TCP_BUFFERING,
    BMI_TCP_COMPLETE
};

/* tcp private portion of operation structure */
struct tcp_op
{
    struct tcp_msg_header env;	/* envelope for this message */
    enum bmi_tcp_state tcp_op_state;
    /* these two fields are used as place holders for the buffer
     * list and size list when we really don't have lists (regular
     * BMI_send or BMI_recv operations); it allows us to use
     * generic code to handle both cases 
     */
    void *buffer_list_stub;
    bmi_size_t size_list_stub;
};

/* static io vector for use with readv and writev; we can only use
 * this because BMI serializes module calls
 */
#define BMI_TCP_IOV_COUNT 10
static struct iovec stat_io_vector[BMI_TCP_IOV_COUNT+1];

/* internal utility functions */
static int tcp_server_init(void);
static void dealloc_tcp_method_addr(method_addr_p map);
static int tcp_sock_init(method_addr_p my_method_addr);
static int enqueue_operation(op_list_p target_list,
			     enum bmi_op_type send_recv,
			     method_addr_p map,
			     void *const *buffer_list,
			     const bmi_size_t *size_list,
			     int list_count,
			     bmi_size_t amt_complete,
			     bmi_size_t env_amt_complete,
			     bmi_op_id_t * id,
			     int tcp_op_state,
			     struct tcp_msg_header header,
			     void *user_ptr,
			     bmi_size_t actual_size,
			     bmi_size_t expected_size,
			     bmi_context_id context_id);
static int tcp_cleanse_addr(method_addr_p map, int error_code);
static int tcp_shutdown_addr(method_addr_p map);
static int tcp_do_work(int max_idle_time);
static int tcp_do_work_error(method_addr_p map);
static int tcp_do_work_recv(method_addr_p map, int* stall_flag);
static int tcp_do_work_send(method_addr_p map, int* stall_flag);
static int work_on_recv_op(method_op_p my_method_op,
			   int *stall_flag);
static int work_on_send_op(method_op_p my_method_op,
			   int *blocked_flag, int* stall_flag);
static int tcp_accept_init(int *socket);
static method_op_p alloc_tcp_method_op(void);
static void dealloc_tcp_method_op(method_op_p old_op);
static int handle_new_connection(method_addr_p map);
static int BMI_tcp_post_send_generic(bmi_op_id_t * id,
				     method_addr_p dest,
				     const void *const *buffer_list,
				     const bmi_size_t *size_list,
				     int list_count,
				     enum bmi_buffer_type buffer_type,
				     struct tcp_msg_header my_header,
				     void *user_ptr,
				     bmi_context_id context_id);
static int tcp_post_recv_generic(bmi_op_id_t * id,
				 method_addr_p src,
				 void *const *buffer_list,
				 const bmi_size_t *size_list,
				 int list_count,
				 bmi_size_t expected_size,
				 bmi_size_t * actual_size,
				 enum bmi_buffer_type buffer_type,
				 bmi_msg_tag_t tag,
				 void *user_ptr,
				 bmi_context_id context_id);
static int payload_progress(int s, void *const *buffer_list, const bmi_size_t* 
    size_list, int list_count, bmi_size_t total_size, int* list_index, 
    bmi_size_t* current_index_complete, enum bmi_op_type send_recv, 
    char* enc_hdr, bmi_size_t* env_amt_complete);

/* exported method interface */
struct bmi_method_ops bmi_tcp_ops = {
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
    BMI_tcp_testcontext,
    BMI_tcp_testunexpected,
    BMI_tcp_method_addr_lookup,
    BMI_tcp_post_send_list,
    BMI_tcp_post_recv_list,
    BMI_tcp_post_sendunexpected_list,
    BMI_tcp_open_context,
    BMI_tcp_close_context,
    BMI_tcp_cancel,
};

/* module parameters */
static method_params_st tcp_method_params;

/* op_list_array indices */
enum
{
    NUM_INDICES = 5,
    IND_SEND = 0,
    IND_RECV = 1,
    IND_RECV_INFLIGHT = 2,
    IND_RECV_EAGER_DONE_BUFFERING = 3,
    IND_COMPLETE_RECV_UNEXP = 4,	/* MAKE THIS COMES LAST */
};

/* internal operation lists */
static op_list_p op_list_array[6] = { NULL, NULL, NULL, NULL,
    NULL, NULL
};

/* internal completion queues */
static op_list_p completion_array[BMI_MAX_CONTEXTS] = { NULL };

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
    TCP_WORK_METRIC = 128
};

/* TCP message modes */
enum
{
    TCP_MODE_IMMED = 1,		/* not used for TCP/IP */
    TCP_MODE_UNEXP = 2,
    TCP_MODE_EAGER = 4,
    TCP_MODE_REND = 8
};

/* Allowable sizes for each mode */
enum
{
    TCP_MODE_EAGER_LIMIT = 16384,	/* 16K */
    TCP_MODE_REND_LIMIT = 16777216	/* 16M */
};

/* toggles cancel mode; for bmi_tcp this will result in socket being closed
 * in all cancellation cases
 */
static int forceful_cancel_mode = 0;

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
int BMI_tcp_initialize(method_addr_p listen_addr,
		       int method_id,
		       int init_flags)
{

    int ret = -1;
    int tmp_errno = bmi_tcp_errno_to_pvfs(-ENOSYS);
    struct tcp_addr *tcp_addr_data = NULL;
    int i = 0;

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Initializing TCP/IP module.\n");

    /* check args */
    if ((init_flags & BMI_INIT_SERVER) && !listen_addr)
    {
	gossip_lerr("Error: bad parameters given to TCP/IP module.\n");
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    gen_mutex_lock(&interface_mutex);

    /* zero out our parameter structure and fill it in */
    memset(&tcp_method_params, 0, sizeof(struct method_params));
    tcp_method_params.method_id = method_id;
    tcp_method_params.method_flags = init_flags;

    if (init_flags & BMI_INIT_SERVER)
    {
	/* hang on to our local listening address if needed */
	tcp_method_params.listen_addr = listen_addr;
	/* and initialize server functions */
	ret = tcp_server_init();
	if (ret < 0)
	{
	    tmp_errno = bmi_tcp_errno_to_pvfs(ret);
	    gossip_err("Error: tcp_server_init() failure.\n");
	    goto initialize_failure;
	}
    }

    /* set up the operation lists */
    for (i = 0; i < NUM_INDICES; i++)
    {
	op_list_array[i] = op_list_new();
	if (!op_list_array[i])
	{
	    tmp_errno = bmi_tcp_errno_to_pvfs(-ENOMEM);
	    goto initialize_failure;
	}
    }

    /* set up the socket collection */
    if (tcp_method_params.method_flags & BMI_INIT_SERVER)
    {
	tcp_addr_data = tcp_method_params.listen_addr->method_data;
	tcp_socket_collection_p = BMI_socket_collection_init(tcp_addr_data->socket);
    }
    else
    {
	tcp_socket_collection_p = BMI_socket_collection_init(-1);
    }

    if (!tcp_socket_collection_p)
    {
	tmp_errno = bmi_tcp_errno_to_pvfs(-ENOMEM);
	goto initialize_failure;
    }

    gen_mutex_unlock(&interface_mutex);
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                  "TCP/IP module successfully initialized.\n");
    return (0);

  initialize_failure:

    /* cleanup data structures and bail out */
    for (i = 0; i < NUM_INDICES; i++)
    {
	if (op_list_array[i])
	{
	    op_list_cleanup(op_list_array[i]);
	}
    }
    if (tcp_socket_collection_p)
    {
	BMI_socket_collection_finalize(tcp_socket_collection_p);
    }
    gen_mutex_unlock(&interface_mutex);
    return (tmp_errno);
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

    gen_mutex_lock(&interface_mutex);

    /* shut down our listen addr, if we have one */
    if ((tcp_method_params.method_flags & BMI_INIT_SERVER)
	&& tcp_method_params.listen_addr)
    {
	dealloc_tcp_method_addr(tcp_method_params.listen_addr);
    }

    /* note that this forcefully shuts down operations */
    for (i = 0; i < NUM_INDICES; i++)
    {
	if (op_list_array[i])
	{
	    op_list_cleanup(op_list_array[i]);
	    op_list_array[i] = NULL;
	}
    }

    /* get rid of socket collection */
    if (tcp_socket_collection_p)
    {
	BMI_socket_collection_finalize(tcp_socket_collection_p);
	tcp_socket_collection_p = NULL;
    }

    /* NOTE: we are trusting the calling BMI layer to deallocate 
     * all of the method addresses (this will close any open sockets)
     */
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "TCP/IP module finalized.\n");
    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/*
 * BMI_tcp_method_addr_lookup()
 *
 * resolves the string representation of an address into a method
 * address structure.  
 *
 * returns a pointer to method_addr on success, NULL on failure
 */
method_addr_p BMI_tcp_method_addr_lookup(const char *id_string)
{
    char *tcp_string = NULL;
    char *delim = NULL;
    char *hostname = NULL;
    method_addr_p new_addr = NULL;
    struct tcp_addr *tcp_addr_data = NULL;
    int ret = -1;
    char local_tag[] = "NULL";

    tcp_string = string_key("tcp", id_string);
    if (!tcp_string)
    {
	/* the string doesn't even have our info */
	return (NULL);
    }

    /* start breaking up the method information */
    /* for normal tcp, it is simply hostname:port */
    if ((delim = index(tcp_string, ':')) == NULL)
    {
	gossip_lerr("Error: malformed tcp address.\n");
	free(tcp_string);
	return (NULL);
    }

    /* looks ok, so let's build the method addr structure */
    new_addr = alloc_tcp_method_addr();
    if (!new_addr)
    {
	free(tcp_string);
	return (NULL);
    }
    tcp_addr_data = new_addr->method_data;

    ret = sscanf((delim + 1), "%d", &(tcp_addr_data->port));
    if (ret != 1)
    {
	gossip_lerr("Error: malformed tcp address.\n");
	dealloc_tcp_method_addr(new_addr);
	free(tcp_string);
	return (NULL);
    }

    hostname = (char *) malloc((delim - tcp_string + 1));
    if (!hostname)
    {
	dealloc_tcp_method_addr(new_addr);
	free(tcp_string);
	return (NULL);
    }
    strncpy(hostname, tcp_string, (delim - tcp_string));
    hostname[delim - tcp_string] = '\0';

    tcp_addr_data->hostname = hostname;

    if (strcmp(hostname, local_tag) == 0)
    {
	new_addr->local_addr = 1;
    }

    free(tcp_string);
    return (new_addr);
}


/* BMI_tcp_memalloc()
 * 
 * Allocates memory that can be used in native mode by tcp.
 *
 * returns 0 on success, -errno on failure
 */
void *BMI_tcp_memalloc(bmi_size_t size,
		       enum bmi_op_type send_recv)
{
    /* we really don't care what flags the caller uses, TCP/IP has no
     * preferences about how the memory should be configured.
     */

    return (malloc((size_t) size));
}


/* BMI_tcp_memfree()
 * 
 * Frees memory that was allocated with BMI_tcp_memalloc()
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_memfree(void *buffer,
		    bmi_size_t size,
		    enum bmi_op_type send_recv)
{
    /* NOTE: I am not going to bother to check to see if it is really our
     * buffer.  This function trusts the caller.
     * We also could care less whether it was a send or recv buffer.
     */
    if (buffer)
    {
	free(buffer);
        buffer = NULL;
    }

    return (0);
}

/* BMI_tcp_set_info()
 * 
 * Pass in optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_set_info(int option,
		     void *inout_parameter)
{

    int ret = -1;
    method_addr_p tmp_addr = NULL;

    gen_mutex_lock(&interface_mutex);

    switch (option)
    {

    case BMI_FORCEFUL_CANCEL_MODE:
	forceful_cancel_mode = 1;
	ret = 0;
	break;
    case BMI_DROP_ADDR:
	if (inout_parameter == NULL)
	{
	    ret = bmi_tcp_errno_to_pvfs(-EINVAL);
	}
	else
	{
	    tmp_addr = (method_addr_p) inout_parameter;
	    /* take it out of the socket collection */
	    tcp_forget_addr(tmp_addr, 1, 0);
	    ret = 0;
	}
	break;
    default:
	gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                      "TCP hint %d not implemented.\n", option);
	ret = 0;
	break;
    }

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}

/* BMI_tcp_get_info()
 * 
 * Query for optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_get_info(int option,
		     void *inout_parameter)
{
    struct method_drop_addr_query* query;
    struct tcp_addr* tcp_addr_data;

    gen_mutex_lock(&interface_mutex);

    switch (option)
    {
    case BMI_CHECK_MAXSIZE:
	*((int *) inout_parameter) = TCP_MODE_REND_LIMIT;
	gen_mutex_unlock(&interface_mutex);
	return(0);
	break;
    case BMI_DROP_ADDR_QUERY:
	query = (struct method_drop_addr_query*)inout_parameter;
	tcp_addr_data=query->addr->method_data;
	/* only suggest that we discard the address if we have experienced
	 * an error and there is no way to reconnect
	 */
	if(tcp_addr_data->addr_error != 0 &&
	    tcp_addr_data->dont_reconnect == 1)
	{
	    query->response = 1;
	}
	else
	{
	    query->response = 0;
	}
	gen_mutex_unlock(&interface_mutex);
	return(0);
	break;
    default:
	gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                      "TCP hint %d not implemented.\n", option);
	gen_mutex_unlock(&interface_mutex);
	return(0);
	break;
    }

    gen_mutex_unlock(&interface_mutex);
    return (bmi_tcp_errno_to_pvfs(-ENOSYS));
}


/* BMI_tcp_post_send()
 * 
 * Submits send operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_tcp_post_send(bmi_op_id_t * id,
		      method_addr_p dest,
		      const void *buffer,
		      bmi_size_t size,
		      enum bmi_buffer_type buffer_type,
		      bmi_msg_tag_t tag,
		      void *user_ptr,
		      bmi_context_id context_id)
{
    struct tcp_msg_header my_header;
    int ret = -1;

    /* clear the id field for safety */
    *id = 0;

    /* fill in the TCP-specific message header */
    if (size > TCP_MODE_REND_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EMSGSIZE));
    }

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
    my_header.magic_nr = BMI_MAGIC_NR;

    gen_mutex_lock(&interface_mutex);

    ret = BMI_tcp_post_send_generic(id, dest, &buffer,
				      &size, 1, buffer_type, my_header,
				      user_ptr, context_id);
    if(ret >= 0)
	BMI_EVENT_START(PVFS_EVENT_BMI_SEND, *id);
    if(ret == 1)
	BMI_EVENT_END(PVFS_EVENT_BMI_SEND, size, *id);

    gen_mutex_unlock(&interface_mutex);
    return(ret);
}


/* BMI_tcp_post_sendunexpected()
 * 
 * Submits unexpected send operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_tcp_post_sendunexpected(bmi_op_id_t * id,
				method_addr_p dest,
				const void *buffer,
				bmi_size_t size,
				enum bmi_buffer_type buffer_type,
				bmi_msg_tag_t tag,
				void *user_ptr,
				bmi_context_id context_id)
{
    struct tcp_msg_header my_header;
    int ret = -1;

    /* clear the id field for safety */
    *id = 0;

    if (size > TCP_MODE_EAGER_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EMSGSIZE));
    }

    my_header.mode = TCP_MODE_UNEXP;
    my_header.tag = tag;
    my_header.size = size;
    my_header.magic_nr = BMI_MAGIC_NR;

    gen_mutex_lock(&interface_mutex);

    ret = BMI_tcp_post_send_generic(id, dest, &buffer,
				      &size, 1, buffer_type, my_header,
				      user_ptr, context_id);
    if(ret >= 0)
	BMI_EVENT_START(PVFS_EVENT_BMI_SEND, *id);
    if(ret == 1)
	BMI_EVENT_END(PVFS_EVENT_BMI_SEND, size, *id);

    gen_mutex_unlock(&interface_mutex);
    return(ret);
}



/* BMI_tcp_post_recv()
 * 
 * Submits recv operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_tcp_post_recv(bmi_op_id_t * id,
		      method_addr_p src,
		      void *buffer,
		      bmi_size_t expected_size,
		      bmi_size_t * actual_size,
		      enum bmi_buffer_type buffer_type,
		      bmi_msg_tag_t tag,
		      void *user_ptr,
		      bmi_context_id context_id)
{
    int ret = -1;

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

    if (expected_size > TCP_MODE_REND_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }
    gen_mutex_lock(&interface_mutex);

    ret = tcp_post_recv_generic(id, src, &buffer, &expected_size,
				1, expected_size, actual_size,
				buffer_type, tag,
				user_ptr, context_id);

    if(ret >= 0)
	BMI_EVENT_START(PVFS_EVENT_BMI_RECV, *id);
    if(ret == 1)
	BMI_EVENT_END(PVFS_EVENT_BMI_RECV, *actual_size, *id);

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_tcp_test()
 * 
 * Checks to see if a particular message has completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_test(bmi_op_id_t id,
		 int *outcount,
		 bmi_error_code_t * error_code,
		 bmi_size_t * actual_size,
		 void **user_ptr,
		 int max_idle_time,
		 bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = (method_op_p)id_gen_safe_lookup(id);

    assert(query_op != NULL);

    gen_mutex_lock(&interface_mutex);

    /* do some ``real work'' here */
    ret = tcp_do_work(max_idle_time);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    if(((struct tcp_op*)(query_op->method_data))->tcp_op_state ==
	BMI_TCP_COMPLETE)
    {
	assert(query_op->context_id == context_id);
	op_list_remove(query_op);
	if (user_ptr != NULL)
	{
	    (*user_ptr) = query_op->user_ptr;
	}
	(*error_code) = query_op->error_code;
	(*actual_size) = query_op->actual_size;
	if(query_op->send_recv == BMI_SEND)
	    BMI_EVENT_END(PVFS_EVENT_BMI_SEND, *actual_size, id);
	else
	    BMI_EVENT_END(PVFS_EVENT_BMI_RECV, *actual_size, id);
	dealloc_tcp_method_op(query_op);
	(*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);
}

/* BMI_tcp_testsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testsome(int incount,
		     bmi_op_id_t * id_array,
		     int *outcount,
		     int *index_array,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time,
		     bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = NULL;
    int i;

    gen_mutex_lock(&interface_mutex);

    /* do some ``real work'' here */
    ret = tcp_do_work(max_idle_time);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    for(i=0; i<incount; i++)
    {
	if(id_array[i])
	{
	    /* NOTE: this depends on the user passing in valid id's;
	     * otherwise we segfault.  
	     */
	    query_op = (method_op_p)id_gen_safe_lookup(id_array[i]);
	    if(((struct tcp_op*)(query_op->method_data))->tcp_op_state ==
		BMI_TCP_COMPLETE)
	    {
		assert(query_op->context_id == context_id);
		/* this one's done; pop it out */
		op_list_remove(query_op);
		error_code_array[*outcount] = query_op->error_code;
		actual_size_array[*outcount] = query_op->actual_size;
		index_array[*outcount] = i;
		if (user_ptr_array != NULL)
		{
		    user_ptr_array[*outcount] = query_op->user_ptr;
		}
		if(query_op->send_recv == BMI_SEND)
		    BMI_EVENT_END(PVFS_EVENT_BMI_SEND, query_op->actual_size, id_array[i]);
		else
		    BMI_EVENT_END(PVFS_EVENT_BMI_RECV, query_op->actual_size, id_array[i]);
		dealloc_tcp_method_op(query_op);
		(*outcount)++;
	    }
	}
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* BMI_tcp_testunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testunexpected(int incount,
			   int *outcount,
			   struct method_unexpected_info *info,
			   int max_idle_time)
{
    int ret = -1;
    method_op_p query_op = NULL;

    gen_mutex_lock(&interface_mutex);

    if(op_list_empty(op_list_array[IND_COMPLETE_RECV_UNEXP]))
    {
        /* do some ``real work'' here */
        ret = tcp_do_work(max_idle_time);
        if (ret < 0)
        {
            gen_mutex_unlock(&interface_mutex);
            return (ret);
        }
    }

    *outcount = 0;

    /* go through the completed/unexpected list as long as we are finding 
     * stuff and we have room in the info array for it
     */
    while ((*outcount < incount) &&
	   (query_op =
	    op_list_shownext(op_list_array[IND_COMPLETE_RECV_UNEXP])))
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
    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/* BMI_tcp_testcontext()
 * 
 * Checks to see if any messages from the specified context have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testcontext(int incount,
		     bmi_op_id_t* out_id_array,
		     int *outcount,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time,
		     bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = NULL;

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    if(op_list_empty(completion_array[context_id]))
    {
        /* if there are unexpected ops ready to go, then short out so
         * that the next testunexpected call can pick it up without
         * delay
         */
        if(!op_list_empty(op_list_array[IND_COMPLETE_RECV_UNEXP]))
        {
            gen_mutex_unlock(&interface_mutex);
            return(0);
        }

        /* do some ``real work'' here */
        ret = tcp_do_work(max_idle_time);
        if (ret < 0)
        {
            gen_mutex_unlock(&interface_mutex);
            return (ret);
        }
    }

    /* pop as many items off of the completion queue as we can */
    while((*outcount < incount) && (query_op = 
	op_list_shownext(completion_array[context_id])))
    {
        assert(query_op);
	assert(query_op->context_id == context_id);

	/* this one's done; pop it out */
	op_list_remove(query_op);
	error_code_array[*outcount] = query_op->error_code;
	actual_size_array[*outcount] = query_op->actual_size;
	out_id_array[*outcount] = query_op->op_id;
	if (user_ptr_array != NULL)
	{
	    user_ptr_array[*outcount] = query_op->user_ptr;
	}
	if(query_op->send_recv == BMI_SEND)
	    BMI_EVENT_END(PVFS_EVENT_BMI_SEND, query_op->actual_size, query_op->op_id);
	else
	    BMI_EVENT_END(PVFS_EVENT_BMI_RECV, query_op->actual_size, query_op->op_id);

	dealloc_tcp_method_op(query_op);
        query_op = NULL;
	(*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}



/* BMI_tcp_post_send_list()
 *
 * same as the BMI_tcp_post_send() function, except that it sends
 * from an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_send_list(bmi_op_id_t * id,
			   method_addr_p dest,
			   const void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id)
{
    struct tcp_msg_header my_header;
    int ret = -1;

    /* clear the id field for safety */
    *id = 0;

    /* fill in the TCP-specific message header */
    if (total_size > TCP_MODE_REND_LIMIT)
    {
	gossip_lerr("Error: BMI message too large!\n");
	return (bmi_tcp_errno_to_pvfs(-EMSGSIZE));
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
    my_header.magic_nr = BMI_MAGIC_NR;

    gen_mutex_lock(&interface_mutex);

    ret = BMI_tcp_post_send_generic(id, dest, buffer_list,
				      size_list, list_count, buffer_type,
				      my_header, user_ptr, context_id);
    if(ret >= 0)
	BMI_EVENT_START(PVFS_EVENT_BMI_SEND, *id);
    if(ret == 1)
	BMI_EVENT_END(PVFS_EVENT_BMI_SEND, total_size, *id);

    gen_mutex_unlock(&interface_mutex);
    return(ret);
}

/* BMI_tcp_post_recv_list()
 *
 * same as the BMI_tcp_post_recv() function, except that it recvs
 * into an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_recv_list(bmi_op_id_t * id,
			   method_addr_p src,
			   void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_expected_size,
			   bmi_size_t * total_actual_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id)
{
    int ret = -1;

    if (total_expected_size > TCP_MODE_REND_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    gen_mutex_lock(&interface_mutex);

    ret = tcp_post_recv_generic(id, src, buffer_list, size_list,
				list_count, total_expected_size,
				total_actual_size, buffer_type, tag, user_ptr,
				context_id);

    if(ret >= 0)
	BMI_EVENT_START(PVFS_EVENT_BMI_RECV, *id);
    if(ret == 1)
	BMI_EVENT_END(PVFS_EVENT_BMI_RECV, *total_actual_size, *id);

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_tcp_post_sendunexpected_list()
 *
 * same as the BMI_tcp_post_sendunexpected() function, except that 
 * it sends from an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_sendunexpected_list(bmi_op_id_t * id,
				     method_addr_p dest,
				     const void *const *buffer_list,
				     const bmi_size_t *size_list,
				     int list_count,
				     bmi_size_t total_size,
				     enum bmi_buffer_type buffer_type,
				     bmi_msg_tag_t tag,
				     void *user_ptr,
				     bmi_context_id context_id)
{
    struct tcp_msg_header my_header;
    int ret = -1;

    /* clear the id field for safety */
    *id = 0;

    if (total_size > TCP_MODE_EAGER_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EMSGSIZE));
    }

    my_header.mode = TCP_MODE_UNEXP;
    my_header.tag = tag;
    my_header.size = total_size;
    my_header.magic_nr = BMI_MAGIC_NR;

    gen_mutex_lock(&interface_mutex);

    ret = BMI_tcp_post_send_generic(id, dest, buffer_list,
				      size_list, list_count, buffer_type,
				      my_header, user_ptr, context_id);
    if(ret >= 0)
	BMI_EVENT_START(PVFS_EVENT_BMI_SEND, *id);
    if(ret == 1)
	BMI_EVENT_END(PVFS_EVENT_BMI_SEND, total_size, *id);

    gen_mutex_unlock(&interface_mutex);
    return(ret);
}


/* BMI_tcp_open_context()
 *
 * opens a new context with the specified context id
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_open_context(bmi_context_id context_id)
{

    gen_mutex_lock(&interface_mutex);

    /* start a new queue for tracking completions in this context */
    completion_array[context_id] = op_list_new();
    if (!completion_array[context_id])
    {
	gen_mutex_unlock(&interface_mutex);
	return(bmi_tcp_errno_to_pvfs(-ENOMEM));
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* BMI_tcp_close_context()
 *
 * shuts down a context, previously opened with BMI_tcp_open_context()
 *
 * no return value
 */
void BMI_tcp_close_context(bmi_context_id context_id)
{
    
    gen_mutex_lock(&interface_mutex);

    /* tear down completion queue for this context */
    op_list_cleanup(completion_array[context_id]);

    gen_mutex_unlock(&interface_mutex);
    return;
}


/* BMI_tcp_cancel()
 *
 * attempt to cancel a pending bmi tcp operation
 *
 * returns 0 on success, -errno on failure
 * TODO: pick better error code than -EINTR for canceled operations
 */
int BMI_tcp_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
    method_op_p query_op = (method_op_p)id_gen_safe_lookup(id);

    assert(query_op);

    gen_mutex_lock(&interface_mutex);

    /* easy case: is the operation already completed? */
    if(((struct tcp_op*)(query_op->method_data))->tcp_op_state ==
	BMI_TCP_COMPLETE)
    {
	/* only close socket in forceful cancel mode */
	if(forceful_cancel_mode)
	    tcp_forget_addr(query_op->addr, 0, -BMI_ECANCEL);
	/* we are done! status will be collected during test */
	gen_mutex_unlock(&interface_mutex);
	return(0);
    }

    /* has the operation started moving data yet? */
    if(query_op->env_amt_complete)
    {
	/* be pessimistic and kill the socket, even if not in forceful
	 * cancel mode */
	/* NOTE: this may place other operations beside this one into
	 * EINTR error state 
	 */
	tcp_forget_addr(query_op->addr, 0, -BMI_ECANCEL);
	gen_mutex_unlock(&interface_mutex);
	return(0);
    }

    /* if we fall to this point, op has been posted, but no data has moved
     * for it yet as far as we know
     */

    /* mark op as canceled, move to completion queue */
    query_op->error_code = -BMI_ECANCEL;
    if(query_op->send_recv == BMI_SEND)
    {
	BMI_socket_collection_remove_write_bit(tcp_socket_collection_p,
					   query_op->addr);
    }
    op_list_remove(query_op);
    ((struct tcp_op*)(query_op->method_data))->tcp_op_state = 
	BMI_TCP_COMPLETE;
    /* only close socket in forceful cancel mode */
    if(forceful_cancel_mode)
	tcp_forget_addr(query_op->addr, 0, -BMI_ECANCEL);
    op_list_add(completion_array[query_op->context_id], query_op);
    gen_mutex_unlock(&interface_mutex);
    return(0);
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
void tcp_forget_addr(method_addr_p map,
		     int dealloc_flag,
		     int error_code)
{
    struct tcp_addr* tcp_addr_data = map->method_data;
    int tmp_outcount;
    method_addr_p tmp_addr;
    int tmp_status;

    tcp_shutdown_addr(map);
    if (tcp_socket_collection_p)
    {
	BMI_socket_collection_remove(tcp_socket_collection_p, map);
	/* perform a test to force the socket collection to act on the remove
	 * request before continuing
	 */
	BMI_socket_collection_testglobal(tcp_socket_collection_p,
	    0, &tmp_outcount, &tmp_addr, &tmp_status, 0, &interface_mutex);
    }
    tcp_cleanse_addr(map, error_code);
    tcp_addr_data->addr_error = error_code;
    if (dealloc_flag)
    {
	dealloc_tcp_method_addr(map);
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
static void dealloc_tcp_method_addr(method_addr_p map)
{

    struct tcp_addr *tcp_addr_data = NULL;

    tcp_addr_data = map->method_data;
    /* close the socket, as long as it is not the one we are listening on
     * as a server.
     */
    if (!tcp_addr_data->server_port)
    {
	if (tcp_addr_data->socket > -1)
	{
	    close(tcp_addr_data->socket);
	}
    }

    if (tcp_addr_data->hostname)
	free(tcp_addr_data->hostname);

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
method_addr_p alloc_tcp_method_addr(void)
{

    struct method_addr *my_method_addr = NULL;
    struct tcp_addr *tcp_addr_data = NULL;

    my_method_addr =
	alloc_method_addr(tcp_method_params.method_id, sizeof(struct tcp_addr));
    if (!my_method_addr)
    {
	return (NULL);
    }

    /* note that we trust the alloc_method_addr() function to have zeroed
     * out the structures for us already 
     */

    tcp_addr_data = my_method_addr->method_data;
    tcp_addr_data->socket = -1;
    tcp_addr_data->port = -1;
    tcp_addr_data->map = my_method_addr;
    tcp_addr_data->sc_index = -1;

    return (my_method_addr);
}


/*
 * tcp_server_init()
 *
 * this function is used to prepare a node to recieve incoming
 * connections if it is initialized in a server configuration.   
 *
 * returns 0 on succes, -errno on failure
 */
static int tcp_server_init(void)
{

    int oldfl = 0;		/* old socket flags */
    struct tcp_addr *tcp_addr_data = NULL;
    int tmp_errno = bmi_tcp_errno_to_pvfs(-EINVAL);

    /* create a socket */
    tcp_addr_data = tcp_method_params.listen_addr->method_data;
    if ((tcp_addr_data->socket = BMI_sockio_new_sock()) < 0)
    {
	tmp_errno = errno;
	gossip_lerr("Error: BMI_sockio_new_sock: %s\n", strerror(tmp_errno));
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* set it to non-blocking operation */
    oldfl = fcntl(tcp_addr_data->socket, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
    {
	fcntl(tcp_addr_data->socket, F_SETFL, oldfl | O_NONBLOCK);
    }

    /* setup for a fast restart to avoid bind addr in use errors */
    BMI_sockio_set_sockopt(tcp_addr_data->socket, SO_REUSEADDR, 1);

    /* bind it to the appropriate port */
    if (BMI_sockio_bind_sock(tcp_addr_data->socket, tcp_addr_data->port) < 0)
    {
	tmp_errno = errno;
	gossip_err("Error: BMI_sockio_bind_sock: %s\n", strerror(tmp_errno));
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* go ahead and listen to the socket */
    if (listen(tcp_addr_data->socket, TCP_BACKLOG) != 0)
    {
	tmp_errno = errno;
	gossip_err("Error: listen: %s\n", strerror(tmp_errno));
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    return (0);
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

    return (query_op);
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
static int tcp_sock_init(method_addr_p my_method_addr)
{

    int oldfl = 0;		/* socket flags */
    int ret = -1;
    struct pollfd poll_conn;
    struct tcp_addr *tcp_addr_data = my_method_addr->method_data;
    int tmp_errno = 0;

    /* check for obvious problems */
    assert(my_method_addr);
    assert(my_method_addr->method_type == tcp_method_params.method_id);
    assert(tcp_addr_data->server_port == 0);

    /* fail immediately if the address is in failure mode and we have no way
     * to reconnect
     */
    if(tcp_addr_data->addr_error && tcp_addr_data->dont_reconnect)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, 
	"Warning: BMI communication attempted on an address in failure mode.\n");
	return(tcp_addr_data->addr_error);
    }

    if(tcp_addr_data->addr_error)
    {
	/* TODO: make this a debug rather than error message once we have
	 * tested this out enough
	 */
	gossip_err("Warning: BMI attempting reconnect.\n");
	tcp_addr_data->addr_error = 0;
	assert(tcp_addr_data->socket < 0);
	tcp_addr_data->not_connected = 1;
    }

    /* is there already a socket? */
    if (tcp_addr_data->socket > -1)
    {
	/* check to see if we still need to work on the connect.. */
	if (tcp_addr_data->not_connected)
	{
	    /* this is a little weird, but we complete the nonblocking
	     * connection by polling */
	    poll_conn.fd = tcp_addr_data->socket;
	    poll_conn.events = POLLOUT;
	    ret = poll(&poll_conn, 1, 2);
	    if ((ret < 0) || (poll_conn.revents & POLLERR))
	    {
		tmp_errno = errno;
		gossip_lerr("Error: poll: %s\n", strerror(tmp_errno));
		return (bmi_tcp_errno_to_pvfs(-tmp_errno));
	    }
	    if (poll_conn.revents & POLLOUT)
	    {
		tcp_addr_data->not_connected = 0;
	    }
	}
	/* return.  the caller should check the "not_connected" flag to
	 * see if the socket is usable yet. */
	return (0);
    }

    /* at this point there is no socket.  try to build it */
    if (tcp_addr_data->port < 1)
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    /* make a socket */
    if ((tcp_addr_data->socket = BMI_sockio_new_sock()) < 0)
    {
	tmp_errno = errno;
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* set it to non-blocking operation */
    oldfl = fcntl(tcp_addr_data->socket, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
    {
	fcntl(tcp_addr_data->socket, F_SETFL, oldfl | O_NONBLOCK);
    }

    /* turn of Nagle's algorithm */
    if (BMI_sockio_set_tcpopt(tcp_addr_data->socket, TCP_NODELAY, 1) < 0)
    {
	tmp_errno = errno;
	gossip_lerr("Error: failed to set TCP_NODELAY option.\n");
	close(tcp_addr_data->socket);
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    if (tcp_addr_data->hostname)
    {
	gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
		      "Connect: socket=%d, hostname=%s, port=%d\n",
		      tcp_addr_data->socket, tcp_addr_data->hostname,
		      tcp_addr_data->port);
	ret =
	    BMI_sockio_connect_sock(tcp_addr_data->socket, tcp_addr_data->hostname,
			 tcp_addr_data->port);
    }
    else
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    if (ret < 0)
    {
	if (errno == EINPROGRESS)
	{
	    tcp_addr_data->not_connected = 1;
	    /* this will have to be connected later with a poll */
	}
	else
	{
	    gossip_lerr("Error: BMI_sockio_connect_sock: %s\n", strerror(errno));
	    return (bmi_tcp_errno_to_pvfs(-errno));
	}
    }

    return (0);
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
static int enqueue_operation(op_list_p target_list,
			     enum bmi_op_type send_recv,
			     method_addr_p map,
			     void *const *buffer_list,
			     const bmi_size_t *size_list,
			     int list_count,
			     bmi_size_t amt_complete,
			     bmi_size_t env_amt_complete,
			     bmi_op_id_t * id,
			     int tcp_op_state,
			     struct tcp_msg_header header,
			     void *user_ptr,
			     bmi_size_t actual_size,
			     bmi_size_t expected_size,
			     bmi_context_id context_id)
{
    method_op_p new_method_op = NULL;
    struct tcp_op *tcp_op_data = NULL;
    struct tcp_addr* tcp_addr_data = NULL;
    int i;

    /* allocate the operation structure */
    new_method_op = alloc_tcp_method_op();
    if (!new_method_op)
    {
	return (bmi_tcp_errno_to_pvfs(-ENOMEM));
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
    new_method_op->list_count = list_count;
    new_method_op->context_id = context_id;

    /* set our current position in list processing */
    i=0;
    new_method_op->list_index = 0;
    new_method_op->cur_index_complete = 0;
    while(amt_complete > 0)
    {
	if(amt_complete >= size_list[i])
	{
	    amt_complete -= size_list[i];
	    new_method_op->list_index++;
	    i++;
	}
	else
	{
	    new_method_op->cur_index_complete = amt_complete;
	    amt_complete = 0;
	}
    }

    tcp_op_data = new_method_op->method_data;
    tcp_op_data->tcp_op_state = tcp_op_state;
    tcp_op_data->env = header;

    /* if there is only one item in the list, then keep the list stored
     * in the op structure.  This allows us to use the same code for send
     * and recv as we use for send_list and recv_list, without having to 
     * malloc lists for those special cases
     */
    if (list_count == 1)
    {
	new_method_op->buffer_list = &tcp_op_data->buffer_list_stub;
	new_method_op->size_list = &tcp_op_data->size_list_stub;
	((void**)new_method_op->buffer_list)[0] = buffer_list[0];
	((bmi_size_t*)new_method_op->size_list)[0] = size_list[0];
    }
    else
    {
	new_method_op->size_list = size_list;
	new_method_op->buffer_list = buffer_list;
    }

    tcp_addr_data = map->method_data;

    if(tcp_addr_data->addr_error)
    {
	/* server should always fail here, client should let receives queue
	 * as if nothing were wrong
	 */
	if(tcp_addr_data->dont_reconnect || send_recv == BMI_SEND)
	{
	    gossip_debug(GOSSIP_BMI_DEBUG_TCP, 
		       "Warning: BMI communication attempted on an "
		       "address in failure mode.\n");
	    new_method_op->error_code = tcp_addr_data->addr_error;
	    op_list_add(op_list_array[new_method_op->context_id],
			new_method_op);
	    return(tcp_addr_data->addr_error);
	}
    }

#if 0
    if(tcp_addr_data->addr_error)
    {
        /* this address is bad, don't try to do anything with it */
        gossip_err("Warning: BMI communication attempted on an "
                   "address in failure mode.\n");

        new_method_op->error_code = tcp_addr_data->addr_error;
        op_list_add(op_list_array[new_method_op->context_id],
                    new_method_op);
        return(tcp_addr_data->addr_error);
    }
#endif

    /* add the socket to poll on */
    BMI_socket_collection_add(tcp_socket_collection_p, map);
    if(send_recv == BMI_SEND)
    {
        BMI_socket_collection_add_write_bit(tcp_socket_collection_p, map);
    }

    /* keep up with the operation */
    op_list_add(target_list, new_method_op);

    return (0);
}


/* tcp_post_recv_generic()
 *
 * does the real work of posting an operation - works for both
 * eager and rendezvous messages
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
static int tcp_post_recv_generic(bmi_op_id_t * id,
				 method_addr_p src,
				 void *const *buffer_list,
				 const bmi_size_t *size_list,
				 int list_count,
				 bmi_size_t expected_size,
				 bmi_size_t * actual_size,
				 enum bmi_buffer_type buffer_type,
				 bmi_msg_tag_t tag,
				 void *user_ptr,
				 bmi_context_id context_id)
{
    method_op_p query_op = NULL;
    int ret = -1;
    struct tcp_addr *tcp_addr_data = NULL;
    struct tcp_op *tcp_op_data = NULL;
    struct tcp_msg_header bogus_header;
    struct op_list_search_key key;
    int copy_size = 0;
    bmi_size_t total_copied = 0;
    int i;

    tcp_addr_data = src->method_data;

    /* short out immediately if the address is bad and we have no way to
     * reconnect
     */
    if(tcp_addr_data->addr_error && tcp_addr_data->dont_reconnect)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, 
	"Warning: BMI communication attempted on an address in failure mode.\n");
	return(tcp_addr_data->addr_error);
    }

    /* lets make sure that the message hasn't already been fully
     * buffered in eager mode before doing anything else
     */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = src;
    key.method_addr_yes = 1;
    key.msg_tag = tag;
    key.msg_tag_yes = 1;

    query_op =
	op_list_search(op_list_array[IND_RECV_EAGER_DONE_BUFFERING], &key);
    if (query_op)
    {
	/* make sure it isn't too big */
	if (query_op->actual_size > expected_size)
	{
	    gossip_lerr("Error: message ordering violation;\n");
	    gossip_lerr("Error: message too large for next buffer.\n");
	    return (bmi_tcp_errno_to_pvfs(-EPROTO));
	}

	/* whoohoo- it is already done! */
	/* copy buffer out to list segments; handle short case */
	for (i = 0; i < query_op->list_count; i++)
	{
	    copy_size = query_op->size_list[i];
	    if (copy_size + total_copied > query_op->actual_size)
	    {
		copy_size = query_op->actual_size - total_copied;
	    }
	    memcpy(buffer_list[i], (void *) ((char *) query_op->buffer +
					     total_copied), copy_size);
	    total_copied += copy_size;
	    if (total_copied == query_op->actual_size)
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
	return (1);
    }

    /* look for a message that is already being received */
    query_op = op_list_search(op_list_array[IND_RECV_INFLIGHT], &key);
    if (query_op)
    {
	tcp_op_data = query_op->method_data;
    }

    /* see if it is being buffered into a temporary memory region */
    if (query_op && tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
    {
	/* make sure it isn't too big */
	if (query_op->actual_size > expected_size)
	{
	    gossip_lerr("Error: message ordering violation;\n");
	    gossip_lerr("Error: message too large for next buffer.\n");
	    return (bmi_tcp_errno_to_pvfs(-EPROTO));
	}

	/* copy what we have so far into the correct buffers */
	total_copied = 0;
	for (i = 0; i < query_op->list_count; i++)
	{
	    copy_size = query_op->size_list[i];
	    if (copy_size + total_copied > query_op->amt_complete)
	    {
		copy_size = query_op->amt_complete - total_copied;
	    }
	    if (copy_size > 0)
	    {
		memcpy(buffer_list[i], (void *) ((char *) query_op->buffer +
						 total_copied), copy_size);
	    }
	    total_copied += copy_size;
	    if (total_copied == query_op->amt_complete)
	    {
		query_op->list_index = i;
		query_op->cur_index_complete = copy_size;
		break;
	    }
	}

	/* see if we ended on a buffer boundary */
	if (query_op->cur_index_complete ==
	    query_op->size_list[query_op->list_index])
	{
	    query_op->list_index++;
	    query_op->cur_index_complete = 0;
	}

	/* release the old buffer */
	if (query_op->buffer)
	{
	    free(query_op->buffer);
	}

	*id = query_op->op_id;
	tcp_op_data = query_op->method_data;
	tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;

	query_op->list_count = list_count;
	query_op->user_ptr = user_ptr;
	query_op->context_id = context_id;
	/* if there is only one item in the list, then keep the list stored
	 * in the op structure.  This allows us to use the same code for send
	 * and recv as we use for send_list and recv_list, without having to 
	 * malloc lists for those special cases
	 */
	if (list_count == 1)
	{
	    query_op->buffer_list = &tcp_op_data->buffer_list_stub;
	    query_op->size_list = &tcp_op_data->size_list_stub;
	    ((void **)query_op->buffer_list)[0] = buffer_list[0];
	    ((bmi_size_t *)query_op->size_list)[0] = size_list[0];
	}
	else
	{
	    query_op->buffer_list = buffer_list;
	    query_op->size_list = size_list;
	}

	if (query_op->amt_complete < query_op->actual_size)
	{
	    /* try to recv some more data */
	    tcp_addr_data = query_op->addr->method_data;
	    ret = payload_progress(tcp_addr_data->socket,
		query_op->buffer_list,
		query_op->size_list,
		query_op->list_count,
		query_op->actual_size,
		&(query_op->list_index),
		&(query_op->cur_index_complete),
		BMI_RECV,
		NULL,
		0);
	    if (ret < 0)
	    {
		gossip_lerr("Error: payload_progress: %s\n", strerror(-ret));
		tcp_forget_addr(query_op->addr, 0, ret);
		return (ret);
	    }

	    query_op->amt_complete += ret;
	}
	assert(query_op->amt_complete <= query_op->actual_size);
	if (query_op->amt_complete == query_op->actual_size)
	{
	    /* we are done */
	    op_list_remove(query_op);
	    *id = 0;
	    (*actual_size) = query_op->actual_size;
	    dealloc_tcp_method_op(query_op);
	    return (1);
	}
	else
	{
	    /* there is still more work to do */
	    tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
	    return (0);
	}
    }

    /* NOTE: if the message was in flight, but not buffering, then
     * that means that it has already matched an earlier receive
     * post or else is an unexpected message that doesn't require a
     * matching receive post - at any rate it shouldn't be handled
     * here
     */

    /* if we hit this point we must enqueue */
    if (expected_size <= TCP_MODE_EAGER_LIMIT)
    {
	bogus_header.mode = TCP_MODE_EAGER;
    }
    else
    {
	bogus_header.mode = TCP_MODE_REND;
    }
    bogus_header.tag = tag;
    ret = enqueue_operation(op_list_array[IND_RECV],
			    BMI_RECV, src, buffer_list, size_list,
			    list_count, 0, 0, id, BMI_TCP_INPROGRESS,
			    bogus_header, user_ptr, 0,
			    expected_size, context_id);
    /* just for safety; this field isn't valid to the caller anymore */
    (*actual_size) = 0;
    /* TODO: figure out why this causes deadlocks; observable in 2
     * scenarios:
     * - pvfs2-client-core with threaded library and nptl
     * - pvfs2-server threaded with nptl sending messages to itself
     */
#if 0
    if (ret >= 0)
    {
	/* go ahead and try to do some work while we are in this
	 * function since we appear to be backlogged.  Make sure that
	 * we do not wait in the poll, however.
	 */
	ret = tcp_do_work(0);
    }
#endif
    return (ret);
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
static int tcp_cleanse_addr(method_addr_p map, int error_code)
{
    int i = 0;
    struct op_list_search_key key;
    method_op_p query_op = NULL;

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;

    /* NOTE: we know the unexpected completed queue is the last index! */
    for (i = 0; i < (NUM_INDICES - 1); i++)
    {
	if (op_list_array[i])
	{
	    while ((query_op = op_list_search(op_list_array[i], &key)))
	    {
		op_list_remove(query_op);
		query_op->error_code = error_code;
		if (query_op->mode == TCP_MODE_UNEXP && query_op->send_recv
		    == BMI_RECV)
		{
		    op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP],
				query_op);
		}
		else
		{
		    ((struct tcp_op*)(query_op->method_data))->tcp_op_state = 
			BMI_TCP_COMPLETE;
		    op_list_add(completion_array[query_op->context_id], query_op);
		}
	    }
	}
    }

    return (0);
}


/* tcp_shutdown_addr()
 *
 * closes connections associated with a tcp method address
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_shutdown_addr(method_addr_p map)
{

    struct tcp_addr *tcp_addr_data = map->method_data;
    if (tcp_addr_data->socket > -1)
    {
	close(tcp_addr_data->socket);
    }
    tcp_addr_data->socket = -1;
    tcp_addr_data->not_connected = 1;

    return (0);
}


/* tcp_do_work()
 *
 * this is the function that actually does communication work during
 * BMI_tcp_testXXX and BMI_tcp_waitXXX functions.  The amount of work 
 * that it does is tunable.
 *
 * returns 0 on success, -errno on failure.
 */
static int tcp_do_work(int max_idle_time)
{
    int ret = -1;
    method_addr_p addr_array[TCP_WORK_METRIC];
    int status_array[TCP_WORK_METRIC];
    int socket_count = 0;
    int i = 0;
    int stall_flag = 0;
    int busy_flag = 1;
    struct timespec req;
    struct tcp_addr* tcp_addr_data = NULL;

    /* now we need to poll and see what to work on */
    /* drop mutex while we make this call */
    gen_mutex_unlock(&interface_mutex);
    ret = BMI_socket_collection_testglobal(tcp_socket_collection_p,
				       TCP_WORK_METRIC, &socket_count,
				       addr_array, status_array,
				       max_idle_time, &interface_mutex);
    gen_mutex_lock(&interface_mutex);
    if (ret < 0)
    {
	return (ret);
    }

    if(socket_count == 0)
	busy_flag = 0;

    /* do different kinds of work depending on results */
    for (i = 0; i < socket_count; i++)
    {
	tcp_addr_data = addr_array[i]->method_data;
	/* skip working on addresses in failure mode */
	if(tcp_addr_data->addr_error)
	{
	    tcp_forget_addr(addr_array[i], 0, tcp_addr_data->addr_error);
	    continue;
	}

	if (status_array[i] & SC_ERROR_BIT)
	{
	    ret = tcp_do_work_error(addr_array[i]);
	    if (ret < 0)
	    {
		return (ret);
	    }
	}
	else
	{
	    if (status_array[i] & SC_WRITE_BIT)
	    {
		ret = tcp_do_work_send(addr_array[i], &stall_flag);
		if (ret < 0)
		{
		    return (ret);
		}
		if(!stall_flag)
		    busy_flag = 0;
	    }
	    if (status_array[i] & SC_READ_BIT)
	    {
		ret = tcp_do_work_recv(addr_array[i], &stall_flag);
		if (ret < 0)
		{
		    return (ret);
		}
		if(!stall_flag)
		    busy_flag = 0;
	    }
	}
    }

    /* IMPORTANT NOTE: if we have set the following flag, then it indicates that
     * poll() is finding data on our sockets, yet we are not able to move
     * any of it right now.  This means that the sockets are backlogged, and
     * BMI is in danger of busy spinning during test functions.  Let's sleep
     * for a millisecond here in hopes of letting the rest of the system
     * catch up somehow (either by clearing a backlog in another I/O
     * component, or by posting more matching BMI recieve operations)
     */
    if(busy_flag)
    {
	req.tv_sec = 0;
	req.tv_nsec = 1000;
        gen_mutex_unlock(&interface_mutex);
	nanosleep(&req, NULL);
        gen_mutex_lock(&interface_mutex);
    }

    return (0);
}


/* tcp_do_work_send()
 *
 * does work on a TCP address that is ready to send data.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_send(method_addr_p map, int* stall_flag)
{
    method_op_p active_method_op = NULL;
    struct op_list_search_key key;
    int blocked_flag = 0;
    int ret = 0;
    int tmp_stall_flag;

    *stall_flag = 1;

    while (blocked_flag == 0 && ret == 0)
    {
	/* what we want to do here is find the first operation in the send
	 * queue for this address.
	 */
	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = map;
	key.method_addr_yes = 1;
	active_method_op = op_list_search(op_list_array[IND_SEND], &key);
	if (!active_method_op)
	{
	    /* ran out of queued sends to work on */
	    return (0);
	}

	ret = work_on_send_op(active_method_op, &blocked_flag, &tmp_stall_flag);
	if(!tmp_stall_flag)
	    *stall_flag = 0;
    }

    return (ret);
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
    struct tcp_addr *tcp_addr_data = NULL;
    int accepted_socket = -1;
    method_addr_p new_addr = NULL;
    int ret = -1;

    ret = tcp_accept_init(&accepted_socket);
    if (ret < 0)
    {
	return (ret);
    }
    if (accepted_socket < 0)
    {
	/* guess it wasn't ready after all */
	return (0);
    }

    /* ok, we have a new socket.  what now?  Probably simplest
     * thing to do is to create a new method_addr, add it to the
     * socket collection, and return.  It will get caught the next
     * time around */
    new_addr = alloc_tcp_method_addr();
    if (!new_addr)
    {
	return (bmi_tcp_errno_to_pvfs(-ENOMEM));
    }
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                  "Assigning socket %d to new method addr.\n",
		  accepted_socket);
    tcp_addr_data = new_addr->method_data;
    tcp_addr_data->socket = accepted_socket;
    /* set a flag to make sure that we never try to reconnect this address
     * in the future
     */
    tcp_addr_data->dont_reconnect = 1;
    /* register this address with the method control layer */
    ret = bmi_method_addr_reg_callback(new_addr);
    if (ret < 0)
    {
	tcp_shutdown_addr(new_addr);
	dealloc_tcp_method_addr(new_addr);
	dealloc_tcp_method_addr(map);
	return (ret);
    }
    BMI_socket_collection_add(tcp_socket_collection_p, new_addr);

    dealloc_tcp_method_addr(map);
    return (0);

}


/* tcp_do_work_recv()
 * 
 * does work on a TCP address that is ready to recv data.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_recv(method_addr_p map, int* stall_flag)
{

    method_op_p active_method_op = NULL;
    int ret = -1;
    void *new_buffer = NULL;
    struct op_list_search_key key;
    struct tcp_msg_header new_header;
    struct tcp_addr *tcp_addr_data = map->method_data;
    struct tcp_op *tcp_op_data = NULL;
    int tmp_errno;
    int tmp;
    bmi_size_t old_amt_complete = 0;

    *stall_flag = 1;

    /* figure out if this is a new connection */
    if (tcp_addr_data->server_port)
    {
	/* just try to accept connection- no work yet */
	*stall_flag = 0;
	return (handle_new_connection(map));
    }

    /* look for a recv for this address that is already in flight */
    active_method_op = find_recv_inflight(map);
    /* see if we found one in progress... */
    if (active_method_op)
    {
	tcp_op_data = active_method_op->method_data;
	if (active_method_op->mode == TCP_MODE_REND &&
	    tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
	{
	    /* we must wait for recv post */
	    return (0);
	}
	else
	{
	    old_amt_complete = active_method_op->amt_complete;
	    ret = work_on_recv_op(active_method_op, stall_flag);
            gossip_debug(GOSSIP_BMI_DEBUG_TCP, "actual_size=%d, "
                         "amt_complete=%d, old_amt_complete=%d\n",
                         (int)active_method_op->actual_size,
                         (int)active_method_op->amt_complete,
                         (int)old_amt_complete);

	    if ((ret == 0) &&
                (old_amt_complete == active_method_op->amt_complete) &&
                active_method_op->actual_size &&
                (active_method_op->amt_complete <
                 active_method_op->actual_size))
	    {
                gossip_debug(
                    GOSSIP_BMI_DEBUG_TCP, "Warning: bmi_tcp unable "
                    "to recv any data reported by poll(). [1]\n");

                if (tcp_addr_data->zero_read_limit++ ==
                    BMI_TCP_ZERO_READ_LIMIT)
                {
                    gossip_debug(GOSSIP_BMI_DEBUG_TCP,
                                 "...dropping connection.\n");
                    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EPIPE));
                }
	    }
            else
            {
                tcp_addr_data->zero_read_limit = 0;
            }
	    return(ret);
	}
    }

    /* let's see if a the entire header is ready to be received.  If so
     * we will go ahead and pull it.  Otherwise, we will try again later.
     * It isn't worth the complication of reading only a partial message
     * header - we really want it atomically
     */
    ret = BMI_sockio_nbpeek(tcp_addr_data->socket,
                            new_header.enc_hdr, TCP_ENC_HDR_SIZE);
    if (ret < 0)
    {
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-errno));
	return (0);
    }

    if (ret == 0)
    {
        gossip_debug(
            GOSSIP_BMI_DEBUG_TCP, "Warning: bmi_tcp unable "
            "to recv any data reported by poll(). [2]\n");

        if (tcp_addr_data->zero_read_limit++ ==
            BMI_TCP_ZERO_READ_LIMIT)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_TCP,
                         "...dropping connection.\n");
            tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EPIPE));
        }
	return(ret);
    }
    else
    {
        tcp_addr_data->zero_read_limit = 0;
    }

    if (ret < TCP_ENC_HDR_SIZE)
    {
	/* header not ready yet */
	return (0);
    }

    *stall_flag = 0;
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Reading header for new op.\n");
    /* NOTE: we only allow a blocking call here because we peeked to see
     * if this amount of data was ready above.  
     */
    ret = BMI_sockio_brecv(tcp_addr_data->socket,
                           new_header.enc_hdr, TCP_ENC_HDR_SIZE);
    if (ret < TCP_ENC_HDR_SIZE)
    {
	tmp_errno = errno;
	gossip_err("Error: BMI_sockio_brecv: %s\n", strerror(tmp_errno));
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-tmp_errno));
	return (0);
    }

    /* decode the header */
    BMI_TCP_DEC_HDR(new_header);

    /* so we have the header. now what?  These are the possible
     * scenarios:
     * a) unexpected message
     * b) eager message for which a recv has been posted
     * c) eager message for which a recv has not been posted
     * d) rendezvous messsage for which a recv has been posted
     * e) rendezvous messsage for which a recv has not been posted
     * f) eager message for which a rend. recv has been posted
     */

    /* check magic number of message */
    if(new_header.magic_nr != BMI_MAGIC_NR)
    {
	gossip_err("Error: bad magic in BMI TCP message.\n");
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EBADMSG));
	return(0);
    }

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Received new message; mode: %d.\n",
		  (int) new_header.mode);
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "tag: %d\n", (int) new_header.tag);

    if (new_header.mode == TCP_MODE_UNEXP)
    {
	/* allocate the operation structure */
	active_method_op = alloc_tcp_method_op();
	if (!active_method_op)
	{
	    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	    return (bmi_tcp_errno_to_pvfs(-ENOMEM));
	}
	/* create data buffer */
	new_buffer = malloc(new_header.size);
	if (!new_buffer)
	{
	    dealloc_tcp_method_op(active_method_op);
	    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	    return (bmi_tcp_errno_to_pvfs(-ENOMEM));
	}

	/* set the fields */
	active_method_op->send_recv = BMI_RECV;
	active_method_op->addr = map;
	active_method_op->actual_size = new_header.size;
	active_method_op->expected_size = 0;
	active_method_op->amt_complete = 0;
	active_method_op->env_amt_complete = TCP_ENC_HDR_SIZE;
	active_method_op->msg_tag = new_header.tag;
	active_method_op->buffer = new_buffer;
	active_method_op->mode = TCP_MODE_UNEXP;
	active_method_op->buffer_list = &(active_method_op->buffer);
	active_method_op->size_list = &(active_method_op->actual_size);
	active_method_op->list_count = 1;
	tcp_op_data = active_method_op->method_data;
	tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
	tcp_op_data->env = new_header;

	op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);
	/* grab some data if we can */
	return (work_on_recv_op(active_method_op, &tmp));
    }

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;
    key.msg_tag = new_header.tag;
    key.msg_tag_yes = 1;

    /* look for a match within the posted operations */
    active_method_op = op_list_search(op_list_array[IND_RECV], &key);

    if (active_method_op)
    {
	/* make sure it isn't too big */
	if (new_header.size > active_method_op->expected_size)
	{
	    gossip_lerr("Error: message ordering violation;\n");
	    gossip_lerr("Error: message too large for next buffer.\n");
	    gossip_lerr("Error: incoming size: %ld, expected size: %ld\n",
			(long) new_header.size,
			(long) active_method_op->expected_size);
	    /* TODO: return error here or do something else? */
	    return (bmi_tcp_errno_to_pvfs(-EPROTO));
	}

	/* we found a match.  go work on it and return */
	op_list_remove(active_method_op);
	active_method_op->env_amt_complete = TCP_ENC_HDR_SIZE;
	active_method_op->actual_size = new_header.size;
	op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);
	return (work_on_recv_op(active_method_op, &tmp));
    }

    /* no match anywhere.  Start a new operation */
    /* allocate the operation structure */
    active_method_op = alloc_tcp_method_op();
    if (!active_method_op)
    {
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	return (bmi_tcp_errno_to_pvfs(-ENOMEM));
    }

    if (new_header.mode == TCP_MODE_EAGER)
    {
	/* create data buffer for eager messages */
	new_buffer = malloc(new_header.size);
	if (!new_buffer)
	{
	    dealloc_tcp_method_op(active_method_op);
	    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	    return (bmi_tcp_errno_to_pvfs(-ENOMEM));
	}
    }
    else
    {
	new_buffer = NULL;
    }

    /* set the fields */
    active_method_op->send_recv = BMI_RECV;
    active_method_op->addr = map;
    active_method_op->actual_size = new_header.size;
    active_method_op->expected_size = 0;
    active_method_op->amt_complete = 0;
    active_method_op->env_amt_complete = TCP_ENC_HDR_SIZE;
    active_method_op->msg_tag = new_header.tag;
    active_method_op->buffer = new_buffer;
    active_method_op->mode = new_header.mode;
    active_method_op->buffer_list = &(active_method_op->buffer);
    active_method_op->size_list = &(active_method_op->actual_size);
    active_method_op->list_count = 1;
    tcp_op_data = active_method_op->method_data;
    tcp_op_data->tcp_op_state = BMI_TCP_BUFFERING;
    tcp_op_data->env = new_header;

    op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);

    /* grab some data if we can */
    if (new_header.mode == TCP_MODE_EAGER)
    {
	return (work_on_recv_op(active_method_op, &tmp));
    }

    return (0);
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
static int work_on_send_op(method_op_p my_method_op,
			   int *blocked_flag, int* stall_flag)
{
    int ret = -1;
    struct tcp_addr *tcp_addr_data = my_method_op->addr->method_data;
    struct tcp_op *tcp_op_data = my_method_op->method_data;

    *blocked_flag = 1;
    *stall_flag = 0;

    /* make sure that the connection is done before we continue */
    if (tcp_addr_data->not_connected)
    {
	ret = tcp_sock_init(my_method_op->addr);
	if (ret < 0)
	{
	    tcp_forget_addr(my_method_op->addr, 0, ret);
	    return (0);
	}
	if (tcp_addr_data->not_connected)
	{
	    /* try again later- still could not connect */
	    tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
	    return (0);
	}
    }

    ret = payload_progress(tcp_addr_data->socket,
	my_method_op->buffer_list,
	my_method_op->size_list,
	my_method_op->list_count,
	my_method_op->actual_size,
	&(my_method_op->list_index),
	&(my_method_op->cur_index_complete),
	BMI_SEND,
	tcp_op_data->env.enc_hdr,
	&my_method_op->env_amt_complete);
    if (ret < 0)
    {
	gossip_err("Error: payload_progress: %s\n", strerror(-ret));
	tcp_forget_addr(my_method_op->addr, 0, ret);
	return (0);
    }

    if(ret == 0)
	*stall_flag = 1;

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Sent: %d bytes of data.\n", ret);
    my_method_op->amt_complete += ret;
    assert(my_method_op->amt_complete <= my_method_op->actual_size);

    if (my_method_op->amt_complete == my_method_op->actual_size && my_method_op->env_amt_complete == TCP_ENC_HDR_SIZE)
    {
	/* we are done */
	my_method_op->error_code = 0;
	BMI_socket_collection_remove_write_bit(tcp_socket_collection_p,
					   my_method_op->addr);
	op_list_remove(my_method_op);
	((struct tcp_op*)(my_method_op->method_data))->tcp_op_state = 
	    BMI_TCP_COMPLETE;
	op_list_add(completion_array[my_method_op->context_id], my_method_op);
	*blocked_flag = 0;
    }
    else
    {
	/* there is still more work to do */
	tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
    }

    return (0);
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
static int work_on_recv_op(method_op_p my_method_op, int* stall_flag)
{

    int ret = -1;
    struct tcp_addr *tcp_addr_data = my_method_op->addr->method_data;
    struct tcp_op *tcp_op_data = my_method_op->method_data;

    *stall_flag = 1;

    if (my_method_op->actual_size != 0)
    {
	/* now let's try to recv some actual data */
	ret = payload_progress(tcp_addr_data->socket,
	    my_method_op->buffer_list,
	    my_method_op->size_list,
	    my_method_op->list_count,
	    my_method_op->actual_size,
	    &(my_method_op->list_index),
	    &(my_method_op->cur_index_complete),
	    BMI_RECV,
	    NULL,
	    0);
	if (ret < 0)
	{
	    gossip_lerr("Error: payload_progress: %s\n", strerror(-ret));
	    tcp_forget_addr(my_method_op->addr, 0, ret);
	    return (0);
	}
    }
    else
    {
	ret = 0;
    }

    if(ret > 0)
	*stall_flag = 0;

    my_method_op->amt_complete += ret;
    assert(my_method_op->amt_complete <= my_method_op->actual_size);

    if (my_method_op->amt_complete == my_method_op->actual_size)
    {
	/* we are done */
	op_list_remove(my_method_op);
	if (tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
	{
	    /* queue up to wait on matching post recv */
	    op_list_add(op_list_array[IND_RECV_EAGER_DONE_BUFFERING],
			my_method_op);
	}
	else
	{
	    my_method_op->error_code = 0;
	    if (my_method_op->mode == TCP_MODE_UNEXP)
	    {
		op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP],
			    my_method_op);
	    }
	    else
	    {
		((struct tcp_op*)(my_method_op->method_data))->tcp_op_state = 
		    BMI_TCP_COMPLETE;
		op_list_add(completion_array[my_method_op->context_id], my_method_op);
	    }
	}
    }

    return (0);
}


/* tcp_do_work_error()
 * 
 * handles a tcp address that has indicated an error during polling.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_error(method_addr_p map)
{
    struct tcp_addr *tcp_addr_data = NULL;
    int buf;
    int ret;
    int tmp_errno;

    tcp_addr_data = map->method_data;

    /* perform a read on the socket so that we can get a real errno */
    ret = read(tcp_addr_data->socket, &buf, sizeof(int));
    tmp_errno = errno;

    gossip_err("Error: bmi_tcp: %s\n", strerror(tmp_errno));

    if (tcp_addr_data->server_port)
    {
	/* Ignore this and hope it goes away... we don't want to lose
	 * our local socket */
	dealloc_tcp_method_addr(map);
	gossip_lerr("Warning: error polling on server socket, continuing.\n");
	return (0);
    }

    if(tmp_errno == 0)
	tmp_errno = EPROTO;

    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-tmp_errno));

    return (0);
}

/* 
 * tcp_accept_init()
 * 
 * used to establish a connection from the server side.  Attempts an
 * accept call and provides the socket if it succeeds.
 *
 * returns 0 on success, -errno on failure.
 */
static int tcp_accept_init(int *socket)
{

    int ret = -1;
    int tmp_errno = 0;
    struct tcp_addr *tcp_addr_data = tcp_method_params.listen_addr->method_data;

    /* do we have a socket on this end yet? */
    if (tcp_addr_data->socket < 0)
    {
	ret = tcp_server_init();
	if (ret < 0)
	{
	    return (ret);
	}
    }

    *socket = accept(tcp_addr_data->socket, NULL, 0);

    if (*socket < 0)
    {
	if ((errno == EAGAIN) ||
	    (errno == EWOULDBLOCK) ||
	    (errno == ENETDOWN) ||
	    (errno == EPROTO) ||
	    (errno == ENOPROTOOPT) ||
	    (errno == EHOSTDOWN) ||
	    (errno == ENONET) ||
	    (errno == EHOSTUNREACH) ||
	    (errno == EOPNOTSUPP) || (errno == ENETUNREACH))
	{
	    /* try again later */
	    return (0);
	}
	else
	{
	    gossip_lerr("Error: accept: %s\n", strerror(errno));
	    return (bmi_tcp_errno_to_pvfs(-errno));
	}
    }

    /* we accepted a new connection.  turn off Nagle's algorithm. */
    if (BMI_sockio_set_tcpopt(*socket, TCP_NODELAY, 1) < 0)
    {
	tmp_errno = errno;
	gossip_lerr("Error: failed to set TCP_NODELAY option.\n");
	close(*socket);
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }
    return (0);
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

    return (my_method_op);
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
static int BMI_tcp_post_send_generic(bmi_op_id_t * id,
				     method_addr_p dest,
				     const void *const *buffer_list,
				     const bmi_size_t *size_list,
				     int list_count,
				     enum bmi_buffer_type buffer_type,
				     struct tcp_msg_header my_header,
				     void *user_ptr,
				     bmi_context_id context_id)
{
    struct tcp_addr *tcp_addr_data = dest->method_data;
    method_op_p query_op = NULL;
    int ret = -1;
    bmi_size_t amt_complete = 0;
    bmi_size_t env_amt_complete = 0;
    struct op_list_search_key key;
    int list_index = 0;
    bmi_size_t cur_index_complete = 0;

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

    /* NOTE: we also don't care what the buffer_type says, TCP could care
     * less what buffers it is using.
     */

    /* encode the message header */
    BMI_TCP_ENC_HDR(my_header);

    /* the first thing we must do is find out if another send is queued
     * up for this address so that we don't mess up our ordering.    */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = dest;
    key.method_addr_yes = 1;
    query_op = op_list_search(op_list_array[IND_SEND], &key);
    if (query_op)
    {
	/* queue up operation */
	ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
				dest, (void **) buffer_list,
				size_list, list_count, 0, 0,
				id, BMI_TCP_INPROGRESS, my_header, user_ptr,
				my_header.size, 0,
				context_id);

        /* TODO: is this causing deadlocks?  See similar call in recv
         * path for another example.  This particular one seems to be an
         * issue under a heavy bonnie++ load that Neill has been
         * debugging.  Comment out for now to see if the problem goes
         * away.
         */
#if 0
	if (ret >= 0)
	{
	    /* go ahead and try to do some work while we are in this
	     * function since we appear to be backlogged.  Make sure that
	     * we do not wait in the poll, however.
	     */
	    ret = tcp_do_work(0);
	}
#endif
	if (ret < 0)
	{
	    gossip_lerr("Error: enqueue_operation() or tcp_do_work() returned: %d\n", ret);
	}
	return (ret);
    }

    /* make sure the connection is established */
    ret = tcp_sock_init(dest);
    if (ret < 0)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, "tcp_sock_init() failure.\n");
	tcp_forget_addr(dest, 0, ret);
	return (ret);
    }

    tcp_addr_data = dest->method_data;

#if 0
    /* TODO: this is a hack for testing! */
    /* disables immediate send completion... */
    ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
			    dest, buffer_list, size_list, list_count, 0, 0,
			    id, BMI_TCP_INPROGRESS, my_header, user_ptr,
			    my_header.size, 0,
			    context_id);
    return(ret);
#endif

    if (tcp_addr_data->not_connected)
    {
	/* if the connection is not completed, queue up for later work */
	ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
				dest, (void **) buffer_list, size_list,
				list_count, 0, 0,
				id, BMI_TCP_INPROGRESS, my_header, user_ptr,
				my_header.size, 0,
				context_id);
	if(ret < 0)
	{
	    gossip_lerr("Error: enqueue_operation() returned: %d\n", ret);
	}
	return (ret);
    }

    /* try to send some data */
    env_amt_complete = 0;
    ret = payload_progress(tcp_addr_data->socket,
	(void **) buffer_list,
	size_list, list_count, my_header.size, &list_index,
	&cur_index_complete, BMI_SEND, my_header.enc_hdr, &env_amt_complete);
    if (ret < 0)
    {
        char buf[64] = {0};
        PVFS_strerror_r(-ret, buf, 64);
	gossip_lerr("Error: payload_progress: %s\n", buf);
	tcp_forget_addr(dest, 0, ret);
	return (ret);
    }

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Sent: %d bytes of data.\n", ret);
    amt_complete = ret;
    assert(amt_complete <= my_header.size);
    if (amt_complete == my_header.size && env_amt_complete == TCP_ENC_HDR_SIZE)
    {
	/* we are already done */
	return (1);
    }

    /* queue up the remainder */
    ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
			    dest, (void **) buffer_list,
			    size_list, list_count,
			    amt_complete, env_amt_complete, id,
			    BMI_TCP_INPROGRESS, my_header, user_ptr,
			    my_header.size, 0, context_id);

    if(ret < 0)
    {
	gossip_lerr("Error: enqueue_operation() returned: %d\n", ret);
    }
    return (ret);
}


/* payload_progress()
 *
 * makes progress on sending/recving data payload portion of a message
 *
 * returns amount completed on success, -errno on failure
 */
static int payload_progress(int s, void *const *buffer_list, const bmi_size_t* 
    size_list, int list_count, bmi_size_t total_size, int* list_index, 
    bmi_size_t* current_index_complete, enum bmi_op_type send_recv, 
    char* enc_hdr, bmi_size_t* env_amt_complete)
{
    int i;
    int count = 0;
    int ret;
    int completed;
    /* used for finding the stopping point on short receives */
    int final_index = list_count-1;
    bmi_size_t final_size = size_list[list_count-1];
    bmi_size_t sum = 0;
    int vector_index = 0;
    int header_flag = 0;
    int tmp_env_done = 0;

    if(send_recv == BMI_RECV)
    {
	/* find out if we should stop short in list processing */
	for(i=0; i<list_count; i++)
	{
	    sum += size_list[i];
	    if(sum >= total_size)
	    {
		final_index = i;
		final_size = size_list[i] - (sum-total_size);
		break;
	    }
	}
    }

    assert(list_count > *list_index);

    /* make sure we don't overrun our preallocated iovec array */
    if((list_count - (*list_index)) > BMI_TCP_IOV_COUNT)
    {
	list_count = (*list_index) + BMI_TCP_IOV_COUNT;
    }

    /* do we need to send any of the header? */
    if(send_recv == BMI_SEND && *env_amt_complete < TCP_ENC_HDR_SIZE)
    {
	stat_io_vector[vector_index].iov_base = &enc_hdr[*env_amt_complete];
	stat_io_vector[vector_index].iov_len = TCP_ENC_HDR_SIZE - *env_amt_complete;
	count++;
	vector_index++;
	header_flag = 1;
    }

    /* setup vector */
    stat_io_vector[vector_index].iov_base = 
	(char*)buffer_list[*list_index] + *current_index_complete;
    count++;
    if(final_index == 0)
    {
	stat_io_vector[vector_index].iov_len = final_size - *current_index_complete;
    }
    else
    {
	stat_io_vector[vector_index].iov_len = 
	    size_list[*list_index] - *current_index_complete;
	for(i = (*list_index + 1); i < list_count; i++)
	{
	    vector_index++;
	    count++;
	    stat_io_vector[vector_index].iov_base = buffer_list[i];
	    if(i == final_index)
	    {
		stat_io_vector[vector_index].iov_len = final_size;
		break;
	    }
	    else
	    {
		stat_io_vector[vector_index].iov_len = size_list[i];
	    }
	}
    }

    assert(count > 0);

    if(send_recv == BMI_RECV)
    {
	ret = BMI_sockio_nbvector(s, stat_io_vector, count, 1);
    }
    else
    {
	ret = BMI_sockio_nbvector(s, stat_io_vector, count, 0);
    }

    /* if error or nothing done, return now */
    if(ret == 0)
	return(0);
    if(ret <= 0)
	return(bmi_tcp_errno_to_pvfs(-errno));

    completed = ret;
    if(header_flag && (completed >= 0))
    {
	/* take care of completed header status */
	tmp_env_done = TCP_ENC_HDR_SIZE - *env_amt_complete;
	if(tmp_env_done > completed)
	    tmp_env_done = completed;
	completed -= tmp_env_done;
	ret -= tmp_env_done;
	(*env_amt_complete) += tmp_env_done;
    }

    i=header_flag;
    while(completed > 0)
    {
	/* take care of completed data payload */
	if(completed >= stat_io_vector[i].iov_len)
	{
	    completed -= stat_io_vector[i].iov_len;
	    *current_index_complete = 0;
	    (*list_index)++;
	    i++;
	}
	else
	{
	    *current_index_complete += completed;
	    completed = 0;
	}
    }

    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* GM implementation of a BMI method */

#include<errno.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>

#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "bmi-gm-addressing.h"
#include "op-list.h"
#include "bmi-gm-addr-list.h"
#include "gossip.h"
#include "quicklist.h"
#include "bmi-gm-regcache.h"
#include "bmi-gm-bufferpool.h"
#include<config.h>

#include<gm.h>

/* function prototypes */
int BMI_gm_initialize(method_addr_p listen_addr,
		      bmi_flag_t method_id,
		      bmi_flag_t init_flags);
int BMI_gm_finalize(void);
int BMI_gm_set_info(int option,
		    void *inout_parameter);
int BMI_gm_get_info(int option,
		    void *inout_parameter);
void *BMI_gm_memalloc(bmi_size_t size,
		      bmi_flag_t send_recv);
int BMI_gm_memfree(void *buffer,
		   bmi_size_t size,
		   bmi_flag_t send_recv);
int BMI_gm_post_send(bmi_op_id_t * id,
		     method_addr_p dest,
		     void *buffer,
		     bmi_size_t size,
		     bmi_size_t expected_size,
		     bmi_flag_t buffer_flag,
		     bmi_msg_tag_t tag,
		     void *user_ptr);
int BMI_gm_post_sendunexpected(bmi_op_id_t * id,
			       method_addr_p dest,
			       void *buffer,
			       bmi_size_t size,
			       bmi_flag_t buffer_flag,
			       bmi_msg_tag_t tag,
			       void *user_ptr);
int BMI_gm_post_recv(bmi_op_id_t * id,
		     method_addr_p src,
		     void *buffer,
		     bmi_size_t expected_size,
		     bmi_size_t * actual_size,
		     bmi_flag_t buffer_flag,
		     bmi_msg_tag_t tag,
		     void *user_ptr);
int BMI_gm_wait(bmi_op_id_t id,
		int *outcount,
		bmi_error_code_t * error_code,
		bmi_size_t * actual_size,
		void **user_ptr);
int BMI_gm_waitsome(int incount,
		    bmi_op_id_t * id_array,
		    int *outcount,
		    int *index_array,
		    bmi_error_code_t * error_code_array,
		    bmi_size_t * actual_size_array,
		    void **user_ptr_array);
int BMI_gm_waitunexpected(int incount,
			  int *outcount,
			  struct method_unexpected_info *info);
int BMI_gm_test(bmi_op_id_t id,
		int *outcount,
		bmi_error_code_t * error_code,
		bmi_size_t * actual_size,
		void **user_ptr);
int BMI_gm_testsome(int incount,
		    bmi_op_id_t * id_array,
		    int *outcount,
		    int *index_array,
		    bmi_error_code_t * error_code_array,
		    bmi_size_t * actual_size_array,
		    void **user_ptr_array);
int BMI_gm_testunexpected(int incount,
			  int *outcount,
			  struct method_unexpected_info *info);
method_addr_p BMI_gm_method_addr_lookup(const char *id_string);
char BMI_gm_method_name[] = "bmi_gm";

/* exported method interface */
struct bmi_method_ops bmi_gm_ops = {
    BMI_gm_method_name,
    BMI_gm_initialize,
    BMI_gm_finalize,
    BMI_gm_set_info,
    BMI_gm_get_info,
    BMI_gm_memalloc,
    BMI_gm_memfree,
    BMI_gm_post_send,
    BMI_gm_post_sendunexpected,
    BMI_gm_post_recv,
    BMI_gm_wait,
    BMI_gm_waitsome,
    BMI_gm_waitunexpected,
    BMI_gm_test,
    BMI_gm_testsome,
    BMI_gm_testunexpected,
    BMI_gm_method_addr_lookup,
    NULL,
    NULL,
    NULL
};

/* module parameters */
static method_params_st gm_method_params;

/* op_list_array indices */
enum
{
    /* be careful changing these indexes!  op list searches rely on these
     * numerical values.
     */
    NUM_INDICES = 10,
    IND_NEED_SEND_TOK_HI_CTRLACK = 0,
    IND_NEED_SEND_TOK_LOW = 1,
    IND_NEED_SEND_TOK_HI_CTRL = 2,
    IND_NEED_SEND_TOK_HI_PUT = 3,
    IND_SENDING = 4,
    IND_RECVING = 5,
    IND_NEED_RECV_POST = 6,
    IND_NEED_CTRL_MATCH = 7,
    IND_COMPLETE = 8,
    IND_COMPLETE_RECV_UNEXP = 9
};

/* buffer status indicator */
enum
{
    GM_BUF_USER_ALLOC = 1,
    GM_BUF_METH_ALLOC = 2,
    GM_BUF_METH_REG = 3
};

/* internal operations lists */
static op_list_p op_list_array[NUM_INDICES] = { NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL
};

/* GM message modes */
enum
{
    GM_MODE_IMMED = 1,		/* immediate */
    GM_MODE_UNEXP = 2,		/* unexpected */
    GM_MODE_REND = 4		/* rendezvous */
};

/* control message types */
enum
{
    CTRL_REQ_TYPE = 1,
    CTRL_ACK_TYPE = 2,
    CTRL_IMMED_TYPE = 3,
    CTRL_PUT_TYPE = 4
};

/* control messages */
struct ctrl_req
{
    bmi_flag_t mode;		/* immediate, rendezvous, etc. */
    bmi_size_t actual_size;	/* size of message we want to send */
    bmi_size_t expected_size;	/* expected size of message we want to send */
    bmi_msg_tag_t msg_tag;	/* message tag */
    bmi_op_id_t sender_op_id;	/* used for matching ctrl's */
    /* will probably be padded later for immediate messages */
};
struct ctrl_ack
{
    id_gen_t sender_op_id;	/* used for matching ctrl's */
    bmi_op_id_t receiver_op_id;	/* used for matching ctrl's */
    gm_remote_ptr_t remote_ptr;	/* peer address to target */
};
struct ctrl_immed
{
    bmi_flag_t mode;		/* immediate, rendezvous, etc. */
    bmi_msg_tag_t msg_tag;	/* message tag */
    int32_t actual_size;
    int32_t expected_size;
};
struct ctrl_put
{
    bmi_op_id_t receiver_op_id;	/* remote op that this completes */
};
struct ctrl_msg
{
    bmi_flag_t ctrl_type;
    union
    {
	struct ctrl_req req;
	struct ctrl_ack ack;
	struct ctrl_immed immed;
	struct ctrl_put put;
    }
    u;
};

/* tunable parameters */
enum
{
    GM_WAIT_METRIC = 50000,	/* maximum usecs we will wait during tests */
    /* max number of network events we will process per function call. */
    GM_MAX_EVENT_CYCLES = 1
};

/* Allowable sizes for each mode */
enum
{
    GM_IMMED_SIZE = 14,
    GM_CTRL_LENGTH = sizeof(struct ctrl_msg),
    GM_MODE_IMMED_LIMIT = 0,
    GM_MODE_REND_LIMIT = 262144,	/* 256K */
    GM_MODE_UNEXP_LIMIT = 16384	/* 16K */
};
static gm_size_t GM_IMMED_LENGTH;

/* gm particular method op data */
struct gm_op
{
    /* used by callback functions to discard ctrl message buffers */
    struct ctrl_msg *freeable_ctrl_buffer;
    bmi_op_id_t peer_op_id;	/* op id of partner's operation */
    /* indicates how buffer was allocated or pinned */
    bmi_flag_t buffer_status;
    gm_remote_ptr_t remote_ptr;
    void *tmp_xfer_buffer;
};

/* the local port that we are communicating on */
static struct gm_port *local_port = NULL;
/* and what we will call it */
static char BMI_GM_PORT_NAME[] = "pvfs_gm";

/* keep up with recv tokens */
static int recv_token_count_high = 0;

static int global_timeout_flag = 0;

/* internal gm method address list */
QLIST_HEAD(gm_addr_list);

/* static buffer pools */
static struct bufferpool *ctrl_send_pool = NULL;
#ifdef ENABLE_GM_BUFPOOL
static struct bufferpool *io_pool = NULL;
#endif /* ENABLE_GM_BUFPOOL */

/* internal utility functions */
static method_addr_p alloc_gm_method_addr(void);
static void dealloc_gm_method_addr(method_addr_p map);
static int gm_post_send_check_resource(bmi_op_id_t * id,
				       method_addr_p dest,
				       void *buffer,
				       bmi_size_t size,
				       bmi_size_t expected_size,
				       bmi_msg_tag_t tag,
				       bmi_flag_t mode,
				       bmi_flag_t buffer_status,
				       void *user_ptr);
static void ctrl_req_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status);
void dealloc_gm_method_op(method_op_p op_p);
static method_op_p alloc_gm_method_op(void);
static int ctrl_recv_pool_init(int pool_size);
static int test_done(bmi_op_id_t id,
		     int *outcount,
		     bmi_error_code_t * error_code,
		     bmi_size_t * actual_size,
		     void **user_ptr);
static int gm_do_work(int wait_time);
static void delayed_token_sweep(void);
static int receive_cycle(int timeout);
void alarm_callback(void *context);
static int high_recv_handler(gm_recv_event_t * poll_event,
			     int fast);
static void ctrl_ack_handler(bmi_op_id_t ctrl_op_id,
			     unsigned int node_id,
			     gm_remote_ptr_t remote_ptr,
			     bmi_op_id_t peer_op_id);
static int ctrl_req_handler_rend(bmi_op_id_t ctrl_op_id,
				 bmi_size_t ctrl_actual_size,
				 bmi_size_t ctrl_expected_size,
				 bmi_msg_tag_t ctrl_tag,
				 unsigned int node_id);
static int immed_unexp_recv_handler(bmi_size_t size,
				    bmi_msg_tag_t msg_tag,
				    method_addr_p map,
				    void *buffer);
static int immed_recv_handler(bmi_size_t actual_size,
			      bmi_size_t expected_size,
			      bmi_msg_tag_t msg_tag,
			      method_addr_p map,
			      void *buffer);
static void put_recv_handler(bmi_op_id_t ctrl_op_id);
static void ctrl_ack_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status);
static void data_send_callback(struct gm_port *port,
			       void *context,
			       gm_status_t status);
static void immed_send_callback(struct gm_port *port,
				void *context,
				gm_status_t status);
static void ctrl_put_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status);
static int test_done_some(int incount,
			  bmi_op_id_t * id_array,
			  int *outcount,
			  int *index_array,
			  bmi_error_code_t * error_code_array,
			  bmi_size_t * actual_size_array,
			  void **user_ptr);
static int test_done_unexpected(int incount,
				int *outcount,
				struct method_unexpected_info *info);
static void initiate_send_rend(method_op_p mop);
static void initiate_send_immed(method_op_p mop);
static void initiate_put_announcement(method_op_p mop);
static void send_data_buffer(method_op_p mop);
static void prepare_for_recv(method_op_p mop);
static int gm_generic_testwait(bmi_op_id_t id,
			       int *outcount,
			       bmi_error_code_t * error_code,
			       bmi_size_t * actual_size,
			       void **user_ptr,
			       int wait_time);
static int gm_generic_testwaitsome(int incount,
				   bmi_op_id_t * id_array,
				   int *outcount,
				   int *index_array,
				   bmi_error_code_t * error_code_array,
				   bmi_size_t * actual_size_array,
				   void **user_ptr_array,
				   int wait_time);
static int gm_generic_testwaitunexpected(int incount,
					 int *outcount,
					 struct method_unexpected_info *info,
					 int wait_time);

/*************************************************************************
 * Visible Interface 
 */

/* BMI_gm_initialize()
 *
 * Initializes the gm method.  Must be called before any other gm
 * method functions.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_initialize(method_addr_p listen_addr,
		      bmi_flag_t method_id,
		      bmi_flag_t init_flags)
{
    gm_status_t gm_ret;
    unsigned int rec_tokens = 0;
    unsigned int send_tokens = 0;
    int ret = -1;
    char gm_host_name[GM_MAX_HOST_NAME_LEN];
    unsigned int gm_host_id = 0;
    unsigned int min_message_size = 0;
    int i = 0;
    int tmp_errno = 0;

    gossip_ldebug(BMI_DEBUG_GM, "Initializing GM module.\n");

    /* check args */
    if ((init_flags & BMI_INIT_SERVER) && !listen_addr)
    {
	gossip_lerr("Error: bad parameters to GM module.\n");
	return (-EINVAL);
    }

    GM_IMMED_LENGTH = gm_max_length_for_size(GM_IMMED_SIZE) -
	sizeof(struct ctrl_msg);

    /* zero out our parameter structure and fill it in */
    memset(&gm_method_params, 0, sizeof(struct method_params));
    gm_method_params.method_id = method_id;
    gm_method_params.mode_immed_limit = GM_MODE_IMMED_LIMIT;
    gm_method_params.mode_eager_limit = GM_MODE_REND_LIMIT;
    gm_method_params.mode_rend_limit = GM_MODE_REND_LIMIT;
    gm_method_params.mode_unexp_limit = GM_MODE_UNEXP_LIMIT;
    gm_method_params.method_flags = init_flags;

    gossip_ldebug(BMI_DEBUG_GM, "Setting up GM operation lists.\n");
    /* set up the operation lists */
    for (i = 0; i < NUM_INDICES; i++)
    {
	op_list_array[i] = op_list_new();
	if (!op_list_array[i])
	{
	    tmp_errno = -ENOMEM;
	    goto gm_initialize_failure;
	}
    }

    if (init_flags & BMI_INIT_SERVER)
    {
	/* hang on to our local listening address if needed */
	gm_method_params.listen_addr = listen_addr;
    }

    /* start up gm */
    gm_ret = gm_init();
    if (gm_ret != GM_SUCCESS)
    {
	gossip_lerr("Error: gm_init() failure.\n");
	return (-EPROTO);
    }

    /* open our local port for communication */
    gm_ret = gm_open(&local_port, BMI_GM_UNIT_NUM, BMI_GM_PORT_NUM,
		     BMI_GM_PORT_NAME, GM_API_VERSION_1_3);
    if (gm_ret != GM_SUCCESS)
    {
	ret = -EPROTO;
	goto gm_initialize_failure;
    }

    rec_tokens = gm_num_receive_tokens(local_port);
    send_tokens = gm_num_send_tokens(local_port);
    gossip_ldebug(BMI_DEBUG_GM, "Available recieve tokens: %u.\n", rec_tokens);
    gossip_ldebug(BMI_DEBUG_GM, "Available send tokens: %u.\n", send_tokens);

    /* we will use half of the send tokens for low priority, and half for
     * high priority */
    gm_free_send_tokens(local_port, GM_LOW_PRIORITY, send_tokens / 2);
    gm_free_send_tokens(local_port, GM_HIGH_PRIORITY, send_tokens / 2);
    /* ditto for recv tokens */
    recv_token_count_high = rec_tokens / 2;

    /* go ahead and post buffers for receiving ctrl messages */
    ret = ctrl_recv_pool_init(recv_token_count_high);
    if (ret < 0)
    {
	tmp_errno = ret;
	goto gm_initialize_failure;
    }

#ifdef ENABLE_GM_REGCACHE
    /* initialize the memory registration cache */
    ret = bmi_gm_regcache_init(local_port);
    if (ret < 0)
    {
	tmp_errno = ret;
	goto gm_initialize_failure;
    }
#endif /* ENABLE_GM_REGCACHE */

    /* intialize the control buffer cache */
    ctrl_send_pool = bmi_gm_bufferpool_init(local_port, send_tokens,
					    GM_CTRL_LENGTH);
    if (!ctrl_send_pool)
    {
	tmp_errno = ret;
	goto gm_initialize_failure;
    }

#ifdef ENABLE_GM_BUFPOOL
    /* initialize the io buffer cache */
    io_pool = bmi_gm_bufferpool_init(local_port, 32, GM_MODE_REND_LIMIT);
    if (!io_pool)
    {
	tmp_errno = ret;
	gossip_lerr("Error: failed to obtain memory for buffer pool.\n");
	goto gm_initialize_failure;
    }
#endif /* ENABLE_GM_BUFPOOL */

    /* allow directed sends */
    gm_allow_remote_memory_access(local_port);

    /* print out a little debugging info about the interface. */
    gm_get_host_name(local_port, gm_host_name);
    gm_get_node_id(local_port, &gm_host_id);
    min_message_size = gm_min_message_size(local_port);
    gossip_ldebug(BMI_DEBUG_GM, "GM Interface host name: %s.\n", gm_host_name);
    gossip_ldebug(BMI_DEBUG_GM, "GM Interface node id: %u.\n", gm_host_id);
    gossip_ldebug(BMI_DEBUG_GM, "GM Interface min msg size: %u.\n",
		  min_message_size);
    gossip_ldebug(BMI_DEBUG_GM, "GM immediate mode limit: %d.\n",
		  GM_MODE_IMMED_LIMIT);

    gossip_ldebug(BMI_DEBUG_GM, "GM module successfully initialized.\n");
    return (0);

  gm_initialize_failure:

    /* cleanup data structures and bail out */
    for (i = 0; i < NUM_INDICES; i++)
    {
	if (op_list_array[i])
	{
	    op_list_cleanup(op_list_array[i]);
	}
    }

#ifdef ENABLE_GM_REGCACHE
    /* shut down regcache */
    bmi_gm_regcache_finalize();
#endif /* ENABLE_GM_REGCACHE */

    /* shut down ctrl buffer cache */
    if (ctrl_send_pool)
	bmi_gm_bufferpool_finalize(ctrl_send_pool);

#ifdef ENABLE_GM_BUFPOOL
    if (io_pool)
	bmi_gm_bufferpool_finalize(io_pool);
#endif /* ENABLE_GM_BUFPOOL */

    gm_finalize();
    gossip_lerr("Error: BMI_gm_initialize failure.\n");
    return (ret);

}


/* BMI_gm_finalize()
 * 
 * Shuts down the gm method.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_finalize(void)
{
    int i = 0;

    /* note that this forcefully shuts down operations */
    for (i = 0; i < NUM_INDICES; i++)
    {
	if (op_list_array[i])
	{
	    op_list_cleanup(op_list_array[i]);
	    op_list_array[i] = NULL;
	}
    }

#ifdef ENABLE_GM_REGCACHE
    bmi_gm_regcache_finalize();
#endif /* ENABLE_GM_REGCACHE */

    bmi_gm_bufferpool_finalize(ctrl_send_pool);

#ifdef ENABLE_GM_BUFPOOL
    bmi_gm_bufferpool_finalize(io_pool);
#endif /* ENABLE_GM_BUFPOOL */

    gm_close(local_port);
    /* shut down the gm system */
    gm_finalize();
    gossip_ldebug(BMI_DEBUG_GM, "GM module finalized.\n");
    return (0);
}


/*
 * BMI_gm_method_addr_lookup()
 *
 * resolves the string representation of an address into a method
 * address structure.  
 *
 * returns a pointer to method_addr on success, NULL on failure
 */
method_addr_p BMI_gm_method_addr_lookup(const char *id_string)
{
    char *gm_string = NULL;
    method_addr_p new_addr = NULL;
    struct gm_addr *gm_data = NULL;
    char local_tag[] = "NULL";

    gm_string = string_key("gm", id_string);
    if (!gm_string)
    {
	/* the string doesn't even have our info */
	return (NULL);
    }

    /* start breaking up the method information */
    /* for normal gm, it is simply a hostname */

    /* looks ok, so let's build the method addr structure */
    new_addr = alloc_gm_method_addr();
    if (!new_addr)
    {
	free(gm_string);
	return (NULL);
    }
    gm_data = new_addr->method_data;

    if (strncmp(gm_string, local_tag, strlen(local_tag)) == 0)
    {
	new_addr->local_addr = 1;
    }
    else
    {
	gm_data->node_id = gm_host_name_to_node_id(local_port, gm_string);
	if (gm_data->node_id == GM_NO_SUCH_NODE_ID)
	{
	    dealloc_method_addr(new_addr);
	    free(gm_string);
	    return (NULL);
	}
    }

    free(gm_string);
    /* keep up with the address here */
    gm_addr_add(&gm_addr_list, new_addr);
    return (new_addr);
}


/* BMI_gm_memalloc()
 * 
 * Allocates memory that can be used in native mode by gm.
 *
 * returns 0 on success, -errno on failure
 */
void *BMI_gm_memalloc(bmi_size_t size,
		      bmi_flag_t send_recv)
{
    /* NOTE: In the send case, we allocate a little bit of extra memory
     * at the END of the buffer to use for message trailers.  We stick it
     * at the back in order to easily preserve alignment
     * NOTE: We are being pretty trustful of the caller.  Checks are done
     * (hopefully :) at the BMI level before we get here.
     */
    void *new_buffer = NULL;

    if (send_recv == BMI_RECV_BUFFER)
    {
	if (size <= GM_IMMED_LENGTH)
	{
	    new_buffer = malloc(size);
	}
	else
	{
	    new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
							     long) size);
	}
    }
    else if (send_recv == BMI_SEND_BUFFER)
    {
	if (size <= GM_IMMED_LENGTH)
	{
	    /* pad enough room for a ctrl structure */
	    size += sizeof(struct ctrl_msg);
	    new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
							     long) size);
	}
	else
	{
	    new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
							     long) size);
	}
    }
    else
    {
	new_buffer = NULL;
    }

    return (new_buffer);
}


/* BMI_gm_memfree()
 * 
 * Frees memory that was allocated with BMI_gm_memalloc()
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_memfree(void *buffer,
		   bmi_size_t size,
		   bmi_flag_t send_recv)
{
    if (send_recv == BMI_RECV_BUFFER)
    {
	if (size <= GM_IMMED_LENGTH)
	{
	    free(buffer);
	}
	else
	{
	    gm_dma_free(local_port, buffer);
	}
    }
    else if (send_recv == BMI_SEND_BUFFER)
    {
	gm_dma_free(local_port, buffer);
    }
    else
    {
	return (-EINVAL);
    }

    buffer = NULL;
    return (0);
}


/* BMI_gm_set_info()
 * 
 * Pass in optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_set_info(int option,
		    void *inout_parameter)
{
    return (-ENOSYS);

}

/* BMI_gm_get_info()
 * 
 * Query for optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_get_info(int option,
		    void *inout_parameter)
{
    return (-ENOSYS);
}


/* BMI_gm_post_send()
 * 
 * Submits send operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_gm_post_send(bmi_op_id_t * id,
		     method_addr_p dest,
		     void *buffer,
		     bmi_size_t size,
		     bmi_size_t expected_size,
		     bmi_flag_t buffer_flag,
		     bmi_msg_tag_t tag,
		     void *user_ptr)
{
    bmi_flag_t buffer_status = GM_BUF_USER_ALLOC;
    void *new_buffer = NULL;
    struct ctrl_msg *new_ctrl_msg = NULL;
    bmi_size_t buffer_size = 0;

    gossip_ldebug(BMI_DEBUG_GM, "BMI_gm_post_send called.\n");

    /* clear id immediately for safety */
    *id = 0;

    /* make sure it's not too big */
    if (expected_size > GM_MODE_REND_LIMIT)
    {
	return (-EINVAL);
    }

    if (!(buffer_flag & BMI_PRE_ALLOC))
    {
	if (expected_size <= GM_IMMED_LENGTH)
	{
	    /* pad enough room for a ctrl structure */
	    buffer_size = sizeof(struct ctrl_msg) + size;

	    /* create a new buffer and copy */
	    new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
							     long) buffer_size);
	    if (!new_buffer)
	    {
		gossip_lerr("Error: gm_dma_malloc failure.\n");
		return (-ENOMEM);
	    }
	    memcpy(new_buffer, buffer, size);
	    buffer_status = GM_BUF_METH_ALLOC;
	    /* this seems little shady, but we are going to go ahead and forget
	     * about the buffer that the user gave us.  It serves no purpose
	     * here anymore.
	     */
	    buffer = new_buffer;
	}
	else
	{
	    buffer_status = GM_BUF_METH_REG;
	}
    }

    if (expected_size <= GM_IMMED_LENGTH)
    {
	/* Immediate mode stuff */
	new_ctrl_msg = (struct ctrl_msg *) (buffer + size);
	new_ctrl_msg->ctrl_type = CTRL_IMMED_TYPE;
	new_ctrl_msg->u.immed.actual_size = size;
	new_ctrl_msg->u.immed.expected_size = expected_size;
	new_ctrl_msg->u.immed.msg_tag = tag;
	new_ctrl_msg->u.immed.mode = GM_MODE_IMMED;
	return (gm_post_send_check_resource(id, dest, buffer, size,
					    expected_size, tag,
					    new_ctrl_msg->u.immed.mode,
					    buffer_status, user_ptr));
    }
    else
    {
	/* 3 way rendezvous mode */
	return (gm_post_send_check_resource(id, dest, buffer, size,
					    expected_size, tag, GM_MODE_REND,
					    buffer_status, user_ptr));
    }
}


/* BMI_gm_post_sendunexpected()
 * 
 * Submits unexpected send operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_gm_post_sendunexpected(bmi_op_id_t * id,
			       method_addr_p dest,
			       void *buffer,
			       bmi_size_t size,
			       bmi_flag_t buffer_flag,
			       bmi_msg_tag_t tag,
			       void *user_ptr)
{
    bmi_flag_t buffer_status = GM_BUF_USER_ALLOC;
    void *new_buffer = NULL;
    struct ctrl_msg *new_ctrl_msg = NULL;
    bmi_size_t buffer_size = 0;

    gossip_ldebug(BMI_DEBUG_GM, "BMI_gm_post_sendunexpected called.\n");

    /* clear id immediately for safety */
    *id = 0;

    if (size > GM_MODE_UNEXP_LIMIT)
    {
	return (-EINVAL);
    }

    if (!(buffer_flag & BMI_PRE_ALLOC))
    {
	/* pad enough room for a ctrl structure */
	buffer_size = sizeof(struct ctrl_msg) + size;

	/* create a new buffer and copy */
	new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
							 long) buffer_size);
	if (!new_buffer)
	{
	    gossip_lerr("Error: gm_dma_malloc failure.\n");
	    return (-ENOMEM);
	}
	memcpy(new_buffer, buffer, size);
	buffer_status = GM_BUF_METH_ALLOC;
	/* this seems little shady, but we are going to go ahead and forget
	 * about the buffer that the user gave us.  It serves no purpose
	 * here anymore.
	 */
	buffer = new_buffer;
    }

    /* Immediate mode stuff */
    new_ctrl_msg = (struct ctrl_msg *) (buffer + size);
    new_ctrl_msg->ctrl_type = CTRL_IMMED_TYPE;
    new_ctrl_msg->u.immed.actual_size = size;
    new_ctrl_msg->u.immed.expected_size = 0;
    new_ctrl_msg->u.immed.msg_tag = tag;
    new_ctrl_msg->u.immed.mode = GM_MODE_UNEXP;

    return (gm_post_send_check_resource(id, dest, buffer, size,
					0, tag, new_ctrl_msg->u.immed.mode,
					buffer_status, user_ptr));
}



/* BMI_gm_post_recv()
 * 
 * Submits recv operations.
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
int BMI_gm_post_recv(bmi_op_id_t * id,
		     method_addr_p src,
		     void *buffer,
		     bmi_size_t expected_size,
		     bmi_size_t * actual_size,
		     bmi_flag_t buffer_flag,
		     bmi_msg_tag_t tag,
		     void *user_ptr)
{
    bmi_flag_t mode = 0;
    method_op_p query_op = NULL;
    method_op_p new_method_op = NULL;
    struct op_list_search_key key;
    struct gm_op *gm_op_data = NULL;
    struct gm_addr *gm_addr_data = NULL;
    int ret = -1;
    int buffer_status = GM_BUF_USER_ALLOC;

    gossip_ldebug(BMI_DEBUG_GM, "BMI_gm_post_recv called.\n");

    /* what happens here ?
     * see if the operation is already in progress (IND_NEED_RECV_POST)
     *  - if so, match it and poke it to continue
     *  - if not, create an op and queue it up in IND_NEED_CTRL_MATCH
     */

    /* clear id immediately for safety */
    *id = 0;

    /* make sure it's not too big */
    if (expected_size > GM_MODE_REND_LIMIT)
    {
	return (-EINVAL);
    }

    if (expected_size <= GM_IMMED_LENGTH)
    {
	mode = GM_MODE_IMMED;
    }
    else
    {
	mode = GM_MODE_REND;
	/* check the type of buffer and pinn it if necessary */
	if (buffer_flag != BMI_PRE_ALLOC)
	{
	    buffer_status = GM_BUF_METH_REG;
	}
    }

    /* push work first; use this as an opportunity to make sure that the
     * receive keeps buffers moving as quickly as possible
     */
    ret = gm_do_work(0);
    if (ret < 0)
    {
	return (ret);
    }

    /* see if this operation has already begun... */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = src;
    key.method_addr_yes = 1;
    key.expected_size = expected_size;
    key.expected_size_yes = 1;
    key.msg_tag = tag;
    key.msg_tag_yes = 1;
    key.mode_mask = mode;
    key.mode_mask_yes = 1;

    query_op = op_list_search(op_list_array[IND_NEED_RECV_POST], &key);
    if (query_op)
    {
	gm_addr_data = query_op->addr->method_data;
	*id = query_op->op_id;
	/* we found the operation in progress. */
	if (mode == GM_MODE_REND)
	{
	    /* post has occurred */
	    op_list_remove(query_op);
	    query_op->buffer = buffer;

	    /* now we need a token to send a ctrl ack */
	    if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
	    {
#ifdef ENABLE_GM_BUFPOOL
		if (!bmi_gm_bufferpool_empty(io_pool))
		{
#endif /* ENABLE_GM_BUFPOOL */
		    prepare_for_recv(query_op);
		    ret = 0;
#ifdef ENABLE_GM_BUFPOOL
		}
		else
		{
		    gm_free_send_token(local_port, GM_HIGH_PRIORITY);
		    op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK],
				query_op);
		    ret = 0;
		}
#endif /* ENABLE_GM_BUFPOOL */
	    }
	    else
	    {
		/* we don't have enough tokens */
		op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK],
			    query_op);
		ret = 0;
	    }
	}
	else if (mode == GM_MODE_IMMED)
	{
	    /* all is done except memory copy- complete instantly */
	    op_list_remove(query_op);
	    gm_op_data = query_op->method_data;
	    memcpy(buffer, query_op->buffer, query_op->actual_size);
	    *actual_size = query_op->actual_size;
	    free(query_op->buffer);
	    *id = 0;
	    dealloc_gm_method_op(query_op);
	    ret = 1;

	}
	else
	{
	    /* we don't have any other modes implemented yet */
	    ret = -ENOSYS;
	}
    }
    else
    {
	/* we must create the operation and queue it up */
	new_method_op = alloc_gm_method_op();
	if (!new_method_op)
	{
	    return (-ENOMEM);
	}
	*id = new_method_op->op_id;
	new_method_op->user_ptr = user_ptr;
	new_method_op->send_recv = BMI_OP_RECV;
	new_method_op->addr = src;
	new_method_op->buffer = buffer;
	new_method_op->expected_size = expected_size;
	new_method_op->actual_size = 0;
	new_method_op->msg_tag = tag;
	new_method_op->mode = mode;
	gm_op_data = new_method_op->method_data;
	gm_op_data->buffer_status = buffer_status;

	/* just for safety; the user should not use this value in this case */
	*actual_size = 0;

	op_list_add(op_list_array[IND_NEED_CTRL_MATCH], new_method_op);
	ret = 0;
    }

    return (ret);
}


/* BMI_gm_test()
 * 
 * Checks to see if a particular message has completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_test(bmi_op_id_t id,
		int *outcount,
		bmi_error_code_t * error_code,
		bmi_size_t * actual_size,
		void **user_ptr)
{
    return (gm_generic_testwait(id, outcount, error_code,
				actual_size, user_ptr, 0));
}

/* BMI_gm_wait()
 * 
 * Checks to see if a particular message has completed.  Will block
 * briefly even if there is nothing to do initially.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_wait(bmi_op_id_t id,
		int *outcount,
		bmi_error_code_t * error_code,
		bmi_size_t * actual_size,
		void **user_ptr)
{
    return (gm_generic_testwait(id, outcount, error_code, actual_size, user_ptr,
				GM_WAIT_METRIC));
}

/* BMI_gm_testsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_testsome(int incount,
		    bmi_op_id_t * id_array,
		    int *outcount,
		    int *index_array,
		    bmi_error_code_t * error_code_array,
		    bmi_size_t * actual_size_array,
		    void **user_ptr_array)
{
    return (gm_generic_testwaitsome(incount, id_array, outcount,
				    index_array, error_code_array,
				    actual_size_array, user_ptr_array, 0));
}


/* BMI_gm_waitsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 * Will block briefly even if there is initially nothing to do.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_waitsome(int incount,
		    bmi_op_id_t * id_array,
		    int *outcount,
		    int *index_array,
		    bmi_error_code_t * error_code_array,
		    bmi_size_t * actual_size_array,
		    void **user_ptr_array)
{
    return (gm_generic_testwaitsome(incount, id_array, outcount,
				    index_array, error_code_array,
				    actual_size_array, user_ptr_array,
				    GM_WAIT_METRIC));
}

/* BMI_gm_testunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_testunexpected(int incount,
			  int *outcount,
			  struct method_unexpected_info *info)
{
    return (gm_generic_testwaitunexpected(incount, outcount, info, 0));
}

/* BMI_gm_waitunexpected()
 * 
 * Checks to see if any unexpected messages have completed.  Will block
 * briefly even if there is initially nothing to do.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_waitunexpected(int incount,
			  int *outcount,
			  struct method_unexpected_info *info)
{
    return (gm_generic_testwaitunexpected(incount, outcount, info,
					  GM_WAIT_METRIC));
}


/******************************************************************
 * Internal support functions
 */

/*
 * dealloc_gm_method_addr()
 *
 * destroys method address structures generated by the gm module.
 *
 * no return value
 */
static void dealloc_gm_method_addr(method_addr_p map)
{

    dealloc_method_addr(map);

    return;
}


/*
 * alloc_gm_method_addr()
 *
 * creates a new method address with defaults filled in for GM.
 *
 * returns pointer to struct on success, NULL on failure
 */
static method_addr_p alloc_gm_method_addr(void)
{

    struct method_addr *my_method_addr = NULL;
    struct gm_addr *gm_data = NULL;

    my_method_addr = alloc_method_addr(gm_method_params.method_id, sizeof(struct
									  gm_addr));
    if (!my_method_addr)
    {
	return (NULL);
    }

    /* note that we trust the alloc_method_addr() function to have zeroed
     * out the structures for us already 
     */

    gm_data = my_method_addr->method_data;
    gm_data->node_id = GM_NO_SUCH_NODE_ID;
    return (my_method_addr);
}


/*
 * alloc_gm_method_op()
 *
 * creates a new method op with defaults filled in for GM.
 *
 * returns pointer to struct on success, NULL on failure
 */
static method_op_p alloc_gm_method_op(void)
{
    method_op_p my_method_op = NULL;

    my_method_op = alloc_method_op(sizeof(struct gm_op));

    /* note that we trust the alloc_method_addr() function to have zeroed
     * out the structures for us already 
     */

    return (my_method_op);
}


/* 
 * dealloc_gm_method_op()
 *
 * frees the memory allocated to a gm method_op structure 
 * 
 * no return value
 */
void dealloc_gm_method_op(method_op_p op_p)
{
    dealloc_method_op(op_p);
    return;
}


/* gm_post_send_check_resource()
 *
 * Checks to see if communication can proceed for a given send operation
 *
 * returns 0 on success, -errno on failure
 */
static int gm_post_send_check_resource(bmi_op_id_t * id,
				       method_addr_p dest,
				       void *buffer,
				       bmi_size_t size,
				       bmi_size_t expected_size,
				       bmi_msg_tag_t tag,
				       bmi_flag_t mode,
				       bmi_flag_t buffer_status,
				       void *user_ptr)
{
    method_op_p new_method_op = NULL;
    int ret = -1;
    struct gm_addr *gm_data = NULL;
    struct gm_op *gm_op_data = NULL;

    gossip_ldebug(BMI_DEBUG_GM, "gm_post_send_check_resource() called.\n");

    /* what do we want to do here? 
     * For now, try to send a control message and then bail out.  The
     * poll function will drive the rest of the way.
     */

    /* we need an op structure to keep up with this send */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (-ENOMEM);
    }
    *id = new_method_op->op_id;
    new_method_op->user_ptr = user_ptr;
    new_method_op->send_recv = BMI_OP_SEND;
    new_method_op->addr = dest;
    new_method_op->buffer = buffer;
    new_method_op->actual_size = size;
    new_method_op->expected_size = expected_size;
    new_method_op->msg_tag = tag;
    gossip_ldebug(BMI_DEBUG_GM, "Tag: %d.\n", (int) tag);
    new_method_op->mode = mode;

    gm_data = dest->method_data;
    gm_op_data = new_method_op->method_data;
    gm_op_data->buffer_status = buffer_status;

    /* make sure that we are not bypassing any operation that has stalled
     * waiting on tokens
     */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_HI_CTRL]))
    {
	op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRL], new_method_op);
	/* push on existing work rather than attempting this send */
	return (gm_do_work(0));
    }

    /* all clear; let's see if we have a send token for the control message */
    if (!gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
    {
	/* queue up and wait for a token */
	op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRL], new_method_op);
	/* push on existing work rather than attempting this send */
	ret = gm_do_work(0);
    }
    else
    {
	gossip_ldebug(BMI_DEBUG_GM, "Proceeding with communication.\n");
	if (mode == GM_MODE_REND)
	{
	    initiate_send_rend(new_method_op);
	    ret = 0;
	}
	else
	{
	    initiate_send_immed(new_method_op);
	    ret = 0;
	}
    }

    return (ret);
}


/* initiate_send_immed()
 *
 * starts off an immediate network transfer (data + ctrl message in one
 * buffer).  Assumes that we have already acquired a high priority send
 * token.
 *
 * no return value
 */
static void initiate_send_immed(method_op_p mop)
{

    struct gm_addr *gm_addr_data = mop->addr->method_data;
    bmi_size_t true_msg_len = 0;

    gossip_ldebug(BMI_DEBUG_GM, "Sending immediate msg.\n");

    true_msg_len = mop->actual_size + sizeof(struct ctrl_msg);

    /* send ctrl message */
    gm_send_to_peer_with_callback(local_port, mop->buffer,
				  GM_IMMED_SIZE, true_msg_len, GM_HIGH_PRIORITY,
				  gm_addr_data->node_id, immed_send_callback,
				  mop);

    /* queue up to wait for completion */
    op_list_add(op_list_array[IND_SENDING], mop);

    return;
}


/* initiate_put_announcement()
 *
 * sends a control message to inform a target host that a directed send
 * has completed
 *
 * no return value.
 */
static void initiate_put_announcement(method_op_p mop)
{
    struct gm_op *gm_op_data = mop->method_data;
    struct gm_addr *gm_addr_data = mop->addr->method_data;
    struct ctrl_msg *my_ctrl = NULL;

    /* woohoo- we have a token, work on a ctrl message */
    my_ctrl = bmi_gm_bufferpool_get(ctrl_send_pool);

    my_ctrl->ctrl_type = CTRL_PUT_TYPE;
    my_ctrl->u.put.receiver_op_id = gm_op_data->peer_op_id;
    /* keep up with this buffer in the op structure */
    gm_op_data->freeable_ctrl_buffer = my_ctrl;

    gossip_ldebug(BMI_DEBUG_GM, "Sending ctrl msg.\n");

    /* send ctrl message */
    gm_send_to_peer_with_callback(local_port, my_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  ctrl_put_callback, mop);

    /* queue up to wait for completion */
    op_list_add(op_list_array[IND_SENDING], mop);

    return;
}

/* initiate_send_rend()
 *
 * actually starts off a rendezvous send operation by sending the control
 * message.  Assumes that we have already acquired a high priority send
 * token.
 *
 * no return value
 */
static void initiate_send_rend(method_op_p mop)
{
    struct gm_op *gm_op_data = mop->method_data;
    struct gm_addr *gm_addr_data = mop->addr->method_data;
    struct ctrl_msg *my_ctrl = NULL;

    /* woohoo- we have a token, work on a ctrl message */
    my_ctrl = bmi_gm_bufferpool_get(ctrl_send_pool);

    my_ctrl->ctrl_type = CTRL_REQ_TYPE;
    my_ctrl->u.req.mode = mop->mode;
    my_ctrl->u.req.actual_size = mop->actual_size;
    my_ctrl->u.req.expected_size = mop->expected_size;
    my_ctrl->u.req.msg_tag = mop->msg_tag;
    my_ctrl->u.req.sender_op_id = mop->op_id;
    /* keep up with this buffer in the op structure */
    gm_op_data->freeable_ctrl_buffer = my_ctrl;

    gossip_ldebug(BMI_DEBUG_GM, "Sending ctrl msg.\n");

    /* send ctrl message */
    gm_send_to_peer_with_callback(local_port, my_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  ctrl_req_callback, mop);

    /* queue up to wait for completion */
    op_list_add(op_list_array[IND_SENDING], mop);

    return;
}

/* ctrl_put_callback()
 *
 * callback function triggered on completion of a directed send
 * announcement  
 *
 * no return value
 */
static void ctrl_put_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status)
{
    /* the context is our operation */
    method_op_p my_op = context;
    struct gm_op *gm_op_data = my_op->method_data;

    gossip_ldebug(BMI_DEBUG_GM, "ctrl_put_callback() called.\n");

    /* free up ctrl message buffer */
    bmi_gm_bufferpool_put(ctrl_send_pool, gm_op_data->freeable_ctrl_buffer);

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled...\n");
	op_list_remove(my_op);
	my_op->error_code = -ETIMEDOUT;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_lerr("Error value: %d\n", (int) status);
	op_list_remove(my_op);
	my_op->error_code = -EPROTO;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* operation is now complete */
    op_list_remove(my_op);
    my_op->error_code = 0;
    op_list_add(op_list_array[IND_COMPLETE], my_op);

    gossip_ldebug(BMI_DEBUG_GM, "Finished ctrl_put_callback().\n");

    return;
}


/* immed_send_callback()
 *
 * callback function triggered on completion of an immediate send
 * operation.
 *
 * no return value
 */
static void immed_send_callback(struct gm_port *port,
				void *context,
				gm_status_t status)
{
    /* the context is our operation */
    method_op_p my_op = context;
    struct gm_op *gm_op_data = my_op->method_data;

    gossip_ldebug(BMI_DEBUG_GM, "immed_send_callback() called.\n");

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    if (gm_op_data->buffer_status == GM_BUF_METH_ALLOC)
    {
	/* this was an internally allocated buffer */
	gm_dma_free(local_port, my_op->buffer);
    }

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled...\n");
	op_list_remove(my_op);
	my_op->error_code = -ETIMEDOUT;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_lerr("Error value: %d\n", (int) status);
	op_list_remove(my_op);
	my_op->error_code = -EPROTO;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* golden! */

    /* we need to drive the state of this operation to the next phase, in
     * which we wait for a ctrl msg from the reciever before proceeding
     */
    op_list_remove(my_op);
    my_op->error_code = 0;
    /* let it hang around in the receiving queue until we get a control
     * message back from the target that indicates that we can continue.
     */
    op_list_add(op_list_array[IND_COMPLETE], my_op);

    gossip_ldebug(BMI_DEBUG_GM, "Finished immed_send_callback().\n");

    return;
}


/* ctrl_req_callback()
 *
 * callback function triggered on the completion of a request control
 * message send.
 *
 * no return value
 */
static void ctrl_req_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status)
{
    /* the context is our operation */
    method_op_p my_op = context;
    struct gm_op *gm_op_data = NULL;

    gossip_ldebug(BMI_DEBUG_GM, "ctrl_req_callback() called.\n");

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    /* free up ctrl message buffer */
    gm_op_data = my_op->method_data;
    bmi_gm_bufferpool_put(ctrl_send_pool, gm_op_data->freeable_ctrl_buffer);

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled...\n");
	op_list_remove(my_op);
	my_op->error_code = -ETIMEDOUT;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_lerr("Error value: %d\n", (int) status);
	op_list_remove(my_op);
	my_op->error_code = -EPROTO;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* golden! */

    /* don't touch the operation.  The ack receive handler will drive it
     * to the next state.
     */

    gossip_ldebug(BMI_DEBUG_GM, "Finished ctrl_req_callback.\n");

    /* etc. etc. */
    return;
}

/* ctrl_recv_pool_init
 *
 * sets up a pool of receive buffers for the purpose of handling control
 * messages.  These buffers will be automatically re-posted as control
 * messages are handled.
 * NOTE: the caller is trusted to not exceed our token limit ...
 *
 * returns 0 on success, -errno on failure
 */
static int ctrl_recv_pool_init(int pool_size)
{

    struct ctrl_msg *tmp_ctrl = NULL;
    int i = 0;

    for (i = 0; i < pool_size; i++)
    {
	tmp_ctrl = gm_dma_malloc(local_port,
				 gm_max_length_for_size(GM_IMMED_SIZE));
	if (!tmp_ctrl)
	{
	    return (-ENOMEM);
	}
	gm_provide_receive_buffer(local_port, tmp_ctrl,
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
    }

    recv_token_count_high -= pool_size;

    return (0);
}


/* test_done()
 * 
 * nonblocking check to see if a certain operation has
 * completed.  
 *
 * returns 0 on success, -errno on failure
 */
static int test_done(bmi_op_id_t id,
		     int *outcount,
		     bmi_error_code_t * error_code,
		     bmi_size_t * actual_size,
		     void **user_ptr)
{
    method_op_p query_op = NULL;
    struct op_list_search_key key;

    *outcount = 0;

    /* search for done or error unexpected ops */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.op_id = id;
    key.op_id_yes = 1;

    query_op = op_list_search(op_list_array[IND_COMPLETE], &key);
    if (query_op)
    {
	op_list_remove(query_op);
	(*error_code) = query_op->error_code;
	(*actual_size) = query_op->actual_size;
	if (user_ptr != NULL)
	{
	    (*user_ptr) = query_op->user_ptr;
	}
	dealloc_gm_method_op(query_op);
	(*outcount)++;
    }

    return (0);
}


/* test_done_some()
 * 
 * nonblocking check to see if a certain set of operations have
 * completed.  Indicates which ones completed using the index array.
 *
 * returns 0 on success, -errno on failure
 */
static int test_done_some(int incount,
			  bmi_op_id_t * id_array,
			  int *outcount,
			  int *index_array,
			  bmi_error_code_t * error_code_array,
			  bmi_size_t * actual_size_array,
			  void **user_ptr_array)
{
    method_op_p query_op = NULL;
    struct op_list_search_key key;
    int i = 0;

    *outcount = 0;
    memset(&key, 0, sizeof(struct op_list_search_key));

    for (i = 0; i < incount; i++)
    {
	if (id_array[i])
	{
	    key.op_id = id_array[i];
	    key.op_id_yes = 1;

	    query_op = op_list_search(op_list_array[IND_COMPLETE], &key);
	    if (query_op)
	    {
		op_list_remove(query_op);
		error_code_array[*outcount] = query_op->error_code;
		actual_size_array[*outcount] = query_op->actual_size;
		index_array[*outcount] = i;
		if (user_ptr_array != NULL)
		{
		    user_ptr_array[*outcount] = query_op->user_ptr;
		}
		dealloc_gm_method_op(query_op);
		(*outcount)++;
	    }
	}
    }

    return (0);
}


/* test_done_unexpected()
 * 
 * nonblocking check to see if any unexpected operations have completed.
 * The ops may be in the complete or error state.
 *
 * returns 0 on success, -errno on failure
 */
static int test_done_unexpected(int incount,
				int *outcount,
				struct method_unexpected_info *info)
{
    method_op_p query_op = NULL;
    struct op_list_search_key key;

    *outcount = 0;

    /* search for done or error unexpected ops */
    memset(&key, 0, sizeof(struct op_list_search_key));
    /* TODO: since there are no search parameters, we could probably
     * optimize this search.  We really just want to find the "next" one 
     */

    /* go through the completed list as long as we are finding stuff and 
     * we have room in the info array for it
     */
    while ((*outcount < incount) &&
	   (query_op =
	    op_list_search(op_list_array[IND_COMPLETE_RECV_UNEXP], &key)))
    {
	info[*outcount].error_code = query_op->error_code;
	info[*outcount].addr = query_op->addr;
	info[*outcount].buffer = query_op->buffer;
	info[*outcount].size = query_op->actual_size;
	info[*outcount].tag = query_op->msg_tag;
	op_list_remove(query_op);
	dealloc_gm_method_op(query_op);
	(*outcount)++;
    }
    return (0);
};


/* gm_do_work()
 * 
 * this is a generic function that forces work to be done within the gm
 * method when test functions are called.  It does not target any
 * particular operation.
 *
 * returns 0 on success, -errno on failure
 */
static int gm_do_work(int wait_time)
{
    int events = 0;
    int ret = -1;

    /* we have a lot of responsibilities here 
     * 1) peek to see if there are any events pending
     * 2) if not, see how many tokens we have and sweep op lists to do
     * work on ops waiting on tokens
     * 3) if there were events pending, do recv cycle
     * 4) if there were no events, set alarm and do cycle
     * 5) see how many tokens we have and sweep op lists again.
     */

    /* see if there is any work ready */
    events = gm_receive_pending(local_port);
    if (events == 0)
    {
	/* since there isn't any GM work to do right this second, let's
	 * try to service operations waiting on tokens first.
	 */
	delayed_token_sweep();

	/* See if there is anything to do now; specify timeout so we 
	 * don't busy spin if idle 
	 */
	if (gm_receive_pending(local_port) || wait_time > 0)
	{
	    ret = receive_cycle(wait_time);
	    if (ret < 0)
	    {
		return (ret);
	    }
	}
    }
    else
    {
	/* we know there is work to do- call receive with no timeout */
	ret = receive_cycle(0);
	if (ret < 0)
	{
	    return (ret);
	}
	/* maybe we have tokens available now */
	delayed_token_sweep();
    }

    return (0);
}


/* delayed_token_sweep()
 * 
 * Attempts to service any operations that are waiting on tokens to
 * become available
 *
 * returns 0 on success, -errno on failure
 */
static void delayed_token_sweep(void)
{
    method_op_p query_op = NULL;

    /* NOTE: the order is important here.  We try to push the
     * last steps of stalled messages first in order to preserve
     * ordering.
     */

    /* waiting for one last high priority send token to complete a
     * rendezvous communication
     */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_HI_PUT]))
    {
	/* see if we have any high priority send tokens */
	if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
	{
	    query_op =
		op_list_shownext(op_list_array[IND_NEED_SEND_TOK_HI_PUT]);
	    op_list_remove(query_op);
	    initiate_put_announcement(query_op);
	    return;
	}
    }

    /* look for stuff waiting on low send tokens */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_LOW]))
    {
	/* we need a low priority send token */
	if (gm_alloc_send_token(local_port, GM_LOW_PRIORITY))
	{
#ifdef ENABLE_GM_BUFPOOL
	    if (!bmi_gm_bufferpool_empty(io_pool))
	    {
#endif /* ENABLE_GM_BUFPOOL */
		query_op =
		    op_list_shownext(op_list_array[IND_NEED_SEND_TOK_LOW]);
		op_list_remove(query_op);
		send_data_buffer(query_op);
		return;
#ifdef ENABLE_GM_BUFPOOL
	    }
	    else
	    {
		gm_free_send_token(local_port, GM_LOW_PRIORITY);
	    }
#endif /* ENABLE_GM_BUFPOOL */
	}
    }

    /* look for stuff waiting for tokens to acknowledge send request */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK]))
    {
	if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
	{
#ifdef ENABLE_GM_BUFPOOL
	    if (!bmi_gm_bufferpool_empty(io_pool))
	    {
#endif /* ENABLE_GM_BUFPOOL */
		query_op =
		    op_list_shownext(op_list_array
				     [IND_NEED_SEND_TOK_HI_CTRLACK]);
		op_list_remove(query_op);
		prepare_for_recv(query_op);
		return;
#ifdef ENABLE_GM_BUFPOOL
	    }
	    else
	    {
		gm_free_send_token(local_port, GM_HIGH_PRIORITY);
	    }
#endif /* ENABLE_GM_BUFPOOL */
	}
    }

    /* look for stuff that is waiting for a token to send a ctrl requeset */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_HI_CTRL]))
    {
	/* see if we have any high priority send tokens */
	if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
	{
	    query_op =
		op_list_shownext(op_list_array[IND_NEED_SEND_TOK_HI_CTRL]);
	    op_list_remove(query_op);
	    if (query_op->mode == GM_MODE_REND)
	    {
		initiate_send_rend(query_op);
		return;
	    }
	    else
	    {
		initiate_send_immed(query_op);
		return;
	    }
	}
    }

    return;
}


/* receive_cycle()
 *
 * attempts to do a GM receive.  The argument specifies how long of a
 * timeout to use.
 *
 * returns 0 on success, -errno on failure
 */
static int receive_cycle(int timeout)
{
    gm_alarm_t poll_timeout_alarm;
    int ret = -1;
    gm_recv_event_t *poll_event = NULL;
    int handled_events = 0;

    if (timeout > 0)
    {
	gm_initialize_alarm(&poll_timeout_alarm);
	gm_set_alarm(local_port, &poll_timeout_alarm, timeout,
		     alarm_callback, NULL);
    }
    global_timeout_flag = 0;
    do
    {
	if (timeout > 0)
	{
	    /* this call is in place to get around the fact that
	     * gm_blocking_receive() never seems to sleep (in GM 1.5.1)
	     * if I have an alarm set.
	     */
	    poll_event = gm_blocking_receive_no_spin(local_port);
	}
	else
	{
	    poll_event = gm_receive(local_port);
	}
	switch (gm_ntohc(poll_event->recv.type))
	{
	case GM_FAST_HIGH_PEER_RECV_EVENT:
	case GM_FAST_HIGH_RECV_EVENT:
	    handled_events++;
	    ret = high_recv_handler(poll_event, 1);
	    break;
	case GM_HIGH_PEER_RECV_EVENT:
	case GM_HIGH_RECV_EVENT:
	    handled_events++;
	    ret = high_recv_handler(poll_event, 0);
	    break;
	case GM_NO_RECV_EVENT:
#if 0
	    gossip_lerr("Error: no recv event!\n");
#endif
	    /* fall through */

	default:
	    handled_events++;
	    gm_unknown(local_port, poll_event);
	    ret = 0;
	    break;
	}
    } while (((timeout > 0 && !global_timeout_flag && ret == 0) ||
	      (timeout == 0 && gm_receive_pending(local_port) && ret == 0)) &&
	     handled_events < GM_MAX_EVENT_CYCLES);

    if (timeout > 0 && !global_timeout_flag)
    {
	gm_cancel_alarm(&poll_timeout_alarm);
    }

    return (ret);
}


/* alarm_callback()
 *
 * function callback that occurs if our receive cycle times out.
 * Doesn't actually do anything but keep us from blocking.
 *
 * no return value
 */
void alarm_callback(void *context)
{
    global_timeout_flag = 1;
    gossip_lerr("Timer expired.\n");
    return;
}


/* immed_unexp_recv_handler()
 * 
 * handles immediate receive events for which the user has not yet
 * provided a buffer.
 *
 * returns 0 on success, -errno on failure
 */
static int immed_unexp_recv_handler(bmi_size_t size,
				    bmi_msg_tag_t msg_tag,
				    method_addr_p map,
				    void *buffer)
{
    method_op_p new_method_op = NULL;

    /* we need an op structure to keep up with this */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (-ENOMEM);
    }
    new_method_op->send_recv = BMI_OP_RECV;
    new_method_op->addr = map;
    new_method_op->actual_size = size;
    new_method_op->expected_size = 0;
    new_method_op->msg_tag = msg_tag;
    new_method_op->error_code = 0;
    new_method_op->mode = GM_MODE_UNEXP;
    new_method_op->buffer = buffer;

    op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP], new_method_op);

    return (0);
}

/* immed_recv_handler()
 * 
 * handles immediate receive events for which the user has not yet
 * provided a buffer.
 *
 * returns 0 on success, -errno on failure
 */
static int immed_recv_handler(bmi_size_t actual_size,
			      bmi_size_t expected_size,
			      bmi_msg_tag_t msg_tag,
			      method_addr_p map,
			      void *buffer)
{
    method_op_p new_method_op = NULL;

    /* we need an op structure to keep up with this */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (-ENOMEM);
    }
    new_method_op->send_recv = BMI_OP_RECV;
    new_method_op->addr = map;
    new_method_op->actual_size = actual_size;
    new_method_op->expected_size = expected_size;
    new_method_op->msg_tag = msg_tag;
    new_method_op->mode = GM_MODE_IMMED;
    new_method_op->buffer = buffer;

    /* queue up until user posts matching receive */
    op_list_add(op_list_array[IND_NEED_RECV_POST], new_method_op);

    return (0);
}


/* high_recv_handler()
 * 
 * handles low priority receive events as detected by gm_do_work()
 *
 * returns 0 on success, -errno on failure
 */
static int high_recv_handler(gm_recv_event_t * poll_event,
			     int fast)
{
    struct ctrl_msg *my_ctrl = NULL;
    int ret = -ENOSYS;
    bmi_op_id_t ctrl_op_id = 0;
    bmi_op_id_t ctrl_peer_op_id = 0;
    bmi_size_t ctrl_actual_size = 0;
    bmi_size_t ctrl_expected_size = 0;
    bmi_msg_tag_t ctrl_tag = 0;
    method_addr_p map = NULL;
    struct op_list_search_key key;
    void *tmp_buffer = NULL;
    method_op_p query_op = NULL;
    struct gm_addr *gm_addr_data = NULL;
    gm_remote_ptr_t remote_ptr = 0;

    gossip_ldebug(BMI_DEBUG_GM, "high_recv_handler() called.\n");
    /* what are the possibilities here? 
     * 1) recv ctrl_ack for a send that we initiated
     * 2) recv ctrl_req from someone who wishes to send to us
     *    a) unexpected
     *    b) immediate
     *    c) rendezvous
     */

    /* NOTE: we trust buffers that we get at this point- if we want to do
     * any validation of messages it should have already been done
     */

    /* NOTE: we *must* return ctrl buffers as quickly as possible.  They
     * must be available for accepting new messages at any time and we
     * cannot let too many remain out of service.
     */

    /* grab the control message out of the event */
    if (fast)
    {
	my_ctrl = (struct ctrl_msg *) (gm_ntohp(poll_event->recv.message) +
				       gm_ntohl(poll_event->recv.length) -
				       sizeof(struct ctrl_msg));
    }
    else
    {
	my_ctrl = (struct ctrl_msg *) (gm_ntohp(poll_event->recv.buffer) +
				       gm_ntohl(poll_event->recv.length) -
				       sizeof(struct ctrl_msg));
    }

    /* see what it is */
    gossip_ldebug(BMI_DEBUG_GM, "Ctrl_type: %d.\n", my_ctrl->ctrl_type);
    if (my_ctrl->ctrl_type == CTRL_ACK_TYPE)
    {
	ctrl_op_id = my_ctrl->u.ack.sender_op_id;
	ctrl_peer_op_id = my_ctrl->u.ack.receiver_op_id;
	remote_ptr = my_ctrl->u.ack.remote_ptr;
	/* Post this control buffer again to be ready for next time  */
	gossip_ldebug(BMI_DEBUG_GM, "Replacing control buffer.\n");
	gm_provide_receive_buffer(local_port,
				  gm_ntohp(poll_event->recv.buffer),
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	/* this is a response to one of our control requests */
	ctrl_ack_handler(ctrl_op_id,
			 gm_ntohs(poll_event->recv.sender_node_id), remote_ptr,
			 ctrl_peer_op_id);
	ret = 0;
    }
    else if (my_ctrl->ctrl_type == CTRL_REQ_TYPE)
    {
	gossip_ldebug(BMI_DEBUG_GM, "Mode: %d.\n", (int) my_ctrl->u.req.mode);
	ctrl_op_id = my_ctrl->u.req.sender_op_id;
	ctrl_actual_size = my_ctrl->u.req.actual_size;
	ctrl_expected_size = my_ctrl->u.req.expected_size;
	ctrl_tag = my_ctrl->u.req.msg_tag;

	/* Post this control buffer again to be ready for next time  */
	gossip_ldebug(BMI_DEBUG_GM, "Replacing control buffer.\n");
	gm_provide_receive_buffer(local_port,
				  gm_ntohp(poll_event->recv.buffer),
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	/* this is a new control request from someone */
	switch (my_ctrl->u.req.mode)
	{
	case GM_MODE_REND:
	    ret = ctrl_req_handler_rend(ctrl_op_id, ctrl_actual_size,
					ctrl_expected_size, ctrl_tag,
					gm_ntohs(poll_event->recv.
						 sender_node_id));
	    break;

	default:
	    gossip_lerr("Error: bad ctrl request!\n");
	    /* TODO: handle critical error here */
	    ret = -ENOSYS;
	}

    }
    else if (my_ctrl->ctrl_type == CTRL_IMMED_TYPE && my_ctrl->u.immed.mode
	     == GM_MODE_IMMED)
    {
	ctrl_actual_size = my_ctrl->u.immed.actual_size;
	ctrl_expected_size = my_ctrl->u.immed.expected_size;
	ctrl_tag = my_ctrl->u.immed.msg_tag;

	/* try to find a matching post from the receiver so that we don't
	 * have to buffer this yet again */
	map = gm_addr_search(&gm_addr_list,
			     gm_ntohs(poll_event->recv.sender_node_id));
	if (!map)
	{
	    /* TODO: handle this error better */
	    gossip_lerr("Error: unknown sender!\n");
	    return (-EPROTO);
	}

	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = map;
	key.method_addr_yes = 1;
	key.expected_size = my_ctrl->u.immed.expected_size;
	key.expected_size_yes = 1;
	key.msg_tag = my_ctrl->u.immed.msg_tag;
	key.msg_tag_yes = 1;
	key.mode_mask = my_ctrl->u.immed.mode;
	key.mode_mask_yes = 1;

	query_op = op_list_search(op_list_array[IND_NEED_CTRL_MATCH], &key);
	if (!query_op)
	{
	    gossip_ldebug(BMI_DEBUG_GM, "Doh! Using extra buffer.\n");
	    tmp_buffer = malloc(my_ctrl->u.immed.actual_size);
	    if (!tmp_buffer)
	    {
		/* TODO: handle error */
		return (-ENOMEM);
	    }
	    if (fast)
	    {
		memcpy(tmp_buffer, gm_ntohp(poll_event->recv.message),
		       my_ctrl->u.immed.actual_size);
	    }
	    else
	    {
		memcpy(tmp_buffer, gm_ntohp(poll_event->recv.buffer),
		       my_ctrl->u.immed.actual_size);
	    }
	    gm_provide_receive_buffer(local_port,
				      gm_ntohp(poll_event->recv.buffer),
				      GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	    ret = immed_recv_handler(ctrl_actual_size, ctrl_expected_size,
				     ctrl_tag, map, tmp_buffer);
	}
	else
	{
	    /* found a match */
	    if (fast)
	    {
		memcpy(query_op->buffer, gm_ntohp(poll_event->recv.message),
		       ctrl_actual_size);
	    }
	    else
	    {
		memcpy(query_op->buffer, gm_ntohp(poll_event->recv.buffer),
		       ctrl_actual_size);
	    }
	    gm_provide_receive_buffer(local_port,
				      gm_ntohp(poll_event->recv.buffer),
				      GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	    op_list_remove(query_op);
	    query_op->actual_size = ctrl_actual_size;
	    query_op->error_code = 0;
	    op_list_add(op_list_array[IND_COMPLETE], query_op);
	    ret = 0;
	}
    }
    else if (my_ctrl->ctrl_type == CTRL_IMMED_TYPE && my_ctrl->u.immed.mode
	     == GM_MODE_UNEXP)
    {
	ctrl_actual_size = my_ctrl->u.immed.actual_size;
	ctrl_tag = my_ctrl->u.immed.msg_tag;
	map = gm_addr_search(&gm_addr_list,
			     gm_ntohs(poll_event->recv.sender_node_id));
	if (!map)
	{
	    /* new address! */
	    map = alloc_gm_method_addr();
	    gm_addr_data = map->method_data;
	    gm_addr_data->node_id = gm_ntohs(poll_event->recv.sender_node_id);
	    /* let the bmi layer know about it */
	    ret = bmi_method_addr_reg_callback(map);
	    if (ret < 0)
	    {
		dealloc_gm_method_addr(map);
		return (ret);
	    }
	    /* keep up with it ourselves also */
	    gm_addr_add(&gm_addr_list, map);
	}

	tmp_buffer = malloc(my_ctrl->u.immed.actual_size);
	if (!tmp_buffer)
	{
	    /* TODO: handle error */
	    return (-ENOMEM);
	}
	if (fast)
	{
	    memcpy(tmp_buffer, gm_ntohp(poll_event->recv.message),
		   my_ctrl->u.immed.actual_size);
	}
	else
	{
	    memcpy(tmp_buffer, gm_ntohp(poll_event->recv.buffer),
		   my_ctrl->u.immed.actual_size);
	}
	gm_provide_receive_buffer(local_port,
				  gm_ntohp(poll_event->recv.buffer),
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	ret =
	    immed_unexp_recv_handler(ctrl_actual_size, ctrl_tag, map,
				     tmp_buffer);
    }
    else if (my_ctrl->ctrl_type == CTRL_PUT_TYPE)
    {
	ctrl_op_id = my_ctrl->u.put.receiver_op_id;
	gm_provide_receive_buffer(local_port,
				  gm_ntohp(poll_event->recv.buffer),
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	put_recv_handler(ctrl_op_id);
	ret = 0;
    }

    return (ret);
}

/* put_recv_handler()
 * 
 * handles announcements from sender that a directed send has completed
 *
 * returns 0 on success, -errno on failure
 */
static void put_recv_handler(bmi_op_id_t ctrl_op_id)
{
    method_op_p query_op = NULL;
    struct gm_op *gm_op_data = NULL;

    /* find the matching operation */
    query_op = id_gen_fast_lookup(ctrl_op_id);
    op_list_remove(query_op);
    gm_op_data = query_op->method_data;

    /* let go of the buffer if we need to */
    if (gm_op_data->buffer_status == GM_BUF_METH_REG)
    {
#ifdef ENABLE_GM_REGCACHE
	bmi_gm_unuse_interval((unsigned long) query_op->buffer,
			      query_op->actual_size);
#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
	gm_deregister_memory(local_port, query_op->buffer,
			     query_op->actual_size);
#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
	memcpy(query_op->buffer, gm_op_data->tmp_xfer_buffer,
	       query_op->actual_size);
	gm_dma_free(local_port, gm_op_data->tmp_xfer_buffer);
#endif /* ENABLE_GM_BUFCOPY */
#ifdef ENABLE_GM_BUFPOOL
	memcpy(query_op->buffer, gm_op_data->tmp_xfer_buffer,
	       query_op->actual_size);
	bmi_gm_bufferpool_put(io_pool, gm_op_data->tmp_xfer_buffer);
#endif /* ENABLE_GM_BUFPOOL */
    }

    /* done */
    query_op->error_code = 0;
    op_list_add(op_list_array[IND_COMPLETE], query_op);
    return;
}


/* ctrl_ack_handler()
 * 
 * handles control message acknowledgements (for sender)
 *
 * returns 0 on success, -errno on failure
 */
static void ctrl_ack_handler(bmi_op_id_t ctrl_op_id,
			     unsigned int node_id,
			     gm_remote_ptr_t remote_ptr,
			     bmi_op_id_t peer_op_id)
{
    method_op_p query_op = NULL;
    struct gm_op *gm_op_data = NULL;
    /* we got a control ack- what now?
     * 1) figure out which op this matches
     * 2) see if we have a send token
     *    a) if not, queue up
     * 3) send data
     */

    /* find the matching operation */
    query_op = id_gen_fast_lookup(ctrl_op_id);
    op_list_remove(query_op);
    gm_op_data = query_op->method_data;
    gm_op_data->remote_ptr = remote_ptr;
    gm_op_data->peer_op_id = peer_op_id;

    /* make sure that we are not bypassing any operations that
     * are stalled waiting on tokens
     */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_LOW]))
    {
	gossip_ldebug(BMI_DEBUG_GM, "Stalling behind stalled message.\n");
	op_list_add(op_list_array[IND_NEED_SEND_TOK_LOW], query_op);
	return;
    }

    if (!gm_alloc_send_token(local_port, GM_LOW_PRIORITY))
    {
	/* we don't have a send token */
	op_list_add(op_list_array[IND_NEED_SEND_TOK_LOW], query_op);
	return;
    }

#ifdef ENABLE_GM_BUFPOOL
    if (bmi_gm_bufferpool_empty(io_pool))
    {
	gm_free_send_token(local_port, GM_LOW_PRIORITY);
	op_list_add(op_list_array[IND_NEED_SEND_TOK_LOW], query_op);
	return;
    }
#endif /* ENABLE_GM_BUFPOOL */

    send_data_buffer(query_op);
    return;
}


/* send_data_buffer()
 *
 * sends the actual message data for an operation.  Assumes that a low
 * priority send token has already been acquired
 *
 * returns 0 on success, -errno on failure.
 */
static void send_data_buffer(method_op_p mop)
{
    struct gm_addr *gm_addr_data = mop->addr->method_data;
    struct gm_op *gm_op_data = mop->method_data;
    bmi_size_t pinned_size = 0;

    /* send actual buffer */
    /* NOTE: the ctrl message communication leading up to this send allows 
     * us to use the "actual size" field here rather than the expected 
     * size when transfering data
     */
    if (gm_op_data->buffer_status == GM_BUF_METH_REG)
    {
#ifdef ENABLE_GM_REGCACHE
	pinned_size = bmi_gm_use_interval((unsigned long) mop->buffer,
					  mop->actual_size);
	if (pinned_size != mop->actual_size)
	{
	    gossip_lerr
		("Error: could not register memory, wanted: %d, got: %d\n",
		 (int) mop->actual_size, (int) pinned_size);
	    bmi_gm_unuse_interval((unsigned long) mop->buffer, pinned_size);
	    /* TODO: handle this better */
	    mop->error_code = -ENOMEM;
	    op_list_add(op_list_array[IND_COMPLETE], mop);
	    return;
	}
#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
	pinned_size = mop->actual_size;
	if (gm_register_memory(local_port, mop->buffer,
			       mop->actual_size) != GM_SUCCESS)
	{
	    gossip_lerr("Error: could not register memory.\n");
	    /* TODO: handle this better */
	    mop->error_code = -ENOMEM;
	    op_list_add(op_list_array[IND_COMPLETE], mop);
	    return;
	}
#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
	pinned_size = mop->actual_size;
	gm_op_data->tmp_xfer_buffer = gm_dma_malloc(local_port,
						    (unsigned long) mop->
						    actual_size);
	if (!gm_op_data->tmp_xfer_buffer)
	{
	    gossip_lerr("Error: gm_dma_malloc().\n");
	    mop->error_code = -ENOMEM;
	    op_list_add(op_list_array[IND_COMPLETE], mop);
	    return;
	}
	memcpy(gm_op_data->tmp_xfer_buffer, mop->buffer, mop->actual_size);
	mop->buffer = gm_op_data->tmp_xfer_buffer;
#endif /* ENABLE_GM_BUFCOPY */
#ifdef ENABLE_GM_BUFPOOL
	pinned_size = mop->actual_size;
	gm_op_data->tmp_xfer_buffer = bmi_gm_bufferpool_get(io_pool);
	memcpy(gm_op_data->tmp_xfer_buffer, mop->buffer, mop->actual_size);
	mop->buffer = gm_op_data->tmp_xfer_buffer;
#endif /* ENABLE_GM_BUFPOOL */

    }
    gm_directed_send_with_callback(local_port, mop->buffer,
				   gm_op_data->remote_ptr,
				   mop->actual_size, GM_LOW_PRIORITY,
				   gm_addr_data->node_id, BMI_GM_PORT_NUM,
				   data_send_callback, mop);

    op_list_add(op_list_array[IND_SENDING], mop);

    return;
}


/* prepare_for_recv()
 *
 * provides a receive buffer and sends a control ack to allow the sender
 * to continue.  Assumes that the send and recv tokens have already been
 * acquired.
 *
 * returns 0 on success, -errno on failure
 */
static void prepare_for_recv(method_op_p mop)
{
    struct gm_addr *gm_addr_data = mop->addr->method_data;
    struct gm_op *gm_op_data = mop->method_data;
    struct ctrl_msg *new_ctrl = NULL;
    bmi_size_t pinned_size = 0;

    /* prepare a ctrl message response */
    new_ctrl = bmi_gm_bufferpool_get(ctrl_send_pool);

    new_ctrl->ctrl_type = CTRL_ACK_TYPE;
    new_ctrl->u.ack.sender_op_id = gm_op_data->peer_op_id;
    new_ctrl->u.ack.receiver_op_id = mop->op_id;
    /* doing this to avoid a warning about type size mismatch */
    new_ctrl->u.ack.remote_ptr = 0;
    new_ctrl->u.ack.remote_ptr += (unsigned long) mop->buffer;

    /* keep up with this buffer in the op structure */
    gm_op_data->freeable_ctrl_buffer = new_ctrl;

    /* pinn the buffer to be ready for the data transfer */
    if (gm_op_data->buffer_status == GM_BUF_METH_REG)
    {
#ifdef ENABLE_GM_REGCACHE
	pinned_size = bmi_gm_use_interval((unsigned long) mop->buffer,
					  mop->actual_size);
	if (pinned_size != mop->actual_size)
	{
	    gossip_lerr
		("Error: could not register memory, wanted: %d, got: %d\n",
		 (int) mop->actual_size, (int) pinned_size);
	    /* TODO: handle this error better */
	    mop->error_code = -ENOMEM;
	    op_list_add(op_list_array[IND_COMPLETE], mop);
	    return;
	}
#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
	pinned_size = mop->actual_size;
	if (gm_register_memory(local_port, mop->buffer,
			       mop->actual_size) != GM_SUCCESS)
	{
	    gossip_lerr("Error: could not register memory.\n");
	    /* TODO: handle this better */
	    mop->error_code = -ENOMEM;
	    op_list_add(op_list_array[IND_COMPLETE], mop);
	    return;
	}
#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
	pinned_size = mop->actual_size;
	gm_op_data->tmp_xfer_buffer = gm_dma_malloc(local_port,
						    (unsigned long) mop->
						    actual_size);
	if (!gm_op_data->tmp_xfer_buffer)
	{
	    gossip_lerr("Error: gm_dma_malloc().\n");
	    /* TODO: handle this better */
	    mop->error_code = -ENOMEM;
	    op_list_add(op_list_array[IND_COMPLETE], mop);
	    return;
	}
	new_ctrl->u.ack.remote_ptr = 0;
	new_ctrl->u.ack.remote_ptr +=
	    (unsigned long) gm_op_data->tmp_xfer_buffer;
#endif /* ENABLE_GM_BUFCOPY */
#ifdef ENABLE_GM_BUFPOOL
	pinned_size = mop->actual_size;
	gm_op_data->tmp_xfer_buffer = bmi_gm_bufferpool_get(io_pool);
	new_ctrl->u.ack.remote_ptr = 0;
	new_ctrl->u.ack.remote_ptr +=
	    (unsigned long) gm_op_data->tmp_xfer_buffer;
#endif /* ENABLE_GM_BUFPOOL */

    }

    /* send ctrl message */
    gossip_ldebug(BMI_DEBUG_GM, "Sending ctrl ack.\n");
    gm_send_to_peer_with_callback(local_port, new_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  ctrl_ack_callback, mop);

    /* queue up op */
    op_list_add(op_list_array[IND_SENDING], mop);
    return;

}


/* ctrl_ack_callback()
 *
 * callback function triggered on the completion of a control ack
 * message send.
 *
 * no return value
 */
static void ctrl_ack_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status)
{
    /* the context is our operation */
    method_op_p my_op = context;
    struct gm_op *gm_op_data = NULL;

    gossip_ldebug(BMI_DEBUG_GM, "ctrl_ack_callback() called.\n");

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    /* free up ctrl message buffer */
    gm_op_data = my_op->method_data;
    bmi_gm_bufferpool_put(ctrl_send_pool, gm_op_data->freeable_ctrl_buffer);

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled.\n");
	op_list_remove(my_op);
	my_op->error_code = -ETIMEDOUT;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_lerr("Error value: %d\n", (int) status);
	op_list_remove(my_op);
	my_op->error_code = -ETIMEDOUT;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* golden! */

    /* we need to drive the state of this operation to the next phase, in
     * which we actually receive the message data
     */
    op_list_remove(my_op);
    op_list_add(op_list_array[IND_RECVING], my_op);

    /* etc. etc. */
    return;
}


/* data_send_callback()
 *
 * callback function triggered on the completion of a data
 * message send.
 *
 * no return value
 */
static void data_send_callback(struct gm_port *port,
			       void *context,
			       gm_status_t status)
{
    /* the context is our operation */
    method_op_p my_op = context;
    struct gm_op *gm_op_data = my_op->method_data;

    /* give back a send token */
    gm_free_send_token(local_port, GM_LOW_PRIORITY);

    if (gm_op_data->buffer_status == GM_BUF_METH_REG)
    {
#ifdef ENABLE_GM_REGCACHE
	bmi_gm_unuse_interval((unsigned long) my_op->buffer,
			      my_op->actual_size);
#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
	gm_deregister_memory(local_port, my_op->buffer, my_op->actual_size);
#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
	gm_dma_free(local_port, my_op->buffer);
#endif /* ENABLE_GM_BUFCOPY */
#ifdef ENABLE_GM_BUFPOOL
	bmi_gm_bufferpool_put(io_pool, my_op->buffer);
#endif /* ENABLE_GM_BUFPOOL */
    }

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled.\n");
	op_list_remove(my_op);
	my_op->error_code = -ETIMEDOUT;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_lerr("Error: GM return value: %d.\n", (int) status);
	op_list_remove(my_op);
	my_op->error_code = -EPROTO;
	op_list_add(op_list_array[IND_COMPLETE], my_op);
	return;
    }

    op_list_remove(my_op);
    /* we must now let the other side know that the data has arrived */
    if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
    {
	initiate_put_announcement(my_op);
    }
    else
    {
	/* we have to wait for a token to become available */
	op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_PUT], my_op);
    }
    return;
}

/* ctrl_req_handler_rend()
 * 
 * handles incoming control message requests for rendezvous messages
 *
 * returns 0 on success, -errno on failure
 */
static int ctrl_req_handler_rend(bmi_op_id_t ctrl_op_id,
				 bmi_size_t ctrl_actual_size,
				 bmi_size_t ctrl_expected_size,
				 bmi_msg_tag_t ctrl_tag,
				 unsigned int node_id)
{
    method_addr_p map = NULL;
    struct gm_addr *gm_addr_data = NULL;
    struct gm_op *gm_op_data = NULL;
    method_op_p active_method_op = NULL;
    int ret = -1;
    struct op_list_search_key key;

    gossip_ldebug(BMI_DEBUG_GM, "ctrl_req_handler_rend() called.\n");

    /* someone wants to send a buffer in rendezvous mode 
     * - look to see if the matching receive has been posted
     *      1)      - if so, provide receive buffer from posted op
     *    - send an ack with buffer pointer
     *              - queue up
     * 2) - if not, create a method op
     *      - fill in info
     *      - queue up as need receive post
     */

    map = gm_addr_search(&gm_addr_list, node_id);
    if (!map)
    {
	/* where did this come from?! */
	/* TODO: handle this error condition */
	gossip_lerr("Error: ctrl message from unknown host!!!\n");
	return (-EPROTO);
    }
    gm_addr_data = map->method_data;

    /* see if this operation has already begun... */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;
    key.expected_size = ctrl_expected_size;
    key.expected_size_yes = 1;
    key.msg_tag = ctrl_tag;
    key.msg_tag_yes = 1;

    active_method_op = op_list_search(op_list_array[IND_NEED_CTRL_MATCH], &key);
    if (active_method_op)
    {
	op_list_remove(active_method_op);

	/* store remote side's op_id */
	gm_op_data = active_method_op->method_data;
	gm_op_data->peer_op_id = ctrl_op_id;

	/* store actual size of transfer */
	active_method_op->actual_size = ctrl_actual_size;

	/* now we need one send token to send ctrl ack */
	if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
	{
	    /* got it! */
#ifdef ENABLE_GM_BUFPOOL
	    if (bmi_gm_bufferpool_empty(io_pool))
	    {
		gm_free_send_token(local_port, GM_HIGH_PRIORITY);
		op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK],
			    active_method_op);
		ret = 0;
	    }
	    else
	    {
#endif /* ENABLE_GM_BUFPOOL */
		prepare_for_recv(active_method_op);
		ret = 0;
#ifdef ENABLE_GM_BUFPOOL
	    }
#endif /* ENABLE_GM_BUFPOOL */
	}
	else
	{
	    /* we don't have enough tokens */
	    op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK],
			active_method_op);
	    ret = 0;
	}
    }
    else
    {
	/* post has _not_ occurred, queue up and wait for post */
	/* we need an op structure to keep up with this */
	active_method_op = alloc_gm_method_op();
	if (!active_method_op)
	{
	    return (-ENOMEM);
	}
	active_method_op->send_recv = BMI_OP_RECV;
	active_method_op->addr = map;
	active_method_op->actual_size = ctrl_actual_size;
	active_method_op->expected_size = ctrl_expected_size;
	active_method_op->msg_tag = ctrl_tag;
	active_method_op->mode = GM_MODE_REND;
	gm_op_data = active_method_op->method_data;
	gm_op_data->peer_op_id = ctrl_op_id;

	op_list_add(op_list_array[IND_NEED_RECV_POST], active_method_op);
	ret = 0;
    }
    return (ret);

}


/* gm_generic_testwait()
 * 
 * Checks to see if a particular message has completed.
 *
 * returns 0 on success, -errno on failure
 */
static int gm_generic_testwait(bmi_op_id_t id,
			       int *outcount,
			       bmi_error_code_t * error_code,
			       bmi_size_t * actual_size,
			       void **user_ptr,
			       int wait_time)
{
    int ret = -1;

    /* do some ``real work'' here */
    ret = gm_do_work(wait_time);
    if (ret < 0)
    {
	return (ret);
    }

    ret = test_done(id, outcount, error_code, actual_size, user_ptr);

    return (ret);

}


/* gm_generic_testwaitsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 *
 * returns 0 on success, -errno on failure
 */
static int gm_generic_testwaitsome(int incount,
				   bmi_op_id_t * id_array,
				   int *outcount,
				   int *index_array,
				   bmi_error_code_t * error_code_array,
				   bmi_size_t * actual_size_array,
				   void **user_ptr_array,
				   int wait_time)
{
    int ret = -1;

    /* do some ``real work'' here */
    ret = gm_do_work(wait_time);
    if (ret < 0)
    {
	return (ret);
    }

    ret = test_done_some(incount, id_array, outcount, index_array,
			 error_code_array, actual_size_array, user_ptr_array);

    return (ret);
}

/* gm_generic_testwaitunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
 */
static int gm_generic_testwaitunexpected(int incount,
					 int *outcount,
					 struct method_unexpected_info *info,
					 int wait_time)
{
    int ret = -1;

    /* do some ``real work'' here */
    ret = gm_do_work(wait_time);
    if (ret < 0)
    {
	return (ret);
    }

    /* check again now that we have done some work to see if any
     * unexpected messages completed 
     */
    ret = test_done_unexpected(incount, outcount, info);

    return (ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */

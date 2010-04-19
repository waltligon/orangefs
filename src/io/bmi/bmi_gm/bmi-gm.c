/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* GM implementation of a BMI method */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <sys/time.h>
#include <gm.h>

#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "bmi-gm-addressing.h"
#include "op-list.h"
#include "bmi-gm-addr-list.h"
#include "gossip.h"
#include "quicklist.h"
#include "bmi-gm-bufferpool.h"
#include "pvfs2-config.h"
#include "id-generator.h"
#include "gen-locks.h"
#ifdef ENABLE_GM_REGCACHE
#include "bmi-gm-regcache.h"
#endif
#include "pvfs2-debug.h"

static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;
static unsigned int bmi_gm_reserved_ports[BMI_GM_MAX_PORTS] = 
    {1,1,0,1,0,0,0,0};

/* how long to wait on cancelled rendezvous receive operations to finish 
 * before reusing the buffer for something else
 */
/* 15 minutes */
#define PINT_CANCELLED_REND_RECLAIM_TIMEOUT (60*15)

/* function prototypes */
int BMI_gm_initialize(bmi_method_addr_p listen_addr,
		      int method_id,
		      int init_flags);
int BMI_gm_finalize(void);
int BMI_gm_set_info(int option,
		    void *inout_parameter);
int BMI_gm_get_info(int option,
		    void *inout_parameter);
void *BMI_gm_memalloc(bmi_size_t size,
		      enum bmi_op_type send_recv);
int BMI_gm_memfree(void *buffer,
		   bmi_size_t size,
		   enum bmi_op_type send_recv);
int BMI_gm_unexpected_free(void *buffer);
int BMI_gm_post_send_list(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_size,
    enum bmi_buffer_type buffer_type,
    bmi_msg_tag_t tag,
    void *user_ptr,
    bmi_context_id context_id,
    PVFS_hint hints);
int BMI_gm_post_sendunexpected_list(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_size,
    enum bmi_buffer_type buffer_type,
    bmi_msg_tag_t tag,
    uint8_t class,
    void *user_ptr,
    bmi_context_id context_id,
    PVFS_hint hints);
int BMI_gm_post_recv_list(bmi_op_id_t * id,
    bmi_method_addr_p src,
    void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_expected_size,
    bmi_size_t * total_actual_size,
    enum bmi_buffer_type buffer_type,
    bmi_msg_tag_t tag,
    void *user_ptr,
    bmi_context_id context_id,
    PVFS_hint hints);
int BMI_gm_test(bmi_op_id_t id,
		int *outcount,
		bmi_error_code_t * error_code,
		bmi_size_t * actual_size,
		void **user_ptr,
		int max_idle_time_ms,
		bmi_context_id context_id);
int BMI_gm_testsome(int incount,
		    bmi_op_id_t * id_array,
		    int *outcount,
		    int *index_array,
		    bmi_error_code_t * error_code_array,
		    bmi_size_t * actual_size_array,
		    void **user_ptr_array,
		    int max_idle_time_ms,
		    bmi_context_id context_id);
int BMI_gm_testcontext(int incount,
    bmi_op_id_t * out_id_array,
    int *outcount,
    bmi_error_code_t * error_code_array,
    bmi_size_t * actual_size_array,
    void **user_ptr_array,
    int max_idle_time_ms,
    bmi_context_id context_id);
int BMI_gm_testunexpected(int incount,
			  int *outcount,
			  struct bmi_method_unexpected_info *info,
                          uint8_t class,
			  int max_idle_time_ms);
bmi_method_addr_p BMI_gm_method_addr_lookup(const char *id_string);
int BMI_gm_open_context(bmi_context_id context_id);
void BMI_gm_close_context(bmi_context_id context_id);
int BMI_gm_cancel(bmi_op_id_t id, bmi_context_id context_id);
int BMI_gm_get_unexp_maxsize(void);

char BMI_gm_method_name[] = "bmi_gm";

/* exported method interface */
const struct bmi_method_ops bmi_gm_ops = {
    .method_name = BMI_gm_method_name,
    .flags = 0,
    .initialize = BMI_gm_initialize,
    .finalize = BMI_gm_finalize,
    .set_info = BMI_gm_set_info,
    .get_info = BMI_gm_get_info,
    .memalloc = BMI_gm_memalloc,
    .memfree = BMI_gm_memfree,
    .unexpected_free = BMI_gm_unexpected_free,
    .test = BMI_gm_test,
    .testsome = BMI_gm_testsome,
    .testcontext = BMI_gm_testcontext,
    .testunexpected = BMI_gm_testunexpected,
    .method_addr_lookup = BMI_gm_method_addr_lookup,
    .post_send_list = BMI_gm_post_send_list,
    .post_recv_list = BMI_gm_post_recv_list,
    .post_sendunexpected_list = BMI_gm_post_sendunexpected_list,
    .open_context = BMI_gm_open_context,
    .close_context = BMI_gm_close_context,
    .cancel = BMI_gm_cancel,
    .rev_lookup_unexpected = NULL,
    .query_addr_range = NULL,
};

/* module parameters */
static struct
{
    int method_flags;
    int method_id;
    bmi_method_addr_p listen_addr;
} gm_method_params;

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
    IND_COMPLETE_RECV_UNEXP = 8,
    IND_CANCELLED_REND = 9,
};

static int cancelled_rend_count = 0;

/* buffer status indicator */
enum
{
    GM_BUF_USER_ALLOC = 1,
    GM_BUF_METH_ALLOC = 2,
    GM_BUF_METH_REG = 3
};

/* internal operations lists */
static op_list_p op_list_array[NUM_INDICES] = { NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL
};

static op_list_p completion_array[BMI_MAX_CONTEXTS] = {NULL};

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
    CTRL_PUT_TYPE = 4,
    CTRL_UNEXP_TYPE = 5
};

/* control messages */
struct ctrl_req
{
    bmi_size_t actual_size;	/* size of message we want to send */
    bmi_msg_tag_t msg_tag;	/* message tag */
    bmi_op_id_t sender_op_id;	/* used for matching ctrl's */
    /* will probably be padded later for immediate messages */
};
struct ctrl_ack
{
    PVFS_id_gen_t sender_op_id;	/* used for matching ctrl's */
    bmi_op_id_t receiver_op_id;	/* used for matching ctrl's */
    gm_remote_ptr_t remote_ptr;	/* peer address to target */
};
struct ctrl_immed
{
    bmi_msg_tag_t msg_tag;	/* message tag */
    int32_t actual_size;
    uint8_t class;
};
struct ctrl_put
{
    bmi_op_id_t receiver_op_id;	/* remote op that this completes */
};

struct ctrl_msg
{
    uint32_t ctrl_type;
    uint32_t magic_nr;
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
    GM_MODE_REND_LIMIT = 524288,	/* 512K */
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
    int buffer_status;
    gm_remote_ptr_t remote_ptr;
    void *tmp_xfer_buffer;
    uint8_t complete; /* indicates when operation is completed */
    uint8_t list_flag; /* indicates this is a list operation */
    uint8_t cancelled; /* indicates operation has been cancelled */
    long cancelled_tv_sec; /* timestamp for when the op was cancelled */
};

/* the local port that we are communicating on */
static struct gm_port *local_port = NULL;
/* and what we will call it */
static char BMI_GM_PORT_NAME[] = "pvfs_gm";

/* keep up with recv tokens */
static int recv_token_count_high = 0;

static int global_timeout_flag = 0;

static char gm_host_name[GM_MAX_HOST_NAME_LEN];

/* internal gm method address list */
QLIST_HEAD(gm_addr_list);

/* static buffer pools */
static struct bufferpool *ctrl_send_pool = NULL;
#ifdef ENABLE_GM_BUFPOOL
static struct bufferpool *io_pool = NULL;
#endif /* ENABLE_GM_BUFPOOL */

/* internal utility functions */
static bmi_method_addr_p alloc_gm_method_addr(void);
static void dealloc_gm_method_addr(bmi_method_addr_p map);
static int gm_post_send_check_resource(struct method_op* mop);
static int gm_post_send_build_op(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *buffer,
    bmi_size_t size,
    bmi_msg_tag_t tag,
    int mode,
    int buffer_status,
    void *user_ptr, bmi_context_id context_id);
static int gm_post_send_build_op_list(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_size,
    bmi_msg_tag_t tag,
    int mode,
    int buffer_status,
    void *user_ptr, bmi_context_id context_id);
static void ctrl_req_callback(struct gm_port *port,
			      void *context,
			      gm_status_t status);
void dealloc_gm_method_op(method_op_p op_p);
static method_op_p alloc_gm_method_op(void);
static int ctrl_recv_pool_init(int pool_size);
static int gm_do_work(int wait_time);
static void delayed_token_sweep(void);
static int receive_cycle(int timeout);
void alarm_callback(void *context);
static int recv_event_handler(gm_recv_event_t * poll_event,
			     int fast);
static void ctrl_ack_handler(bmi_op_id_t ctrl_op_id,
			     unsigned int node_id,
			     gm_remote_ptr_t remote_ptr,
			     bmi_op_id_t peer_op_id);
static int ctrl_req_handler_rend(bmi_op_id_t ctrl_op_id,
				 bmi_size_t ctrl_actual_size,
				 bmi_msg_tag_t ctrl_tag,
				 unsigned int node_id,
                                 unsigned int port_id);
static int immed_unexp_recv_handler(bmi_size_t size,
				    bmi_msg_tag_t msg_tag,
				    bmi_method_addr_p map,
				    void *buffer,
                                    uint8_t class);
static int immed_recv_handler(bmi_size_t actual_size,
			      bmi_msg_tag_t msg_tag,
			      bmi_method_addr_p map,
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
static void initiate_send_rend(method_op_p mop);
static void initiate_send_immed(method_op_p mop);
static void initiate_put_announcement(method_op_p mop);
static void send_data_buffer(method_op_p mop);
static void prepare_for_recv(method_op_p mop);
static void setup_send_data_buffer(method_op_p mop);
static void setup_send_data_buffer_list(method_op_p mop);
static void prepare_for_recv_list(method_op_p mop);
static void reclaim_cancelled_io_buffers(void);
static int io_buffers_exhausted(void);

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
int BMI_gm_initialize(bmi_method_addr_p listen_addr,
		      int method_id,
		      int init_flags)
{
    gm_status_t gm_ret;
    unsigned int rec_tokens = 0;
    unsigned int send_tokens = 0;
    int ret = -1;
    unsigned int gm_host_id = 0;
    unsigned int min_message_size = 0;
    int i = 0;
    int tmp_errno = 0;
    struct gm_addr *gm_addr_data = NULL;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Initializing GM module.\n");

    /* check args */
    if ((init_flags & BMI_INIT_SERVER) && !listen_addr)
    {
	gossip_lerr("Error: bad parameters to GM module.\n");
	return (bmi_gm_errno_to_pvfs(-EINVAL));
    }

    gen_mutex_lock(&interface_mutex);

    GM_IMMED_LENGTH = gm_max_length_for_size(GM_IMMED_SIZE) -
	sizeof(struct ctrl_msg);

    /* zero out our parameter structure and fill it in */
    memset(&gm_method_params, 0, sizeof(gm_method_params));
    gm_method_params.method_id = method_id;
    gm_method_params.method_flags = init_flags;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Setting up GM operation lists.\n");
    /* set up the operation lists */
    for (i = 0; i < NUM_INDICES; i++)
    {
	op_list_array[i] = op_list_new();
	if (!op_list_array[i])
	{
	    tmp_errno = bmi_gm_errno_to_pvfs(-ENOMEM);
	    goto gm_initialize_failure;
	}
    }

    /* start up gm */
    gm_ret = gm_init();
    if (gm_ret != GM_SUCCESS)
    {
	gossip_lerr("Error: gm_init() failure.\n");
	gen_mutex_unlock(&interface_mutex);
	return (bmi_gm_errno_to_pvfs(-EPROTO));
    }

    if(init_flags & BMI_INIT_SERVER)
    {
	/* hang on to our local listening address if needed */
	gm_method_params.listen_addr = listen_addr;

	/* open our local port for communication */
	gm_addr_data = listen_addr->method_data;
	gm_ret = gm_open(&local_port, BMI_GM_UNIT_NUM, 
	    gm_addr_data->port_id, BMI_GM_PORT_NAME, GM_API_VERSION_1_3);
	if (gm_ret != GM_SUCCESS)
	{
	    ret = bmi_gm_errno_to_pvfs(-EPROTO);
	    goto gm_initialize_failure;
	}
    }
    else
    {
	/* we must cycle through and find an open port */
	for(i=0; i<BMI_GM_MAX_PORTS; i++)
	{
	    if(!bmi_gm_reserved_ports[i])
	    {
		gm_ret = gm_open(&local_port, BMI_GM_UNIT_NUM, 
		    (unsigned int)i, BMI_GM_PORT_NAME, GM_API_VERSION_1_3);
		if(gm_ret == GM_SUCCESS)
		    break;
	    }
	}
	if(i >= BMI_GM_MAX_PORTS)
	{
	    gossip_lerr("Error: failed to find available GM port.\n");
	    ret = bmi_gm_errno_to_pvfs(-EPROTO);
	    goto gm_initialize_failure;
	}
	gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Using port number %i.\n", i);
    }

    rec_tokens = gm_num_receive_tokens(local_port);
    send_tokens = gm_num_send_tokens(local_port);
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Available recieve tokens: %u.\n", rec_tokens);
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Available send tokens: %u.\n", send_tokens);

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
	tmp_errno = bmi_gm_errno_to_pvfs(ret);
	goto gm_initialize_failure;
    }

#ifdef ENABLE_GM_REGCACHE
    /* initialize the memory registration cache */
    ret = bmi_gm_regcache_init(local_port);
    if (ret < 0)
    {
	tmp_errno = bmi_gm_errno_to_pvfs(ret);
	goto gm_initialize_failure;
    }
#endif /* ENABLE_GM_REGCACHE */

    /* intialize the control buffer cache */
    ctrl_send_pool = bmi_gm_bufferpool_init(local_port, send_tokens,
					    GM_CTRL_LENGTH);
    if (!ctrl_send_pool)
    {
	tmp_errno = bmi_gm_errno_to_pvfs(ret);
	goto gm_initialize_failure;
    }

#ifdef ENABLE_GM_BUFPOOL
    /* initialize the io buffer cache */
    io_pool = bmi_gm_bufferpool_init(local_port, 32, GM_MODE_REND_LIMIT);
    if (!io_pool)
    {
	tmp_errno = bmi_gm_errno_to_pvfs(ret);
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
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "GM Interface host name: %s.\n", gm_host_name);
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "GM Interface node id: %u.\n", gm_host_id);
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "GM Interface min msg size: %u.\n",
		  min_message_size);
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "GM immediate mode limit: %d.\n",
		  GM_MODE_IMMED_LIMIT);

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "GM module successfully initialized.\n");
    gen_mutex_unlock(&interface_mutex);
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
    gen_mutex_unlock(&interface_mutex);
    return (bmi_gm_errno_to_pvfs(ret));
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

    gen_mutex_lock(&interface_mutex);
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
    gen_mutex_unlock(&interface_mutex);
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "GM module finalized.\n");
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
bmi_method_addr_p BMI_gm_method_addr_lookup(const char *id_string)
{
    char *gm_string = NULL;
    bmi_method_addr_p new_addr = NULL;
    struct gm_addr *gm_data = NULL;
    char local_tag[] = "NULL";
    char* delim = NULL;
    int ret = -1;

    gm_string = string_key("gm", id_string);
    if (!gm_string)
    {
	/* the string doesn't even have our info */
	gossip_lerr("Error: NULL id_string.\n");
	return (NULL);
    }

    /* start breaking up the method information */
    /* for gm, it is hostname:portnumber */
    if((delim = index(gm_string, ':')) == NULL)
    {
	gossip_lerr("Error: malformed gm address.\n");
	free(gm_string);
	return(NULL);
    }

    /* looks ok, so let's build the method addr structure */
    new_addr = alloc_gm_method_addr();
    if (!new_addr)
    {
	gossip_lerr("Error: unable to allocate GM method address.\n");
	free(gm_string);
	return (NULL);
    }
    gm_data = new_addr->method_data;

    /* pull off the port first */
    ret = sscanf((delim+1), "%u", &(gm_data->port_id));
    if(ret != 1)
    {
	gossip_lerr("Error: malformed gm address.\n");
	dealloc_gm_method_addr(new_addr);
	free(gm_string);
	return(NULL);
    }

    /* now chop off the port information and parse the rest */
    *delim = '\0';

    gen_mutex_lock(&interface_mutex);
    if (strncmp(gm_string, local_tag, strlen(local_tag)) == 0)
    {
	/* is a local address */
	;
    }
    else if(local_port != NULL)
    {
	gm_data->node_id = gm_host_name_to_node_id(local_port, gm_string);
	if (gm_data->node_id == GM_NO_SUCH_NODE_ID)
	{
	    gossip_lerr("Error: gm_host_name_to_node_id() failure for: %s.\n",
		gm_string);
	    bmi_dealloc_method_addr(new_addr);
	    free(gm_string);
	    gen_mutex_unlock(&interface_mutex);
	    return (NULL);
	}
    }

    free(gm_string);
    /* keep up with the address here */
    gm_addr_add(&gm_addr_list, new_addr);
    gen_mutex_unlock(&interface_mutex);
    return (new_addr);
}


/* BMI_gm_memalloc()
 * 
 * Allocates memory that can be used in native mode by gm.
 *
 * returns 0 on success, -errno on failure
 */
void *BMI_gm_memalloc(bmi_size_t size,
		      enum bmi_op_type send_recv)
{
    /* NOTE: In the send case, we allocate a little bit of extra memory
     * at the END of the buffer to use for message trailers.  We stick it
     * at the back in order to easily preserve alignment
     * NOTE: We are being pretty trustful of the caller.  Checks are done
     * (hopefully :) at the BMI level before we get here.
     */
    void *new_buffer = NULL;

    gen_mutex_lock(&interface_mutex);
    if (send_recv == BMI_RECV)
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
    else if (send_recv == BMI_SEND)
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

    gen_mutex_unlock(&interface_mutex);
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
		   enum bmi_op_type send_recv)
{
    gen_mutex_lock(&interface_mutex);
    if (send_recv == BMI_RECV)
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
    else if (send_recv == BMI_SEND)
    {
	gm_dma_free(local_port, buffer);
    }
    else
    {
	gen_mutex_unlock(&interface_mutex);
	return (bmi_gm_errno_to_pvfs(-EINVAL));
    }

    buffer = NULL;
    gen_mutex_unlock(&interface_mutex);
    return (0);
}

int BMI_gm_unexpected_free(void *buffer)
{
    if (buffer)
    {
        free(buffer);
    }
    return 0;
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
    switch(option)
    {
        case BMI_TCP_BUFFER_SEND_SIZE:
        case BMI_TCP_BUFFER_RECEIVE_SIZE:
        case BMI_FORCEFUL_CANCEL_MODE:
        case BMI_DROP_ADDR:
#ifdef USE_TRUSTED
        case BMI_TRUSTED_CONNECTION:
#endif
            /* these tcp-specific hints mean nothing to GM */
            return 0;
        default:
            return (bmi_gm_errno_to_pvfs(-ENOSYS));
    }
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
    int ret = -1;

    switch (option)
    {
    case BMI_CHECK_MAXSIZE:
	*((int *) inout_parameter) = GM_MODE_REND_LIMIT;
	ret = 0;
        break;
    case BMI_GET_UNEXP_SIZE:
        *((int *) inout_parameter) = GM_MODE_UNEXP_LIMIT;
        ret = 0;
        break;
    default:
	gossip_ldebug(GOSSIP_BMI_DEBUG_GM, 
	    "BMI GM hint %d not implemented.\n", option);
	ret = 0;
    break;
    }

    return (ret);
}

/* BMI_gm_post_send_list()
 *
 * same as post_send, except that it sends from an array of
 * possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_gm_post_send_list(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_size,
    enum bmi_buffer_type buffer_type,
    bmi_msg_tag_t tag,
    void *user_ptr,
    bmi_context_id context_id,
    PVFS_hint hints)
{
    int buffer_status = GM_BUF_USER_ALLOC;
    void *new_buffer = NULL;
    void *copy_buffer = NULL;
    struct ctrl_msg *new_ctrl_msg = NULL;
    bmi_size_t buffer_size = 0;
    int i;
    int ret;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "BMI_gm_post_send_list called.\n");

    /* TODO: think about this some.  For now this is going to be
     * lame because we aren't going to take advantage of
     * having buffers pinned in advance...
     */
    buffer_status = GM_BUF_METH_REG;

    /* clear id immediately for safety */
    *id = 0;

    /* make sure it's not too big */
    if (total_size > GM_MODE_REND_LIMIT)
    {
	return (bmi_gm_errno_to_pvfs(-EMSGSIZE));
    }

    gen_mutex_lock(&interface_mutex);
    if (total_size <= GM_IMMED_LENGTH)
    {
	/* pad enough room for a ctrl structure */
	buffer_size = sizeof(struct ctrl_msg) + total_size;

	/* create a new buffer and copy */
	new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
							 long) buffer_size);
	if (!new_buffer)
	{
	    gossip_lerr("Error: gm_dma_malloc failure.\n");
	    gen_mutex_unlock(&interface_mutex);
	    return (bmi_gm_errno_to_pvfs(-ENOMEM));
	}

	copy_buffer = new_buffer;
	for(i=0; i<list_count; i++)
	{
	    memcpy(copy_buffer, buffer_list[i], size_list[i]);
	    copy_buffer = (void*)((long)copy_buffer + (long)size_list[i]);
	}

	/* Immediate mode stuff */
	new_ctrl_msg = (struct ctrl_msg *) (new_buffer + total_size);
	new_ctrl_msg->ctrl_type = CTRL_IMMED_TYPE;
	new_ctrl_msg->magic_nr = BMI_MAGIC_NR;
	new_ctrl_msg->u.immed.actual_size = total_size;
	new_ctrl_msg->u.immed.msg_tag = tag;
	buffer_status = GM_BUF_METH_ALLOC;
	ret = gm_post_send_build_op(id, dest, new_buffer, total_size,
					    tag,
					    GM_MODE_IMMED,
					    buffer_status, user_ptr, context_id);
	gen_mutex_unlock(&interface_mutex);
	return(ret);
    }
    else
    {
	ret = gm_post_send_build_op_list(id, dest, buffer_list, 
	    size_list, list_count, total_size, tag, GM_MODE_REND, 
	    buffer_status, user_ptr, context_id);
	gen_mutex_unlock(&interface_mutex);
	return(ret);
    }

}


/* BMI_gm_post_sendunexpected_list()
 *
 * same as post_sendunexpected, except that it sends from an array of
 * possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_gm_post_sendunexpected_list(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_size,
    enum bmi_buffer_type buffer_type,
    bmi_msg_tag_t tag,
    uint8_t class,
    void *user_ptr,
    bmi_context_id context_id,
    PVFS_hint hints)
{
    int buffer_status = GM_BUF_USER_ALLOC;
    void *new_buffer = NULL;
    void *copy_buffer = NULL;
    struct ctrl_msg *new_ctrl_msg = NULL;
    bmi_size_t buffer_size = 0;
    int i;
    int ret;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, 
	"BMI_gm_post_sendunexpected_list called.\n");

    /* TODO: think about this some.  For now this is going to be
     * lame because we aren't going to take advantage of
     * having buffers pinned in advance...
     */
    buffer_status = GM_BUF_METH_ALLOC;

    /* clear id immediately for safety */
    *id = 0;

    /* make sure it's not too big */
    if (total_size > GM_MODE_UNEXP_LIMIT)
    {
	return (bmi_gm_errno_to_pvfs(-EMSGSIZE));
    }

    /* pad enough room for a ctrl structure */
    buffer_size = sizeof(struct ctrl_msg) + total_size;

    gen_mutex_lock(&interface_mutex);

    /* create a new buffer and copy */
    new_buffer = (void *) gm_dma_malloc(local_port, (unsigned
						     long) buffer_size);
    if (!new_buffer)
    {
	gen_mutex_unlock(&interface_mutex);
	gossip_lerr("Error: gm_dma_malloc failure.\n");
	return (bmi_gm_errno_to_pvfs(-ENOMEM));
    }

    copy_buffer = new_buffer;
    for(i=0; i<list_count; i++)
    {
	memcpy(copy_buffer, buffer_list[i], size_list[i]);
	copy_buffer = (void*)((long)copy_buffer + (long)size_list[i]);
    }

    /* Immediate mode stuff */
    new_ctrl_msg = (struct ctrl_msg *) (new_buffer + total_size);
    new_ctrl_msg->ctrl_type = CTRL_UNEXP_TYPE;
    new_ctrl_msg->magic_nr = BMI_MAGIC_NR;
    new_ctrl_msg->u.immed.actual_size = total_size;
    new_ctrl_msg->u.immed.msg_tag = tag;
    new_ctrl_msg->u.immed.class = class;
    ret = gm_post_send_build_op(id, dest, new_buffer, total_size,
					tag,
					GM_MODE_UNEXP,
					buffer_status, user_ptr, context_id);
    gen_mutex_unlock(&interface_mutex);
    return(ret);
}


/* BMI_gm_post_recv_list()
 *
 * same as post_recv, except it operates on an array of possibly
 * discontiguous memory regions
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_post_recv_list(bmi_op_id_t * id,
    bmi_method_addr_p src,
    void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_expected_size,
    bmi_size_t * total_actual_size,
    enum bmi_buffer_type buffer_type,
    bmi_msg_tag_t tag,
    void *user_ptr,
    bmi_context_id context_id,
    PVFS_hint hints)
{
    method_op_p query_op = NULL;
    method_op_p new_method_op = NULL;
    struct op_list_search_key key;
    struct gm_op *gm_op_data = NULL;
    struct gm_addr *gm_addr_data = NULL;
    int ret = -1;
    int buffer_status = GM_BUF_USER_ALLOC;
    int i;
    void* copy_buffer;
    bmi_size_t copy_size, total_copied;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "BMI_gm_post_recv_list called.\n");

    /* what happens here ?
     * see if the operation is already in progress (IND_NEED_RECV_POST)
     *  - if so, match it and poke it to continue
     *  - if not, create an op and queue it up in IND_NEED_CTRL_MATCH
     */

    /* clear id immediately for safety */
    *id = 0;

    /* make sure it's not too big */
    if (total_expected_size > GM_MODE_REND_LIMIT)
    {
	return (bmi_gm_errno_to_pvfs(-EMSGSIZE));
    }

    /* TODO: this is going to be lame for a while; if we are in
     * rendezvous mode on list operations we will buffer copy
     * regardless of how the caller prepared the buffer 
     */
    /* set flag to indicate if we need to pinn this buffer internally */ 
    if(total_expected_size > GM_IMMED_LENGTH)
	buffer_status = GM_BUF_METH_REG;

    gen_mutex_lock(&interface_mutex);
    /* push work first; use this as an opportunity to make sure that the
     * receive keeps buffers moving as quickly as possible
     */
    ret = gm_do_work(0);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    /* see if this operation has already begun... */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = src;
    key.method_addr_yes = 1;
    key.msg_tag = tag;
    key.msg_tag_yes = 1;

    query_op = op_list_search(op_list_array[IND_NEED_RECV_POST], &key);
    if (query_op)
    {
	gm_addr_data = query_op->addr->method_data;
	*id = query_op->op_id;
	query_op->context_id = context_id;
	query_op->user_ptr = user_ptr;

	if(query_op->actual_size > total_expected_size)
	{
	    gossip_lerr("Error: message ordering violation;\n");
	    gossip_lerr("Error: message too large for next buffer.\n");
	    gen_mutex_unlock(&interface_mutex);
	    return(bmi_gm_errno_to_pvfs(-EPROTO));
	}

	/* we found the operation in progress. */

	if (query_op->mode == GM_MODE_REND)
	{
	    /* post has occurred */
	    op_list_remove(query_op);

	    gm_op_data = query_op->method_data;
	    gm_op_data->list_flag = 1;
	    gm_op_data->buffer_status = GM_BUF_METH_REG;
	    query_op->buffer_list = buffer_list;
	    query_op->size_list = size_list;
	    query_op->list_count = list_count;

	    /* now we need a token to send a ctrl ack */
	    if (gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
	    {
#ifdef ENABLE_GM_BUFPOOL
		if (!io_buffers_exhausted())
		{
#endif /* ENABLE_GM_BUFPOOL */
		    
		    prepare_for_recv_list(query_op);
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
	else if (query_op->mode == GM_MODE_IMMED)
	{
	    /* all is done except memory copy- complete instantly */
	    op_list_remove(query_op);
	    gm_op_data = query_op->method_data;
	    copy_buffer = query_op->buffer;
	    total_copied = 0;
	    for(i=0; i<list_count; i++)
	    {
		if(total_copied == query_op->actual_size)
		    break;
		copy_size = query_op->actual_size - total_copied;
		if(copy_size > size_list[i])
		    copy_size = size_list[i];
		memcpy(buffer_list[i], copy_buffer, copy_size);
		copy_buffer = (void*)((long)copy_buffer + (long)copy_size);
		total_copied += copy_size;
	    }
	    *total_actual_size = query_op->actual_size;
	    free(query_op->buffer);
	    *id = 0;
	    dealloc_gm_method_op(query_op);
	    ret = 1;
	}
	else
	{
	    /* we don't have any other modes implemented yet */
	    ret = bmi_gm_errno_to_pvfs(-ENOSYS);
	}
    }
    else
    {
	/* we must create the operation and queue it up */
	new_method_op = alloc_gm_method_op();
	if (!new_method_op)
	{
	    gen_mutex_unlock(&interface_mutex);
	    return (bmi_gm_errno_to_pvfs(-ENOMEM));
	}
	*id = new_method_op->op_id;
	new_method_op->user_ptr = user_ptr;
	new_method_op->send_recv = BMI_RECV;
	new_method_op->addr = src;
	new_method_op->buffer = NULL;
	new_method_op->expected_size = total_expected_size;
	new_method_op->actual_size = 0;
	new_method_op->msg_tag = tag;
	new_method_op->context_id = context_id;
	/* TODO: make sure this is ok */
	new_method_op->mode = 0;

	gm_op_data = new_method_op->method_data;
	gm_op_data->buffer_status = buffer_status;
	gm_op_data->list_flag = 1;

	new_method_op->buffer_list = buffer_list;
	new_method_op->size_list = size_list;
	new_method_op->list_count = list_count;
	/* just for safety; the user should not use this value in this case */
	*total_actual_size = 0;

	op_list_add(op_list_array[IND_NEED_CTRL_MATCH], new_method_op);
	ret = 0;
    }

    gen_mutex_unlock(&interface_mutex);
    return(ret);
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
		void **user_ptr,
		int max_idle_time_ms,
		bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = (method_op_p)id_gen_fast_lookup(id);
    struct gm_op *gm_op_data = query_op->method_data;

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    if(gm_op_data->complete)
    {
	assert(query_op->context_id == context_id);
	op_list_remove(query_op);
	if(user_ptr != NULL)
	{
	    (*user_ptr) = query_op->user_ptr;
	}
	(*error_code) = query_op->error_code;
	(*actual_size) = query_op->actual_size;
	dealloc_gm_method_op(query_op);
	(*outcount)++;
    }
    if(gm_op_data->cancelled && gm_op_data->cancelled_tv_sec)
    {
        /* report this operation as complete; it is only hanging around
         * to protect a recv buffer after cancellation
         */
        if(user_ptr != NULL)
	{
	    (*user_ptr) = query_op->user_ptr;
	}
	(*error_code) = -PVFS_ECANCEL;
	(*actual_size) = 0;
	(*outcount)++;
    }
    if(*outcount)
    {
        gen_mutex_unlock(&interface_mutex);
        return(0);
    }

    /* do some ``real work'' here */
    ret = gm_do_work(max_idle_time_ms*1000);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    if(gm_op_data->complete)
    {
	assert(query_op->context_id == context_id);
	op_list_remove(query_op);
	if(user_ptr != NULL)
	{
	    (*user_ptr) = query_op->user_ptr;
	}
	(*error_code) = query_op->error_code;
	(*actual_size) = query_op->actual_size;
	dealloc_gm_method_op(query_op);
	(*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);
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
		    void **user_ptr_array,
		    int max_idle_time_ms,
		    bmi_context_id context_id)
{
    int ret = -1;
    int i;
    method_op_p query_op;
    struct gm_op *gm_op_data;

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    for(i=0; i<incount; i++)
    {
	if(id_array[i])
	{
	    query_op = (method_op_p)id_gen_fast_lookup(id_array[i]);
	    gm_op_data = query_op->method_data;
	    if(gm_op_data->complete)
	    {
		assert(query_op->context_id == context_id);
		op_list_remove(query_op);
		error_code_array[*outcount] = query_op->error_code;
		actual_size_array[*outcount] = query_op->actual_size;
		index_array[*outcount] = i;
		if (user_ptr_array != NULL)
		    user_ptr_array[*outcount] = query_op->user_ptr;
		dealloc_gm_method_op(query_op);
		(*outcount)++;
	    }
            if(gm_op_data->cancelled && gm_op_data->cancelled_tv_sec)
            {
                /* report this operation as complete; it is only hanging around
                 * to protect a recv buffer after cancellation
                 */
		error_code_array[*outcount] = -PVFS_ECANCEL;
		actual_size_array[*outcount] = 0;
		index_array[*outcount] = i;
                if(user_ptr_array != NULL)
                    user_ptr_array[*outcount] = query_op->user_ptr;
                (*outcount)++;
            }
	}
    }
    if(*outcount)
    {
        gen_mutex_unlock(&interface_mutex);
        return(0);
    }

    /* do some ``real work'' here */
    ret = gm_do_work(max_idle_time_ms*1000);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    for(i=0; i<incount; i++)
    {
	if(id_array[i])
	{
	    query_op = (method_op_p)id_gen_fast_lookup(id_array[i]);
	    gm_op_data = query_op->method_data;
	    if(gm_op_data->complete)
	    {
		assert(query_op->context_id == context_id);
		op_list_remove(query_op);
		error_code_array[*outcount] = query_op->error_code;
		actual_size_array[*outcount] = query_op->actual_size;
		index_array[*outcount] = i;
		if (user_ptr_array != NULL)
		    user_ptr_array[*outcount] = query_op->user_ptr;
		dealloc_gm_method_op(query_op);
		(*outcount)++;
	    }
	}
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* BMI_gm_testcontext()
 *
 * Checks to see if any operations from the specified context have
 * completed
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_testcontext(int incount,
    bmi_op_id_t * out_id_array,
    int *outcount,
    bmi_error_code_t * error_code_array,
    bmi_size_t * actual_size_array,
    void **user_ptr_array,
    int max_idle_time_ms,
    bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = NULL;
    op_list_p tmp_entry = NULL;
    struct gm_op *gm_op_data = NULL;

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    /* check queue before doing anything */
    while((*outcount < incount) && (query_op = 
	op_list_shownext(completion_array[context_id])))
    {
	assert(query_op->context_id == context_id);
	op_list_remove(query_op);
	error_code_array[*outcount] = query_op->error_code;
	actual_size_array[*outcount] = query_op->actual_size;
	out_id_array[*outcount] = query_op->op_id;
	if(user_ptr_array != NULL)
	    user_ptr_array[*outcount] = query_op->user_ptr;
	dealloc_gm_method_op(query_op);
	(*outcount)++;
    }
    /* this is kind of nasty- look for cancelled rend recvs that we
     * have not reported yet.  Must iterate queue.
     */
    qlist_for_each(tmp_entry, op_list_array[IND_CANCELLED_REND])
    {
        if(*outcount >= incount)
            break;
        query_op = qlist_entry(tmp_entry, struct method_op, op_list_entry);
	gm_op_data = query_op->method_data;
        if(!gm_op_data->complete)
        {
            gm_op_data->complete = 1;
            error_code_array[*outcount] = -PVFS_ECANCEL;
            actual_size_array[*outcount] = 0;
            out_id_array[*outcount] = query_op->op_id;
            if(user_ptr_array != NULL)
                user_ptr_array[*outcount] = query_op->user_ptr;
	    (*outcount)++;
        }
    }
    if(*outcount)
    {
        gen_mutex_unlock(&interface_mutex);
        return(0);
    }

    /* do some ``real work'' here */
    ret = gm_do_work(max_idle_time_ms*1000);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    /* check queue again */
    while((*outcount < incount) && (query_op = 
	op_list_shownext(completion_array[context_id])))
    {
	assert(query_op->context_id == context_id);
	op_list_remove(query_op);
	error_code_array[*outcount] = query_op->error_code;
	actual_size_array[*outcount] = query_op->actual_size;
	out_id_array[*outcount] = query_op->op_id;
	if(user_ptr_array != NULL)
	    user_ptr_array[*outcount] = query_op->user_ptr;
	dealloc_gm_method_op(query_op);
	(*outcount)++;
    }
    /* this is kind of nasty- look for cancelled rend recvs that we
     * have not reported yet.  Must iterate queue.
     */
    qlist_for_each(tmp_entry, op_list_array[IND_CANCELLED_REND])
    {
        if(*outcount >= incount)
            break;
        query_op = qlist_entry(tmp_entry, struct method_op, op_list_entry);
	gm_op_data = query_op->method_data;
        if(!gm_op_data->complete)
        {
            gm_op_data->complete = 1;
            error_code_array[*outcount] = -PVFS_ECANCEL;
            actual_size_array[*outcount] = 0;
            out_id_array[*outcount] = query_op->op_id;
            if(user_ptr_array != NULL)
                user_ptr_array[*outcount] = query_op->user_ptr;
	    (*outcount)++;
        }
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* BMI_gm_testunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_testunexpected(int incount,
			  int *outcount,
			  struct bmi_method_unexpected_info *info,
                          uint8_t class,
			  int max_idle_time_ms)
{
    int ret = -1;
    method_op_p query_op = NULL;
    struct op_list_search_key key;

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.class = class;
    key.class_yes = 1;

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    if(op_list_empty(op_list_array[IND_COMPLETE_RECV_UNEXP]))
    {
        /* nothing ready yet, do some work */
        ret = gm_do_work(max_idle_time_ms*1000);
        if (ret < 0)
        {
            gen_mutex_unlock(&interface_mutex);
            return (ret);
        }
    }

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

    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/* BMI_gm_open_context()
 *
 * opens a new context with the specified context id
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_open_context(bmi_context_id context_id)
{
    gen_mutex_lock(&interface_mutex);
    /* start a new queue for tracking completions in this context */
    completion_array[context_id] = op_list_new();
    if (!completion_array[context_id])
    {
	gen_mutex_unlock(&interface_mutex);
	return(bmi_gm_errno_to_pvfs(-ENOMEM));
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}

/* BMI_gm_close_context()
 *
 * closes a context previously created with BMI_gm_open_context()
 *
 * no return value
 */
void BMI_gm_close_context(bmi_context_id context_id)
{
    gen_mutex_lock(&interface_mutex);
    /* tear down completion queue for this context */
    op_list_cleanup(completion_array[context_id]);

    gen_mutex_unlock(&interface_mutex);
    return;
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
static void dealloc_gm_method_addr(bmi_method_addr_p map)
{

    bmi_dealloc_method_addr(map);

    return;
}


/*
 * alloc_gm_method_addr()
 *
 * creates a new method address with defaults filled in for GM.
 *
 * returns pointer to struct on success, NULL on failure
 */
static bmi_method_addr_p alloc_gm_method_addr(void)
{

    struct bmi_method_addr *my_method_addr = NULL;
    struct gm_addr *gm_data = NULL;

    my_method_addr = bmi_alloc_method_addr(gm_method_params.method_id, 
            sizeof(struct gm_addr));
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

    my_method_op = bmi_alloc_method_op(sizeof(struct gm_op));

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
    bmi_dealloc_method_op(op_p);
    return;
}


/* gm_post_send_build_op_list()
 *
 * builds a method op structure for the specified send operation
 *
 * returns 0 on success, -errno on failure
 */
static int gm_post_send_build_op_list(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *const *buffer_list,
    const bmi_size_t *size_list,
    int list_count,
    bmi_size_t total_size,
    bmi_msg_tag_t tag,
    int mode,
    int buffer_status,
    void *user_ptr, bmi_context_id context_id)
{
    method_op_p new_method_op = NULL;
    struct gm_op *gm_op_data = NULL;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "gm_post_send_build_op_list() called.\n");

    /* we need an op structure to keep up with this send */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (bmi_gm_errno_to_pvfs(-ENOMEM));
    }
    *id = new_method_op->op_id;
    new_method_op->user_ptr = user_ptr;
    new_method_op->send_recv = BMI_SEND;
    new_method_op->addr = dest;
    new_method_op->buffer = NULL;
    new_method_op->actual_size = total_size;
    /* TODO: is this right thing to do for send side? */
    new_method_op->expected_size = 0;  
    new_method_op->msg_tag = tag;
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Tag: %d.\n", (int) tag);
    new_method_op->mode = mode;
    new_method_op->context_id = context_id;

    new_method_op->buffer_list = (void **) buffer_list;
    new_method_op->size_list = size_list;
    new_method_op->list_count = list_count;

    gm_op_data = new_method_op->method_data;
    gm_op_data->buffer_status = buffer_status;
    gm_op_data->list_flag = 1;

    return (gm_post_send_check_resource(new_method_op));
}


/* gm_post_send_build_op()
 *
 * builds a method op structure for the specified send operation
 *
 * returns 0 on success, -errno on failure
 */
static int gm_post_send_build_op(bmi_op_id_t * id,
    bmi_method_addr_p dest,
    const void *buffer,
    bmi_size_t size,
    bmi_msg_tag_t tag,
    int mode,
    int buffer_status,
    void *user_ptr, bmi_context_id context_id)
{
    method_op_p new_method_op = NULL;
    struct gm_op *gm_op_data = NULL;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "gm_post_send_build_op() called.\n");

    /* we need an op structure to keep up with this send */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (bmi_gm_errno_to_pvfs(-ENOMEM));
    }
    *id = new_method_op->op_id;
    new_method_op->user_ptr = user_ptr;
    new_method_op->send_recv = BMI_SEND;
    new_method_op->addr = dest;
    new_method_op->buffer = (void *) buffer;
    new_method_op->actual_size = size;
    /* TODO: is this right thing to do for send side? */
    new_method_op->expected_size = 0;  
    new_method_op->msg_tag = tag;
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Tag: %d.\n", (int) tag);
    new_method_op->mode = mode;
    new_method_op->context_id = context_id;

    gm_op_data = new_method_op->method_data;
    gm_op_data->buffer_status = buffer_status;

    return (gm_post_send_check_resource(new_method_op));
}


/* gm_post_send_check_resource()
 *
 * Checks to see if communication can proceed for a given send operation
 *
 * returns 0 on success, -errno on failure
 */
static int gm_post_send_check_resource(struct method_op* mop)
{
    int ret = -1;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "gm_post_send_check_resource() called.\n");

    /* what do we want to do here? 
     * For now, try to send a control message and then bail out.  The
     * poll function will drive the rest of the way.
     */

    /* make sure that we are not bypassing any operation that has stalled
     * waiting on tokens
     */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_HI_CTRL]))
    {
	op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRL], mop);
	/* push on existing work rather than attempting this send */
	return (gm_do_work(0));
    }

    /* all clear; let's see if we have a send token for the control message */
    if (!gm_alloc_send_token(local_port, GM_HIGH_PRIORITY))
    {
	/* queue up and wait for a token */
	op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRL], mop);
	/* push on existing work rather than attempting this send */
	ret = gm_do_work(0);
    }
    else
    {
	gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Proceeding with communication.\n");
	if (mop->mode == GM_MODE_REND)
	{
	    initiate_send_rend(mop);
	    ret = 0;
	}
	else
	{
	    initiate_send_immed(mop);
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

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Sending immediate msg.\n");

    true_msg_len = mop->actual_size + sizeof(struct ctrl_msg);

    /* send ctrl message */
    gm_send_with_callback(local_port, mop->buffer,
				  GM_IMMED_SIZE, true_msg_len, GM_HIGH_PRIORITY,
				  gm_addr_data->node_id, gm_addr_data->port_id, immed_send_callback,
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
    my_ctrl->magic_nr = BMI_MAGIC_NR;
    my_ctrl->u.put.receiver_op_id = gm_op_data->peer_op_id;
    /* keep up with this buffer in the op structure */
    gm_op_data->freeable_ctrl_buffer = my_ctrl;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Sending ctrl msg.\n");

    /* send ctrl message */
    gm_send_with_callback(local_port, my_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  gm_addr_data->port_id,
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
    my_ctrl->magic_nr = BMI_MAGIC_NR;
    my_ctrl->u.req.actual_size = mop->actual_size;
    my_ctrl->u.req.msg_tag = mop->msg_tag;
    my_ctrl->u.req.sender_op_id = mop->op_id;
    /* keep up with this buffer in the op structure */
    gm_op_data->freeable_ctrl_buffer = my_ctrl;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Sending ctrl msg.\n");

    /* send ctrl message */
    gm_send_with_callback(local_port, my_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  gm_addr_data->port_id,
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
    struct gm_addr *gm_addr_data = my_op->addr->method_data;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "ctrl_put_callback() called.\n");

    /* free up ctrl message buffer */
    bmi_gm_bufferpool_put(ctrl_send_pool, gm_op_data->freeable_ctrl_buffer);
    gm_op_data->freeable_ctrl_buffer = NULL;

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled...\n");
	op_list_remove(my_op);
	my_op->error_code = bmi_gm_errno_to_pvfs(-ETIMEDOUT);
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_err("Error: %s (%d)\n", gm_strerror(status), (int)status);
	gossip_err("Sending from %s to %s\n", gm_host_name,
	    gm_node_id_to_host_name(local_port, gm_addr_data->node_id));
	op_list_remove(my_op);
	/* TODO: need generic solution to map GM codes to pvfs2 codes */
	if(status == GM_SEND_TARGET_NODE_UNREACHABLE)
	    my_op->error_code = -PVFS_EHOSTUNREACH;
	else
	    my_op->error_code = -PVFS_EPROTO;
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    /* operation is now complete */
    op_list_remove(my_op);
    my_op->error_code = 0;
    op_list_add(completion_array[my_op->context_id], my_op);
    gm_op_data->complete = 1;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Finished ctrl_put_callback().\n");

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
    struct gm_addr *gm_addr_data = my_op->addr->method_data;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "immed_send_callback() called.\n");

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
	my_op->error_code = bmi_gm_errno_to_pvfs(-ETIMEDOUT);
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_err("Error: %s (%d)\n", gm_strerror(status), (int)status);
	gossip_err("Sending from %s to %s\n", gm_host_name,
	    gm_node_id_to_host_name(local_port, gm_addr_data->node_id));
	op_list_remove(my_op);
	/* TODO: need generic solution to map GM codes to pvfs2 codes */
	if(status == GM_SEND_TARGET_NODE_UNREACHABLE)
	    my_op->error_code = -PVFS_EHOSTUNREACH;
	else
	    my_op->error_code = -PVFS_EPROTO;
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
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
    op_list_add(completion_array[my_op->context_id], my_op);
    gm_op_data->complete = 1;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Finished immed_send_callback().\n");

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
    struct gm_addr *gm_addr_data = my_op->addr->method_data;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "ctrl_req_callback() called.\n");

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    /* free up ctrl message buffer */
    gm_op_data = my_op->method_data;
    bmi_gm_bufferpool_put(ctrl_send_pool, gm_op_data->freeable_ctrl_buffer);
    gm_op_data->freeable_ctrl_buffer = NULL;

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled...\n");
	op_list_remove(my_op);
	my_op->error_code = bmi_gm_errno_to_pvfs(-ETIMEDOUT);
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_err("Error: %s (%d)\n", gm_strerror(status), (int)status);
	gossip_err("Sending from %s to %s\n", gm_host_name,
	    gm_node_id_to_host_name(local_port, gm_addr_data->node_id));
	op_list_remove(my_op);
	/* TODO: need generic solution to map GM codes to pvfs2 codes */
	if(status == GM_SEND_TARGET_NODE_UNREACHABLE)
	    my_op->error_code = -PVFS_EHOSTUNREACH;
	else
	    my_op->error_code = -PVFS_EPROTO;
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    if(gm_op_data->cancelled)
    {
        /* this operation has been cancelled; don't do any further work */
	op_list_remove(my_op);
        my_op->error_code = -PVFS_ECANCEL;
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
        return;
    }

    /* golden! */

    /* don't touch the operation.  The ack receive handler will drive it
     * to the next state.
     */

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Finished ctrl_req_callback.\n");

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
	    return (bmi_gm_errno_to_pvfs(-ENOMEM));
	}
	gm_provide_receive_buffer(local_port, tmp_ctrl,
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
    }

    recv_token_count_high -= pool_size;

    return (0);
}


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
    struct gm_op* gm_op_data = NULL;

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
	    if (!io_buffers_exhausted())
	    {
#endif /* ENABLE_GM_BUFPOOL */
		query_op =
		    op_list_shownext(op_list_array[IND_NEED_SEND_TOK_LOW]);
		
		op_list_remove(query_op);
		gm_op_data = query_op->method_data;
		if(gm_op_data->list_flag)
		    setup_send_data_buffer_list(query_op);
		else
		    setup_send_data_buffer(query_op);
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
	    if (!io_buffers_exhausted())
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
        if(timeout > 0)
        {
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
		ret = recv_event_handler(poll_event, 1);
		break;
	    case GM_HIGH_PEER_RECV_EVENT:
	    case GM_HIGH_RECV_EVENT:
		handled_events++;
		ret = recv_event_handler(poll_event, 0);
		break;
            case _GM_SLEEP_EVENT:
		handled_events++;
                gen_mutex_unlock(&interface_mutex);
                gm_unknown(local_port, poll_event);
                gen_mutex_lock(&interface_mutex);
		ret = 0;
                break;
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
    gossip_debug(GOSSIP_BMI_DEBUG_GM, "Timer expired.\n");
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
				    bmi_method_addr_p map,
				    void *buffer,
                                    uint8_t class)
{
    method_op_p new_method_op = NULL;
    struct gm_op *gm_op_data = NULL;

    /* we need an op structure to keep up with this */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (bmi_gm_errno_to_pvfs(-ENOMEM));
    }
    new_method_op->send_recv = BMI_RECV;
    new_method_op->addr = map;
    new_method_op->actual_size = size;
    new_method_op->expected_size = 0;
    new_method_op->msg_tag = msg_tag;
    new_method_op->error_code = 0;
    new_method_op->mode = GM_MODE_UNEXP;
    new_method_op->buffer = buffer;
    new_method_op->class = class;
    gm_op_data = new_method_op->method_data;

    op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP], new_method_op);
    gm_op_data->complete = 1;

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
			      bmi_msg_tag_t msg_tag,
			      bmi_method_addr_p map,
			      void *buffer)
{
    method_op_p new_method_op = NULL;

    /* we need an op structure to keep up with this */
    new_method_op = alloc_gm_method_op();
    if (!new_method_op)
    {
	return (bmi_gm_errno_to_pvfs(-ENOMEM));
    }
    new_method_op->send_recv = BMI_RECV;
    new_method_op->addr = map;
    new_method_op->actual_size = actual_size;
    /* TODO: is this the right thing to do here? */
    new_method_op->expected_size = 0;
    new_method_op->msg_tag = msg_tag;
    new_method_op->mode = GM_MODE_IMMED;
    new_method_op->buffer = buffer;
    new_method_op->context_id = -1;

    /* queue up until user posts matching receive */
    op_list_add(op_list_array[IND_NEED_RECV_POST], new_method_op);

    return (0);
}


/* recv_event_handler()
 * 
 * handles low priority receive events as detected by gm_do_work()
 *
 * returns 0 on success, -errno on failure
 */
static int recv_event_handler(gm_recv_event_t * poll_event,
			     int fast)
{
    struct ctrl_msg ctrl_copy;
    int ret = bmi_gm_errno_to_pvfs(-ENOSYS);
    bmi_method_addr_p map = NULL;
    struct op_list_search_key key;
    void *tmp_buffer = NULL;
    method_op_p query_op = NULL;
    struct gm_addr *gm_addr_data = NULL;
    struct gm_op *gm_op_data = NULL;
    void* copy_buffer;
    bmi_size_t copy_size, total_copied;
    int i;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "recv_event_handler() called.\n");
    /* what are the possibilities here? 
     * 1) recv ctrl_ack for a send that we initiated
     * 2) recv ctrl_req from someone who wishes to send to us
     *    a) unexpected
     *    b) immediate
     *    c) rendezvous
     */

    /* NOTE: we *must* return ctrl buffers as quickly as possible.  They
     * must be available for accepting new messages at any time and we
     * cannot let too many remain out of service.
     */

    /* grab a copy of the control message out of the event */
    if (fast)
    {
	ctrl_copy = *(struct ctrl_msg *) (gm_ntohp(poll_event->recv.message) +
				       gm_ntohl(poll_event->recv.length) -
				       sizeof(struct ctrl_msg));
    }
    else
    {
	ctrl_copy = *(struct ctrl_msg *) (gm_ntohp(poll_event->recv.buffer) +
				       gm_ntohl(poll_event->recv.length) -
				       sizeof(struct ctrl_msg));
    }

    /* check magic */
    if(ctrl_copy.magic_nr != BMI_MAGIC_NR)
    {
	gossip_err("Error: bad magic in bmi_gm message.\n");
	gm_provide_receive_buffer(local_port,
				  gm_ntohp(poll_event->recv.buffer),
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	return(0);
    }

    /* repost buffer ASAP unless we need to copy data out of it */
    if(ctrl_copy.ctrl_type != CTRL_IMMED_TYPE && 
	ctrl_copy.ctrl_type != CTRL_UNEXP_TYPE)
    {
	gm_provide_receive_buffer(local_port,
				  gm_ntohp(poll_event->recv.buffer),
				  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
    }

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Ctrl_type: %d.\n", ctrl_copy.ctrl_type);
    switch(ctrl_copy.ctrl_type)
    {
	case CTRL_ACK_TYPE:
	    /* this is a response to one of our control requests */
	    ctrl_ack_handler(ctrl_copy.u.ack.sender_op_id,
			     gm_ntohs(poll_event->recv.sender_node_id), 
			     ctrl_copy.u.ack.remote_ptr,
			     ctrl_copy.u.ack.receiver_op_id);
	    ret = 0;
	    break;
	case CTRL_REQ_TYPE:
	    /* this is a new control request from someone */
	    ret = ctrl_req_handler_rend(ctrl_copy.u.req.sender_op_id, 
					    ctrl_copy.u.req.actual_size,
					    ctrl_copy.u.req.msg_tag,
					    gm_ntohs(poll_event->recv.
						     sender_node_id),
					    gm_ntoh_u8(poll_event->recv.
						     sender_port_id));
	    break;
	case CTRL_PUT_TYPE: 
	    put_recv_handler(ctrl_copy.u.put.receiver_op_id);
	    ret = 0;
	    break;
	case CTRL_IMMED_TYPE:
	    /* try to find a matching post from the receiver so that we don't
	     * have to buffer this yet again */
	    map = gm_addr_search(&gm_addr_list,
				 gm_ntohs(poll_event->recv.sender_node_id),
				 gm_ntoh_u8(poll_event->recv.sender_port_id));
	    if (!map)
	    {
		/* TODO: handle this error better */
		gossip_lerr("Error: unknown sender!\n");
		return (bmi_gm_errno_to_pvfs(-EPROTO));
	    }

	    memset(&key, 0, sizeof(struct op_list_search_key));
	    key.method_addr = map;
	    key.method_addr_yes = 1;
	    key.msg_tag = ctrl_copy.u.immed.msg_tag;
	    key.msg_tag_yes = 1;

	    query_op = op_list_search(op_list_array[IND_NEED_CTRL_MATCH], &key);
	    if (!query_op)
	    {
		gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Doh! Using extra buffer.\n");
		tmp_buffer = malloc(ctrl_copy.u.immed.actual_size);
		if (!tmp_buffer)
		{
		    /* TODO: handle error */
		    return (bmi_gm_errno_to_pvfs(-ENOMEM));
		}
		if (fast)
		{
		    memcpy(tmp_buffer, gm_ntohp(poll_event->recv.message),
			   ctrl_copy.u.immed.actual_size);
		}
		else
		{
		    memcpy(tmp_buffer, gm_ntohp(poll_event->recv.buffer),
			   ctrl_copy.u.immed.actual_size);
		}
		gm_provide_receive_buffer(local_port,
					  gm_ntohp(poll_event->recv.buffer),
					  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
		ret = immed_recv_handler(ctrl_copy.u.immed.actual_size,
					 ctrl_copy.u.immed.msg_tag, map, 
					 tmp_buffer);
	    }
	    else
	    {
		/* found a match */
		gm_op_data = query_op->method_data;
		if(fast)
		    copy_buffer = gm_ntohp(poll_event->recv.message);
		else
		    copy_buffer = gm_ntohp(poll_event->recv.buffer);

		if(gm_op_data->list_flag)
		{
		    total_copied = 0;
		    for(i=0; i<query_op->list_count; i++)
		    {
			if(total_copied == ctrl_copy.u.immed.actual_size)
			    break;

			copy_size = ctrl_copy.u.immed.actual_size - total_copied;
			if(copy_size > query_op->size_list[i])
			    copy_size = query_op->size_list[i];
			memcpy(query_op->buffer_list[i], copy_buffer, copy_size);
			copy_buffer = (void*)((long)copy_buffer + (long)copy_size);
			total_copied += copy_size;
		    }
		}
		else
		{
		    memcpy(query_op->buffer, copy_buffer,
			   ctrl_copy.u.immed.actual_size);
		}

		gm_provide_receive_buffer(local_port,
					  gm_ntohp(poll_event->recv.buffer),
					  GM_IMMED_SIZE, GM_HIGH_PRIORITY);
		op_list_remove(query_op);
		query_op->actual_size = ctrl_copy.u.immed.actual_size;
		query_op->error_code = 0;
		op_list_add(completion_array[query_op->context_id], query_op);
		gm_op_data->complete = 1;
		ret = 0;
	    }
	    break;
	case CTRL_UNEXP_TYPE:
	    map = gm_addr_search(&gm_addr_list,
				 gm_ntohs(poll_event->recv.sender_node_id),
				 gm_ntoh_u8(poll_event->recv.sender_port_id));
	    if (!map)
	    {
		/* new address! */
		map = alloc_gm_method_addr();
		gm_addr_data = map->method_data;
		gm_addr_data->node_id = gm_ntohs(poll_event->recv.sender_node_id);
		gm_addr_data->port_id = gm_ntohc(poll_event->recv.sender_port_id);
		/* let the bmi layer know about it */
		gm_addr_data->bmi_addr = bmi_method_addr_reg_callback(map);
		if (!gm_addr_data->bmi_addr)
		{
		    dealloc_gm_method_addr(map);
		    return (-BMI_ENOMEM);
		}
		/* keep up with it ourselves also */
		gm_addr_add(&gm_addr_list, map);
	    }

	    tmp_buffer = malloc(ctrl_copy.u.immed.actual_size);
	    if (!tmp_buffer)
	    {
		/* TODO: handle error */
		return (bmi_gm_errno_to_pvfs(-ENOMEM));
	    }
	    if (fast)
	    {
		memcpy(tmp_buffer, gm_ntohp(poll_event->recv.message),
		       ctrl_copy.u.immed.actual_size);
	    }
	    else
	    {
		memcpy(tmp_buffer, gm_ntohp(poll_event->recv.buffer),
		       ctrl_copy.u.immed.actual_size);
	    }
	    gm_provide_receive_buffer(local_port,
				      gm_ntohp(poll_event->recv.buffer),
				      GM_IMMED_SIZE, GM_HIGH_PRIORITY);
	    ret =
		immed_unexp_recv_handler(ctrl_copy.u.immed.actual_size, 
		    ctrl_copy.u.immed.msg_tag, map, tmp_buffer,
                    ctrl_copy.u.immed.class);
	    break;
	default:
	    /* TODO: handle this better */
	    assert(0);
	    break;
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
    void* copy_buffer;
    bmi_size_t copy_size, total_copied;
    int i;

    /* find the matching operation */
    query_op = id_gen_fast_lookup(ctrl_op_id);
    if(!query_op)
    {
        /* operation must have been cancelled; just return */
        return;
    }

    op_list_remove(query_op);
    gm_op_data = query_op->method_data;

    /* let go of the buffer if we need to */
    if (gm_op_data->buffer_status == GM_BUF_METH_REG)
    {
	if(gm_op_data->list_flag)
	{
#ifdef ENABLE_GM_REGCACHE
	    /* we don't handle this yet */
	    assert(0);

#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
	    /* we don't handle this yet */
	    assert(0);

#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
	    /* we don't handle this yet */
	    assert(0);

#endif /* ENABLE_GM_BUFCOPY */
#ifdef ENABLE_GM_BUFPOOL
            if(!gm_op_data->cancelled)
            {
                copy_buffer = gm_op_data->tmp_xfer_buffer;
                total_copied = 0;
                for(i=0; i<query_op->list_count; i++)
                {
                    if(total_copied == query_op->actual_size)
                        break;

                    copy_size = query_op->actual_size - total_copied;
                    if(copy_size > query_op->size_list[i])
                        copy_size = query_op->size_list[i];

                    memcpy(query_op->buffer_list[i], copy_buffer, copy_size);
                    copy_buffer = (void*)((long)copy_buffer + (long)copy_size);
                    total_copied += copy_size;
                }
            }
	    bmi_gm_bufferpool_put(io_pool, gm_op_data->tmp_xfer_buffer);
#endif /* ENABLE_GM_BUFPOOL */
	}
	else
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
            if(!gm_op_data->cancelled)
            {
                memcpy(query_op->buffer, gm_op_data->tmp_xfer_buffer,
                       query_op->actual_size);
            }
	    bmi_gm_bufferpool_put(io_pool, gm_op_data->tmp_xfer_buffer);
#endif /* ENABLE_GM_BUFPOOL */
	}
    }

    /* if this is an operation that has been cancelled, then don't put
     * it in the completion queue (it should have already been announced)
     * Instead, just quietly gid rid of the operation
     */
    if(gm_op_data->cancelled)
    {
        cancelled_rend_count--;
	dealloc_gm_method_op(query_op);
        return;
    }

    /* done */
    query_op->error_code = 0;
    op_list_add(completion_array[query_op->context_id], query_op);
    gm_op_data->complete = 1;
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

    if(!query_op)
    {
        /* the op is gone; probably canceled.  Just return. */
        return;
    }

    op_list_remove(query_op);
    gm_op_data = query_op->method_data;
    gm_op_data->remote_ptr = remote_ptr;
    gm_op_data->peer_op_id = peer_op_id;

    /* make sure that we are not bypassing any operations that
     * are stalled waiting on tokens
     */
    if (!op_list_empty(op_list_array[IND_NEED_SEND_TOK_LOW]))
    {
	gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Stalling behind stalled message.\n");
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
    if (io_buffers_exhausted())
    {
	gm_free_send_token(local_port, GM_LOW_PRIORITY);
	op_list_add(op_list_array[IND_NEED_SEND_TOK_LOW], query_op);
	return;
    }
#endif /* ENABLE_GM_BUFPOOL */

    if(gm_op_data->list_flag)
	setup_send_data_buffer_list(query_op);
    else
	setup_send_data_buffer(query_op);
    send_data_buffer(query_op);
    return;
}


/* setup_send_data_buffer()
 *
 * prepares a buffer to be sent 
 *
 * no return value
 */
static void setup_send_data_buffer(method_op_p mop)
{
    struct gm_op *gm_op_data = mop->method_data;
    bmi_size_t pinned_size = 0;

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
	    mop->error_code = bmi_gm_errno_to_pvfs(-ENOMEM);
	    op_list_add(completion_array[mop->context_id], mop);
	    gm_op_data->complete = 1;
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
	    mop->error_code = bmi_gm_errno_to_pvfs(-ENOMEM);
	    op_list_add(completion_array[mop->context_id], mop);
	    gm_op_data->complete = 1;
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
	    mop->error_code = bmi_gm_errno_to_pvfs(-ENOMEM);
	    op_list_add(completion_array[mop->context_id], mop);
	    gm_op_data->complete = 1;
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

    return;
}


/* setup_send_data_buffer_list()
 *
 * prepares a buffer to be sent (for list operations)
 *
 * no return value
 */
static void setup_send_data_buffer_list(method_op_p mop)
{
    struct gm_op *gm_op_data = mop->method_data;
    int i;
    void* copy_buffer;

#ifdef ENABLE_GM_REGCACHE
    /* we don't handle this yet */
    assert(0);
#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
    /* we don't handle this yet */
    assert(0);
#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
    /* we don't handle this yet */
    assert(0);
#endif /* ENABLE_GM_BUFCOPY */
#ifdef ENABLE_GM_BUFPOOL
    gm_op_data->tmp_xfer_buffer = bmi_gm_bufferpool_get(io_pool);
    copy_buffer = gm_op_data->tmp_xfer_buffer;
    for(i=0; i<mop->list_count; i++)
    {
	memcpy(copy_buffer, mop->buffer_list[i], mop->size_list[i]);
	copy_buffer = (void*)((long)copy_buffer + (long)mop->size_list[i]);
    }
    mop->buffer = gm_op_data->tmp_xfer_buffer;
#endif /* ENABLE_GM_BUFPOOL */

    return;
}

/* send_data_buffer()
 *
 * sends the actual message data for an operation.  Assumes that a low
 * priority send token has already been acquired, and that the
 * send buffer has already been prepared.
 *
 * no return value
 */
static void send_data_buffer(method_op_p mop)
{
    struct gm_addr *gm_addr_data = mop->addr->method_data;
    struct gm_op *gm_op_data = mop->method_data;

    /* send actual buffer */
    /* NOTE: the ctrl message communication leading up to this send allows 
     * us to use the "actual size" field here rather than the expected 
     * size when transfering data
     */

    gm_directed_send_with_callback(local_port, mop->buffer,
				   gm_op_data->remote_ptr,
				   mop->actual_size, GM_LOW_PRIORITY,
				   gm_addr_data->node_id, gm_addr_data->port_id,
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
    new_ctrl->magic_nr = BMI_MAGIC_NR;
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
	    mop->error_code = bmi_gm_errno_to_pvfs(-ENOMEM);
	    op_list_add(completion_array[mop->context_id], mop);
	    gm_op_data->complete = 1;
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
	    mop->error_code = bmi_gm_errno_to_pvfs(-ENOMEM);
	    op_list_add(completion_array[mop->context_id], mop);
	    gm_op_data->complete = 1;
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
	    mop->error_code = bmi_gm_errno_to_pvfs(-ENOMEM);
	    op_list_add(completion_array[mop->context_id], mop);
	    gm_op_data->complete = 1;
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
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Sending ctrl ack.\n");
    gm_send_with_callback(local_port, new_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  gm_addr_data->port_id,
				  ctrl_ack_callback, mop);

    /* queue up op */
    op_list_add(op_list_array[IND_SENDING], mop);
    return;

}


/* prepare_for_recv_list()
 *
 * provides a receive buffer and sends a control ack to allow the sender
 * to continue.  Assumes that the send and recv tokens have already been
 * acquired.
 *
 * returns 0 on success, -errno on failure
 */
static void prepare_for_recv_list(method_op_p mop)
{
    struct gm_addr *gm_addr_data = mop->addr->method_data;
    struct gm_op *gm_op_data = mop->method_data;
    struct ctrl_msg *new_ctrl = NULL;
    bmi_size_t pinned_size = 0;

    /* prepare a ctrl message response */
    new_ctrl = bmi_gm_bufferpool_get(ctrl_send_pool);

    new_ctrl->ctrl_type = CTRL_ACK_TYPE;
    new_ctrl->magic_nr = BMI_MAGIC_NR;
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
	/* we don't handle this yet */
	assert(0);
#endif /* ENABLE_GM_REGCACHE */
#ifdef ENABLE_GM_REGISTER
	/* we don't handle this yet */
	assert(0);
#endif /* ENABLE_GM_REGISTER */
#ifdef ENABLE_GM_BUFCOPY
	/* we don't handle this yet */
	assert(0);
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
    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "Sending ctrl ack.\n");
    gm_send_with_callback(local_port, new_ctrl,
				  GM_IMMED_SIZE, GM_CTRL_LENGTH,
				  GM_HIGH_PRIORITY, gm_addr_data->node_id,
				  gm_addr_data->port_id,
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
    struct gm_addr *gm_addr_data = my_op->addr->method_data;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "ctrl_ack_callback() called.\n");

    /* give back a send token */
    gm_free_send_token(local_port, GM_HIGH_PRIORITY);

    /* free up ctrl message buffer */
    gm_op_data = my_op->method_data;
    bmi_gm_bufferpool_put(ctrl_send_pool, gm_op_data->freeable_ctrl_buffer);
    gm_op_data->freeable_ctrl_buffer = NULL;

    /* see if the receiver couldn't keep up */
    if (status == GM_SEND_TIMED_OUT)
    {
	gossip_lerr("Error: GM TIMEOUT!  Not handled.\n");
	op_list_remove(my_op);
	my_op->error_code = bmi_gm_errno_to_pvfs(-ETIMEDOUT);
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_err("Error: %s (%d)\n", gm_strerror(status), (int)status);
	gossip_err("Sending from %s to %s\n", gm_host_name,
	    gm_node_id_to_host_name(local_port, gm_addr_data->node_id));
	op_list_remove(my_op);
	my_op->error_code = bmi_gm_errno_to_pvfs(-ETIMEDOUT);
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
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
    struct gm_addr *gm_addr_data = my_op->addr->method_data;

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
	my_op->error_code = bmi_gm_errno_to_pvfs(-ETIMEDOUT);
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
	return;
    }

    /* look for other errors */
    if (status != GM_SUCCESS)
    {
	gossip_lerr("Error: GM send failure, detected in callback.\n");
	gossip_err("Error: GM return value: %d.\n", (int) status);
	gossip_err("Sending from %s to %s\n", gm_host_name,
	    gm_node_id_to_host_name(local_port, gm_addr_data->node_id));
	op_list_remove(my_op);
	/* TODO: need generic solution to map GM codes to pvfs2 codes */
	if(status == GM_SEND_TARGET_NODE_UNREACHABLE)
	    my_op->error_code = -PVFS_EHOSTUNREACH;
	else
	    my_op->error_code = -PVFS_EPROTO;
	op_list_add(completion_array[my_op->context_id], my_op);
	gm_op_data->complete = 1;
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
				 bmi_msg_tag_t ctrl_tag,
				 unsigned int node_id,
                                 unsigned int port_id)
{
    bmi_method_addr_p map = NULL;
    struct gm_addr *gm_addr_data = NULL;
    struct gm_op *gm_op_data = NULL;
    method_op_p active_method_op = NULL;
    int ret = -1;
    struct op_list_search_key key;

    gossip_ldebug(GOSSIP_BMI_DEBUG_GM, "ctrl_req_handler_rend() called.\n");

    /* someone wants to send a buffer in rendezvous mode 
     * - look to see if the matching receive has been posted
     *      1)      - if so, provide receive buffer from posted op
     *    - send an ack with buffer pointer
     *              - queue up
     * 2) - if not, create a method op
     *      - fill in info
     *      - queue up as need receive post
     */

    map = gm_addr_search(&gm_addr_list, node_id, port_id);
    if (!map)
    {
	/* where did this come from?! */
	/* TODO: handle this error condition */
	gossip_lerr("Error: ctrl message from unknown host!!!\n");
	return (bmi_gm_errno_to_pvfs(-EPROTO));
    }
    gm_addr_data = map->method_data;

    /* see if this operation has already begun... */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;
    key.msg_tag = ctrl_tag;
    key.msg_tag_yes = 1;

    active_method_op = op_list_search(op_list_array[IND_NEED_CTRL_MATCH], &key);
    if (active_method_op)
    {
	op_list_remove(active_method_op);

	if(ctrl_actual_size > active_method_op->expected_size)
	{
	    gossip_lerr("Error: message ordering violation;\n");
	    gossip_lerr("Error: message too large for next buffer.\n");
	    /* TODO: handle this better */
	    return(bmi_gm_errno_to_pvfs(-EPROTO));
	}

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
	    if (io_buffers_exhausted())
	    {
		gm_free_send_token(local_port, GM_HIGH_PRIORITY);
		op_list_add(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK],
			    active_method_op);
		ret = 0;
	    }
	    else
	    {
#endif /* ENABLE_GM_BUFPOOL */
		if(gm_op_data->list_flag)
		    prepare_for_recv_list(active_method_op);
		else
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
	    return (bmi_gm_errno_to_pvfs(-ENOMEM));
	}
	active_method_op->send_recv = BMI_RECV;
	active_method_op->addr = map;
	active_method_op->actual_size = ctrl_actual_size;
	/* TODO: is this the right thing to do here? */
	active_method_op->expected_size = 0;
	active_method_op->msg_tag = ctrl_tag;
	active_method_op->mode = GM_MODE_REND;
	active_method_op->context_id = -1;
	gm_op_data = active_method_op->method_data;
	gm_op_data->peer_op_id = ctrl_op_id;

	op_list_add(op_list_array[IND_NEED_RECV_POST], active_method_op);
	ret = 0;
    }
    return (ret);

}

/* BMI_gm_cancel()
 *
 * attempt to cancel a pending bmi gm operation
 *
 * returns 0 on success, -errno on failure
 */
int BMI_gm_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
    method_op_p query_op = (method_op_p)id_gen_fast_lookup(id);
    method_op_p tmp_op;
    struct gm_op *gm_op_data = query_op->method_data;
    struct op_list_search_key key;

    assert(query_op);

#ifndef ENABLE_GM_BUFPOOL
    /* we don't implement cancel for any other buffering mechanisms */
    assert(0);
#endif

    gen_mutex_lock(&interface_mutex);

    gossip_debug(GOSSIP_BMI_DEBUG_GM, 
        "BMI_gm_cancel: send_recv: %d, complete: %d, mode: %d\n",
        query_op->send_recv, gm_op_data->complete, query_op->mode);

    /* easy case: is the operation already completed? */
    if(gm_op_data->complete)
    {
        gossip_debug(GOSSIP_BMI_DEBUG_GM, "BMI_gm_cancel: complete case.\n");
        /* do nothing */
	gen_mutex_unlock(&interface_mutex);
	return(0);
    }

    /* send case */
    if(query_op->send_recv == BMI_SEND)
    {
        /* find out if we are blocking on resource usage and haven't sent 
         * anything yet
         */
        memset(&key, 0, sizeof(struct op_list_search_key));
        key.op_id = query_op->op_id;
        key.op_id_yes = 1;
        tmp_op = op_list_search(op_list_array[IND_NEED_SEND_TOK_HI_CTRL], &key);
        if(tmp_op)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                "BMI_gm_cancel: nothing sent yet.\n");
            /* nothing sent yet; clean up and move to comp. queue */
            assert(tmp_op == query_op);
            op_list_remove(query_op);
            if (gm_op_data->buffer_status == GM_BUF_METH_ALLOC)
            {
                /* this was an internally allocated buffer */
                gm_dma_free(local_port, query_op->buffer);
            }
            query_op->error_code = -PVFS_ECANCEL;
            op_list_add(completion_array[query_op->context_id], query_op);
            gm_op_data->complete = 1;
	    gen_mutex_unlock(&interface_mutex);
            return(0);
        }

        /* see if the send is literally in flight (waiting on a gm callback) */
        /* may be immediate mode, unexp mode, or rend mode during req or
         * done message
         */
        tmp_op = op_list_search(op_list_array[IND_SENDING], &key);
        if(tmp_op)
        {
            assert(tmp_op == query_op);
            if(query_op->mode == GM_MODE_IMMED ||
                query_op->mode == GM_MODE_UNEXP)
            {
                gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                    "BMI_gm_cancel: sending immed msg.\n");
                /* op is practically done; just wait for GM callback to 
                 * trigger and transition it to completion queue */ 
                gen_mutex_unlock(&interface_mutex);
                return(0);
            }
            else if(gm_op_data->freeable_ctrl_buffer &&
                (gm_op_data->freeable_ctrl_buffer->ctrl_type == CTRL_PUT_TYPE))
            {
                /* op is practically done; just wait for GM callback to 
                 * trigger and transition it to completion queue */ 
                gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                    "BMI_gm_cancel: sending put msg.\n");
                gen_mutex_unlock(&interface_mutex);
                return(0);
            }
            else if(gm_op_data->freeable_ctrl_buffer &&
                (gm_op_data->freeable_ctrl_buffer->ctrl_type == CTRL_REQ_TYPE))
            {
                /* waiting on ctrl req callback */
                /* mark op as being in a cancelled state and handle it in
                 * req callback and ack handler
                 */
                gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                    "BMI_gm_cancel: sending req msg.\n");
                gm_op_data->cancelled = 1;
                gen_mutex_unlock(&interface_mutex);
                return(0);
            }
            else
            {
                gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                    "BMI_gm_cancel: sending payload.\n");
                /* waiting on data send callback */
                /* nothing sane to do here except let it try to finish.  If
                 * the data send callback triggers with successful status,
                 * then we should try to announce the put so the other end 
                 * can release the buffer.  If it errors, then we are done.
                 */
                gen_mutex_unlock(&interface_mutex);
                return(0);
            }
        }
        
        /* see if we are waiting on a token for a put msg */
        tmp_op = op_list_search(op_list_array[IND_NEED_SEND_TOK_HI_PUT], &key);
        if(tmp_op)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                "BMI_gm_cancel: waiting on put token.\n");
            assert(tmp_op == query_op);
            /* nothing to do here; operation is practically finished and we
             * should make a best effort to tell the receiver it can 
             * release the buffer
             */
            gen_mutex_unlock(&interface_mutex);
            return(0);
        }
        
        /* see if we are waiting on a token for the data payload */
        tmp_op = op_list_search(op_list_array[IND_NEED_SEND_TOK_LOW], &key);
        if(tmp_op)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                "BMI_gm_cancel: waiting on data payload token.\n");
            assert(tmp_op == query_op);

            /* no payload sent yet, clean up and get out */
            op_list_remove(query_op);
            query_op->error_code = -PVFS_ECANCEL;
            op_list_add(completion_array[query_op->context_id], query_op);
            gm_op_data->complete = 1;
            gen_mutex_unlock(&interface_mutex);
            return(0);
        }
    }

    /* recv case */
    if(query_op->send_recv == BMI_RECV)
    {
        memset(&key, 0, sizeof(struct op_list_search_key));
        key.op_id = query_op->op_id;
        key.op_id_yes = 1;

        /* easy case for recv: have we even been contacted yet? */
        tmp_op = op_list_search(op_list_array[IND_NEED_CTRL_MATCH], &key);
        if(tmp_op)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                "BMI_gm_cancel: nothing received yet.\n");
            /* nothing to do, no resources consumed */
            assert(tmp_op == query_op);
            op_list_remove(query_op);
            query_op->error_code = -PVFS_ECANCEL;
            op_list_add(completion_array[query_op->context_id], query_op);
            gm_op_data->complete = 1;
	    gen_mutex_unlock(&interface_mutex);
            return(0);
        }

        /* are we waiting on resources to send a ctrl ack? */
        tmp_op = op_list_search(op_list_array[IND_NEED_SEND_TOK_HI_CTRLACK], &key);
        if(tmp_op)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                "BMI_gm_cancel: nothing received yet.\n");
            assert(tmp_op == query_op);
            /* luckily no resources are consumed at this stage, just clean
             * up and get out
             */
            op_list_remove(query_op);
            query_op->error_code = -PVFS_ECANCEL;
            op_list_add(completion_array[query_op->context_id], query_op);
            gm_op_data->complete = 1;
	    gen_mutex_unlock(&interface_mutex);
            return(0);
        }

        /* are we actually in the process of receiving data? */
        tmp_op = op_list_search(op_list_array[IND_RECVING], &key);
        if(tmp_op)
        {
            struct timeval tv;
            gossip_debug(GOSSIP_BMI_DEBUG_GM, 
                "BMI_gm_cancel: receiving data.\n");
            assert(tmp_op == query_op);

            /* resources have been consumed (big recv buffer), 
             * and we can't risk reusing it.  The sender may wake up and
             * perform a remote put operation
             */
            /* mark operation as cancelled, set timestamp, and move to
             * special queue; we may reclaim it much later
             */
            op_list_remove(query_op);
            gm_op_data->cancelled = 1;
            gettimeofday(&tv, NULL);
            gm_op_data->cancelled_tv_sec = tv.tv_sec;
            op_list_add(op_list_array[IND_CANCELLED_REND], query_op);
            cancelled_rend_count++;

            /* NOTE: test functions will find this special case operation
             * and report it as completed with PVFS_ECANCEL error code so 
             * that the caller doesn't get hung.  The caller can continue
             * without any risk at this point. */
            
	    gen_mutex_unlock(&interface_mutex);
            return(0);
        }
    }

    /* if we fall through to here then something has gone terribly wrong,
     * we lost track of the operation or something
     */
    assert(0);

    gen_mutex_unlock(&interface_mutex);

    return(0);
}

static void reclaim_cancelled_io_buffers(void)
{
    struct timeval tv;
    method_op_p query_op = NULL;
    struct gm_op *gm_op_data = NULL;

    gettimeofday(&tv, NULL);

    if(cancelled_rend_count)
    {
        while((query_op = op_list_shownext(op_list_array[IND_CANCELLED_REND])))
        {
	    gm_op_data = query_op->method_data;
            if((tv.tv_sec - gm_op_data->cancelled_tv_sec) <
                PINT_CANCELLED_REND_RECLAIM_TIMEOUT)
            {
                /* these are too recent; move on */
                break;
            }

            /* we want to put this one back into service */
            /* let put recv handler do the work as if we had gotten msg
             * from sender
             */
            put_recv_handler(query_op->op_id);
            cancelled_rend_count--;
        }

        /* if after doing this we still have too many exhausted buffers, then
         * just give up - we did the best we could...
         */
        if((io_pool->num_buffers - cancelled_rend_count) < 4)
        {
            gossip_lerr(
                "Error: BMI_GM: exhausted memory buffers due to cancellation.\n");
            assert(0);
        }
    }

    return;
}

static int io_buffers_exhausted(void)
{
    if(!bmi_gm_bufferpool_empty(io_pool))
    {
        return(0);
    }

    /* try to get some cancelled buffers back */
    reclaim_cancelled_io_buffers();

    if(!bmi_gm_bufferpool_empty(io_pool))
    {
        return(0);
    }
    else
    {
        return(1);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

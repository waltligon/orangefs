/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* stubs for bmi/trove flowprotocol */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "gossip.h"
#include "trove-types.h"
#include "flow.h"
#include "flowproto-support.h"
#include "quicklist.h"
#include "trove-id-queue.h"
#include "trove-proto.h"
#include "pvfs-request.h"

/**********************************************************
 * interface prototypes 
 */
int flowproto_bmi_trove_initialize(int flowproto_id);

int flowproto_bmi_trove_finalize(void);

int flowproto_bmi_trove_getinfo(flow_descriptor * flow_d,
				int option,
				void *parameter);

int flowproto_bmi_trove_setinfo(flow_descriptor * flow_d,
				int option,
				void *parameter);

void *flowproto_bmi_trove_memalloc(flow_descriptor * flow_d,
				   PVFS_size size,
				   int send_recv_flag);

int flowproto_bmi_trove_memfree(flow_descriptor * flow_d,
				void *buffer,
				int send_recv_flag);

int flowproto_bmi_trove_announce_flow(flow_descriptor * flow_d);

int flowproto_bmi_trove_checkworld(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms);

int flowproto_bmi_trove_service(flow_descriptor * flow_d);

char flowproto_bmi_trove_name[] = "flowproto_bmi_trove";

struct flowproto_ops flowproto_bmi_trove_ops = {
    flowproto_bmi_trove_name,
    flowproto_bmi_trove_initialize,
    flowproto_bmi_trove_finalize,
    flowproto_bmi_trove_getinfo,
    flowproto_bmi_trove_setinfo,
    flowproto_bmi_trove_memalloc,
    flowproto_bmi_trove_memfree,
    flowproto_bmi_trove_announce_flow,
    flowproto_bmi_trove_checkworld,
    flowproto_bmi_trove_service
};

/****************************************************
 * internal types and global variables
 */

/* buffer states for use when we are in double buffering mode */
enum buffer_state
{
    BUF_READY_TO_FILL = 1,
    BUF_READY_TO_DRAIN = 2,
    BUF_DRAINING = 3,
    BUF_FILLING = 4,
    BUF_READY_TO_SWAP = 5,
    BUF_DONE = 6
};

/* types of flows */
enum flow_type
{
    BMI_TO_TROVE = 1,
    TROVE_TO_BMI = 2,
    BMI_TO_MEM = 3,
    MEM_TO_BMI = 4
};

/* default buffer size to use for I/O */
static int DEFAULT_BUFFER_SIZE = 1024 * 1024 * 4;
/* our assigned flowproto id */
static int flowproto_bmi_trove_id = -1;

/* max number of discontig regions we will handle at once */
#define MAX_REGIONS 16

/* bmi context */
static bmi_context_id global_bmi_context = -1;

/* array of bmi ops in flight; filled in when needed to call
 * testcontext() 
 */
#define BMI_TEST_SIZE 8
static bmi_op_id_t bmi_op_array[BMI_TEST_SIZE];
static int bmi_error_code_array[BMI_TEST_SIZE];
static void* bmi_usrptr_array[BMI_TEST_SIZE];
static bmi_size_t bmi_actualsize_array[BMI_TEST_SIZE];
static int bmi_pending_count = 0;

/* array of trove ops in flight; filled in when needed to call testsome()
 * or waitsome() at the trove level. */
static PVFS_ds_id *trove_op_array = NULL;
static int trove_op_array_len = 0;
/* continuously updated list of trove ops in flight */
static trove_id_queue_p trove_inflight_queue;
static int trove_pending_count = 0;

/* queue of flows that that have either completed or need service but 
 * have not yet been passed back by flow_proto_checkXXX()
 */
static QLIST_HEAD(done_checking_queue);

/* this struct contains information associated with a flow that is
 * specific to this flow protocol, this one is for mem<->bmi ops
 */
struct bmi_trove_flow_data
{
    /* type of flow */
    enum flow_type type;
    /* queue link */
    struct qlist_head queue_link;
    /* pointer to the flow that this structure is associated with */
    flow_descriptor *parent;

    /* array of memory regions for current BMI operation */
    PVFS_size bmi_size_list[MAX_REGIONS];
    PVFS_offset bmi_offset_list[MAX_REGIONS];
    void *bmi_buffer_list[MAX_REGIONS];
    PVFS_size bmi_total_size;
    int bmi_list_count;

    /* array of regions for current Trove operation */
    PVFS_size trove_size_list[MAX_REGIONS];
    PVFS_offset trove_offset_list[MAX_REGIONS];
    PVFS_size trove_total_size;
    int trove_list_count;

    /* intermediate buffer to use in certain situations */
    void *intermediate_buffer;
    PVFS_size intermediate_used;

    /* id of current BMI operation */
    bmi_op_id_t bmi_id;
    /* ditto for trove */
    PVFS_ds_id trove_id;

    /* the remaining fields are only used in double buffering
     * situations 
     *********************************************************/

    char *fill_buffer;		/* buffer where data comes in from source */
    PVFS_size fill_buffer_offset;	/* current offset into buffer */
    enum buffer_state fill_buffer_state;	/* status of buffer */
    PVFS_size fill_buffer_used;	/* amount of buffer actually in use */
    /* size of the current operation on the buffer */
    PVFS_size fill_buffer_stepsize;
    /* total amount of data that has passed through this buffer */
    PVFS_size total_filled;

    char *drain_buffer;		/* buffer where data goes out to dest */
    PVFS_size drain_buffer_offset;	/* current offset into buffer */
    enum buffer_state drain_buffer_state;	/* status of buffer */
    PVFS_size drain_buffer_used;	/* amount of buffer actually in use */
    /* size of the current operation on the buffer */
    PVFS_size drain_buffer_stepsize;
    /* total amount of data that has passed through this buffer */
    PVFS_size total_drained;

    /* max size of buffers being used */
    int max_buffer_size;

    /* extra request processing state information for the bmi receive
     * half of bmi->trove flows
     */
    PINT_Request_state *dup_req_state;
    PVFS_offset dup_req_offset;
};

/****************************************************
 * internal helper function declarations
 */

/* a macro to help get to our private data that is stashed in each flow
 * descriptor
 */
#define PRIVATE_FLOW(target_flow)\
	((struct bmi_trove_flow_data*)(target_flow->flow_protocol_data))

static void release_flow(flow_descriptor * flow_d);
static void buffer_teardown_bmi_to_mem(flow_descriptor * flow_d);
static void buffer_teardown_mem_to_bmi(flow_descriptor * flow_d);
static void buffer_teardown_trove_to_bmi(flow_descriptor * flow_d);
static void buffer_teardown_bmi_to_trove(flow_descriptor * flow_d);
static int buffer_setup_mem_to_bmi(flow_descriptor * flow_d);
static int buffer_setup_bmi_to_mem(flow_descriptor * flow_d);
static int check_support(struct flowproto_type_support *type);
static int alloc_flow_data(flow_descriptor * flow_d);
static void service_mem_to_bmi(flow_descriptor * flow_d);
static void service_trove_to_bmi(flow_descriptor * flow_d);
static void service_bmi_to_trove(flow_descriptor * flow_d);
static void service_bmi_to_mem(flow_descriptor * flow_d);
static flow_descriptor *bmi_completion(bmi_error_code_t error_code,
				       bmi_size_t actual_size,
				       void *user_ptr);
static flow_descriptor *trove_completion(PVFS_ds_state error_code,
					 void *user_ptr);
static void bmi_completion_bmi_to_mem(bmi_error_code_t error_code,
				      bmi_size_t actual_size,
				      flow_descriptor * flow_d);
static void bmi_completion_bmi_to_trove(bmi_error_code_t error_code,
					bmi_size_t actual_size,
					flow_descriptor * flow_d);
static void bmi_completion_mem_to_bmi(bmi_error_code_t error_code,
				      flow_descriptor * flow_d);
static void bmi_completion_trove_to_bmi(bmi_error_code_t error_code,
					flow_descriptor * flow_d);
static void trove_completion_trove_to_bmi(PVFS_ds_state error_code,
					  flow_descriptor * flow_d);
static void trove_completion_bmi_to_trove(PVFS_ds_state error_code,
					  flow_descriptor * flow_d);
static int buffer_setup_trove_to_bmi(flow_descriptor * flow_d);
static int buffer_setup_bmi_to_trove(flow_descriptor * flow_d);

/****************************************************
 * public interface functions
 */

/* flowproto_bmi_trove_initialize()
 *
 * initializes the flowprotocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_initialize(int flowproto_id)
{
    int ret = -1;
    int tmp_maxsize;
    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_bmi_trove initialized.\n");

    /* make sure that the bmi interface is initialized */
    if ((ret = BMI_get_info(0, BMI_CHECK_INIT, NULL)) < 0)
    {
	return (ret);
    }

    /* TODO: make sure that the trove interface is initialized */

    /* make sure that the default buffer size does not exceed the
     * max buffer size allowed by BMI
     */
    if ((ret = BMI_get_info(0, BMI_CHECK_MAXSIZE, &tmp_maxsize)) < 0)
    {
	return (ret);
    }
    if (tmp_maxsize < DEFAULT_BUFFER_SIZE)
    {
	DEFAULT_BUFFER_SIZE = tmp_maxsize;
    }
    if (DEFAULT_BUFFER_SIZE < 1)
    {
	gossip_lerr("Error: BMI buffer size too small!\n");
	return (-EINVAL);
    }

    /* get a BMI context */
    ret = BMI_open_context(&global_bmi_context);
    if(ret < 0)
    {
	return(ret);
    }

    /* setup our queues to track low level operations */
    trove_inflight_queue = trove_id_queue_new();
    if (!trove_inflight_queue)
    {
	BMI_close_context(global_bmi_context);
	return (-ENOMEM);
    }

    flowproto_bmi_trove_id = flowproto_id;

    return (0);
}

/* flowproto_bmi_trove_finalize()
 *
 * shuts down the flow protocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_finalize(void)
{
    if (trove_op_array)
    {
	free(trove_op_array);
    }
    trove_op_array_len = 0;

    trove_id_queue_cleanup(trove_inflight_queue);

    BMI_close_context(global_bmi_context);

    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_bmi_trove shut down.\n");
    return (0);
}

/* flowproto_bmi_trove_getinfo()
 *
 * reads optional parameters from the flow protocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_getinfo(flow_descriptor * flow_d,
				int option,
				void *parameter)
{
    int* type;

    switch (option)
    {
    case FLOWPROTO_SUPPORT_QUERY:
	return (check_support(parameter));
    case FLOWPROTO_TYPE_QUERY:
	type = parameter;
	if(*type == FLOWPROTO_BMI_TROVE)
	    return(0);
	else
	    return(-ENOPROTOOPT);
    default:
	return (-ENOSYS);
	break;
    }
}

/* flowproto_bmi_trove_setinfo()
 *
 * sets optional flow protocol parameters
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_setinfo(flow_descriptor * flow_d,
				int option,
				void *parameter)
{
    return (-ENOSYS);
}

/* flowproto_bmi_trove_memalloc()
 *
 * allocates a memory region optimized for use with the flow protocol
 *
 * returns pointer to buffer on success, NULL on failure
 */
void *flowproto_bmi_trove_memalloc(flow_descriptor * flow_d,
				   PVFS_size size,
				   int send_recv_flag)
{
    return (NULL);
}

/* flowproto_bmi_trove_memfree()
 *
 * releases a buffer previously allocated with memalloc()
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_memfree(flow_descriptor * flow_d,
				void *buffer,
				int send_recv_flag)
{
    return (-ENOSYS);
}

/* flowproto_bmi_trove_announce_flow()
 *
 * informs the flow protocol that it is responsible for the given flow
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_announce_flow(flow_descriptor * flow_d)
{
    int ret = -1;

    ret = alloc_flow_data(flow_d);
    if (ret < 0)
    {
	return (ret);
    }

    flow_d->flowproto_id = flowproto_bmi_trove_id;

    if (ret == 1)
    {
	/* already done! */
	flow_d->state = FLOW_COMPLETE;
    }
    else
    {
	/* we are ready for service now */
	flow_d->state = FLOW_SVC_READY;
    }

    return (0);
}

/* flowproto_bmi_trove_checkworld()
 *
 * checks to see if any previously posted flows need to be serviced
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_checkworld(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms)
{
    PVFS_ds_state *trove_error_code_array = NULL;
    void **trove_usrptr_array = NULL;
    int *trove_index_array = NULL;
    int trove_count = *count;
    int bmi_outcount = 0;
    int ret = -1;
    int i = 0;
    flow_descriptor *active_flowd = NULL;
    struct bmi_trove_flow_data *flow_data = NULL;
    int incount = *count;
    PVFS_ds_id *trove_op_array = NULL;
    int split_idle_time_ms = max_idle_time_ms;
    int query_offset = 0;
    PVFS_coll_id tmp_coll_id;

    /* TODO: do something more clever with the max_idle_time_ms
     * argument.  For now we just split it evenly among the
     * interfaces that we are talking to
     */

    /* what to do here: 
     * 1) test for completion of any in flight bmi operations
     * 2) handle any flows impacted by above, add to done_checking_queue
     *              as needed;
     * 3) test for completion of any in flight trove operations
     * 4) handle any flows impacted by above, add to done_checking_queue
     *      as needed
     * 5) collect flows out of the done_checking_queue and return in
     *      array
     */

    /* build arrays to use for checking for completion of low level ops */
    trove_op_array = alloca(sizeof(PVFS_ds_id) * (trove_count));
    if (!trove_op_array)
    {
	return (-ENOMEM);
    }
    /* divide up the idle time if we need to */
    if (max_idle_time_ms && bmi_pending_count && trove_pending_count)
    {
	split_idle_time_ms = max_idle_time_ms / 2;
	if (!split_idle_time_ms)
	    split_idle_time_ms = 1;
    }

    if (bmi_pending_count > 0)
    {
	/* test for completion */
	ret = BMI_testcontext(BMI_TEST_SIZE, bmi_op_array, &bmi_outcount,
			   bmi_error_code_array,
			   bmi_actualsize_array, bmi_usrptr_array,
			   split_idle_time_ms, global_bmi_context);
	if (ret < 0)
	{
	    return (ret);
	}

	bmi_pending_count -= bmi_outcount;

	/* handle each completed bmi operation */
	for (i = 0; i < bmi_outcount; i++)
	{
	    active_flowd = bmi_completion(bmi_error_code_array[i],
					  bmi_actualsize_array[i],
					  bmi_usrptr_array[i]);

	    /* put flows into done_checking_queue if needed */
	    if (active_flowd->state & FLOW_FINISH_MASK ||
		active_flowd->state == FLOW_SVC_READY)
	    {
		qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			       &done_checking_queue);
	    }
	}
    }

    /* manage in flight trove operations */
    if (trove_pending_count > 0)
    {
	trove_count = incount;
	while((ret = trove_id_queue_query(trove_inflight_queue, trove_op_array, 
	    &trove_count, &query_offset, &tmp_coll_id)) == 0)
	{
	    /* TODO: this is stupid.  Make these fixed size arrays */
	    trove_error_code_array = malloc(sizeof(PVFS_ds_state) * trove_count);
	    trove_index_array = malloc(sizeof(int) * trove_count);
	    trove_usrptr_array = malloc(sizeof(void *) * trove_count);
	    if (!trove_error_code_array || !trove_index_array ||
		!trove_usrptr_array)
	    {
		return (-ENOMEM);
	    }

	    /* test for completion */
	    ret = trove_dspace_testsome(tmp_coll_id, trove_op_array,
					&trove_count, trove_index_array, NULL,
					trove_usrptr_array, trove_error_code_array);
	    if (ret < 0)
	    {
		/* TODO: cleanup properly */
		gossip_lerr("Error: unimplemented condition encountered.\n");
		exit(-1);
		return (ret);
	    }

	    /* handle each completed trove operation */
	    for (i = 0; i < trove_count; i++)
	    {
		active_flowd = trove_completion(trove_error_code_array[i],
						trove_usrptr_array[i]);
		trove_id_queue_del(trove_inflight_queue,
				trove_op_array[trove_index_array[i]], 
				tmp_coll_id);
		trove_pending_count--;

		/* put flows into done_checking_queue if needed */
		if (active_flowd->state & FLOW_FINISH_MASK ||
		    active_flowd->state == FLOW_SVC_READY)
		{
		    qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
				   &done_checking_queue);
		}
	    }
	    trove_count = incount;
	    free(trove_error_code_array);
	    free(trove_index_array);
	    free(trove_usrptr_array);
	}
	assert(ret == -EAGAIN);    
    }

    /* collect flows out of the done_checking_queue and return */
    *count = 0;
    while (*count < incount && !qlist_empty(&done_checking_queue))
    {
	flow_data = qlist_entry(done_checking_queue.next,
				struct bmi_trove_flow_data,
				queue_link);
	active_flowd = flow_data->parent;
	qlist_del(done_checking_queue.next);
	if (active_flowd->state & FLOW_FINISH_MASK)
	{
	    release_flow(active_flowd);
	}
	flow_d_array[*count] = active_flowd;
	(*count)++;
    }
    return (0);
}

/* flowproto_bmi_trove_service()
 *
 * services a single flow descriptor
 *
 * returns 0 on success, -ERRNO on failure
 */
int flowproto_bmi_trove_service(flow_descriptor * flow_d)
{
    /* our job here is to:
     * 1) swap buffers as needed 
     * 2) post BMI and Trove operations as needed
     * we do no testing here, and only accept flows in the
     *    "FLOW_SVC_READY" state
     */
    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_bmi_trove_service() called.\n");

    if (flow_d->state != FLOW_SVC_READY)
    {
	gossip_lerr("Error: invalid state.\n");
	return (-EINVAL);
    }

    /* handle the flow differently depending on what type it is */
    /* we don't check return values because errors are indicated by the
     * flow->state at this level
     */
    switch (PRIVATE_FLOW(flow_d)->type)
    {
    case BMI_TO_MEM:
	service_bmi_to_mem(flow_d);
	break;
    case MEM_TO_BMI:
	service_mem_to_bmi(flow_d);
	break;
    case TROVE_TO_BMI:
	service_trove_to_bmi(flow_d);
	break;
    case BMI_TO_TROVE:
	service_bmi_to_trove(flow_d);
	break;
    default:
	gossip_lerr("Error; unknown or unsupported endpoint pair.\n");
	return (-EINVAL);
	break;
    }

    /* clean up before returning if the flow completed */
    if (flow_d->state & FLOW_FINISH_MASK)
    {
	release_flow(flow_d);
    }

    return (0);
}

/*******************************************************
 * definitions for internal utility functions
 */

/* release_flow()
 *
 * used by protocol to reliquish control of a flow (and free any
 * internal resources associated with it) upon completion
 *
 * no return value
 */
static void release_flow(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *tmp_data = PRIVATE_FLOW(flow_d);

    switch (tmp_data->type)
    {
    case BMI_TO_MEM:
	buffer_teardown_bmi_to_mem(flow_d);
	break;
    case MEM_TO_BMI:
	buffer_teardown_mem_to_bmi(flow_d);
	break;
    case TROVE_TO_BMI:
	buffer_teardown_trove_to_bmi(flow_d);
	break;
    case BMI_TO_TROVE:
	buffer_teardown_bmi_to_trove(flow_d);
	break;
    default:
	gossip_lerr("Error: Unknown/unimplemented endpoint combination.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = -EINVAL;
	break;
    }

    /* free flowproto data */
    free(flow_d->flow_protocol_data);
    flow_d->flow_protocol_data = NULL;

    return;
}

/* buffer_teardown_bmi_to_mem()
 *
 * cleans up buffer resources for a particular type of flow
 *
 * returns 0 on success, -errno on failure
 */
static void buffer_teardown_bmi_to_mem(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* destroy any intermediate buffers */
    if (flow_data->intermediate_buffer)
    {
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->intermediate_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
    }

    return;
}

/* buffer_teardown_mem_to_bmi()
 *
 * cleans up buffer resources for a particular type of flow
 *
 * returns 0 on success, -errno on failure
 */
static void buffer_teardown_mem_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* destroy any intermediate buffers */
    if (flow_data->intermediate_buffer)
    {
	BMI_memfree(flow_d->dest.u.bmi.address,
		    flow_data->intermediate_buffer, flow_data->max_buffer_size,
		    BMI_SEND);
    }

    return;
}

/* buffer_setup_bmi_to_mem()
 *
 * sets up initial internal buffer configuration for a particular type
 * of flow
 *
 * returns 0 on success, -errno on failure
 */
static int buffer_setup_bmi_to_mem(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    PVFS_boolean eof_flag = 0;
    int i = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    /* call req processing code to get first set of segments */
    flow_data->bmi_total_size = flow_data->max_buffer_size;
    flow_data->bmi_list_count = MAX_REGIONS;

    gossip_ldebug(FLOW_PROTO_DEBUG, "req proc offset: %ld\n",
		  (long) flow_d->current_req_offset);
    ret = PINT_Process_request(flow_d->request_state,
			       flow_d->file_data, &flow_data->bmi_list_count,
			       flow_data->bmi_offset_list,
			       flow_data->bmi_size_list,
			       &flow_d->current_req_offset,
			       &flow_data->bmi_total_size, &eof_flag,
			       PINT_CLIENT);
    if (ret < 0)
    {
	return (ret);
    }

    if (eof_flag && (flow_data->bmi_total_size == 0))
    {
	/* no work to do here, return 1 to complete the flow */
	return (1);
    }

    /* did we provide enough segments to satisfy the amount of data
     * available < buffer size? 
     */
    if (!eof_flag && flow_d->current_req_offset != -1 &&
	flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	/* We aren't at the end, but we didn't get what we asked
	 * for.  In this case, we want to hang onto the segment
	 * descriptions, but provide an intermediate buffer of
	 * max_buffer_size to be able to handle the next 
	 * incoming message.  Copy out later.  
	 */
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: falling back to intermediate buffer.\n");
	if (flow_data->bmi_list_count != MAX_REGIONS)
	{
	    gossip_lerr("Error: reached an unexpected req processing state.\n");
	    return (-EINVAL);
	}
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    /* setup buffer list */
	    flow_data->bmi_buffer_list[i] =
		flow_d->dest.u.mem.buffer + flow_data->bmi_offset_list[i];
	}
	/* allocate an intermediate buffer if not already present */
	if (!flow_data->intermediate_buffer)
	{
	    flow_data->intermediate_buffer =
		BMI_memalloc(flow_d->src.u.bmi.address,
			     flow_data->max_buffer_size, BMI_RECV);
	    if (!flow_data->intermediate_buffer)
	    {
		return (-ENOMEM);
	    }
	}
    }
    else
    {
	/* "normal" case */
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    /* setup buffer list */
	    flow_data->bmi_buffer_list[i] =
		flow_d->dest.u.mem.buffer + flow_data->bmi_offset_list[i];
	}
    }

    return (0);
}

/* buffer_setup_mem_to_bmi()
 *
 * sets up initial internal buffer configuration for a particular type
 * of flow
 *
 * returns 0 on success, -errno on failure
 */
static int buffer_setup_mem_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    PVFS_boolean eof_flag = 0;
    int i = 0;
    int intermediate_offset = 0;
    char *dest_ptr = NULL;
    char *src_ptr = NULL;
    int done_flag = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    /* call req processing code to get first set of segments */
    flow_data->bmi_total_size = flow_data->max_buffer_size;
    flow_data->bmi_list_count = MAX_REGIONS;

    gossip_ldebug(FLOW_PROTO_DEBUG, "req proc offset: %ld\n",
		  (long) flow_d->current_req_offset);
    ret = PINT_Process_request(flow_d->request_state,
			       flow_d->file_data, &flow_data->bmi_list_count,
			       flow_data->bmi_offset_list,
			       flow_data->bmi_size_list,
			       &flow_d->current_req_offset,
			       &flow_data->bmi_total_size, &eof_flag,
			       PINT_CLIENT);
    if (ret < 0)
    {
	return (ret);
    }

    /* did we provide enough segments to satisfy the amount of data
     * available < buffer size?
     */
    if (!eof_flag && flow_d->current_req_offset != -1 &&
	flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	/* we aren't at the end, but we didn't get the amount of data that
	 * we asked for.  In this case, we should pack into an
	 * intermediate buffer to send with BMI, because apparently we
	 * have a lot of small segments to deal with 
	 */
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: falling back to intermediate buffer.\n");
	if (flow_data->bmi_list_count != MAX_REGIONS)
	{
	    gossip_lerr("Error: reached an unexpected req processing state.\n");
	    return (-EINVAL);
	}

	/* allocate an intermediate buffer if not already present */
	if (!flow_data->intermediate_buffer)
	{
	    flow_data->intermediate_buffer =
		BMI_memalloc(flow_d->dest.u.bmi.address,
			     flow_data->max_buffer_size, BMI_SEND);
	    if (!flow_data->intermediate_buffer)
	    {
		return (-ENOMEM);
	    }
	}

	/* now, cycle through copying a full buffer's worth of data into
	 * a contiguous intermediate buffer
	 */
	do
	{
	    for (i = 0; i < flow_data->bmi_list_count; i++)
	    {
		dest_ptr = ((char *) flow_data->intermediate_buffer +
			    intermediate_offset);
		src_ptr = ((char *) flow_d->src.u.mem.buffer +
			   flow_data->bmi_offset_list[i]);
		memcpy(dest_ptr, src_ptr, flow_data->bmi_size_list[i]);
		intermediate_offset += flow_data->bmi_size_list[i];
	    }

	    if (!eof_flag && flow_d->current_req_offset != -1 &&
		intermediate_offset < flow_data->max_buffer_size)
	    {
		flow_data->bmi_list_count = MAX_REGIONS;
		flow_data->bmi_total_size = flow_data->max_buffer_size -
		    intermediate_offset;

		ret = PINT_Process_request(flow_d->request_state,
					   flow_d->file_data,
					   &flow_data->bmi_list_count,
					   flow_data->bmi_offset_list,
					   flow_data->bmi_size_list,
					   &flow_d->current_req_offset,
					   &flow_data->bmi_total_size,
					   &eof_flag, PINT_CLIENT);
		if (ret < 0)
		{
		    return (ret);
		}
	    }
	    else
	    {
		done_flag = 1;
	    }

	} while (!done_flag);

	/* set pointers and such to intermediate buffer so that we send
	 * from the right place on the next bmi send operation
	 */
	flow_data->bmi_list_count = 1;
	flow_data->bmi_buffer_list[0] = flow_data->intermediate_buffer;
	flow_data->bmi_size_list[0] = intermediate_offset;
	flow_data->bmi_total_size = intermediate_offset;

    }
    else
    {
	/* setup buffer list with respect to user provided region */
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    /* setup buffer list */
	    flow_data->bmi_buffer_list[i] =
		flow_d->src.u.mem.buffer + flow_data->bmi_offset_list[i];
	}
    }

    return (0);
}

/* buffer_setup_bmi_to_trove()
 *
 * sets up initial internal buffer configuration for a particular type
 * of flow
 *
 * returns 0 on success, -errno on failure
 */
static int buffer_setup_bmi_to_trove(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    /* create two buffers and set them to initial states */
    flow_data->fill_buffer =
	BMI_memalloc(flow_d->src.u.bmi.address,
		     flow_data->max_buffer_size, BMI_RECV);
    if (!flow_data->fill_buffer)
    {
	return (-ENOMEM);
    }
    flow_data->fill_buffer_state = BUF_READY_TO_FILL;

    flow_data->drain_buffer =
	BMI_memalloc(flow_d->src.u.bmi.address,
		     flow_data->max_buffer_size, BMI_RECV);
    if (!flow_data->drain_buffer)
    {
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->fill_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
	return (-ENOMEM);
    }
    flow_data->drain_buffer_state = BUF_READY_TO_SWAP;

    /* set up a duplicate request processing state so that the bmi
     * receive half of this flow can figure out its position independent
     * of what the trove half is doing 
     */
    flow_data->dup_req_state = PINT_New_request_state(flow_d->request);
    flow_data->dup_req_offset = 0;
    if (!flow_data->dup_req_state)
    {
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->fill_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->drain_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
	return (-ENOMEM);
    }
    return (0);
}


/* buffer_setup_trove_to_bmi()
 *
 * sets up initial internal buffer configuration for a particular type
 * of flow
 *
 * returns 0 on success, -errno on failure
 */
static int buffer_setup_trove_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    PVFS_boolean eof_flag = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    /* create two buffers and set them to initial states */
    flow_data->fill_buffer =
	BMI_memalloc(flow_d->dest.u.bmi.address,
		     flow_data->max_buffer_size, BMI_SEND);
    if (!flow_data->fill_buffer)
    {
	return (-ENOMEM);
    }
    flow_data->fill_buffer_state = BUF_READY_TO_FILL;

    flow_data->drain_buffer =
	BMI_memalloc(flow_d->dest.u.bmi.address,
		     flow_data->max_buffer_size, BMI_SEND);
    if (!flow_data->drain_buffer)
    {
	BMI_memfree(flow_d->dest.u.bmi.address,
		    flow_data->fill_buffer, flow_data->max_buffer_size,
		    BMI_SEND);
	return (-ENOMEM);
    }
    flow_data->drain_buffer_state = BUF_READY_TO_SWAP;

    /* call req processing code to get first set of segments to
     * read from disk */
    flow_data->fill_buffer_used = 0;
    flow_data->trove_list_count = MAX_REGIONS;
    flow_data->fill_buffer_stepsize = flow_data->max_buffer_size;

    ret = PINT_Process_request(flow_d->request_state,
			       flow_d->file_data, &flow_data->trove_list_count,
			       flow_data->trove_offset_list,
			       flow_data->trove_size_list,
			       &flow_d->current_req_offset,
			       &flow_data->fill_buffer_stepsize, &eof_flag,
			       PINT_SERVER);
    if (ret < 0)
    {
	return (ret);
    }

    if (eof_flag && (flow_data->fill_buffer_stepsize == 0))
    {
	/* there is no work to do; zero length flow */
	return (1);
    }

    if (flow_data->fill_buffer_stepsize < flow_data->max_buffer_size
	&& flow_d->current_req_offset != -1)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: going into multistage mode for trove to bmi flow.\n");
    }

    return (0);
}



/* check_support()
 *
 * handles queries about which endpoint types we support
 *
 * returns 0 on success, -errno on failure
 */
static int check_support(struct flowproto_type_support *type)
{
    /* bmi to memory */
    if (type->src_endpoint_id == BMI_ENDPOINT &&
	type->dest_endpoint_id == MEM_ENDPOINT)
    {
	return (0);
    }

    /* memory to bmi */
    if (type->src_endpoint_id == MEM_ENDPOINT &&
	type->dest_endpoint_id == BMI_ENDPOINT)
    {
	return (0);
    }

    /* bmi to storage */
    if (type->src_endpoint_id == BMI_ENDPOINT &&
	type->dest_endpoint_id == TROVE_ENDPOINT)
    {
	return (0);
    }

    /* storage to bmi */
    if (type->src_endpoint_id == TROVE_ENDPOINT &&
	type->dest_endpoint_id == BMI_ENDPOINT)
    {
	return (0);
    }

    /* we don't know about anything else */
    return (-ENOPROTOOPT);
}


/* alloc_flow_data()
 *
 * fills in the part of the flow descriptor that is private to this
 * protocol
 *
 * returns 0 on success, -errno on failure
 */
static int alloc_flow_data(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = NULL;
    int ret = -1;
    PVFS_size extent = 0;

    /* allocate the structure */
    flow_data = (struct bmi_trove_flow_data *) malloc(sizeof(struct
							     bmi_trove_flow_data));
    if (!flow_data)
    {
	return (-errno);
    }
    memset(flow_data, 0, sizeof(struct bmi_trove_flow_data));
    flow_d->flow_protocol_data = flow_data;
    flow_data->parent = flow_d;

    /* the rest of the buffer setup varies depending on the endpoints */
    if (flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == MEM_ENDPOINT)
    {
	/* sanity check size of mem buffer */
	ret = PVFS_Request_extent(flow_d->request, &extent);
	if (ret < 0)
	{
	    free(flow_data);
	    return (ret);
	}

	if (extent > flow_d->dest.u.mem.size)
	{
	    gossip_lerr("Error: buffer not large enough for request.\n");
	    free(flow_data);
	    return (-EINVAL);
	}

	flow_data->type = BMI_TO_MEM;
	ret = buffer_setup_bmi_to_mem(flow_d);
    }
    else if (flow_d->src.endpoint_id == MEM_ENDPOINT &&
	     flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	/* sanity check size of mem buffer */
	ret = PVFS_Request_extent(flow_d->request, &extent);
	if (ret < 0)
	{
	    free(flow_data);
	    return (ret);
	}

	if (extent > flow_d->src.u.mem.size)
	{
	    gossip_lerr("Error: buffer not large enough for request.\n");
	    free(flow_data);
	    return (-EINVAL);
	}

	flow_data->type = MEM_TO_BMI;
	ret = buffer_setup_mem_to_bmi(flow_d);
    }
    else if (flow_d->src.endpoint_id == TROVE_ENDPOINT &&
	     flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	flow_data->type = TROVE_TO_BMI;
	ret = buffer_setup_trove_to_bmi(flow_d);
    }
    else if (flow_d->src.endpoint_id == BMI_ENDPOINT &&
	     flow_d->dest.endpoint_id == TROVE_ENDPOINT)
    {
	flow_data->type = BMI_TO_TROVE;
	ret = buffer_setup_bmi_to_trove(flow_d);
    }
    else
    {
	gossip_lerr("Error: Unknown/unimplemented endpoint combination.\n");
	free(flow_data);
	return (-EINVAL);
    }

    if (ret < 0)
    {
	free(flow_data);
    }

    /* NOTE: we may return 1 here; the caller will catch it */
    return (ret);
}


/* service_mem_to_bmi() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_mem_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    enum bmi_buffer_type buffer_type;

    /* make sure BMI knows if we are using an intermediate buffer or not,
     * because those have been created with bmi_memalloc()
     */
    if (flow_data->bmi_buffer_list[0] == flow_data->intermediate_buffer)
    {
	buffer_type = BMI_PRE_ALLOC;
    }
    else
    {
	buffer_type = BMI_EXT_ALLOC;
    }

    /* post list send */
    gossip_ldebug(FLOW_PROTO_DEBUG, "Posting send, total size: %ld\n",
		  (long) flow_data->bmi_total_size);
    ret = BMI_post_send_list(&flow_data->bmi_id,
			     flow_d->dest.u.bmi.address,
			     flow_data->bmi_buffer_list,
			     flow_data->bmi_size_list,
			     flow_data->bmi_list_count,
			     flow_data->bmi_total_size, buffer_type,
			     flow_d->tag, flow_d, global_bmi_context);
    if (ret == 1)
    {
	/* handle immediate completion */
	bmi_completion_mem_to_bmi(0, flow_d);
    }
    else if (ret == 0)
    {
	/* successful post, need to test later */
	bmi_pending_count++;
	flow_d->state = FLOW_TRANSMITTING;
    }
    else
    {
	/* error posting operation */
	flow_d->state = FLOW_DEST_ERROR;
	flow_d->error_code = ret;
    }

    return;
}

/* service_bmi_to_mem() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_bmi_to_mem(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    PVFS_size actual_size = 0;

    /* we should be ready to post the next operation when we get to
     * this function 
     */

    /* are we using an intermediate buffer? */
    if (flow_d->current_req_offset != -1 &&
	flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: posting recv to intermediate buffer.\n");

	/* post receive to contig. intermediate buffer */
	gossip_ldebug(FLOW_PROTO_DEBUG, "Posting recv, total size: %ld\n",
		      (long) flow_data->max_buffer_size);
	ret = BMI_post_recv(&flow_data->bmi_id,
			    flow_d->src.u.bmi.address,
			    flow_data->intermediate_buffer,
			    flow_data->max_buffer_size, &actual_size,
			    BMI_PRE_ALLOC, flow_d->tag, flow_d,
			    global_bmi_context);
    }
    else
    {
	
	if(flow_data->bmi_total_size == 0)
	{
	    gossip_lerr("WARNING: encountered odd request state; assuming flow is done.\n");
	    flow_d->state = FLOW_COMPLETE;
	    return;
	}

	/* post normal list operation */
	gossip_ldebug(FLOW_PROTO_DEBUG, "Posting recv, total size: %ld\n",
		      (long) flow_data->bmi_total_size);
	ret = BMI_post_recv_list(&flow_data->bmi_id,
				 flow_d->src.u.bmi.address,
				 flow_data->bmi_buffer_list,
				 flow_data->bmi_size_list,
				 flow_data->bmi_list_count,
				 flow_data->bmi_total_size, &actual_size,
				 BMI_EXT_ALLOC, flow_d->tag, flow_d,
				 global_bmi_context);
    }

    gossip_ldebug(FLOW_PROTO_DEBUG, "Recv post returned %d\n", ret);

    if (ret == 1)
    {
	/* handle immediate completion */
	bmi_completion_bmi_to_mem(0, actual_size, flow_d);
    }
    else if (ret == 0)
    {
	/* successful post, need to test later */
	bmi_pending_count++;
	flow_d->state = FLOW_TRANSMITTING;
    }
    else
    {
	/* error posting operation */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = ret;
    }

    return;
}

/* service_bmi_to_trove() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_bmi_to_trove(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    void *tmp_buffer;
    PVFS_size tmp_used;
    int ret = -1;
    PVFS_boolean eof_flag = 0;
    char *tmp_offset;
    PVFS_size actual_size = 0;
    int32_t segmax = 0;

    gossip_ldebug(FLOW_PROTO_DEBUG, "service_bmi_to_trove() called.\n");

    /* first, swap buffers if we need to */
    if (flow_data->fill_buffer_state == BUF_READY_TO_SWAP &&
	flow_data->drain_buffer_state == BUF_READY_TO_SWAP)
    {
	tmp_buffer = flow_data->fill_buffer;
	tmp_used = flow_data->fill_buffer_used;
	flow_data->fill_buffer = flow_data->drain_buffer;
	flow_data->fill_buffer_offset = 0;
	flow_data->fill_buffer_stepsize = 0;
	flow_data->fill_buffer_used = 0;
	flow_data->fill_buffer_state = BUF_READY_TO_FILL;
	flow_data->drain_buffer = tmp_buffer;
	flow_data->drain_buffer_used = tmp_used;
	flow_data->drain_buffer_state = BUF_READY_TO_DRAIN;
	flow_data->drain_buffer_offset = 0;
	flow_data->drain_buffer_stepsize = 0;

	/* call req processing code to get next set of segments to
	 * write to disk */
	flow_data->drain_buffer_stepsize = flow_data->drain_buffer_used;
	flow_data->trove_list_count = MAX_REGIONS;

	ret = PINT_Process_request(flow_d->request_state,
				   flow_d->file_data,
				   &flow_data->trove_list_count,
				   flow_data->trove_offset_list,
				   flow_data->trove_size_list,
				   &flow_d->current_req_offset,
				   &(flow_data->drain_buffer_stepsize),
				   &eof_flag, PINT_SERVER);
	if (ret < 0)
	{
	    gossip_lerr("Error: PINT_Process_request() failure.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    /* no ops in flight, so we can just kick out error here */
	    return;
	}
#if 0
	gossip_err("FOO1: current_req_offset: %d\n",
		   (int) flow_d->current_req_offset);
	gossip_err("FOO1: trove_list_count: %d\n",
		   (int) flow_data->trove_list_count);
	gossip_err("FOO1: trove_offset_list[0]: %d\n",
		   (int) flow_data->trove_offset_list[0]);
	gossip_err("FOO1: trove_size_list[0]: %d\n",
		   (int) flow_data->trove_size_list[0]);
#endif

	if (flow_data->drain_buffer_stepsize != flow_data->drain_buffer_used)
	{
	    gossip_ldebug(FLOW_PROTO_DEBUG,
			  "Warning: entering multi stage write mode.\n");
	}
    }

    /* post a BMI operation if we can */
    if (flow_data->fill_buffer_state == BUF_READY_TO_FILL)
    {
	/* have we gotten all of the data already? */
	if (flow_data->dup_req_offset == -1)
	{
	    flow_data->fill_buffer_state = BUF_DONE;
	}
	else
	{
	    /* see how much more is in the pipe */
	    flow_data->bmi_total_size = flow_data->max_buffer_size;
	    eof_flag = 0;
	    ret = PINT_Process_request(flow_data->dup_req_state,
		flow_d->file_data,
		&segmax,
		NULL,
		NULL,
		&flow_data->dup_req_offset,
		&flow_data->bmi_total_size,
		&eof_flag,
		PINT_CKSIZE_MODIFY_OFFSET);
	    if (ret < 0)
	    {
		/* TODO: do something */
		gossip_lerr("Error: unimplemented condition encountered.\n");
		exit(-1);
	    }

	    flow_data->fill_buffer_used = flow_data->bmi_total_size;

	    gossip_ldebug(FLOW_PROTO_DEBUG, "Posting recv, total size: %ld\n",
			  (long) flow_data->fill_buffer_used);

	    ret = BMI_post_recv(&flow_data->bmi_id,
				flow_d->src.u.bmi.address,
				flow_data->fill_buffer,
				flow_data->bmi_total_size, &actual_size,
				BMI_PRE_ALLOC, flow_d->tag, flow_d,
				global_bmi_context);
	    if (ret == 1)
	    {
		bmi_completion_bmi_to_trove(0, actual_size, flow_d);
	    }
	    else if (ret == 0)
	    {
		/* successful post, need to test later */
		bmi_pending_count++;
		flow_d->state = FLOW_TRANSMITTING;
		flow_data->fill_buffer_state = BUF_FILLING;
	    }
	    else
	    {
		/* error posting operation */
		flow_d->state = FLOW_DEST_ERROR;
		flow_d->error_code = ret;
	    }
	}
    }

    /* do we have an error to clean up? */
    if (flow_d->state == FLOW_ERROR)
    {
	/* TODO: clean up other side if needed then return */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
    }

    /* do we have trove work to post? */
    flow_data->trove_total_size = flow_data->drain_buffer_stepsize;
    if (flow_data->drain_buffer_state == BUF_READY_TO_DRAIN)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "about to call trove_bstream_write_list().\n");
	tmp_offset = flow_data->drain_buffer + flow_data->drain_buffer_offset;
	ret = trove_bstream_write_list(flow_d->dest.u.trove.coll_id,
				       flow_d->dest.u.trove.handle,
				       &(tmp_offset),
				       &(flow_data->trove_total_size), 1,
				       flow_data->trove_offset_list,
				       flow_data->trove_size_list,
				       flow_data->trove_list_count,
				       &(flow_data->drain_buffer_stepsize), 0,
				       NULL, flow_d, &(flow_data->trove_id));
	if (ret == 1)
	{
	    /* handle immediate completion */
	    /* this function will set the flow state */
	    trove_completion_bmi_to_trove(0, flow_d);
	}
	else if (ret == 0)
	{
	    /* successful post, need to test later */
	    flow_d->state = FLOW_TRANSMITTING;
	    flow_data->drain_buffer_state = BUF_DRAINING;
	    /* keep up with the trove id */
	    if ((ret = trove_id_queue_add(trove_inflight_queue,
				       flow_data->trove_id,
				       flow_d->dest.u.trove.coll_id)) < 0)
	    {
		/* lost track of the id */
		flow_d->state = FLOW_ERROR;
		flow_d->error_code = ret;
		/* TODO: cleanup properly */
		gossip_lerr("Error: unimplemented condition encountered.\n");
		exit(-1);
	    }
	    trove_pending_count++;
	}
	else
	{
	    /* error posting operation */
	    flow_d->state = FLOW_SRC_ERROR;
	    flow_d->error_code = ret;
	    /* TODO: clean up properly */
	    gossip_lerr("return value: %d\n", ret);
	    gossip_lerr("Error: unimplemented condition encountered.\n");
	    exit(-1);
	}
    }

    return;
}

/* service_trove_to_bmi() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_trove_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    void *tmp_buffer;
    PVFS_size tmp_used;
    int ret = -1;
    PVFS_boolean eof_flag = 0;
    char *tmp_offset;

    gossip_ldebug(FLOW_PROTO_DEBUG, "service_trove_to_bmi() called.\n");

    /* first, swap buffers if we need to */
    if (flow_data->fill_buffer_state == BUF_READY_TO_SWAP &&
	flow_data->drain_buffer_state == BUF_READY_TO_SWAP)
    {
	tmp_buffer = flow_data->fill_buffer;
	tmp_used = flow_data->fill_buffer_used;
	flow_data->fill_buffer = flow_data->drain_buffer;
	flow_data->fill_buffer_used = 0;
	flow_data->fill_buffer_state = BUF_READY_TO_FILL;
	flow_data->fill_buffer_stepsize = 0;
	flow_data->fill_buffer_offset = 0;
	flow_data->drain_buffer = tmp_buffer;
	flow_data->drain_buffer_used = tmp_used;
	flow_data->drain_buffer_state = BUF_READY_TO_DRAIN;
	flow_data->drain_buffer_stepsize = 0;
	flow_data->drain_buffer_offset = 0;

	/* call req processing code to get next set of segments to
	 * read from disk */
	flow_data->trove_list_count = MAX_REGIONS;
	flow_data->fill_buffer_stepsize = flow_data->max_buffer_size;
	if (flow_d->current_req_offset != -1)
	{
	    ret = PINT_Process_request(flow_d->request_state,
				       flow_d->file_data,
				       &flow_data->trove_list_count,
				       flow_data->trove_offset_list,
				       flow_data->trove_size_list,
				       &flow_d->current_req_offset,
				       &flow_data->fill_buffer_stepsize,
				       &eof_flag, PINT_SERVER);
	    if (ret < 0)
	    {
		gossip_lerr("Error: PINT_Process_request() failure.\n");
		flow_d->state = FLOW_ERROR;
		flow_d->error_code = ret;
		/* no ops in flight, so we can just kick out error here */
		return;
	    }

	    if (flow_data->fill_buffer_stepsize < flow_data->max_buffer_size
		&& flow_d->current_req_offset != -1)
	    {
		gossip_ldebug(FLOW_PROTO_DEBUG,
			      "Warning: going into multistage mode for trove to bmi flow.\n");
	    }
	}
	else
	{
	    /* trove side is already done */
	    flow_data->fill_buffer_state = BUF_DONE;
	}
    }

    /* post a BMI operation if we can */
    if (flow_data->drain_buffer_state == BUF_READY_TO_DRAIN)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG, "Posting send, total size: %ld\n",
		      (long) flow_data->drain_buffer_used);
	ret = BMI_post_send(&flow_data->bmi_id,
			    flow_d->dest.u.bmi.address, flow_data->drain_buffer,
			    flow_data->drain_buffer_used,
			    BMI_PRE_ALLOC, flow_d->tag, flow_d,
			    global_bmi_context);
	if (ret == 1)
	{
	    /* handle immediate completion */
	    bmi_completion_trove_to_bmi(0, flow_d);
	}
	else if (ret == 0)
	{
	    /* successful post, need to test later */
	    bmi_pending_count++;
	    flow_d->state = FLOW_TRANSMITTING;
	    flow_data->drain_buffer_state = BUF_DRAINING;
	}
	else
	{
	    /* error posting operation */
	    flow_d->state = FLOW_DEST_ERROR;
	    flow_d->error_code = ret;
	}
    }

    /* are we done? */
    if (flow_d->state == FLOW_COMPLETE)
    {
	return;
    }

    /* do we have an error to clean up? */
    if (flow_d->state == FLOW_ERROR)
    {
	/* TODO: clean up other side if needed then return */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
    }

    flow_data->trove_total_size = flow_data->fill_buffer_stepsize;
    /* do we have trove work to post? */
    if (flow_data->fill_buffer_state == BUF_READY_TO_FILL)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "about to call trove_bstream_read_list().\n");
	tmp_offset = flow_data->fill_buffer + flow_data->fill_buffer_offset;
	ret = trove_bstream_read_list(flow_d->src.u.trove.coll_id,
				      flow_d->src.u.trove.handle, &(tmp_offset),
				      &(flow_data->trove_total_size), 1,
				      flow_data->trove_offset_list,
				      flow_data->trove_size_list,
				      flow_data->trove_list_count,
				      &(flow_data->fill_buffer_stepsize), 0,
				      NULL, flow_d, &(flow_data->trove_id));
	if (ret == 1)
	{
	    /* handle immediate completion */
	    /* this function will set the flow state */
	    trove_completion_trove_to_bmi(0, flow_d);
	}
	else if (ret == 0)
	{
	    /* successful post, need to test later */
	    flow_d->state = FLOW_TRANSMITTING;
	    flow_data->fill_buffer_state = BUF_FILLING;
	    /* keep up with the trove id */
	    if ((ret = trove_id_queue_add(trove_inflight_queue,
				       flow_data->trove_id,
				       flow_d->src.u.trove.coll_id)) < 0)
	    {
		/* lost track of the id */
		flow_d->state = FLOW_ERROR;
		flow_d->error_code = ret;
		/* TODO: cleanup properly */
		gossip_lerr("Error: unimplemented condition encountered.\n");
		exit(-1);
	    }
	    trove_pending_count++;
	}
	else
	{
	    /* error posting operation */
	    flow_d->state = FLOW_SRC_ERROR;
	    flow_d->error_code = ret;
	    /* TODO: clean up properly */
	    gossip_lerr("return value: %d\n", ret);
	    gossip_lerr("Error: unimplemented condition encountered.\n");
	    exit(-1);
	}
    }

    return;
}


/* bmi_completion()
 *
 * handles completion of the specified bmi operation
 *
 * returns pointer to associated flow on success, NULL on failure
 */
static flow_descriptor *bmi_completion(bmi_error_code_t error_code,
				       bmi_size_t actual_size,
				       void *user_ptr)
{
    flow_descriptor *flow_d = user_ptr;

    switch (PRIVATE_FLOW(flow_d)->type)
    {
    case BMI_TO_MEM:
	bmi_completion_bmi_to_mem(error_code, actual_size, flow_d);
	break;
    case MEM_TO_BMI:
	bmi_completion_mem_to_bmi(error_code, flow_d);
	break;
    case TROVE_TO_BMI:
	bmi_completion_trove_to_bmi(error_code, flow_d);
	break;
    case BMI_TO_TROVE:
	bmi_completion_bmi_to_trove(error_code, actual_size, flow_d);
	break;
    default:
	gossip_lerr("Error: Unknown/unsupported endpoint combinantion.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = -EINVAL;
	break;
    }

    return (flow_d);
}


/* bmi_completion_mem_to_bmi()
 *
 * handles the completion of bmi operations for memory to bmi transfers
 *
 * no return value
 */
static void bmi_completion_mem_to_bmi(bmi_error_code_t error_code,
				      flow_descriptor * flow_d)
{
    int ret = -1;
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    if (error_code != 0)
    {
	/* the bmi operation failed */
	flow_d->state = FLOW_DEST_ERROR;
	flow_d->error_code = error_code;
	return;
    }

    flow_d->total_transfered += flow_data->bmi_total_size;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (mem to bmi): %ld\n",
		  (long) flow_d->total_transfered);

    /* did this complete the flow? */
    if (flow_d->current_req_offset == -1)
    {
	flow_d->state = FLOW_COMPLETE;
	return;
    }

    /* process next portion of request */
    ret = buffer_setup_mem_to_bmi(flow_d);
    if (ret < 0)
    {
	gossip_lerr("Error: buffer_setup_mem_to_bmi() failure.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = ret;
    }

    flow_d->state = FLOW_SVC_READY;
    return;
}

/* bmi_completion_trove_to_bmi()
 *
 * handles the completion of bmi operations for trove to bmi transfers
 *
 * no return value
 */
static void bmi_completion_trove_to_bmi(bmi_error_code_t error_code,
					flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    if (error_code != 0)
    {
	/* the bmi operation failed */
	flow_d->state = FLOW_DEST_ERROR;
	flow_d->error_code = error_code;
	return;
    }

    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (trove to bmi): %ld\n",
		  (long) flow_d->total_transfered);

    flow_data->total_drained += flow_data->drain_buffer_used;
    flow_d->total_transfered = flow_data->total_drained;
    flow_data->drain_buffer_state = BUF_READY_TO_SWAP;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (trove to bmi): %ld\n",
		  (long) flow_d->total_transfered);

    /* did this complete the flow? */
    if (flow_data->fill_buffer_state == BUF_DONE)
    {
	flow_data->drain_buffer_state = BUF_DONE;
	flow_d->state = FLOW_COMPLETE;
	return;
    }

    /* check the trove side to determine overall state */
    if (flow_data->fill_buffer_state == BUF_FILLING)
	flow_d->state = FLOW_TRANSMITTING;
    else
	flow_d->state = FLOW_SVC_READY;

    return;
}



/* bmi_completion_bmi_to_mem()
 *
 * handles the completion of bmi operations for bmi to memory transfers
 *
 * no return value
 */
static void bmi_completion_bmi_to_mem(bmi_error_code_t error_code,
				      bmi_size_t actual_size,
				      flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    int i = 0;
    PVFS_size total_copied = 0;
    PVFS_size region_size = 0;
    PVFS_boolean eof_flag = 0;

    gossip_ldebug(FLOW_PROTO_DEBUG,
		  "bmi_completion_bmi_to_mem() handling error_code = %d\n",
		  (int) error_code);

    if (error_code != 0)
    {
	/* the bmi operation failed */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = error_code;
	return;
    }

    /* NOTE: lots to handle here:
     * - intermediate buffers
     * - short messages
     * - zero byte messages
     */

    /* TODO: think about this some more.  This isn't the right test (even if
     * we want to detect this case), because it breaks when we are using an 
     * intermediate buffer.
     */
#if 0
    /* see if the flow is being aborted */
    if (actual_size != flow_data->bmi_total_size)
    {
	/* TODO: handle this */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
	return;
    }
#endif

    flow_d->total_transfered += actual_size;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (bmi to mem): %ld\n",
		  (long) flow_d->total_transfered);

    /* were we using an intermediate buffer? */
    if (flow_d->current_req_offset != -1 &&
	flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG, "copying out intermediate buffer.\n");
	/* we need to memcpy out whatever we got to the correct
	 * regions, pushing req proc further if needed
	 */
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    if ((total_copied + flow_data->bmi_size_list[i]) > actual_size)
	    {
		region_size = actual_size - total_copied;
	    }
	    else
	    {
		region_size = flow_data->bmi_size_list[i];
	    }
	    memcpy(flow_data->bmi_buffer_list[i],
		   ((char *) flow_data->intermediate_buffer + total_copied),
		   region_size);
	    total_copied += region_size;
	    if (total_copied == actual_size)
	    {
		break;
	    }
	}

	/* if we didn't exhaust the immediate buffer, then call
	 * request processing code until we get all the way through
	 * it
	 */
	if (total_copied < actual_size)
	{
	    if (eof_flag || flow_d->current_req_offset == -1)
	    {
		gossip_lerr
		    ("Error: Flow sent more data than could be handled?\n");
		exit(-1);
	    }

	    do
	    {
		flow_data->bmi_list_count = MAX_REGIONS;
		flow_data->bmi_total_size = actual_size - total_copied;
		eof_flag = 0;
		gossip_ldebug(FLOW_PROTO_DEBUG, "req proc offset: %ld\n",
			      (long) flow_d->current_req_offset);
		ret = PINT_Process_request(flow_d->request_state,
					   flow_d->file_data,
					   &flow_data->bmi_list_count,
					   flow_data->bmi_offset_list,
					   flow_data->bmi_size_list,
					   &flow_d->current_req_offset,
					   &flow_data->bmi_total_size,
					   &eof_flag, PINT_CLIENT);
		if (ret < 0)
		{
		    flow_d->state = FLOW_DEST_ERROR;
		    flow_d->error_code = error_code;
		    return;
		}

		for (i = 0; i < flow_data->bmi_list_count; i++)
		{
		    region_size = flow_data->bmi_size_list[i];
		    flow_data->bmi_buffer_list[i] =
			(char *) flow_d->dest.u.mem.buffer +
			flow_data->bmi_offset_list[i];
		    memcpy(flow_data->bmi_buffer_list[i],
			   ((char *) flow_data->intermediate_buffer +
			    total_copied), region_size);
		    total_copied += region_size;
		}
	    } while (total_copied < actual_size);
	}
    }
    else
    {
	/* no intermediate buffer */
	/* if we got a short message, then we have to rewind the
	 * request processing stream
	 */
	if (actual_size < flow_data->bmi_total_size)
	{
	    if (flow_d->current_req_offset != -1)
	    {
		flow_d->current_req_offset = (flow_d->total_transfered);
	    }
	}
    }

    /* did this complete the flow? */
    if (flow_d->current_req_offset == -1)
    {
	flow_d->state = FLOW_COMPLETE;
	return;
    }

    /* setup next set of buffers for service */
    ret = buffer_setup_bmi_to_mem(flow_d);
    if (ret < 0)
    {
	gossip_lerr("Error: buffer_setup_bmi_to_mem() failure.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = ret;
    }

    flow_d->state = FLOW_SVC_READY;

    return;
}


/* trove_completion_trove_to_bmi()
 *
 * handles the completion of trove operations for trove to bmi transfers
 *
 * no return value
 */
static void trove_completion_trove_to_bmi(PVFS_ds_state error_code,
					  flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    PVFS_boolean eof_flag = 0;
    int ret = -1;

    if (error_code != 0)
    {
	/* the trove operation failed */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = error_code;
	/* TODO: cleanup properly */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
	return;
    }

    /* see if the read was short */
    if (flow_data->trove_total_size != flow_data->fill_buffer_stepsize)
    {
	/* TODO: need to handle this, make sure flow comes to a halt
	 * afterwards (send 0 byte message eventually
	 */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
    }

    flow_data->total_filled += flow_data->fill_buffer_stepsize;
    flow_data->fill_buffer_offset += flow_data->fill_buffer_stepsize;
    flow_data->fill_buffer_used += flow_data->fill_buffer_stepsize;

    /* if this was the last part of a multi stage read, then 
     * finish it as if it were a normal completion 
     */
    if (flow_data->fill_buffer_offset == flow_data->max_buffer_size ||
	flow_d->current_req_offset == -1)
    {
	flow_data->fill_buffer_state = BUF_READY_TO_SWAP;

	/* set the overall state depending on what both sides are
	 * doing */
	if (flow_data->drain_buffer_state == BUF_READY_TO_SWAP)
	{
	    flow_d->state = FLOW_SVC_READY;
	}
	else if (flow_data->drain_buffer_state == BUF_DRAINING)
	{
	    flow_d->state = FLOW_TRANSMITTING;
	}
	else
	{
	    /* TODO: clean up? */
	    gossip_lerr("Error: inconsistent state encountered.\n");
	    exit(-1);
	}
    }
    else
    {
	/* more work to do before this buffer is fully filled */
	/* find the next set of segments */
	flow_data->trove_list_count = MAX_REGIONS;
	flow_data->fill_buffer_stepsize = flow_data->max_buffer_size -
	    flow_data->fill_buffer_offset;

	ret = PINT_Process_request(flow_d->request_state,
				   flow_d->file_data,
				   &flow_data->trove_list_count,
				   flow_data->trove_offset_list,
				   flow_data->trove_size_list,
				   &flow_d->current_req_offset,
				   &(flow_data->fill_buffer_stepsize),
				   &eof_flag, PINT_SERVER);
	if (ret < 0)
	{
	    gossip_lerr("Error: unimplemented condition encountered.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    exit(-1);
	}

	/* set the state so that the next service will cause a post */
	flow_data->fill_buffer_state = BUF_READY_TO_FILL;
	/* we can go into SVC_READY state regardless of the BMI buffer
	 * state in this case
	 */
	flow_d->state = FLOW_SVC_READY;
    }

    return;
}

/* trove_completion()
 *
 * handles completion of the specified trove operation
 *
 * returns pointer to associated flow on success, NULL on failure
 */
static flow_descriptor *trove_completion(PVFS_ds_state error_code,
					 void *user_ptr)
{
    flow_descriptor *flow_d = user_ptr;

    switch (PRIVATE_FLOW(flow_d)->type)
    {
    case TROVE_TO_BMI:
	trove_completion_trove_to_bmi(error_code, flow_d);
	break;
    case BMI_TO_TROVE:
	trove_completion_bmi_to_trove(error_code, flow_d);
	break;
    default:
	gossip_lerr("Error: Unknown/unsupported endpoint combinantion.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = -EINVAL;
	break;
    }

    return (flow_d);
}


/* buffer_teardown_trove_to_bmi()
 *
 * cleans up buffer resources for a particular type of flow
 *
 * returns 0 on success, -errno on failure
 */
static void buffer_teardown_trove_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* just free the intermediate buffers */
    BMI_memfree(flow_d->dest.u.bmi.address, flow_data->fill_buffer,
		flow_data->max_buffer_size, BMI_SEND);
    BMI_memfree(flow_d->dest.u.bmi.address, flow_data->drain_buffer,
		flow_data->max_buffer_size, BMI_SEND);

    return;
}


/* buffer_teardown_bmi_to_trove()
 *
 * cleans up buffer resources for a particular type of flow
 *
 * returns 0 on success, -errno on failure
 */
static void buffer_teardown_bmi_to_trove(flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* just free the intermediate buffers */
    BMI_memfree(flow_d->src.u.bmi.address, flow_data->fill_buffer,
		flow_data->max_buffer_size, BMI_RECV);
    BMI_memfree(flow_d->src.u.bmi.address, flow_data->drain_buffer,
		flow_data->max_buffer_size, BMI_RECV);
    PINT_Free_request_state(flow_data->dup_req_state);
    return;
}

/* bmi_completion_bmi_to_trove()
 *
 * handles the completion of bmi operations for bmi to trove transfers
 *
 * no return value
 */
static void bmi_completion_bmi_to_trove(bmi_error_code_t error_code,
					bmi_size_t actual_size,
					flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    if (error_code != 0)
    {
	/* the bmi operation failed */
	flow_d->state = FLOW_DEST_ERROR;
	flow_d->error_code = error_code;
	return;
    }

    flow_data->total_filled += flow_data->fill_buffer_used;
    flow_data->fill_buffer_state = BUF_READY_TO_SWAP;

    /* see if the flow is being aborted */
    if (actual_size != flow_data->bmi_total_size)
    {
	/* TODO: handle this */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
	return;
    }

    /* check the trove side to determine overall state */
    if (flow_data->drain_buffer_state == BUF_DRAINING)
	flow_d->state = FLOW_TRANSMITTING;
    else
	flow_d->state = FLOW_SVC_READY;

    return;
}


/* trove_completion_bmi_to_trove()
 *
 * handles the completion of trove operations for bmi to trove transfers
 *
 * no return value
 */
static void trove_completion_bmi_to_trove(PVFS_ds_state error_code,
					  flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    PVFS_boolean eof_flag;
    int ret;

    if (error_code != 0)
    {
	/* the trove operation failed */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = error_code;
	/* TODO: cleanup properly */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
	return;
    }

    /* see if the write was short */
    if (flow_data->trove_total_size != flow_data->drain_buffer_stepsize)
    {
	/* TODO: handle this correctly */
	gossip_lerr("Error: unimplemented condition encountered.\n");
	exit(-1);
    }

    flow_data->total_drained += flow_data->drain_buffer_stepsize;
    flow_data->drain_buffer_offset += flow_data->drain_buffer_stepsize;
    flow_d->total_transfered += flow_data->drain_buffer_stepsize;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (bmi to trove): %ld\n",
		  (long) flow_d->total_transfered);

    /* if this was the last part of a multi stage write, then 
     * finish it as if it were a normal completion 
     */
    if (flow_data->drain_buffer_offset == flow_data->drain_buffer_used)
    {
	flow_data->drain_buffer_state = BUF_READY_TO_SWAP;

	/* are we done ? */
	if (flow_d->current_req_offset == -1 ||
	    flow_data->fill_buffer_state == BUF_DONE)
	{
	    if (flow_d->current_req_offset != -1 ||
		flow_data->fill_buffer_state != BUF_DONE)
	    {
		gossip_lerr
		    ("WARNING: PINT_Process_request() appears to be giving inconsistent results.\n");
		gossip_lerr("WARNING: assuming flow is complete.\n");
	    }
	    flow_d->state = FLOW_COMPLETE;
	}
	else if (flow_data->fill_buffer_state == BUF_READY_TO_SWAP)
	{
	    flow_d->state = FLOW_SVC_READY;
	}
	else if (flow_data->fill_buffer_state == BUF_FILLING)
	{
	    flow_d->state = FLOW_TRANSMITTING;
	}
	else
	{
	    /* TODO: clean up? */
	    gossip_lerr("Error: inconsistent state encountered.\n");
	    exit(-1);
	}
    }
    else
    {
	/* more work to do before this buffer is fully flushed */
	/* find the next set of segments */
	flow_data->trove_list_count = MAX_REGIONS;
	flow_data->drain_buffer_stepsize = flow_data->drain_buffer_used -
	    flow_data->drain_buffer_offset;
	ret = PINT_Process_request(flow_d->request_state,
				   flow_d->file_data,
				   &flow_data->trove_list_count,
				   flow_data->trove_offset_list,
				   flow_data->trove_size_list,
				   &flow_d->current_req_offset,
				   &(flow_data->drain_buffer_stepsize),
				   &eof_flag, PINT_SERVER);
	if (ret < 0)
	{
	    gossip_lerr("Error: unimplemented condition encountered.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    exit(-1);
	}
#if 0
	gossip_err("FOO2: current_req_offset: %d\n",
		   (int) flow_d->current_req_offset);
	gossip_err("FOO2: trove_list_count: %d\n",
		   (int) flow_data->trove_list_count);
	gossip_err("FOO2: trove_offset_list[0]: %d\n",
		   (int) flow_data->trove_offset_list[0]);
	gossip_err("FOO2: trove_size_list[0]: %d\n",
		   (int) flow_data->trove_size_list[0]);
#endif

	/* set the state so that the next service will cause a post */
	flow_data->drain_buffer_state = BUF_READY_TO_DRAIN;
	/* we can go into SVC_READY state regardless of the BMI buffer
	 * state in this case
	 */
	flow_d->state = FLOW_SVC_READY;
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

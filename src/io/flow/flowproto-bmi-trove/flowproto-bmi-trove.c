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
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>

#include "gossip.h"
#include "trove-types.h"
#include "flow.h"
#include "flowproto-support.h"
#include "quicklist.h"
#include "trove-proto.h"
#include "pvfs2-request.h"
#include "thread-mgr.h"
#include "pthread.h"
#include "gen-locks.h"
#include "pint-perf-counter.h"

/* These #defines are special hacks for isolating network performance
 * from disk performance when benchmarking- don't turn this stuff
 * on unless you really know what you are doing
 */
#ifdef __PVFS2_DISABLE_DISK_IO__
#ifdef __PVFS2_TROVE_SUPPORT__
/* write_hack()
 * 
 * this function truncates a file if we write beyond eof, since we don't
 * have a true write to extend the file
 */
static int write_hack(TROVE_coll_id coll_id, TROVE_handle handle, TROVE_size 
    old_size, TROVE_size new_size, TROVE_op_id* tmp_id, 
    TROVE_context_id context_id, void* user_ptr)
{
    if(new_size < old_size)
	return(1);

    return(trove_bstream_resize(coll_id, handle, &new_size, 0, NULL, user_ptr,
	context_id, tmp_id));
};
#endif

/* replacements for bstream_read/write_list that don't do any I/O, but
 * do adjust the file size if necessary
 */
#define TROVE_BSTREAM_READ_LIST(__coll_id, __handle, __mem_offset_array,\
  __mem_size_array, __mem_count, __stream_offset_array, __stream_size_array,\
  __stream_count, __out_size_p, __flags, __vtag, __user_ptr, __context_id,\
  __out_op_id_p) 1; *(__out_size_p) = flow_data->trove_total_size
#define TROVE_BSTREAM_WRITE_LIST(__coll_id, __handle, __mem_offset_array,\
  __mem_size_array, __mem_count, __stream_offset_array, __stream_size_array,\
  __stream_count, __out_size_p, __flags, __vtag, __user_ptr, __context_id,\
  __out_op_id_p) write_hack(__coll_id, __handle, flow_d->file_data.fsize,\
  (__stream_size_array[__stream_count-1]+__stream_offset_array[__stream_count-1]),\
  __out_op_id_p, __context_id, __user_ptr); flow_data->drain_buffer_stepsize = \
  flow_data->trove_total_size
#else
#define TROVE_BSTREAM_READ_LIST trove_bstream_read_list
#define TROVE_BSTREAM_WRITE_LIST trove_bstream_write_list
#endif

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

int flowproto_bmi_trove_post(flow_descriptor * flow_d);

int flowproto_bmi_trove_find_serviceable(flow_descriptor ** flow_d_array,
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
    flowproto_bmi_trove_post,
    flowproto_bmi_trove_find_serviceable,
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
static int DEFAULT_BUFFER_SIZE = (1024 * 256);
/* our assigned flowproto id */
static int flowproto_bmi_trove_id = -1;

/* max number of discontig regions we will handle at once */
#define MAX_REGIONS 16

/* bmi context */
static bmi_context_id global_bmi_context = -1;

#ifdef __PVFS2_TROVE_SUPPORT__
/* trove context */
static TROVE_context_id global_trove_context = -1;
#endif

#define BMI_TEST_SIZE 8
/* array of bmi ops in flight; filled in when needed to call
 * testcontext() 
 */
#ifndef __PVFS2_JOB_THREADED__
static bmi_op_id_t bmi_op_array[BMI_TEST_SIZE];
static int bmi_error_code_array[BMI_TEST_SIZE];
static void* bmi_usrptr_array[BMI_TEST_SIZE];
static bmi_size_t bmi_actualsize_array[BMI_TEST_SIZE];
static PVFS_error trove_error_code_array[BMI_TEST_SIZE];
static void* trove_usrptr_array[BMI_TEST_SIZE];
#ifdef __PVFS2_TROVE_SUPPORT__
static TROVE_op_id trove_op_array[BMI_TEST_SIZE];
#endif
#endif

static int bmi_pending_count = 0;
/* continuously updated list of trove ops in flight */
static int trove_pending_count = 0;

/* queue of flows that that have either completed or need service but 
 * have not yet been passed back by flow_proto_checkXXX()
 */
static QLIST_HEAD(done_checking_queue);

#ifdef __PVFS2_JOB_THREADED__
static pthread_cond_t callback_cond = PTHREAD_COND_INITIALIZER;
static gen_mutex_t callback_mutex = GEN_MUTEX_INITIALIZER;
#endif

/* used to track completions returned by BMI callback function */
struct bmi_completion_notification
{
    void* user_ptr;
    bmi_size_t actual_size;
    bmi_error_code_t error_code;
    struct qlist_head queue_link;
};
static QLIST_HEAD(bmi_notify_queue);
static gen_mutex_t bmi_notify_mutex = GEN_MUTEX_INITIALIZER;

/* used to track completions returned by Trove callback function */
struct trove_completion_notification
{
    void* user_ptr;
    bmi_error_code_t error_code;
    struct qlist_head queue_link;
};
static QLIST_HEAD(trove_notify_queue);
static gen_mutex_t trove_notify_mutex = GEN_MUTEX_INITIALIZER;

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
    TROVE_op_id trove_id;

    /* callback information if we are in threaded mode */
    struct PINT_thread_mgr_bmi_callback bmi_callback;
    struct PINT_thread_mgr_trove_callback trove_callback;

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
    PINT_Request_state *dup_file_req_state;
    PINT_Request_state *dup_mem_req_state;

    struct bmi_completion_notification bmi_notification;
    struct trove_completion_notification trove_notification;
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
static int alloc_flow_data(flow_descriptor * flow_d);
static void service_mem_to_bmi(flow_descriptor * flow_d);
static void service_trove_to_bmi(flow_descriptor * flow_d);
static void service_bmi_to_trove(flow_descriptor * flow_d);
static void service_bmi_to_mem(flow_descriptor * flow_d);
static flow_descriptor *bmi_completion(void *user_ptr,
				       bmi_size_t actual_size,
				       bmi_error_code_t error_code);
static flow_descriptor *trove_completion(PVFS_error error_code,
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
static void trove_completion_trove_to_bmi(PVFS_error error_code,
					  flow_descriptor * flow_d);
static void trove_completion_bmi_to_trove(PVFS_error error_code,
					  flow_descriptor * flow_d);
static void bmi_callback_fn(void *user_ptr,
		         bmi_size_t actual_size,
		         bmi_error_code_t error_code);
static void trove_callback_fn(void *user_ptr,
		           bmi_error_code_t error_code);
#ifdef __PVFS2_TROVE_SUPPORT__
static int buffer_setup_trove_to_bmi(flow_descriptor * flow_d);
static int buffer_setup_bmi_to_trove(flow_descriptor * flow_d);
#endif

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

#ifdef __PVFS2_JOB_THREADED__
    /* take advantage of the thread that the job interface is
     * going to use for BMI operations...
     */
    ret = PINT_thread_mgr_bmi_start();
    if(ret < 0)
    {
	return(ret);
    }
    ret = PINT_thread_mgr_bmi_getcontext(&global_bmi_context);
    /* TODO: this should never fail after a successful start */
    assert(ret == 0);

#ifdef __PVFS2_TROVE_SUPPORT__
    /* take advantage of the thread that the job interface is
     * going to use for Trove operations...
     */
    ret = PINT_thread_mgr_trove_start();
    if(ret < 0)
    {
	return(ret);
    }
    ret = PINT_thread_mgr_trove_getcontext(&global_trove_context);
    /* TODO: this should never fail after a successful start */
    assert(ret == 0);
#endif

#else
    /* get a BMI context */
    ret = BMI_open_context(&global_bmi_context);
    if(ret < 0)
    {
	return(ret);
    }

#ifdef __PVFS2_TROVE_SUPPORT__
    /* get a TROVE context */
    ret = trove_open_context(/*FIXME: HACK*/9,&global_trove_context);
    if (ret < 0)
    {
        return(ret);
    }
#endif
#endif /* __PVFS2_JOB_THREADED__ */

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

#ifdef __PVFS2_JOB_THREADED__
    PINT_thread_mgr_bmi_stop();
#ifdef __PVFS2_TROVE_SUPPORT__
    PINT_thread_mgr_trove_stop();
#endif
#else
    BMI_close_context(global_bmi_context);
#ifdef __PVFS2_TROVE_SUPPORT__
    trove_close_context(/*FIXME: HACK*/9,global_trove_context);
#endif
#endif

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

/* flowproto_bmi_trove_post()
 *
 * informs the flow protocol that it is responsible for the given flow
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_post(flow_descriptor * flow_d)
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

/* flowproto_bmi_trove_find_serviceable()
 *
 * checks to see if any previously posted flows need to be serviced
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_trove_find_serviceable(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms)
{
    int ret = -1;
    flow_descriptor *active_flowd = NULL;
    struct bmi_trove_flow_data *flow_data = NULL;
    int incount = *count;
#ifdef __PVFS2_JOB_THREADED__
    struct timespec pthread_timeout;
    struct timeval base;
    int bmi_notify_queue_empty = 0;
    int trove_notify_queue_empty = 0;
    struct bmi_completion_notification* notification = NULL;
    struct trove_completion_notification* trove_notification = NULL;
#else
    int split_idle_time_ms = max_idle_time_ms;
    int bmi_outcount = 0;
    int i = 0;
    int trove_count = *count;
#ifdef __PVFS2_TROVE_SUPPORT__
    /* TODO: fix later, this is a HACK */
    PVFS_fs_id HACK_coll_id = 9;
#endif
#endif

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


#ifdef __PVFS2_JOB_THREADED__
    /* if we are in threaded mode with callbacks, and the completion queues 
     * are empty, then we will block briefly on a condition variable 
     * to prevent busy spinning until the callback occurs
     */
gen_mutex_lock(&bmi_notify_mutex);
    bmi_notify_queue_empty = qlist_empty(&bmi_notify_queue);
gen_mutex_unlock(&bmi_notify_mutex);
gen_mutex_lock(&trove_notify_mutex);
    trove_notify_queue_empty = qlist_empty(&trove_notify_queue);
gen_mutex_unlock(&trove_notify_mutex);

    if(bmi_notify_queue_empty && trove_notify_queue_empty)
    {
	
	/* figure out how long to wait */
	gettimeofday(&base, NULL);
	pthread_timeout.tv_sec = base.tv_sec + max_idle_time_ms / 1000;
	pthread_timeout.tv_nsec = base.tv_usec * 1000 + 
	    ((max_idle_time_ms % 1000) * 1000000);
	if (pthread_timeout.tv_nsec > 1000000000)
	{
	    pthread_timeout.tv_nsec = pthread_timeout.tv_nsec - 1000000000;
	    pthread_timeout.tv_sec++;
	}

	gen_mutex_lock(&callback_mutex);
	ret = pthread_cond_timedwait(&callback_cond, &callback_mutex, &pthread_timeout);
	gen_mutex_unlock(&callback_mutex);
	if(ret != 0 && ret != ETIMEDOUT)
	{
	    /* TODO: handle this */
	    gossip_lerr("Error: unhandled pthread_cond_timedwait() failure.\n");
	    assert(0);
	}
    }

    /* handle any completed bmi operations from the notification queues */
gen_mutex_lock(&bmi_notify_mutex);
    while(!qlist_empty(&bmi_notify_queue))
    {
	notification = qlist_entry(bmi_notify_queue.next,
				struct bmi_completion_notification,
				queue_link);
	assert(notification);
	qlist_del(bmi_notify_queue.next);
	active_flowd = bmi_completion(notification->user_ptr,
	    notification->actual_size,
	    notification->error_code);
	/* put flows into done_checking_queue if needed */
	if (active_flowd->state & FLOW_FINISH_MASK ||
	    active_flowd->state == FLOW_SVC_READY)
	{
	    gossip_ldebug(FLOW_PROTO_DEBUG, "adding %p to queue.\n", active_flowd);
	    qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			   &done_checking_queue);
	}
    }
gen_mutex_unlock(&bmi_notify_mutex);
#ifdef __PVFS2_TROVE_SUPPORT__
gen_mutex_lock(&trove_notify_mutex);
    while(!qlist_empty(&trove_notify_queue))
    {
	trove_notification = qlist_entry(trove_notify_queue.next,
				struct trove_completion_notification,
				queue_link);
	assert(trove_notification);
	qlist_del(trove_notify_queue.next);
	active_flowd = trove_completion(trove_notification->error_code, 
	    trove_notification->user_ptr);

	/* put flows into done_checking_queue if needed */
	if (active_flowd->state & FLOW_FINISH_MASK ||
	    active_flowd->state == FLOW_SVC_READY)
	{
	    gossip_ldebug(FLOW_PROTO_DEBUG, "adding %p to queue.\n", active_flowd);
	    qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			   &done_checking_queue);
	}
    }
gen_mutex_unlock(&trove_notify_mutex);
#endif


#else /* not __PVFS2_JOB_THREADED__ */
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
	    active_flowd = bmi_completion(bmi_usrptr_array[i],
					  bmi_actualsize_array[i],
					  bmi_error_code_array[i]);

	    /* put flows into done_checking_queue if needed */
	    if (active_flowd->state & FLOW_FINISH_MASK ||
		active_flowd->state == FLOW_SVC_READY)
	    {
		gossip_ldebug(FLOW_PROTO_DEBUG, "adding %p to queue.\n", active_flowd);
		qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			       &done_checking_queue);
	    }
	}
    }

    /* manage in flight trove operations */
    if (trove_pending_count > 0)
    {
	if(trove_pending_count > BMI_TEST_SIZE)
	    trove_count = BMI_TEST_SIZE;
	else
	    trove_count = trove_pending_count;

#ifdef __PVFS2_TROVE_SUPPORT__
	ret = trove_dspace_testcontext(HACK_coll_id,
	    trove_op_array,
	    &trove_count,
	    trove_error_code_array,
	    trove_usrptr_array,
	    split_idle_time_ms,
	    global_trove_context);
#endif
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
	    trove_pending_count--;

	    /* put flows into done_checking_queue if needed */
	    if (active_flowd->state & FLOW_FINISH_MASK ||
		active_flowd->state == FLOW_SVC_READY)
	    {
		gossip_ldebug(FLOW_PROTO_DEBUG, "adding %p to queue.\n", active_flowd);
		qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			       &done_checking_queue);
	    }
	}
    }
#endif

    /* collect flows out of the done_checking_queue and return */
    *count = 0;
    while (*count < incount && !qlist_empty(&done_checking_queue))
    {
	flow_data = qlist_entry(done_checking_queue.next,
				struct bmi_trove_flow_data,
				queue_link);
	active_flowd = flow_data->parent;
	gossip_ldebug(FLOW_PROTO_DEBUG, "found %p in queue.\n", active_flowd);
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
    struct bmi_trove_flow_data *tmp_data = NULL;

    assert(flow_d != NULL);

    tmp_data = PRIVATE_FLOW(flow_d);
    assert(tmp_data != NULL);

    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow (start addr): %p\n",
	flow_d);
    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow (end addr): %p\n",
	((char*)flow_d + sizeof(flow_descriptor)));
    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow_data (start addr): %p\n",
	tmp_data);
    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow_data (end addr): %p\n",
	((char*)tmp_data + sizeof(struct bmi_trove_flow_data)));
	
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
	    gossip_lerr("Error: Unknown/unimplemented "
			"endpoint combination.\n");
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
    int i = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    flow_d->result.offset_array = flow_data->bmi_offset_list;
    flow_d->result.size_array = flow_data->bmi_size_list;
    flow_d->result.bytemax = flow_data->max_buffer_size;
    flow_d->result.segmax = MAX_REGIONS;
    flow_d->result.bytes = 0;
    flow_d->result.segs = 0;
    ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	&flow_d->file_data, &flow_d->result, PINT_CLIENT);
    if (ret < 0)
    {
	return (ret);
    }
    flow_data->bmi_list_count = flow_d->result.segs;
    flow_data->bmi_total_size = flow_d->result.bytes;

    if(PINT_REQUEST_DONE(flow_d->file_req_state) 
	&& (flow_data->bmi_total_size == 0))
    {
	/* no work to do here, return 1 to complete the flow */
	return (1);
    }

    /* did we provide enough segments to satisfy the amount of data
     * available < buffer size? 
     */
    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
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
    int i = 0;
    int intermediate_offset = 0;
    char *dest_ptr = NULL;
    char *src_ptr = NULL;
    int done_flag = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    flow_d->result.offset_array = flow_data->bmi_offset_list;
    flow_d->result.size_array = flow_data->bmi_size_list;
    flow_d->result.bytemax = flow_data->max_buffer_size;
    flow_d->result.segmax = MAX_REGIONS;
    flow_d->result.bytes = 0;
    flow_d->result.segs = 0;
    ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	&flow_d->file_data, &flow_d->result, PINT_CLIENT);
    if (ret < 0)
    {
	return (ret);
    }
    flow_data->bmi_list_count = flow_d->result.segs;
    flow_data->bmi_total_size = flow_d->result.bytes;

    /* did we provide enough segments to satisfy the amount of data
     * available < buffer size?
     */
    if (!PINT_REQUEST_DONE(flow_d->file_req_state) 
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
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

	    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
		&& intermediate_offset < flow_data->max_buffer_size)
	    {
		flow_d->result.bytemax = flow_data->max_buffer_size -
		    intermediate_offset;
		flow_d->result.segmax = MAX_REGIONS;
		flow_d->result.bytes = 0;
		flow_d->result.segs = 0;
		ret  = PINT_Process_request(flow_d->file_req_state, 
		    flow_d->mem_req_state,
		    &flow_d->file_data, &flow_d->result, PINT_CLIENT);
		if (ret < 0)
		{
		    return (ret);
		}
		flow_data->bmi_list_count = flow_d->result.segs;
		flow_data->bmi_total_size = flow_d->result.bytes;
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

#ifdef __PVFS2_TROVE_SUPPORT__
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
    flow_data->dup_file_req_state = PINT_New_request_state(flow_d->file_req);
    if (!flow_data->dup_file_req_state)
    {
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->fill_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->drain_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
	return (-ENOMEM);
    }

    if(flow_d->mem_req)
    {
	flow_data->dup_mem_req_state = PINT_New_request_state(flow_d->mem_req);
	if (!flow_data->dup_mem_req_state)
	{
	    BMI_memfree(flow_d->src.u.bmi.address,
			flow_data->fill_buffer, flow_data->max_buffer_size,
			BMI_RECV);
	    BMI_memfree(flow_d->src.u.bmi.address,
			flow_data->drain_buffer, flow_data->max_buffer_size,
			BMI_RECV);
	    PINT_Free_request_state(flow_data->dup_file_req_state);
	    return (-ENOMEM);
	}
    }

    /* if a file datatype offset was specified, go ahead and skip ahead 
     * before doing anything else
     */
    if(flow_d->file_req_offset)
    {
	PINT_REQUEST_STATE_SET_TARGET(flow_data->dup_file_req_state, 
	    flow_d->file_req_offset);
    }

    /* set boundaries on file datatype based on mem datatype or aggregate size */
    if(flow_d->aggregate_size > -1)
    {
	PINT_REQUEST_STATE_SET_FINAL(flow_data->dup_file_req_state,
	    flow_d->aggregate_size+flow_d->file_req_offset);
    }
    else
    {
	PINT_REQUEST_STATE_SET_FINAL(flow_data->dup_file_req_state,
	    flow_d->file_req_offset +
	    PINT_REQUEST_TOTAL_BYTES(flow_d->mem_req));
    }

    return (0);
}
#endif


#ifdef __PVFS2_TROVE_SUPPORT__
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

    flow_d->result.offset_array = flow_data->trove_offset_list;
    flow_d->result.size_array = flow_data->trove_size_list;
    flow_d->result.bytemax = flow_data->max_buffer_size;
    flow_d->result.segmax = MAX_REGIONS;
    flow_d->result.bytes = 0;
    flow_d->result.segs = 0;
    ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	&flow_d->file_data, &flow_d->result, PINT_SERVER);

    if (ret < 0)
    {
	return (ret);
    }
    flow_data->trove_list_count = flow_d->result.segs;
    flow_data->fill_buffer_stepsize = flow_d->result.bytes;

    if(PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->fill_buffer_stepsize == 0)
    {
	/* there is no work to do; zero length flow */
	BMI_memfree(flow_d->dest.u.bmi.address,
	    flow_data->fill_buffer,
	    flow_data->max_buffer_size,
	    BMI_SEND);
	BMI_memfree(flow_d->dest.u.bmi.address,
	    flow_data->drain_buffer,
	    flow_data->max_buffer_size,
	    BMI_SEND);
	return (1);
    }

    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->fill_buffer_stepsize < flow_data->max_buffer_size)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: going into multistage mode for trove to bmi flow.\n");
    }

    return (0);
}
#endif


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

    /* if a file datatype offset was specified, go ahead and skip ahead 
     * before doing anything else
     */
    if(flow_d->file_req_offset)
	PINT_REQUEST_STATE_SET_TARGET(flow_d->file_req_state,
	    flow_d->file_req_offset);

    /* set boundaries on file datatype based on mem datatype or aggregate size */
    if(flow_d->aggregate_size > -1)
    {
	PINT_REQUEST_STATE_SET_FINAL(flow_d->file_req_state,
	    flow_d->aggregate_size+flow_d->file_req_offset);
    }
    else
    {
	PINT_REQUEST_STATE_SET_FINAL(flow_d->file_req_state,
	    flow_d->file_req_offset +
	    PINT_REQUEST_TOTAL_BYTES(flow_d->mem_req));
    }
    
    /* the rest of the buffer setup varies depending on the endpoints */
    if (flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == MEM_ENDPOINT)
    {
	flow_data->type = BMI_TO_MEM;
	ret = buffer_setup_bmi_to_mem(flow_d);
    }
    else if (flow_d->src.endpoint_id == MEM_ENDPOINT &&
	     flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	flow_data->type = MEM_TO_BMI;
	ret = buffer_setup_mem_to_bmi(flow_d);
    }
#ifdef __PVFS2_TROVE_SUPPORT__
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
#endif
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
    void* user_ptr = flow_d;

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

    flow_data->bmi_callback.fn = bmi_callback_fn;
    flow_data->bmi_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
    user_ptr = &flow_data->bmi_callback;
#endif

    /* post list send */
    gossip_ldebug(FLOW_PROTO_DEBUG, "Posting send, total size: %Ld\n",
		   Ld(flow_data->bmi_total_size));
    ret = BMI_post_send_list(&flow_data->bmi_id,
			     flow_d->dest.u.bmi.address,
			     (const void **) flow_data->bmi_buffer_list,
			     flow_data->bmi_size_list,
			     flow_data->bmi_list_count,
			     flow_data->bmi_total_size, buffer_type,
			     flow_d->tag, user_ptr, global_bmi_context);
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
    void* user_ptr = flow_d;

    /* we should be ready to post the next operation when we get to
     * this function 
     */

    flow_data->bmi_callback.fn = bmi_callback_fn;
    flow_data->bmi_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
    user_ptr = &flow_data->bmi_callback;
#endif

    /* are we using an intermediate buffer? */
    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
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
			    BMI_PRE_ALLOC, flow_d->tag, user_ptr,
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
				 BMI_EXT_ALLOC, flow_d->tag, user_ptr,
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
    char *tmp_offset;
    PVFS_size actual_size = 0;
    void* user_ptr = flow_d;

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

	flow_d->result.offset_array = flow_data->trove_offset_list;
	flow_d->result.size_array = flow_data->trove_size_list;
	flow_d->result.bytemax = flow_data->drain_buffer_stepsize;
	flow_d->result.segmax = MAX_REGIONS;
	flow_d->result.bytes = 0;
	flow_d->result.segs = 0;
	ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	    &flow_d->file_data, &flow_d->result, PINT_SERVER);
	flow_data->trove_list_count = flow_d->result.segs;
	flow_data->drain_buffer_stepsize = flow_d->result.bytes;
	if (ret < 0)
	{
	    gossip_lerr("Error: PINT_Process_request() failure.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    /* no ops in flight, so we can just kick out error here */
	    return;
	}

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
	if(PINT_REQUEST_DONE(flow_data->dup_file_req_state))
	{
	    flow_data->fill_buffer_state = BUF_DONE;
	}
	else
	{
	    flow_data->bmi_callback.fn = bmi_callback_fn;
	    flow_data->bmi_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
	    user_ptr = &flow_data->bmi_callback;
#endif

	    /* see how much more is in the pipe */
	    flow_data->bmi_total_size = flow_data->max_buffer_size;

	    flow_d->result.offset_array = NULL;
	    flow_d->result.size_array = NULL;
	    flow_d->result.bytemax = flow_data->bmi_total_size;
	    flow_d->result.segmax = INT_MAX;
	    flow_d->result.bytes = 0;
	    flow_d->result.segs = 0;
	    ret  = PINT_Process_request(flow_data->dup_file_req_state, 
		flow_data->dup_mem_req_state,
		&flow_d->file_data, &flow_d->result, PINT_CKSIZE_MODIFY_OFFSET);
	    flow_data->bmi_total_size = flow_d->result.bytes;
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
				BMI_PRE_ALLOC, flow_d->tag, user_ptr,
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
	flow_data->trove_callback.fn = trove_callback_fn;
        flow_data->trove_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
	user_ptr = &flow_data->trove_callback;
#endif
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "about to call trove_bstream_write_list().\n");
	tmp_offset = flow_data->drain_buffer + flow_data->drain_buffer_offset;
#ifdef __PVFS2_TROVE_SUPPORT__
	ret = TROVE_BSTREAM_WRITE_LIST(flow_d->dest.u.trove.coll_id,
				       flow_d->dest.u.trove.handle,
				       &(tmp_offset),
				       &(flow_data->trove_total_size), 1,
				       flow_data->trove_offset_list,
				       flow_data->trove_size_list,
				       flow_data->trove_list_count,
				       &(flow_data->drain_buffer_stepsize), 0,
				       NULL, user_ptr, global_trove_context,
				       &(flow_data->trove_id));
#endif
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
    char *tmp_offset;
    void* user_ptr = flow_d;

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
	if(!PINT_REQUEST_DONE(flow_d->file_req_state))
	{
	    flow_d->result.offset_array = flow_data->trove_offset_list;
	    flow_d->result.size_array = flow_data->trove_size_list;
	    flow_d->result.bytemax = flow_data->fill_buffer_stepsize;
	    flow_d->result.segmax = MAX_REGIONS;
	    flow_d->result.bytes = 0;
	    flow_d->result.segs = 0;
	    ret  = PINT_Process_request(flow_d->file_req_state, 
		flow_d->mem_req_state,
		&flow_d->file_data, &flow_d->result, PINT_SERVER);
	    if (ret < 0)
	    {
		gossip_lerr("Error: PINT_Process_request() failure.\n");
		flow_d->state = FLOW_ERROR;
		flow_d->error_code = ret;
		/* no ops in flight, so we can just kick out error here */
		return;
	    }
	    flow_data->trove_list_count = flow_d->result.segs;
	    flow_data->fill_buffer_stepsize = flow_d->result.bytes;

	    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
		&& flow_data->fill_buffer_stepsize <
		flow_data->max_buffer_size)
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
	flow_data->bmi_callback.fn = bmi_callback_fn;
	flow_data->bmi_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
	user_ptr = &flow_data->bmi_callback;
#endif

	gossip_ldebug(FLOW_PROTO_DEBUG, "Posting send, total size: %ld\n",
		      (long) flow_data->drain_buffer_used);
	ret = BMI_post_send(&flow_data->bmi_id,
			    flow_d->dest.u.bmi.address, flow_data->drain_buffer,
			    flow_data->drain_buffer_used,
			    BMI_PRE_ALLOC, flow_d->tag, user_ptr,
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
	flow_data->trove_callback.fn = trove_callback_fn;
        flow_data->trove_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
	user_ptr = &flow_data->trove_callback;
#endif
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "about to call trove_bstream_read_list().\n");
	tmp_offset = flow_data->fill_buffer + flow_data->fill_buffer_offset;
#ifdef __PVFS2_TROVE_SUPPORT__
	ret = TROVE_BSTREAM_READ_LIST(flow_d->src.u.trove.coll_id,
				      flow_d->src.u.trove.handle, &(tmp_offset),
				      &(flow_data->trove_total_size), 1,
				      flow_data->trove_offset_list,
				      flow_data->trove_size_list,
				      flow_data->trove_list_count,
				      &(flow_data->fill_buffer_stepsize), 0,
				      NULL, user_ptr, global_trove_context,
				      &(flow_data->trove_id));
#endif
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

/* trove_callback_fn()
 *
 * callback used upon completion of Trove operations handled by the 
 * thread manager
 *
 * no return value
 */
static void trove_callback_fn(void *user_ptr,
		         bmi_error_code_t error_code)
{
    flow_descriptor* flow_d = (flow_descriptor*)user_ptr;
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    trove_pending_count--;

    /* add an entry to the notification queue */
    flow_data->trove_notification.user_ptr = user_ptr;
    flow_data->trove_notification.error_code = error_code;

gen_mutex_lock(&trove_notify_mutex);
    qlist_add_tail(&flow_data->trove_notification.queue_link,
	&trove_notify_queue);
gen_mutex_unlock(&trove_notify_mutex);

    /* wake up anyone who may be sleeping in find_serviceable() */
#ifdef __PVFS2_JOB_THREADED__
    pthread_cond_signal(&callback_cond);
#endif
    return;
}


/* bmi_callback_fn()
 *
 * callback used upon completion of BMI operations handled by the 
 * thread manager
 *
 * no return value
 */
static void bmi_callback_fn(void *user_ptr,
		         bmi_size_t actual_size,
		         bmi_error_code_t error_code)
{
    flow_descriptor* flow_d = (flow_descriptor*)user_ptr;
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    bmi_pending_count--;

    /* add an entry to the notification queue */
    flow_data->bmi_notification.user_ptr = user_ptr;
    flow_data->bmi_notification.actual_size = actual_size;
    flow_data->bmi_notification.error_code = error_code;

gen_mutex_lock(&bmi_notify_mutex);
    qlist_add_tail(&flow_data->bmi_notification.queue_link,
	&bmi_notify_queue);
gen_mutex_unlock(&bmi_notify_mutex);

    /* wake up anyone who may be sleeping in find_serviceable() */
#ifdef __PVFS2_JOB_THREADED__
    pthread_cond_signal(&callback_cond);
#endif
    return;
}


/* bmi_completion()
 *
 * handles completion of the specified bmi operation
 *
 * returns pointer to associated flow on success, NULL on failure
 */
static flow_descriptor *bmi_completion(void *user_ptr,
				       bmi_size_t actual_size,
				       bmi_error_code_t error_code)
				       
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
    if(PINT_REQUEST_DONE(flow_d->file_req_state))
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
    PINT_perf_count(PINT_PERF_READ, flow_data->drain_buffer_used, 
	PINT_PERF_ADD);
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

    /* TODO: handle case of receiving less data than we expected (or 
     * at least assert on it)
     */

    flow_d->total_transfered += actual_size;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (bmi to mem): %ld\n",
		  (long) flow_d->total_transfered);

    /* were we using an intermediate buffer? */
    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
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
	    if(PINT_REQUEST_DONE(flow_d->file_req_state))
	    {
		gossip_lerr
		    ("Error: Flow sent more data than could be handled?\n");
		exit(-1);
	    }

	    do
	    {
		flow_data->bmi_list_count = MAX_REGIONS;
		flow_data->bmi_total_size = actual_size - total_copied;
		flow_d->result.offset_array = flow_data->bmi_offset_list;
		flow_d->result.size_array = flow_data->bmi_size_list;
		flow_d->result.bytemax = flow_data->bmi_total_size;
		flow_d->result.segmax = MAX_REGIONS;
		flow_d->result.bytes = 0;
		flow_d->result.segs = 0;

		ret  = PINT_Process_request(flow_d->file_req_state, 
		    flow_d->mem_req_state,
		    &flow_d->file_data, &flow_d->result, PINT_CLIENT);
		if (ret < 0)
		{
		    flow_d->state = FLOW_DEST_ERROR;
		    flow_d->error_code = error_code;
		    return;
		}
		flow_data->bmi_list_count = flow_d->result.segs;
		flow_data->bmi_total_size = flow_d->result.bytes;

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
    {   /* no intermediate buffer */
	/* TODO: fix this; we need to do something reasonable if we
	 * receive less data than we expected
	 */
	assert(actual_size == flow_data->bmi_total_size);
    }

    /* did this complete the flow? */
    if(PINT_REQUEST_DONE(flow_d->file_req_state))
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
    else
    {
	flow_d->state = FLOW_SVC_READY;
    }

    return;
}


/* trove_completion_trove_to_bmi()
 *
 * handles the completion of trove operations for trove to bmi transfers
 *
 * no return value
 */
static void trove_completion_trove_to_bmi(PVFS_error error_code,
					  flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
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
    if(PINT_REQUEST_DONE(flow_d->file_req_state)
	|| flow_data->fill_buffer_offset == flow_data->max_buffer_size)

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

	flow_d->result.offset_array = flow_data->trove_offset_list;
	flow_d->result.size_array = flow_data->trove_size_list;
	flow_d->result.bytemax = flow_data->fill_buffer_stepsize;
	flow_d->result.segmax = MAX_REGIONS;
	flow_d->result.bytes = 0;
	flow_d->result.segs = 0;
	ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	    &flow_d->file_data, &flow_d->result, PINT_SERVER);
	if (ret < 0)
	{
	    gossip_lerr("Error: unimplemented condition encountered.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    exit(-1);
	}
	flow_data->trove_list_count = flow_d->result.segs;
	flow_data->fill_buffer_stepsize = flow_d->result.bytes;

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
static flow_descriptor *trove_completion(PVFS_error error_code,
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
    PINT_Free_request_state(flow_data->dup_file_req_state);
    if(flow_data->dup_mem_req_state)
	PINT_Free_request_state(flow_data->dup_mem_req_state);
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
	gossip_err("actual_size: %Ld\n", (long long)actual_size);
	gossip_err("flow_data->bmi_total_size: %Ld\n", (long long)flow_data->bmi_total_size);
	gossip_err("server number: %d\n", flow_d->file_data.iod_num);
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
static void trove_completion_bmi_to_trove(PVFS_error error_code,
					  flow_descriptor * flow_d)
{
    struct bmi_trove_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret;

    if (error_code != 0)
    {
	/* the trove operation failed */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = error_code;
	/* TODO: cleanup properly */
	PVFS_perror_gossip("trove_completion_bmi_to_trove", error_code);
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
    PINT_perf_count(PINT_PERF_WRITE, flow_data->drain_buffer_stepsize, 
	PINT_PERF_ADD);
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (bmi to trove): %ld\n",
		  (long) flow_d->total_transfered);

    /* if this was the last part of a multi stage write, then 
     * finish it as if it were a normal completion 
     */
    if (flow_data->drain_buffer_offset == flow_data->drain_buffer_used)
    {
	flow_data->drain_buffer_state = BUF_READY_TO_SWAP;

	/* are we done ? */
	if(PINT_REQUEST_DONE(flow_d->file_req_state)
	    || flow_data->fill_buffer_state == BUF_DONE)
	{
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
	
	flow_d->result.offset_array = flow_data->trove_offset_list;
	flow_d->result.size_array = flow_data->trove_size_list;
	flow_d->result.bytemax = flow_data->drain_buffer_stepsize;
	flow_d->result.segmax = MAX_REGIONS;
	flow_d->result.bytes = 0;
	flow_d->result.segs = 0;
	ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	    &flow_d->file_data, &flow_d->result, PINT_SERVER);
	if (ret < 0)
	{
	    gossip_lerr("Error: unimplemented condition encountered.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    exit(-1);
	}
	flow_data->trove_list_count = flow_d->result.segs;
	flow_data->drain_buffer_stepsize = flow_d->result.bytes;

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

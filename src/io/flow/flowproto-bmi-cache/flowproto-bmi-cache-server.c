/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "gossip.h"
#include "quicklist.h"
#include "flow.h"
#include "flowproto-support.h"
#include "gen-locks.h"
#include "bmi.h"
#include "thread-mgr.h"

#include "trove.h"
#include "ncac-interface.h"
#include "internal.h"
    
#define BUFFER_SIZE (256*1024)
#define MAX_REGIONS 16

#define INT_REQ_EMPTY		-1
#define INT_REQ_INIT		0
#define INT_REQ_PROCESSING	1
#define INT_REQ_COMPLETE	2

#define NONBLOCKING_FLAG	0
#define BLOCKING_FLAG		1

#define CACHE_ENDPOINT	1

#define BMI_TO_CACHE 	0
#define CACHE_TO_BMI	1

#define NO_MUTEX_FLAG	0
#define MUTEX_FLAG	1

 	
/* Noted by wuj: The flowproto-bmi-cache design has significant 
 * differences from the template "bmi-trove" design in handling 
 * buffers. These difference are reflected in the
 * data structure added and/or changed in fp_queue_item and other
 * related structures. 
 */ 

struct pint_req_entry
{
    PINT_Request_result result;
    PVFS_size size_list[MAX_REGIONS];
    PVFS_offset offset_list[MAX_REGIONS];
};

struct cache_req_entry
{
	cache_request_t request;  /* cache request handle */
	int 	errval;		/* error code */
	int mem_cnt;			/* how many buffers */

	/* buffer size array, array space provided by the cache */
	PVFS_size *msize_list; 

	/* "total_size" is the sum of the size list */
	PVFS_size total_size; 

	/* buffer offset array, provided by the cache */
	PVFS_offset **moff_list; 

	/* if this is not NULL, this buffer is supplied by the flow */
	PVFS_offset *buffer;

	enum bmi_buffer_type buffer_type;
};


/* fp_queue_item describes an individual request being used within the flow.
 * A request --> a flow --> a list of fp_queue_items */
struct fp_queue_item
{
	/* point to the flow descriptor */
	flow_descriptor* 	parent;	

	/* PINT request information */
	struct pint_req_entry 	pint_req; 

	/* cache request information */
	struct cache_req_entry 	cache_req; 
	int int_state;

	/* flag to show whether the callbacks are set up */
	int 	callback_setup;
	struct PINT_thread_mgr_bmi_callback bmi_callback;
	struct PINT_thread_mgr_trove_callback cache_callback;

	struct qlist_head list_link;
};

/* fp_private_data is information specific to this flow protocol, stored
 * in flow descriptor but hidden from caller
 */
struct fp_private_data
{
	/* point to the flow descriptor */
	flow_descriptor* parent;       

	/* PINT request done? */
	int pint_request_done;	
	
	/* PINT request list */
	struct qlist_head pint_req_list;

	/* requests completed on the cache side */ 
	struct qlist_head cache_req_done_list;  

	PVFS_size total_bytes_processed;
	TROVE_context_id trove_context;

	gen_mutex_t flow_mutex;
};

#define PRIVATE_FLOW(target_flow)\
    ((struct fp_private_data*)(target_flow->flow_protocol_data))

static int fp_multiqueue_id = -1;
static bmi_context_id global_bmi_context = -1;

static TROVE_context_id global_trove_context = -1;


static void bmi_recv_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);

static void bmi_send_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);

#if 0
/* the above function is a special case; we need to look at a return
 * value when we invoke it directly, so we use the following function
 * to trigger it from a callback
 */
static void bmi_send_callback_wrapper(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    bmi_send_callback_fn(user_ptr, actual_size, error_code);
    return;
};
#endif

static void cache_read_callback_fn(void *user_ptr,
		           PVFS_error error_code);

static void cache_write_callback_fn(void *user_ptr,
		           PVFS_error error_code);

/* protocol specific functions */
static int  bmi_cache_request_init(struct fp_private_data *flow_data, 
			int direction);

static int  bmi_cache_progress_check(struct flow_descriptor *flow_d, 
			int wait_flag, 
			int mutex_flag,
			int direction);


static int bmi_cache_check_cache_req(struct fp_queue_item *q_item); 
static int bmi_cache_release_cache_src(struct fp_queue_item *q_item);
static int bmi_cache_init_cache_req(struct fp_queue_item *qitem, int op);


/* interface prototypes */
int fp_multiqueue_initialize(int flowproto_id);

int fp_multiqueue_finalize(void);

int fp_multiqueue_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

int fp_multiqueue_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

int fp_multiqueue_post(flow_descriptor * flow_d);


char fp_multiqueue_name[] = "flowproto_bmi_cache";

struct flowproto_ops fp_multiqueue_ops = {
    fp_multiqueue_name,
    fp_multiqueue_initialize,
    fp_multiqueue_finalize,
    fp_multiqueue_getinfo,
    fp_multiqueue_setinfo,
    fp_multiqueue_post
};

/* fp_multiqueue_initialize()
 *
 * starts up the flow protocol
 *
 * returns 0 on succes, -PVFS_error on failure
 */
int fp_multiqueue_initialize(int flowproto_id)
{
    int ret = -1;

    ret = PINT_thread_mgr_bmi_start();
    if(ret < 0)
	return(ret);
    PINT_thread_mgr_bmi_getcontext(&global_bmi_context);

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = PINT_thread_mgr_trove_start();
    if(ret < 0)
    {
	PINT_thread_mgr_bmi_stop();
	return(ret);
    }
    PINT_thread_mgr_trove_getcontext(&global_trove_context);
#endif

    fp_multiqueue_id = flowproto_id;

    return(0);
}

/* fp_multiqueue_finalize()
 *
 * shuts down the flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_finalize(void)
{
    PINT_thread_mgr_bmi_stop();
#ifdef __PVFS2_TROVE_SUPPORT__
    PINT_thread_mgr_trove_stop();
#endif
    return (0);
}

/* fp_multiqueue_getinfo()
 *
 * retrieves runtime parameters from flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    int* type;

    switch(option)
    {
	case FLOWPROTO_TYPE_QUERY:
	    type = parameter;
	    if(*type == FLOWPROTO_MULTIQUEUE)
		return(0);
	    else
		return(-PVFS_ENOPROTOOPT);
	default:
	    return(-PVFS_ENOSYS);
	    break;
    }
}

/* fp_multiqueue_setinfo()
 *
 * sets runtime parameters in flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-PVFS_ENOSYS);
}

/* fp_multiqueue_post()
 *
 * posts a flow descriptor to begin work
 *
 * returns 0 on success, 1 on immediate completion, -PVFS_error on failure
 */
int fp_multiqueue_post(flow_descriptor * flow_d)
{
	struct fp_private_data* flow_data = NULL;
	int ret;

	/* on the server side: only two possible types */
	assert( (flow_d->src.endpoint_id == BMI_ENDPOINT && 
		flow_d->dest.endpoint_id == CACHE_ENDPOINT) ||
		(flow_d->src.endpoint_id == CACHE_ENDPOINT &&
		flow_d->dest.endpoint_id == BMI_ENDPOINT) );

	flow_data = (struct fp_private_data*)malloc(sizeof(struct fp_private_data));
	if(!flow_data)
		return(-PVFS_ENOMEM);
	memset(flow_data, 0, sizeof(struct fp_private_data));

	flow_d->flow_protocol_data = flow_data;
	flow_d->state = FLOW_TRANSMITTING;

	/* protocol specific fields */
	flow_data->parent = flow_d;
	INIT_QLIST_HEAD(&flow_data->pint_req_list);
	flow_data->pint_request_done = 0;
	INIT_QLIST_HEAD(&flow_data->cache_req_done_list);

	flow_data->trove_context = global_trove_context;
	gen_mutex_init(&flow_data->flow_mutex);

	/* if a file datatype offset was specified, go ahead and skip ahead 
 	 * before doing anything else
	 */
	if(flow_d->file_req_offset)
		PINT_REQUEST_STATE_SET_TARGET(flow_d->file_req_state, 
					flow_d->file_req_offset);

	/* set boundaries on file datatype */
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

	/* (1) init requests; (2) check progress; later all progress checks 
	 * are driven by callbacks.
	 */

	if( flow_d->src.endpoint_id == CACHE_ENDPOINT &&
		flow_d->dest.endpoint_id == BMI_ENDPOINT )
	{
		/* CACHE --> BMI flow: read from cache, then send to the 
		 * network. When the cache supports callback, the cache 
		 * callback triggers BMI operations; the BMI callback triggers 
		 * the release of cache resources. 
		 * When the cache does not support callback, as in the current
		 * implementation, we use the following progress method:
		 * (1) wait for the completion of the first cache request;
		 * (2) initiate the BMI send operation;
		 * (3) in the BMI send callback function, we wait for the 
		 *     completion of the next cache request;  
		 * (4) goto step (2).
		 */ 

		ret = bmi_cache_request_init(flow_data, CACHE_TO_BMI);
		if ( ret < 0 ) {
			PVFS_perror_gossip("bmi_cache_request_init error: "
                                           "error_code", ret);
			return ret;
		}

		/* check progress. MUTEX_FLAG is needed because possible
		 * callbacks may happen at the same time.
		 */
		ret = bmi_cache_progress_check( flow_d, 
						BLOCKING_FLAG,
						MUTEX_FLAG,
						CACHE_TO_BMI );
		if ( ret < 0 )
		{
			PVFS_perror_gossip("bmi_cache_progress_check error: "
                                           "error_code", ret);
			return ret;
		}

	}
	else if( flow_d->src.endpoint_id == BMI_ENDPOINT &&
			 flow_d->dest.endpoint_id == CACHE_ENDPOINT )
	{
		/* BMI--->CACHE flow: (1) init requests; (2) check progress;
		 * later all progress checks are driven by callbacks;
		 *   cache callbacks to indicate that data buffers are 
		 *   available;
		 *	then trigger BMI receive operations to receive data 
		 * 	into the cache buffers;
		 *	the BMI call back function will trigger releasing 
		 *	cache buffers.
		 * In the current implementation, cache does not provide 
		 * callback (we will add it later), the progress of cache 
		 * is checked in the BMI call back function. That means, in 
		 * the BMI call back function, we release cache buffers for 
		 * the previous queue item; then we see if the cache buffers 
		 * are available for another queue item (polling with time 
		 * idle), if yes, initiate BMI receive operations.
		 */ 

		ret = bmi_cache_request_init(flow_data, BMI_TO_CACHE);
		if ( ret < 0 ) {
			PVFS_perror_gossip("bmi_cache_request_init error: "
                                           "error_code", ret);
			return ret;
		}

		/* check progress. MUTEX_FLAG is needed because possible
		 * callbacks may happen at the same time.
		 */

		ret = bmi_cache_progress_check(flow_d, 
					BLOCKING_FLAG, 
					MUTEX_FLAG,
					BMI_TO_CACHE);
		if ( ret < 0 )
		{
			PVFS_perror_gossip("bmi_cache_progress_check error: "
                                           "error_code", ret);
			return ret;
		}

	}
	else
	{
		return(-ENOSYS);
	}

	return (0);
}


/* bmi_cache_request_init(): get request information from "request component",
 * and inititate related cache requests
 * The basic idea in bmi_cache_request_init() is to chop a big request into 
 * several smaller requests (we called fp_queue_item) and link these small 
 * requests together. The direct purposes is to enable pipelining and to 
 * reduce buffer preasure.
 * return: 0: success;  <0 error; 1: complete;
 */ 

int  bmi_cache_request_init(struct fp_private_data *flow_data, int direction)
{
	struct flow_descriptor *flow_d = flow_data->parent;
	struct fp_queue_item *new_qitem = NULL;
	struct pint_req_entry* pint_req = NULL;
	PVFS_size bytes_processed = 0;

	int ret;

	/* zero request. This is wrong, TO DO it later. */
	if ( flow_d->total_transfered >= flow_data->total_bytes_processed ) {
		flow_d->state = FLOW_COMPLETE;
		free(flow_data);
		flow_d->release(flow_d);
		flow_d->callback(flow_d);
		return(1);
	}

	/* get request information before launching cache request.
	 * Basically, we need to know what the request is, including
	 * file regions, each region offset, and each region length. 
	 * A pipelining idea is included in the following steps:
	 *    a large flow request ---> several requests with "MAX_REGIONS"
	 *	   and "BUFFER_SIZE" ---> pint_req_list 
	 * Later, we process each element in the pint_req_list:
	 *  1) dequeue an element from the list;
	 *	2) init cache request;
	 *  3) drive BMI operations;
	 *  4) enqueue the element into another list;
	 * If all elements have been through the above steps,
	 * the request is done.
	 */

	assert ( flow_data->pint_request_done == 0 );

	/* get PINT_requests and initiate related cache requests */
	do {
		new_qitem = (struct fp_queue_item *)malloc(sizeof(struct fp_queue_item));
		assert(new_qitem);
		memset(new_qitem, 0 , sizeof(struct fp_queue_item));

		new_qitem->parent = flow_d;
		
		/* process request */
		pint_req = & new_qitem->pint_req;
		pint_req->result.offset_array = pint_req->offset_list;
		pint_req->result.size_array = pint_req->size_list;
		pint_req->result.bytemax = BUFFER_SIZE;
		pint_req->result.bytes = 0;
		pint_req->result.segmax = MAX_REGIONS;
		pint_req->result.segs = 0;

		ret = PINT_Process_request( flow_d->file_req_state,
					flow_d->mem_req_state,
					&flow_d->file_data,
					&pint_req->result,
					PINT_SERVER );
		/* TODO: error handling */
		assert(ret >= 0);

		/* submit the cache request */
		ret = bmi_cache_init_cache_req(new_qitem, direction);

		/* TODO: error handling */
		assert(ret >= 0);

		new_qitem->int_state = INT_REQ_PROCESSING;

		/* immediate completion on the cache request */
		if ( ret == 1 ) 
		{
			new_qitem->int_state = INT_REQ_COMPLETE;
		}
		
		/* put the request into the chain */
		qlist_add_tail(&new_qitem->list_link, &flow_data->pint_req_list);
		bytes_processed += pint_req->result.bytes;

   	} while( !PINT_REQUEST_DONE(flow_d->file_req_state) );
	
	flow_data->pint_request_done = 1;

	flow_data->total_bytes_processed = bytes_processed;

	return 0;

} /* end of bmi_cache_request_init() */


/* bmi_cache_progress_check(): 
 * 	check progress on both cache requests and bmi requests.
 *	mutex_flag:	
 *	 (1) mutex_flag is on.
 *	    -- this is called in the first time for the flow progress.
 *	 (2) mutex_flag is off
 *	    -- this is called in other callbacks which hold mutex.
 *	wait_flag: 
	 (1) non-blocking; (2) blocking	
 *	direction:
 *	 (1) BMI_TO_CACHE; (2) CACHE_TO_BMI
 */

int bmi_cache_progress_check(struct flow_descriptor *flow_d, 
			int wait_flag, 
			int mutex_flag,
			int direction)
{
	struct fp_private_data *flow_data = PRIVATE_FLOW(flow_d);
	struct fp_queue_item *q_item = NULL;
	struct qlist_head *tmp_link = NULL;
	int doneflag = 0;
	int ret;

	assert ( flow_data->pint_request_done == 1 );

	if ( qlist_empty(&flow_data->pint_req_list) ) 
		return 1; 

	doneflag = 0;

	if ( mutex_flag ) {
		gen_mutex_lock(&flow_data->flow_mutex);
	}

check_again:

	qlist_for_each(tmp_link, &flow_data->pint_req_list)
	{
		q_item = qlist_entry(tmp_link, struct fp_queue_item, list_link);

		if ( q_item->int_state == INT_REQ_INIT )
		{
			/* wrong state */
			PVFS_perror_gossip("bmi_cache_progress_check error: "
                                           "wrong internal state:error code", -1);

			if ( mutex_flag )
				gen_mutex_unlock(&flow_data->flow_mutex);

			/* TODO: error code */
			return -1;
		}

		/* internal reuqest has been issued, but in processing */
		if ( q_item->int_state == INT_REQ_PROCESSING ) 
		{
			/* check the cache request */
			ret = bmi_cache_check_cache_req(q_item); 

			/* TODO: error handling */
			if ( ret < 0 ) 
			{
				if ( mutex_flag ) 
					gen_mutex_unlock(&flow_data->flow_mutex);
				return -1;
			}
	
			if ( ret == 1 ) { 
				q_item->int_state = INT_REQ_COMPLETE;
			}
		}

		/* internal reuqest has been finished, buffers are available 
		 * for BMI operations.
		 */
		if ( q_item->int_state == INT_REQ_COMPLETE )
		{
			/* move the item from pint_req_list to cache_req_done_list */
			qlist_del(&q_item->list_link);
			qlist_add_tail(&q_item->list_link, &flow_data->cache_req_done_list);

			if ( direction == BMI_TO_CACHE ) 
			{
				if ( mutex_flag )
					gen_mutex_unlock(&flow_data->flow_mutex);
				cache_write_callback_fn(q_item, 0);

				if ( mutex_flag )
					gen_mutex_lock(&flow_data->flow_mutex);
			} else
			{
				if ( mutex_flag )
					gen_mutex_unlock(&flow_data->flow_mutex);
				cache_read_callback_fn(q_item, 0);

				if ( mutex_flag )
					gen_mutex_lock(&flow_data->flow_mutex);
			}
			doneflag = 1;
		}
		
		if ( !q_item->callback_setup ) 
		{
			q_item->bmi_callback.fn = bmi_recv_callback_fn;
			q_item->bmi_callback.data = q_item;
#ifdef __CACHE_CALLBACK_SUPPORT__
			q_item->cache_callback.fn = cache_write_callback_fn;
			q_item->cache_callback.data = q_item;
#else
			q_item->cache_callback.fn = NULL;
			q_item->cache_callback.data = NULL;
#endif

			q_item->callback_setup = 1;
		}
		
		if ( doneflag ) break;
		
	} /* qlist_for_each element request */
	
	if ( !doneflag && wait_flag == BLOCKING_FLAG )
		goto check_again;

	if ( mutex_flag )
		gen_mutex_unlock(&flow_data->flow_mutex);
	
	return 0;
}


/* bmi_recv_callback_fn()
 *
 * function to be called upon completion of a BMI recv operation
 * Two main jobs:
 *   (1) release cache buffers
 *   (2) if the cache does not support callback, check progress in the cache
 *       side.
 * 
 * no return value
 */
static void bmi_recv_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
	struct fp_queue_item* q_item = user_ptr;
	struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
	struct flow_descriptor * flow_d = flow_data->parent;
	int ret;

	/* TODO: error handling */
	if(error_code != 0)
	{
		PVFS_perror_gossip("bmi_recv_callback_fn error_code", 
					error_code);
		assert(0);
	}

	gen_mutex_lock(&flow_data->flow_mutex);

	/* remove from current queue */
	qlist_del(&q_item->list_link);

	flow_d->total_transfered += actual_size;


	/* release cache resource, since data has been received into 
	 * the cache buffers 
	 */

	ret = bmi_cache_release_cache_src(q_item);

	/* TODO: error handling */
	if ( ret < 0 ) {
		PVFS_perror_gossip("bmi_recv_callback_fn: "
                                   "error from  bmi_cache_release_cache_src:error_code", ret);
		gen_mutex_unlock(&flow_data->flow_mutex);
		return;
	}

	/* when no callback support from the cache, we take advantage of 
	 * the BMI callback to make progress. That is, in the BMI callback 
	 * function, we wait for the completion of another request from 
	 * the cache component, then initiate BMI recv function. All these 
	 * are done in "bmi_cache_progress_check()" with BLOCKING_FLAG.
	 * Be careful of "mutex" stuff. 
	 */

	/* no callback support from the cache */
	if ( !q_item->cache_callback.fn )  
	{
		ret = bmi_cache_progress_check( flow_d, 
						BLOCKING_FLAG, 
						NO_MUTEX_FLAG,
						BMI_TO_CACHE);
		if ( ret < 0 )
		{
			PVFS_perror_gossip("bmi_recv_callback_fn: "
                                           "error from bmi_cache_progress_check:error_code", ret);
			gen_mutex_unlock(&flow_data->flow_mutex);
			return;
		}
	}

	gen_mutex_unlock(&flow_data->flow_mutex);
	return;
}
	
/* cache_write_callback_fn()
 *
 * function to be called upon completion of a cache write operation. 
 * The completion means that the cache component provides needed 
 * buffers to flow. In this function, we initiate BMI receive requests 
 * to transfer data into the cache buffers. 
 *
 * no return value
 */
static void cache_write_callback_fn(void *user_ptr,
		           PVFS_error error_code)
{
	PVFS_size tmp_actual_size;

#ifdef __CACHE_CALLBACK_SUPPORT__
	struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
#endif

	struct fp_queue_item* q_item = user_ptr;
	int ret;
	PVFS_id_gen_t bmi_reqid;

	/* TODO: error handling */
	assert(error_code == 0);
	

	/* if cache supports callback, this function will called
	 * independently. Otherwise, it is called inside of mutex.
	 * Thus, there is no need holding mutex.
	 */ 
#ifdef __CACHE_CALLBACK_SUPPORT__
	gen_mutex_lock(&flow_data->flow_mutex);
#endif

	q_item->int_state = INT_REQ_COMPLETE;

	/* TODO: what if we recv less than expected? */
	ret = BMI_post_recv_list(&bmi_reqid,
		q_item->parent->src.u.bmi.address,
		(void **)q_item->cache_req.moff_list,
		q_item->cache_req.msize_list,
		q_item->cache_req.mem_cnt,
		q_item->cache_req.total_size,
        	&tmp_actual_size,
		q_item->cache_req.buffer_type,
        	q_item->parent->tag,
        	&q_item->bmi_callback,
        	global_bmi_context);

	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
#ifdef __CACHE_CALLBACK_SUPPORT__
		gen_mutex_unlock(&flow_data->flow_mutex);
#endif
		/* immediate completion; trigger callback ourselves */
		bmi_recv_callback_fn(q_item, tmp_actual_size, 0);

#ifdef __CACHE_CALLBACK_SUPPORT__
		gen_mutex_lock(&flow_data->flow_mutex);
#endif
	}

#ifdef __CACHE_CALLBACK_SUPPORT__
	gen_mutex_unlock(&flow_data->flow_mutex);
#endif

	return;
};



/* The following two functions are for flow from the CACHE to BMI 
 * direction: cache_read_callback_fn() and bmi_send_callback_fn().
 */

/* cache_read_callback_fn()
 *
 * function to be called upon completion of a cache read operation. 
 * The completion means that the cache component provides needed 
 * buffers with data to flow. In this function, we initiate BMI 
 * send requests to send data out to the network.
 *
 * no return value
 */
static void cache_read_callback_fn(void *user_ptr,
		           PVFS_error error_code)
{
	PVFS_size sent_size;
	struct fp_queue_item* q_item = user_ptr;

#ifdef __CACHE_CALLBACK_SUPPORT
	struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
#endif

	PVFS_id_gen_t bmi_reqid;
	int ret;

	/* TODO: error handling */
	assert(error_code == 0);

#ifdef __CACHE_CALLBACK_SUPPORT__
	gen_mutex_lock(&flow_data->flow_mutex);
#endif

	q_item->int_state = INT_REQ_COMPLETE;

	ret = BMI_post_send_list(&bmi_reqid,
		q_item->parent->dest.u.bmi.address,
		(const void **)q_item->cache_req.moff_list,
		q_item->cache_req.msize_list,
		q_item->cache_req.mem_cnt,
		q_item->cache_req.total_size,
		q_item->cache_req.buffer_type,
		q_item->parent->tag,
		&q_item->bmi_callback,
		global_bmi_context);
		
	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
#ifdef __CACHE_CALLBACK_SUPPORT__
		gen_mutex_unlock(&flow_data->flow_mutex);
#endif

		/* immediate completion; trigger callback ourselves */
		sent_size = q_item->cache_req.total_size;
		bmi_send_callback_fn(q_item, sent_size, 0);

#ifdef __CACHE_CALLBACK_SUPPORT__
		gen_mutex_lock(&flow_data->flow_mutex);
#endif
	}

#ifdef __CACHE_CALLBACK_SUPPORT__
	gen_mutex_unlock(&flow_data->flow_mutex);
#endif

	return;
};

/* bmi_send_callback_fn()
 *
 * function to be called upon completion of a BMI send operation
 * Two main jobs:
 *   (1) release cache buffers
 *   (2) if the cache does not support callback, check progress on 
 *       the cache side.
 * 
 * no return value
 */
static void bmi_send_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
	struct fp_queue_item* q_item = user_ptr;
	struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
	struct flow_descriptor * flow_d = flow_data->parent;
	int ret;

	/* TODO: error handling */
	if(error_code != 0)
	{
		PVFS_perror_gossip("bmi_send_callback_fn: error", error_code);
		assert(0);
	}

	gen_mutex_lock(&flow_data->flow_mutex);


	flow_d->total_transfered += actual_size;

	/* release cache resource */

	ret = bmi_cache_release_cache_src(q_item);

	/* TODO: error handling */
	if ( ret < 0 ) {
		PVFS_perror_gossip("bmi_send_callback_fn: "
                                   "from bmi_cache_release_cache_src:error_code", ret);
		gen_mutex_unlock(&flow_data->flow_mutex);
		return;
	}

	/* remove from current queue. q_item must be in the 
	 * cache_req_done_list when coming here.  */

	qlist_del(&q_item->list_link);

	/* when no callback support from the cache, we take advantage of 
	 * the BMI callback to make progress. That is, in the BMI callback 
	 * function, we wait for the completion of another request from 
	 * the cache component, then initiate BMI send function. All these 
	 * are done in "bmi_cache_progress_check()" with BLOCKING_FLAG.
	 */

	/* no callback support from the cache. */
	if ( !q_item->cache_callback.fn )  
	{
		ret = bmi_cache_progress_check( flow_d, 
						BLOCKING_FLAG, 
						NO_MUTEX_FLAG,
						CACHE_TO_BMI );
		if ( ret < 0 )
		{
			PVFS_perror_gossip("bmi_send_callback_fn: "
                                           "from bmi_cache_progress_check:error_code", ret);
			gen_mutex_unlock(&flow_data->flow_mutex);
			return;
		}
	}

	gen_mutex_unlock(&flow_data->flow_mutex);

	free(q_item);

	return;
}

static int bmi_cache_check_cache_req(struct fp_queue_item *qitem) 
{
	cache_reply_t reply;
	int flag = 0;
	int ret = -1;
		
	ret = cache_req_test( &(qitem->cache_req.request), &flag, &reply, NULL);
	if ( ret < 0 ) 
	{
		PVFS_perror_gossip("bmi_cache_check_cache_req: "
                        "error_code", ret);
		return ret;
	}

	/* a request is finished. */
	if ( flag )
	{
		qitem->cache_req.mem_cnt = reply.count;
		qitem->cache_req.moff_list = (PVFS_offset **)reply.cbuf_offset_array;
		qitem->cache_req.msize_list = reply.cbuf_size_array;
		qitem->cache_req.errval = reply.errval;

		return 1;
	}
	return 0;
}

static int bmi_cache_release_cache_src(struct fp_queue_item *qitem)
{
	int ret;
	
	ret = cache_req_done( &(qitem->cache_req.request) );	
	if ( ret < 0 ) 
	{
		PVFS_perror_gossip("bmi_cache_release_cache_src: "
                        "error_code", ret);
		return ret;
	}
	
	return 0;
}

static int bmi_cache_init_cache_req(struct fp_queue_item *qitem, int op )
{
	int ret = 0;
	cache_read_desc_t desc1;
	cache_write_desc_t desc2;
	cache_reply_t reply;

	if ( op == BMI_TO_CACHE )  /* write */
	{
		desc2.coll_id = qitem->parent->dest.u.trove.coll_id;
		desc2.handle = qitem->parent->dest.u.trove.handle;
		desc2.context_id = global_trove_context;

		/* TODO: if we use intemediate buffer, change here */
		desc2.buffer = NULL;
		desc2.len = 0;
		
		desc2.stream_array_count  = qitem->pint_req.result.segs; 
		desc2.stream_offset_array = qitem->pint_req.result.offset_array;
		desc2.stream_size_array = qitem->pint_req.result.size_array;

		ret = cache_write_post( &desc2, 
					&qitem->cache_req.request, 
					&reply,
					NULL );
		if ( ret < 0 ) 
		{
			PVFS_perror_gossip("bmi_cache_init_cache_req: "
                                     "error_code", ret);
		}
	}
	else /* read */
	{
		desc1.coll_id = qitem->parent->dest.u.trove.coll_id;
		desc1.handle = qitem->parent->dest.u.trove.handle;
		desc1.context_id = global_trove_context;

		/* TODO: if we use intemediate buffer, change here */
		desc1.buffer = NULL;
		desc1.len = 0;
		
		desc1.stream_array_count  = qitem->pint_req.result.segs; 
		desc1.stream_offset_array = qitem->pint_req.result.offset_array;
		desc1.stream_size_array = qitem->pint_req.result.size_array;

		ret = cache_read_post( &desc1, 
					&qitem->cache_req.request, 
					&reply,
					NULL );
		if ( ret < 0 ) 
		{
			PVFS_perror_gossip("bmi_cache_init_cache_req: "
                                     "error_code", ret);
		}
	}
	return ret;
}

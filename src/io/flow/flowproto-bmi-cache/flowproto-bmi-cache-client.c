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
#include "trove.h"
#include "thread-mgr.h"
#include "pint-perf-counter.h"
    
#define BUFFERS_PER_FLOW 8
#define BUFFER_SIZE (256*1024)
#define MAX_REGIONS 16

struct result_chain_entry
{
    void* buffer_offset;
    PINT_Request_result result;
    PVFS_size size_list[MAX_REGIONS];
    PVFS_offset offset_list[MAX_REGIONS];
    struct result_chain_entry* next;
};

/* fp_queue_item describes an individual buffer being used within the flow */
struct fp_queue_item
{
    int last;
    int seq;
    void* buffer;
    PVFS_size buffer_used;
    struct result_chain_entry result_chain;
    int result_chain_count;
    struct qlist_head list_link;
    flow_descriptor* parent;
    struct PINT_thread_mgr_bmi_callback bmi_callback;
    struct PINT_thread_mgr_trove_callback trove_callback;
};

/* fp_private_data is information specific to this flow protocol, stored
 * in flow descriptor but hidden from caller
 */
struct fp_private_data
{
    flow_descriptor* parent;
    struct fp_queue_item prealloc_array[BUFFERS_PER_FLOW];
    struct qlist_head list_link;
    PVFS_size total_bytes_processed;
    int next_seq;
    int next_seq_to_send;
    int dest_pending;
    int dest_last_posted;
    int initial_posts;
    gen_mutex_t flow_mutex;
    void* tmp_buffer_list[MAX_REGIONS];
    void* intermediate;

    struct qlist_head src_list;
    struct qlist_head dest_list;
    struct qlist_head empty_list;
};
#define PRIVATE_FLOW(target_flow)\
    ((struct fp_private_data*)(target_flow->flow_protocol_data))

static int fp_bmi_cache_id = -1;
static bmi_context_id global_bmi_context = -1;
static void mem_to_bmi_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void bmi_to_mem_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void cleanup_buffers(struct fp_private_data* flow_data);


/* interface prototypes */
static int fp_bmi_cache_initialize(int flowproto_id);

static int fp_bmi_cache_finalize(void);

static int fp_bmi_cache_getinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

static int fp_bmi_cache_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter);

static int fp_bmi_cache_post(flow_descriptor * flow_d);

static char fp_bmi_cache_name[] = "flowproto_bmi_cache";

struct flowproto_ops fp_bmi_cache_ops = {
    fp_bmi_cache_name,
    fp_bmi_cache_initialize,
    fp_bmi_cache_finalize,
    fp_bmi_cache_getinfo,
    fp_bmi_cache_setinfo,
    fp_bmi_cache_post
};

/* fp_bmi_cache_initialize()
 *
 * starts up the flow protocol
 *
 * returns 0 on succes, -PVFS_error on failure
 */
int fp_bmi_cache_initialize(int flowproto_id)
{
    int ret = -1;

    ret = PINT_thread_mgr_bmi_start();
    if(ret < 0)
	return(ret);
    PINT_thread_mgr_bmi_getcontext(&global_bmi_context);

    fp_bmi_cache_id = flowproto_id;

    return(0);
}

/* fp_bmi_cache_finalize()
 *
 * shuts down the flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_bmi_cache_finalize(void)
{
    PINT_thread_mgr_bmi_stop();
    return (0);
}

/* fp_bmi_cache_getinfo()
 *
 * retrieves runtime parameters from flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_bmi_cache_getinfo(flow_descriptor * flow_d,
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

/* fp_bmi_cache_setinfo()
 *
 * sets runtime parameters in flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_bmi_cache_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-PVFS_ENOSYS);
}

/* fp_bmi_cache_post()
 *
 * posts a flow descriptor to begin work
 *
 * returns 0 on success, 1 on immediate completion, -PVFS_error on failure
 */
int fp_bmi_cache_post(flow_descriptor * flow_d)
{
    struct fp_private_data* flow_data = NULL;
    int i;

    assert( (flow_d->src.endpoint_id == MEM_ENDPOINT &&
	    flow_d->dest.endpoint_id == BMI_ENDPOINT) ||
	   (flow_d->src.endpoint_id == BMI_ENDPOINT &&
	    flow_d->dest.endpoint_id == MEM_ENDPOINT) );

    flow_data = (struct fp_private_data*)malloc(sizeof(struct
	fp_private_data));
    if(!flow_data)
	return(-PVFS_ENOMEM);
    memset(flow_data, 0, sizeof(struct fp_private_data));
    
    flow_d->flow_protocol_data = flow_data;
    flow_d->state = FLOW_TRANSMITTING;
    flow_data->parent = flow_d;
    INIT_QLIST_HEAD(&flow_data->src_list);
    INIT_QLIST_HEAD(&flow_data->dest_list);
    INIT_QLIST_HEAD(&flow_data->empty_list);
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

    for(i=0; i<BUFFERS_PER_FLOW; i++)
    {
	flow_data->prealloc_array[i].parent = flow_d;
	flow_data->prealloc_array[i].bmi_callback.data = 
	    &(flow_data->prealloc_array[i]);
	flow_data->prealloc_array[i].trove_callback.data = 
	    &(flow_data->prealloc_array[i]);
    }

    /* remaining setup depends on the endpoints we intend to use */
    if(flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == MEM_ENDPOINT)
    {
	flow_data->prealloc_array[0].buffer = flow_d->dest.u.mem.buffer;
	flow_data->prealloc_array[0].bmi_callback.fn = bmi_to_mem_callback_fn;
	bmi_to_mem_callback_fn(&(flow_data->prealloc_array[0]), 0, 0);
    }
    else if(flow_d->src.endpoint_id == MEM_ENDPOINT &&
	flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	flow_data->prealloc_array[0].buffer = flow_d->src.u.mem.buffer;
	flow_data->prealloc_array[0].bmi_callback.fn = mem_to_bmi_callback_fn;
	mem_to_bmi_callback_fn(&(flow_data->prealloc_array[0]), 0, 0);
    }
    else
    {
	return(-ENOSYS);
    }

    return (0);
}


/* cleanup_buffers()
 *
 * releases any resources consumed during flow processing
 *
 * no return value
 */
static void cleanup_buffers(struct fp_private_data* flow_data)
{
    if(flow_data->parent->src.endpoint_id == MEM_ENDPOINT &&
	flow_data->parent->dest.endpoint_id == BMI_ENDPOINT)
    {
	if(flow_data->intermediate)
	{
	    BMI_memfree(flow_data->parent->dest.u.bmi.address,
		flow_data->intermediate, BUFFER_SIZE, BMI_SEND);
	}
    }
    else if(flow_data->parent->src.endpoint_id == BMI_ENDPOINT &&
	flow_data->parent->dest.endpoint_id == MEM_ENDPOINT)
    {
	if(flow_data->intermediate)
	{
	    BMI_memfree(flow_data->parent->src.u.bmi.address,
		flow_data->intermediate, BUFFER_SIZE, BMI_RECV);
	}
    }

    return;
}

/* mem_to_bmi_callback()
 *
 * function to be called upon completion of bmi operations in memory to
 * bmi transfers
 * 
 * no return value
 */
static void mem_to_bmi_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    int i;
    PVFS_id_gen_t tmp_id;
    PVFS_size bytes_processed = 0;
    char *src_ptr, *dest_ptr;
    enum bmi_buffer_type buffer_type = BMI_EXT_ALLOC;
    struct flow_descriptor* flow_d;

    /* TODO: error handling */
    if(error_code != 0)
    {
	PVFS_perror_gossip("bmi_recv_callback_fn error_code", error_code);
	assert(0);
    }

    gen_mutex_lock(&flow_data->flow_mutex);
    
    flow_data->parent->total_transfered += actual_size;

    /* are we done? */
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
    {
	q_item->parent->state = FLOW_COMPLETE;
	gen_mutex_unlock(&flow_data->flow_mutex);
	cleanup_buffers(flow_data);
	flow_d = flow_data->parent;
	free(flow_data);
	flow_d->release(flow_d);
	flow_d->callback(flow_d);
	return;
    }

    /* process request */
    q_item->result_chain.result.offset_array = 
	q_item->result_chain.offset_list;
    q_item->result_chain.result.size_array = 
	q_item->result_chain.size_list;
    q_item->result_chain.result.bytemax = BUFFER_SIZE;
    q_item->result_chain.result.bytes = 0;
    q_item->result_chain.result.segmax = MAX_REGIONS;
    q_item->result_chain.result.segs = 0;
    q_item->result_chain.buffer_offset = NULL;
    ret = PINT_Process_request(q_item->parent->file_req_state,
	q_item->parent->mem_req_state,
	&q_item->parent->file_data,
	&q_item->result_chain.result,
	PINT_CLIENT);

    /* TODO: error handling */ 
    assert(ret >= 0);

    /* was MAX_REGIONS enough to satisfy this step? */
    if(!PINT_REQUEST_DONE(flow_data->parent->file_req_state) &&
	q_item->result_chain.result.bytes < BUFFER_SIZE)
    {
	/* create an intermediate buffer */
	if(!flow_data->intermediate)
	{
	    flow_data->intermediate = BMI_memalloc(
		flow_data->parent->dest.u.bmi.address,
		BUFFER_SIZE, BMI_SEND);
	    /* TODO: error handling */
	    assert(flow_data->intermediate);
	}

	/* copy what we have so far into intermediate buffer */
	for(i=0; i<q_item->result_chain.result.segs; i++)
	{
	    src_ptr = ((char*)q_item->parent->src.u.mem.buffer + 
		q_item->result_chain.offset_list[i]);
	    dest_ptr = ((char*)flow_data->intermediate + bytes_processed);
	    memcpy(dest_ptr, src_ptr, q_item->result_chain.size_list[i]);
	    bytes_processed += q_item->result_chain.size_list[i];
	}

	do
	{
	    q_item->result_chain.result.bytemax = BUFFER_SIZE - bytes_processed;
	    q_item->result_chain.result.bytes = 0;
	    q_item->result_chain.result.segmax = MAX_REGIONS;
	    q_item->result_chain.result.segs = 0;
	    q_item->result_chain.buffer_offset = NULL;
	    /* process ahead */
	    ret = PINT_Process_request(q_item->parent->file_req_state,
		q_item->parent->mem_req_state,
		&q_item->parent->file_data,
		&q_item->result_chain.result,
		PINT_CLIENT);
	    /* TODO: error handling */
	    assert(ret >= 0);

	    /* copy what we have so far into intermediate buffer */
	    for(i=0; i<q_item->result_chain.result.segs; i++)
	    {
		src_ptr = ((char*)q_item->parent->src.u.mem.buffer + 
		    q_item->result_chain.offset_list[i]);
		dest_ptr = ((char*)flow_data->intermediate + bytes_processed);
		memcpy(dest_ptr, src_ptr, q_item->result_chain.size_list[i]);
		bytes_processed += q_item->result_chain.size_list[i];
	    }
	}while(bytes_processed < BUFFER_SIZE &&
	    !PINT_REQUEST_DONE(q_item->parent->file_req_state));

	/* setup for BMI operation */
	flow_data->tmp_buffer_list[0] = flow_data->intermediate;
	q_item->result_chain.result.size_array[0] = bytes_processed;
	q_item->result_chain.result.bytes = bytes_processed;
	q_item->result_chain.result.segs = 1;
	buffer_type = BMI_PRE_ALLOC;
    }
    else
    {
	/* go ahead and return if there is nothing to do */
	if(q_item->result_chain.result.bytes == 0)
	{	
	    q_item->parent->state = FLOW_COMPLETE;
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    cleanup_buffers(flow_data);
	    flow_d = flow_data->parent;
	    free(flow_data);
	    flow_d->release(flow_d);
	    flow_d->callback(flow_d);
	    return;
	}

	/* convert offsets to memory addresses */
	for(i=0; i<q_item->result_chain.result.segs; i++)
	{
	    flow_data->tmp_buffer_list[i] = 
		(void*)(q_item->result_chain.result.offset_array[i] +
		q_item->buffer);
	}
    }

    ret = BMI_post_send_list(&tmp_id,
	q_item->parent->dest.u.bmi.address,
	(const void**)flow_data->tmp_buffer_list,
	q_item->result_chain.result.size_array,
	q_item->result_chain.result.segs,
	q_item->result_chain.result.bytes,
	buffer_type,
	q_item->parent->tag,
	&q_item->bmi_callback,
	global_bmi_context);
    /* TODO: error handling */
    assert(ret >= 0);

    gen_mutex_unlock(&flow_data->flow_mutex);

    if(ret == 1)
    {
	mem_to_bmi_callback_fn(q_item, 
	    q_item->result_chain.result.bytes, 0);
    }

    return;
}


/* bmi_to_mem_callback()
 *
 * function to be called upon completion of bmi operations in bmi to memory
 * transfers
 * 
 * no return value
 */
static void bmi_to_mem_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    int i;
    PVFS_id_gen_t tmp_id;
    PVFS_size tmp_actual_size;
    PVFS_size* size_array;
    int segs;
    PVFS_size total_size;
    enum bmi_buffer_type buffer_type = BMI_EXT_ALLOC;
    PVFS_size bytes_processed = 0;
    char *src_ptr, *dest_ptr;
    PVFS_size region_size;
    struct flow_descriptor* flow_d;

    /* TODO: error handling */
    if(error_code != 0)
    {
	PVFS_perror_gossip("bmi_recv_callback_fn error_code", error_code);
	assert(0);
    }

    gen_mutex_lock(&flow_data->flow_mutex);
    
    flow_data->parent->total_transfered += actual_size;

    /* if this is the result of a receive into an intermediate buffer,
     * then we must copy out */
    if(flow_data->tmp_buffer_list[0] == flow_data->intermediate &&
	flow_data->intermediate != NULL)
    {
	/* copy out what we have so far */
	for(i=0; i<q_item->result_chain.result.segs; i++)
	{
	    region_size = q_item->result_chain.size_list[i];
	    src_ptr = (char*)(flow_data->intermediate + 
		bytes_processed);
	    dest_ptr = (char*)(q_item->result_chain.offset_list[i]
		+ q_item->parent->dest.u.mem.buffer);
	    memcpy(dest_ptr, src_ptr, region_size);
	    bytes_processed += region_size;
	}

	do
	{
	    q_item->result_chain.result.bytemax = BUFFER_SIZE - bytes_processed;
	    q_item->result_chain.result.bytes = 0;
	    q_item->result_chain.result.segmax = MAX_REGIONS;
	    q_item->result_chain.result.segs = 0;
	    q_item->result_chain.buffer_offset = NULL;
	    /* process ahead */
	    ret = PINT_Process_request(q_item->parent->file_req_state,
		q_item->parent->mem_req_state,
		&q_item->parent->file_data,
		&q_item->result_chain.result,
		PINT_CLIENT);
	    /* TODO: error handling */
	    assert(ret >= 0);
	    /* copy out what we have so far */
	    for(i=0; i<q_item->result_chain.result.segs; i++)
	    {
		region_size = q_item->result_chain.size_list[i];
		src_ptr = (char*)(flow_data->intermediate + 
		    bytes_processed);
		dest_ptr = (char*)(q_item->result_chain.offset_list[i]
		    + q_item->parent->dest.u.mem.buffer);
		memcpy(dest_ptr, src_ptr, region_size);
		bytes_processed += region_size;
	    }
	}while(bytes_processed < BUFFER_SIZE &&
	    !PINT_REQUEST_DONE(q_item->parent->file_req_state));
    }

    /* are we done? */
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
    {
	q_item->parent->state = FLOW_COMPLETE;
	gen_mutex_unlock(&flow_data->flow_mutex);
	cleanup_buffers(flow_data);
	flow_d = flow_data->parent;
	free(flow_data);
	flow_d->release(flow_d);
	flow_d->callback(flow_d);
	return;
    }

    /* process request */
    q_item->result_chain.result.offset_array = 
	q_item->result_chain.offset_list;
    q_item->result_chain.result.size_array = 
	q_item->result_chain.size_list;
    q_item->result_chain.result.bytemax = BUFFER_SIZE;
    q_item->result_chain.result.bytes = 0;
    q_item->result_chain.result.segmax = MAX_REGIONS;
    q_item->result_chain.result.segs = 0;
    q_item->result_chain.buffer_offset = NULL;
    ret = PINT_Process_request(q_item->parent->file_req_state,
	q_item->parent->mem_req_state,
	&q_item->parent->file_data,
	&q_item->result_chain.result,
	PINT_CLIENT);
    /* TODO: error handling */ 
    assert(ret >= 0);

    /* was MAX_REGIONS enough to satisfy this step? */
    if(!PINT_REQUEST_DONE(flow_data->parent->file_req_state) &&
	q_item->result_chain.result.bytes < BUFFER_SIZE)
    {
	/* create an intermediate buffer */
	if(!flow_data->intermediate)
	{
	    flow_data->intermediate = BMI_memalloc(
		flow_data->parent->src.u.bmi.address,
		BUFFER_SIZE, BMI_RECV);
	    /* TODO: error handling */
	    assert(flow_data->intermediate);
	}
	/* setup for BMI operation */
	flow_data->tmp_buffer_list[0] = flow_data->intermediate;
	buffer_type = BMI_PRE_ALLOC;
	q_item->buffer_used = BUFFER_SIZE;
	total_size = BUFFER_SIZE;
	size_array = &q_item->buffer_used;
	segs = 1;
	/* we will copy data out on next iteration */
    }
    else
    {
	/* normal case */
	segs = q_item->result_chain.result.segs;
	size_array = q_item->result_chain.result.size_array;
	total_size = q_item->result_chain.result.bytes;

	/* convert offsets to memory addresses */
	for(i=0; i<q_item->result_chain.result.segs; i++)
	{
	    flow_data->tmp_buffer_list[i] = 
		(void*)(q_item->result_chain.result.offset_array[i] +
		q_item->buffer);
	}

	/* go ahead and return if there is nothing to do */
	if(q_item->result_chain.result.bytes == 0)
	{	
	    q_item->parent->state = FLOW_COMPLETE;
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    cleanup_buffers(flow_data);
	    flow_d = flow_data->parent;
	    free(flow_data);
	    flow_d->release(flow_d);
	    flow_d->callback(flow_d);
	    return;
	}
    }

    ret = BMI_post_recv_list(&tmp_id,
	q_item->parent->src.u.bmi.address,
	flow_data->tmp_buffer_list,
	size_array,
	segs,
	total_size,
	&tmp_actual_size,
	BMI_EXT_ALLOC,
	q_item->parent->tag,
	&q_item->bmi_callback,
	global_bmi_context);
    /* TODO: error handling */
    assert(ret >= 0);

    gen_mutex_unlock(&flow_data->flow_mutex);

    if(ret == 1)
    {
	bmi_to_mem_callback_fn(q_item, tmp_actual_size, 0);
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

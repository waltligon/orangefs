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

static int fp_multiqueue_id = -1;
static bmi_context_id global_bmi_context = -1;
static gen_mutex_t completion_mutex = GEN_MUTEX_INITIALIZER;
static QLIST_HEAD(completion_queue); 
#ifdef __PVFS2_JOB_THREADED__
static pthread_cond_t completion_cond = PTHREAD_COND_INITIALIZER;
#endif

#ifdef __PVFS2_TROVE_SUPPORT__
static TROVE_context_id global_trove_context = -1;
static void bmi_recv_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void bmi_send_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void trove_read_callback_fn(void *user_ptr,
		           PVFS_error error_code);
static void trove_write_callback_fn(void *user_ptr,
		           PVFS_error error_code);
#endif
static void mem_to_bmi_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void bmi_to_mem_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void cleanup_buffers(struct fp_private_data* flow_data);


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

int fp_multiqueue_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms);

int fp_multiqueue_service(flow_descriptor * flow_d);

char fp_multiqueue_name[] = "flowproto_multiqueue";

struct flowproto_ops fp_multiqueue_ops = {
    fp_multiqueue_name,
    fp_multiqueue_initialize,
    fp_multiqueue_finalize,
    fp_multiqueue_getinfo,
    fp_multiqueue_setinfo,
    fp_multiqueue_post,
    fp_multiqueue_find_serviceable,
    fp_multiqueue_service
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
    int i;

    assert((flow_d->src.endpoint_id == BMI_ENDPOINT && 
	    flow_d->dest.endpoint_id == TROVE_ENDPOINT) ||
	   (flow_d->src.endpoint_id == TROVE_ENDPOINT &&
	    flow_d->dest.endpoint_id == BMI_ENDPOINT) ||
	   (flow_d->src.endpoint_id == MEM_ENDPOINT &&
	    flow_d->dest.endpoint_id == BMI_ENDPOINT) ||
	   (flow_d->src.endpoint_id == BMI_ENDPOINT &&
	    flow_d->dest.endpoint_id == MEM_ENDPOINT));

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
#ifdef __PVFS2_TROVE_SUPPORT__
    else if(flow_d->src.endpoint_id == TROVE_ENDPOINT &&
	flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	flow_data->initial_posts = BUFFERS_PER_FLOW;
	for(i=0; i<BUFFERS_PER_FLOW; i++)
	{
	    bmi_send_callback_fn(&(flow_data->prealloc_array[i]), 0, 0);
	}
    }
    else if(flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == TROVE_ENDPOINT)
    {
	/* only post one outstanding recv at a time; easier to manage */
	flow_data->initial_posts = 1;

	/* place remaining buffers on "empty" queue */
	for(i=1; i<BUFFERS_PER_FLOW; i++)
	{
	    qlist_add_tail(&flow_data->prealloc_array[i].list_link,
		&flow_data->empty_list);
	}

	trove_write_callback_fn(&(flow_data->prealloc_array[0]), 0);
    }
#endif
    else
    {
	return(-ENOSYS);
    }

    return (0);
}

/* fp_multiqueue_find_serviceable()
 *
 * looks for flows that have completed or are in need of service
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms)
{
    int incount = *count;
    struct fp_private_data* tmp_data;
    int ret;
#ifdef __PVFS2_JOB_THREADED__
    struct timeval base;
    struct timespec pthread_timeout;
#endif

    *count = 0;

    gen_mutex_lock(&completion_mutex);
    /* see if anything has completed */
    if(qlist_empty(&completion_queue))
    {	
#ifdef __PVFS2_JOB_THREADED__
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

	ret = pthread_cond_timedwait(&completion_cond, 
	    &completion_mutex, &pthread_timeout);
#else
	/* TODO: fill in */
	ret = 0;
#endif
	if(ret == ETIMEDOUT)
	{
	    gen_mutex_unlock(&completion_mutex);
	    return(0);
	}
    }

    /* run down queue, pulling out anything we can find */
    while(*count < incount && !qlist_empty(&completion_queue))
    {
	tmp_data = qlist_entry(completion_queue.next,
	    struct fp_private_data, list_link);
	qlist_del(&tmp_data->list_link);
	flow_d_array[*count] = tmp_data->parent;
	cleanup_buffers(tmp_data);
	free(tmp_data);
	(*count)++;
    }
    gen_mutex_unlock(&completion_mutex);

    return (0);
}

/* fp_multiqueue_service()
 *
 * services a flow descriptor, should never be called for this particular
 * protocol because it services itself autonomously
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_service(flow_descriptor * flow_d)
{
    /* should never get here; this protocol skips the scheduler for now */
    assert(0);
    return (-PVFS_ENOSYS);
}

#ifdef __PVFS2_TROVE_SUPPORT__
/* bmi_recv_callback_fn()
 *
 * function to be called upon completion of a BMI recv operation
 * 
 * no return value
 */
static void bmi_recv_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    PVFS_id_gen_t tmp_id;
    PVFS_size tmp_actual_size;
    struct result_chain_entry* result_tmp;
    struct result_chain_entry* old_result_tmp;
    PVFS_size bytes_processed = 0;
    void* tmp_buffer;

    /* TODO: error handling */
    if(error_code != 0)
    {
	PVFS_perror_gossip("bmi_recv_callback_fn error_code", error_code);
	assert(0);
    }

    gen_mutex_lock(&flow_data->flow_mutex);

    /* remove from current queue */
    qlist_del(&q_item->list_link);
    /* add to dest queue */
    qlist_add_tail(&q_item->list_link, &flow_data->dest_list);
    result_tmp = &q_item->result_chain;
    do{
	assert(result_tmp->result.bytes);
	ret = trove_bstream_write_list(q_item->parent->dest.u.trove.coll_id,
	    q_item->parent->dest.u.trove.handle,
	    (char**)&result_tmp->buffer_offset,
	    &result_tmp->result.bytes,
	    1,
	    result_tmp->result.offset_array,
	    result_tmp->result.size_array,
	    result_tmp->result.segs,
	    &result_tmp->result.bytes,
	    0,
	    NULL,
	    &q_item->trove_callback,
	    global_trove_context,
	    &tmp_id);
	result_tmp = result_tmp->next;

	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    /* immediate completion; trigger callback ourselves */
	    trove_write_callback_fn(q_item, 0);
	    gen_mutex_lock(&flow_data->flow_mutex);
	}
    }while(result_tmp);

    /* do we need to repost another recv? */

    if((!PINT_REQUEST_DONE(q_item->parent->file_req_state)) 
	&& qlist_empty(&flow_data->src_list) 
	&& !qlist_empty(&flow_data->empty_list))
    {
	q_item = qlist_entry(flow_data->empty_list.next,
	    struct fp_queue_item, list_link);
	qlist_del(&q_item->list_link);
	qlist_add_tail(&q_item->list_link, &flow_data->src_list);

	if(!q_item->buffer)
	{
	    /* if the q_item has not been used, allocate a buffer */
	    q_item->buffer = BMI_memalloc(q_item->parent->src.u.bmi.address,
		BUFFER_SIZE, BMI_RECV);
	    /* TODO: error handling */
	    assert(q_item->buffer);
	    q_item->bmi_callback.fn = bmi_recv_callback_fn;
	    q_item->trove_callback.fn = trove_write_callback_fn;
	}
	
	result_tmp = &q_item->result_chain;
	old_result_tmp = result_tmp;
	tmp_buffer = q_item->buffer;
	do{
	    q_item->result_chain_count++;
	    if(!result_tmp)
	    {
		result_tmp = (struct result_chain_entry*)malloc(
		    sizeof(struct result_chain_entry));
		assert(result_tmp);
		old_result_tmp->next = result_tmp;
	    }
	    /* process request */
	    result_tmp->result.offset_array = 
		result_tmp->offset_list;
	    result_tmp->result.size_array = 
		result_tmp->size_list;
	    result_tmp->result.bytemax = BUFFER_SIZE;
	    result_tmp->result.bytes = 0;
	    result_tmp->result.segmax = MAX_REGIONS;
	    result_tmp->result.segs = 0;
	    result_tmp->buffer_offset = tmp_buffer;
	    ret = PINT_Process_request(q_item->parent->file_req_state,
		q_item->parent->mem_req_state,
		&q_item->parent->file_data,
		&result_tmp->result,
		PINT_SERVER);
	    /* TODO: error handling */ 
	    assert(ret >= 0);
	    
	    old_result_tmp = result_tmp;
	    result_tmp = result_tmp->next;
	    tmp_buffer = (void*)((char*)tmp_buffer + old_result_tmp->result.bytes);
	    bytes_processed += old_result_tmp->result.bytes;
	}while(bytes_processed < BUFFER_SIZE && 
	    !PINT_REQUEST_DONE(q_item->parent->file_req_state));

	if(bytes_processed == 0)
	{	
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    return;
	}

	flow_data->total_bytes_processed += bytes_processed;

	/* TODO: what if we recv less than expected? */
	ret = BMI_post_recv(&tmp_id,
	    q_item->parent->src.u.bmi.address,
	    q_item->buffer,
	    BUFFER_SIZE,
	    &tmp_actual_size,
	    BMI_PRE_ALLOC,
	    q_item->parent->tag,
	    &q_item->bmi_callback,
	    global_bmi_context);
	
	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
	    /* immediate completion; trigger callback ourselves */
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    bmi_recv_callback_fn(q_item, tmp_actual_size, 0);
	    gen_mutex_lock(&flow_data->flow_mutex);
	}
    }
	
    gen_mutex_unlock(&flow_data->flow_mutex);
    return;
}


/* trove_read_callback_fn()
 *
 * function to be called upon completion of a trove read operation
 *
 * no return value
 */
static void trove_read_callback_fn(void *user_ptr,
		           PVFS_error error_code)
{
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    struct result_chain_entry* result_tmp;
    struct result_chain_entry* old_result_tmp;
    PVFS_id_gen_t tmp_id;
    int done = 0;
    struct qlist_head* tmp_link;

    /* TODO: error handling */
    if(error_code != 0)
    {
	PVFS_perror_gossip("bmi_recv_callback_fn error_code", error_code);
	assert(0);
    }

    gen_mutex_lock(&flow_data->flow_mutex);
    /* don't do anything until the last read completes */
    if(q_item->result_chain_count > 1)
    {
	q_item->result_chain_count--;
	gen_mutex_unlock(&flow_data->flow_mutex);
	return;
    }

    /* remove from current queue */
    qlist_del(&q_item->list_link);
    /* add to dest queue */
    qlist_add_tail(&q_item->list_link, &flow_data->dest_list);

    result_tmp = &q_item->result_chain;
    do{
	old_result_tmp = result_tmp;
	result_tmp = result_tmp->next;
	if(old_result_tmp != &q_item->result_chain)
	    free(old_result_tmp);
    }while(result_tmp);
    q_item->result_chain_count = 0;

    /* while we hold dest lock, look for next seq no. to send */
    do{
	qlist_for_each(tmp_link, &flow_data->dest_list)
	{
	    q_item = qlist_entry(tmp_link, struct fp_queue_item,
		list_link);
	    if(q_item->seq == flow_data->next_seq_to_send)
		break;
	}

	if(q_item->seq == flow_data->next_seq_to_send)
	{
	    flow_data->dest_pending++;
	    assert(q_item->buffer_used);
	    ret = BMI_post_send(&tmp_id,
		q_item->parent->dest.u.bmi.address,
		q_item->buffer,
		q_item->buffer_used,
		BMI_PRE_ALLOC,
		q_item->parent->tag,
		&q_item->bmi_callback,
		global_bmi_context);
	    flow_data->next_seq_to_send++;
	    if(q_item->last)
		flow_data->dest_last_posted = 1;
	}
	else
	{
	    ret = 0;
	    done = 1;
	}
	
	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    /* immediate completion; trigger callback ourselves */
	    bmi_send_callback_fn(q_item, q_item->buffer_used, 0);
	    gen_mutex_lock(&flow_data->flow_mutex);
	}
    }
    while(!done);

    gen_mutex_unlock(&flow_data->flow_mutex);
    return;
}

/* bmi_send_callback_fn()
 *
 * function to be called upon completion of a BMI send operation
 *
 * no return value
 */
static void bmi_send_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    struct fp_queue_item* q_item = user_ptr;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    int ret;
    struct result_chain_entry* result_tmp;
    struct result_chain_entry* old_result_tmp;
    void* tmp_buffer;
    PVFS_size bytes_processed = 0;
    PVFS_id_gen_t tmp_id;

    /* TODO: error handling */
    assert(error_code == 0);

    gen_mutex_lock(&flow_data->flow_mutex);

    flow_data->parent->total_transfered += actual_size;

    if(q_item->buffer)
    {
	flow_data->dest_pending--;
    }
    else
    {
	flow_data->initial_posts--;
    }

    /* if this was the last operation, then mark the flow as done */
    if(flow_data->initial_posts == 0 &&
	flow_data->dest_pending == 0 && 
	flow_data->dest_last_posted)
    {
	q_item->parent->state = FLOW_COMPLETE;
	gen_mutex_unlock(&flow_data->flow_mutex);
	gen_mutex_lock(&completion_mutex);
	qlist_add_tail(&(flow_data->list_link), 
	    &completion_queue);
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_signal(&completion_cond);
#endif
	gen_mutex_unlock(&completion_mutex);
	return;
    }

    if(q_item->buffer)
    {
	/* if this q_item has been used before, remove it from its 
	 * current queue */
	qlist_del(&q_item->list_link);
    }
    else
    {
	/* if the q_item has not been used, allocate a buffer */
	q_item->buffer = BMI_memalloc(q_item->parent->dest.u.bmi.address,
	    BUFFER_SIZE, BMI_SEND);
	/* TODO: error handling */
	assert(q_item->buffer);
	q_item->bmi_callback.fn = bmi_send_callback_fn;
	q_item->trove_callback.fn = trove_read_callback_fn;
    }
    
    /* add to src queue */
    qlist_add_tail(&q_item->list_link, &flow_data->src_list);

    result_tmp = &q_item->result_chain;
    old_result_tmp = result_tmp;
    tmp_buffer = q_item->buffer;
    q_item->buffer_used = 0;
    do{
	q_item->result_chain_count++;
	if(!result_tmp)
	{
	    result_tmp = (struct result_chain_entry*)malloc(
		sizeof(struct result_chain_entry));
	    assert(result_tmp);
	    memset(result_tmp, 0 , sizeof(struct result_chain_entry));
	    old_result_tmp->next = result_tmp;
	}
	/* process request */
	result_tmp->result.offset_array = 
	    result_tmp->offset_list;
	result_tmp->result.size_array = 
	    result_tmp->size_list;
	result_tmp->result.bytemax = BUFFER_SIZE;
	result_tmp->result.bytes = 0;
	result_tmp->result.segmax = MAX_REGIONS;
	result_tmp->result.segs = 0;
	result_tmp->buffer_offset = tmp_buffer;
	q_item->seq = flow_data->next_seq;
	flow_data->next_seq++;
	ret = PINT_Process_request(q_item->parent->file_req_state,
	    q_item->parent->mem_req_state,
	    &q_item->parent->file_data,
	    &result_tmp->result,
	    PINT_SERVER);
	/* TODO: error handling */ 
	assert(ret >= 0);
	
	old_result_tmp = result_tmp;
	result_tmp = result_tmp->next;
	tmp_buffer = (void*)((char*)tmp_buffer + old_result_tmp->result.bytes);
	bytes_processed += old_result_tmp->result.bytes;
	q_item->buffer_used += old_result_tmp->result.bytes;
    }while(bytes_processed < BUFFER_SIZE && 
	!PINT_REQUEST_DONE(q_item->parent->file_req_state));

    flow_data->total_bytes_processed += bytes_processed;
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
    {
	q_item->last = 1;
	/* special case, we never have a "last" operation when there
	 * is no work to do, trigger manually
	 */
	if(flow_data->total_bytes_processed == 0)
	    flow_data->dest_last_posted = 1;
    }

    if(bytes_processed == 0)
    {	
	gen_mutex_unlock(&flow_data->flow_mutex);
	return;
    }

    assert(q_item->buffer_used);

    result_tmp = &q_item->result_chain;
    do{
	assert(q_item->buffer_used);
	assert(result_tmp->result.bytes);
	ret = trove_bstream_read_list(q_item->parent->src.u.trove.coll_id,
	    q_item->parent->src.u.trove.handle,
	    (char**)&result_tmp->buffer_offset,
	    &result_tmp->result.bytes,
	    1,
	    result_tmp->result.offset_array,
	    result_tmp->result.size_array,
	    result_tmp->result.segs,
	    &result_tmp->result.bytes,
	    0,
	    NULL,
	    &q_item->trove_callback,
	    global_trove_context,
	    &tmp_id);
	result_tmp = result_tmp->next;

	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
	    /* immediate completion; trigger callback ourselves */
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    trove_read_callback_fn(q_item, 0);
	    gen_mutex_lock(&flow_data->flow_mutex);
	}
    }while(result_tmp);

    gen_mutex_unlock(&flow_data->flow_mutex);
    return;
};

/* trove_write_callback_fn()
 *
 * function to be called upon completion of a trove write operation
 *
 * no return value
 */
static void trove_write_callback_fn(void *user_ptr,
		           PVFS_error error_code)
{
    PVFS_id_gen_t tmp_id;
    PVFS_size tmp_actual_size;
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    struct result_chain_entry* result_tmp;
    struct result_chain_entry* old_result_tmp;
    void* tmp_buffer;
    PVFS_size bytes_processed = 0;

    /* TODO: error handling */
    assert(error_code == 0);

    gen_mutex_lock(&flow_data->flow_mutex);

    /* don't do anything until the last write completes */
    if(q_item->result_chain_count > 1)
    {
	q_item->result_chain_count--;
	gen_mutex_unlock(&flow_data->flow_mutex);
	return;
    }

    result_tmp = &q_item->result_chain;
    do{
	q_item->parent->total_transfered += result_tmp->result.bytes;
	old_result_tmp = result_tmp;
	result_tmp = result_tmp->next;
	if(old_result_tmp != &q_item->result_chain)
	    free(old_result_tmp);
    }while(result_tmp);
    q_item->result_chain_count = 0;

    /* if this was the last operation, then mark the flow as done */
    if(flow_data->parent->total_transfered ==
	flow_data->total_bytes_processed &&
	PINT_REQUEST_DONE(flow_data->parent->file_req_state))
    {
	q_item->parent->state = FLOW_COMPLETE;
	gen_mutex_unlock(&flow_data->flow_mutex);
	gen_mutex_lock(&completion_mutex);
	qlist_add_tail(&(flow_data->list_link), 
	    &completion_queue);
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_signal(&completion_cond);
#endif
	gen_mutex_unlock(&completion_mutex);
	return;
    }

    /* if there are no more receives to post, just return */
    if(PINT_REQUEST_DONE(flow_data->parent->file_req_state))
    {
	gen_mutex_unlock(&flow_data->flow_mutex);
	return;
    }

    if(q_item->buffer)
    {
	/* if this q_item has been used before, remove it from its 
	 * current queue */
	qlist_del(&q_item->list_link);
    }
    else
    {
	/* if the q_item has not been used, allocate a buffer */
	q_item->buffer = BMI_memalloc(q_item->parent->src.u.bmi.address,
	    BUFFER_SIZE, BMI_RECV);
	/* TODO: error handling */
	assert(q_item->buffer);
	q_item->bmi_callback.fn = bmi_recv_callback_fn;
	q_item->trove_callback.fn = trove_write_callback_fn;
    }

    /* if src list is empty, then post new recv; otherwise just queue
     * in empty list
     */
    if(qlist_empty(&flow_data->src_list))
    {
	/* ready to post new recv! */
	qlist_add_tail(&q_item->list_link, &flow_data->src_list);
	
	result_tmp = &q_item->result_chain;
	old_result_tmp = result_tmp;
	tmp_buffer = q_item->buffer;
	do{
	    q_item->result_chain_count++;
	    if(!result_tmp)
	    {
		result_tmp = (struct result_chain_entry*)malloc(
		    sizeof(struct result_chain_entry));
		assert(result_tmp);
		memset(result_tmp, 0 , sizeof(struct result_chain_entry));
		old_result_tmp->next = result_tmp;
	    }
	    /* process request */
	    result_tmp->result.offset_array = 
		result_tmp->offset_list;
	    result_tmp->result.size_array = 
		result_tmp->size_list;
	    result_tmp->result.bytemax = BUFFER_SIZE;
	    result_tmp->result.bytes = 0;
	    result_tmp->result.segmax = MAX_REGIONS;
	    result_tmp->result.segs = 0;
	    result_tmp->buffer_offset = tmp_buffer;
	    ret = PINT_Process_request(q_item->parent->file_req_state,
		q_item->parent->mem_req_state,
		&q_item->parent->file_data,
		&result_tmp->result,
		PINT_SERVER);
	    /* TODO: error handling */ 
	    assert(ret >= 0);
	    
	    old_result_tmp = result_tmp;
	    result_tmp = result_tmp->next;
	    tmp_buffer = (void*)((char*)tmp_buffer + old_result_tmp->result.bytes);
	    bytes_processed += old_result_tmp->result.bytes;
	}while(bytes_processed < BUFFER_SIZE && 
	    !PINT_REQUEST_DONE(q_item->parent->file_req_state));

	flow_data->total_bytes_processed += bytes_processed;

	if(bytes_processed == 0)
	{	
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    return;
	}

	/* TODO: what if we recv less than expected? */
	ret = BMI_post_recv(&tmp_id,
	    q_item->parent->src.u.bmi.address,
	    q_item->buffer,
	    BUFFER_SIZE,
	    &tmp_actual_size,
	    BMI_PRE_ALLOC,
	    q_item->parent->tag,
	    &q_item->bmi_callback,
	    global_bmi_context);
	
	/* TODO: error handling */
	assert(ret >= 0);

	if(ret == 1)
	{
	    gen_mutex_unlock(&flow_data->flow_mutex);
	    /* immediate completion; trigger callback ourselves */
	    bmi_recv_callback_fn(q_item, tmp_actual_size, 0);
	    gen_mutex_lock(&flow_data->flow_mutex);
	}
    }
    else
    {
	qlist_add_tail(&q_item->list_link, 
	    &(flow_data->empty_list));
    }

    gen_mutex_unlock(&flow_data->flow_mutex);
    return;
};
#endif

/* cleanup_buffers()
 *
 * releases any resources consumed during flow processing
 *
 * no return value
 */
static void cleanup_buffers(struct fp_private_data* flow_data)
{
    int i;

    if(flow_data->parent->src.endpoint_id == BMI_ENDPOINT &&
	flow_data->parent->dest.endpoint_id == TROVE_ENDPOINT)
    {
	for(i=0; i<BUFFERS_PER_FLOW; i++)
	{
	    if(flow_data->prealloc_array[i].buffer)
	    {
		BMI_memfree(flow_data->parent->src.u.bmi.address,
		    flow_data->prealloc_array[i].buffer,
		    BUFFER_SIZE,
		    BMI_RECV);
	    }
	}
    }
    else if(flow_data->parent->src.endpoint_id == TROVE_ENDPOINT &&
	flow_data->parent->dest.endpoint_id == BMI_ENDPOINT)
    {
	for(i=0; i<BUFFERS_PER_FLOW; i++)
	{
	    if(flow_data->prealloc_array[i].buffer)
	    {
		BMI_memfree(flow_data->parent->dest.u.bmi.address,
		    flow_data->prealloc_array[i].buffer,
		    BUFFER_SIZE,
		    BMI_SEND);
	    }
	}
    }
    else if(flow_data->parent->src.endpoint_id == MEM_ENDPOINT &&
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
	gen_mutex_lock(&completion_mutex);
	qlist_add_tail(&(flow_data->list_link), 
	    &completion_queue);
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_signal(&completion_cond);
#endif
	gen_mutex_unlock(&completion_mutex);
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
	    gen_mutex_lock(&completion_mutex);
	    qlist_add_tail(&(flow_data->list_link), 
		&completion_queue);
#ifdef __PVFS2_JOB_THREADED__
	    pthread_cond_signal(&completion_cond);
#endif
	    gen_mutex_unlock(&completion_mutex);
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
    PVFS_size bytes_processed;
    char *src_ptr, *dest_ptr;
    PVFS_size region_size;

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
	gen_mutex_lock(&completion_mutex);
	qlist_add_tail(&(flow_data->list_link), 
	    &completion_queue);
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_signal(&completion_cond);
#endif
	gen_mutex_unlock(&completion_mutex);
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
	    gen_mutex_lock(&completion_mutex);
	    qlist_add_tail(&(flow_data->list_link), 
		&completion_queue);
#ifdef __PVFS2_JOB_THREADED__
	    pthread_cond_signal(&completion_cond);
#endif
	    gen_mutex_unlock(&completion_mutex);
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "gossip.h"
#include "quicklist.h"
#include "flow.h"
#include "flowproto-support.h"
#include "gen-locks.h"
#include "bmi.h"
#include "trove.h"
#include "thread-mgr.h"
    
#define BUFFERS_PER_FLOW 4
#define BUFFER_SIZE (256*1024)
#define MAX_REGIONS 8

struct fp_queue_item
{
    void* buffer;
    PVFS_size size_list[MAX_REGIONS];
    PVFS_offset offset_list[MAX_REGIONS];
    PINT_Request_result result;
    struct qlist_head list_link;
    int last_flag;
    flow_descriptor* parent;
    struct PINT_thread_mgr_bmi_callback bmi_callback;
    struct PINT_thread_mgr_trove_callback trove_callback;
};

struct fp_private_data
{
    flow_descriptor* parent;
    struct fp_queue_item prealloc_array[BUFFERS_PER_FLOW];
    struct qlist_head list_link;

    gen_mutex_t next_seq_mutex;
    int next_seq_num;

    gen_mutex_t src_mutex;
    struct qlist_head src_list;
    int src_done;

    gen_mutex_t dest_mutex;
    struct qlist_head dest_list;
    int dest_done;

    gen_mutex_t empty_mutex;
    struct qlist_head empty_list;
};
#define PRIVATE_FLOW(target_flow)\
    ((struct fp_private_data*)(target_flow->flow_protocol_data))

static int fp_multiqueue_id = -1;
static bmi_context_id global_bmi_context = -1;
static TROVE_context_id global_trove_context = -1;
static gen_mutex_t completion_mutex = GEN_MUTEX_INITIALIZER;
static pthread_cond_t completion_cond = PTHREAD_COND_INITIALIZER;
static QLIST_HEAD(completion_queue); 

static void bmi_recv_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
static void bmi_send_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code);
#if 0
static void trove_read_callback_fn(void *user_ptr,
		           PVFS_error error_code);
#endif
static void trove_write_callback_fn(void *user_ptr,
		           PVFS_error error_code);

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

char fp_multiqueue_name[] = "fp_multiqueue";

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

int fp_multiqueue_initialize(int flowproto_id)
{
    int ret = -1;

/* only for multithreaded servers */
#ifndef __PVFS2_JOB_THREADED__
assert(0);
#endif
#ifndef __PVFS2_TROVE_SUPPORT__
assert(0);
#endif

    ret = PINT_thread_mgr_bmi_start();
    if(ret < 0)
	return(ret);
    PINT_thread_mgr_bmi_getcontext(&global_bmi_context);

    ret = PINT_thread_mgr_trove_start();
    if(ret < 0)
    {
	PINT_thread_mgr_bmi_stop();
	return(ret);
    }
    PINT_thread_mgr_trove_getcontext(&global_trove_context);

    fp_multiqueue_id = flowproto_id;

    return(0);
}

int fp_multiqueue_finalize(void)
{
    PINT_thread_mgr_bmi_stop();
    PINT_thread_mgr_trove_stop();
    return (0);
}

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

int fp_multiqueue_setinfo(flow_descriptor * flow_d,
			       int option,
			       void *parameter)
{
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_post(flow_descriptor * flow_d)
{
    struct fp_private_data* flow_data = NULL;
    int initial_posts = BUFFERS_PER_FLOW;
    int i;

    assert((flow_d->src.endpoint_id == BMI_ENDPOINT && 
	    flow_d->dest.endpoint_id == TROVE_ENDPOINT) ||
	   (flow_d->src.endpoint_id == TROVE_ENDPOINT &&
	    flow_d->dest.endpoint_id == BMI_ENDPOINT));

    flow_data = (struct fp_private_data*)malloc(sizeof(struct
	fp_private_data));
    if(!flow_data)
	return(-PVFS_ENOMEM);
    memset(flow_data, 0, sizeof(struct fp_private_data));
    
    flow_d->flow_protocol_data = flow_data;
    flow_data->parent = flow_d;
    INIT_QLIST_HEAD(&flow_data->src_list);
    INIT_QLIST_HEAD(&flow_data->dest_list);
    INIT_QLIST_HEAD(&flow_data->empty_list);
    gen_mutex_init(&flow_data->src_mutex);
    gen_mutex_init(&flow_data->dest_mutex);
    gen_mutex_init(&flow_data->empty_mutex);

    /* if a file datatype offset was specified, go ahead and skip ahead 
     * before doing anything else
     */
    if(flow_d->file_req_offset)
	PINT_REQUEST_STATE_SET_TARGET(flow_d->file_req_state,
	    flow_d->file_req_offset);

    /* set boundaries on file datatype */
    assert(flow_d->aggregate_size > -1);
    PINT_REQUEST_STATE_SET_FINAL(flow_d->file_req_state,
	flow_d->aggregate_size+flow_d->file_req_offset);

    if(flow_d->src.endpoint_id == BMI_ENDPOINT)
    {
	/* only post one outstanding recv at a time; easier to manage */
	initial_posts = 1;

	/* place remaining buffers on "empty" queue */
	for(i=1; i<BUFFERS_PER_FLOW; i++)
	{
	    qlist_add_tail(&flow_data->prealloc_array[i].list_link,
		&flow_data->empty_list);
	}
    }

    for(i=0; i<initial_posts; i++)
    {
	/* all progress is driven through callbacks; so we may as well use
	 * the same functions to start
	 */
	if(flow_d->src.endpoint_id == BMI_ENDPOINT)
	{
	    trove_write_callback_fn(&(flow_data->prealloc_array[i]), 0);
	}
	else
	{
	    bmi_send_callback_fn(&(flow_data->prealloc_array[i]), 0, 0);
	}
	if(flow_d->state & FLOW_FINISH_MASK)
	{
	    /* immediate completion */
	    free(flow_data);
	    return(1);
	}
    }

    return (0);
}

int fp_multiqueue_find_serviceable(flow_descriptor ** flow_d_array,
				  int *count,
				  int max_idle_time_ms)
{
    int incount = *count;
    struct fp_private_data* tmp_data;

    *count = 0;

    gen_mutex_lock(&completion_mutex);
	/* see if anything has completed */
	if(qlist_empty(&completion_queue))
	{
	    pthread_cond_wait(&completion_cond, &completion_mutex);
	}

	/* run down queue, pulling out anything we can find */
        while(*count < incount && !qlist_empty(&completion_queue))
	{
	    tmp_data = qlist_entry(completion_queue.next,
		struct fp_private_data,
		list_link);
	    qlist_del(&tmp_data->list_link);
	    flow_d_array[*count] = tmp_data->parent;
	    (*count)++;
	}

    gen_mutex_unlock(&completion_mutex);

    return (0);
}

int fp_multiqueue_service(flow_descriptor * flow_d)
{
    /* should never get here; this protocol skips the scheduler for now */
    assert(0);
    return (-PVFS_ENOSYS);
}

static void bmi_recv_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    PVFS_id_gen_t tmp_id;

    /* TODO: error handling */
    assert(error_code == 0);

    /* remove from current queue */
    gen_mutex_lock(&flow_data->src_mutex);
    qlist_del(&q_item->list_link);
    gen_mutex_unlock(&flow_data->src_mutex);

    /* add to dest queue */
    gen_mutex_lock(&flow_data->dest_mutex);
    qlist_add_tail(&q_item->list_link, &flow_data->dest_list);
    gen_mutex_unlock(&flow_data->dest_mutex);

    /* process request */
    q_item->result.bytemax = actual_size;
    q_item->result.segmax = MAX_REGIONS;
    ret = PINT_Process_request(q_item->parent->file_req_state,
	q_item->parent->mem_req_state,
	&q_item->parent->file_data,
	&q_item->result,
	PINT_SERVER);
    /* TODO: error handling */ 
    assert(ret == 0);
    
     /* TODO: implement handling of > MAX_REGIONS discontig parts */
    assert(q_item->result.bytes == actual_size);

    /* does this finish the flow? */
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
	q_item->last_flag = 1;

    ret = trove_bstream_write_list(q_item->parent->dest.u.trove.coll_id,
	q_item->parent->dest.u.trove.handle,
	(char**)&q_item->buffer,
	&q_item->result.bytes,
	1,
	q_item->result.offset_array,
	q_item->result.size_array,
	q_item->result.segs,
	&q_item->result.bytes,
	0,
	NULL,
	q_item,
	global_trove_context,
	&tmp_id);
    /* TODO: error handling */
    assert(ret >= 0);

    if(ret == 1)
    {
	/* immediate completion; trigger callback ourselves */
	trove_write_callback_fn(q_item, 0);
    }
	
    return;
}

#if 0
static void trove_read_callback_fn(void *user_ptr,
		           PVFS_error error_code)
{
    /* TODO: error handling */
    assert(error_code == 0);

    /* TODO: fill this in */
    assert(0);
    return;
}
#endif

static void bmi_send_callback_fn(void *user_ptr,
		         PVFS_size actual_size,
		         PVFS_error error_code)
{
    /* TODO: error handling */
    assert(error_code == 0);

    /* TODO: fill this in */
    assert(0);
    return;
};

static void trove_write_callback_fn(void *user_ptr,
		           PVFS_error error_code)
{
    PVFS_id_gen_t tmp_id;
    PVFS_size tmp_actual_size;
    struct fp_queue_item* q_item = user_ptr;
    int ret;
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);

    /* TODO: error handling */
    assert(error_code == 0);

    /* if this was the last operation, then mark the flow as done */
    if(q_item->last_flag)
    {
	q_item->parent->total_transfered += q_item->result.bytes;
	q_item->parent->state = FLOW_COMPLETE;
	gen_mutex_lock(&completion_mutex);
	qlist_add_tail(&(flow_data->list_link), 
	    &completion_queue);
	pthread_cond_signal(&completion_cond);
	gen_mutex_unlock(&completion_mutex);
	/* TODO: call cleanup function */
	return;
    }

    if(q_item->buffer)
    {
	/* if this q_item has been used before, remove it from its 
	 * current queue */
	gen_mutex_lock(&flow_data->dest_mutex);
	qlist_del(&q_item->list_link);
	gen_mutex_unlock(&flow_data->dest_mutex);
    }
    else
    {
	/* if the q_item has not been used, allocate a buffer */
	q_item->buffer = BMI_memalloc(q_item->parent->src.u.bmi.address,
	    BUFFER_SIZE, BMI_RECV);
	/* TODO: error handling */
	assert(q_item->buffer);
	q_item->result.size_array = q_item->size_list;
	q_item->result.offset_array = q_item->offset_list;
	q_item->bmi_callback.fn = bmi_recv_callback_fn;
	q_item->bmi_callback.data = q_item;
	q_item->trove_callback.fn = trove_write_callback_fn;
	q_item->trove_callback.data = q_item;
    }

    /* acquire two locks (careful about order to prevent deadlock!)
     * if src list is empty, then post new recv; otherwise just queue
     * in empty list
     */
    gen_mutex_lock(&(flow_data->src_mutex));

    if(qlist_empty(&flow_data->src_list))
    {
	/* ready to post new recv! */
	qlist_add_tail(&q_item->list_link, &flow_data->src_list);
	gen_mutex_unlock(&(flow_data->src_mutex));

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
	    bmi_recv_callback_fn(q_item, tmp_actual_size, 0);
	}
    }
    else
    {
	gen_mutex_lock(&(flow_data->empty_mutex));
	qlist_add_tail(&q_item->list_link, 
	    &(flow_data->empty_list));
	gen_mutex_unlock(&(flow_data->empty_mutex));
	gen_mutex_unlock(&(flow_data->src_mutex));
    }

    return;
};


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

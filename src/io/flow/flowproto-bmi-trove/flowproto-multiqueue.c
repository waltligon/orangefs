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
    int refcount;
    int seq_num;
    PVFS_size size_list[MAX_REGIONS];
    PVFS_offset offset_list[MAX_REGIONS];
    int list_count;
    struct qlist_head;
};

struct fp_private_data
{
    flow_descriptor* parent;
    int flow_done;
    struct fp_queue_item prealloc_array[BUFFERS_PER_FLOW];

    gen_mutex_t next_seq_mutex;
    int next_seq_num;

    gen_mutex_t src_mutex;
    struct qlist_head src_list;
    int src_done;

    gen_mutex_t dest_mutex;
    struct qlist_head dest_list;
    int dest_done;
};

static int fp_multiqueue_id = -1;
static bmi_context_id global_bmi_context = -1;
static TROVE_context_id global_trove_context = -1;

static void bmi_callback_fn(void *user_ptr,
		         bmi_size_t actual_size,
		         bmi_error_code_t error_code);
static void trove_callback_fn(void *user_ptr,
		           bmi_error_code_t error_code);

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
    return (-PVFS_ENOSYS);
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

    /* only post one outstanding recv at a time; easier to manage */
    if(flow_d->src.endpoint_id == BMI_ENDPOINT)
	initial_posts = 1;

    for(i=0; i<initial_posts; i++)
    {
	/* all progress is driven through callbacks; so we may as well use
	 * the same functions to start; key off initial condition with 
	 * error code = 1
	 */
	if(flow_d->src.endpoint_id == BMI_ENDPOINT)
	{
	    bmi_callback_fn(&(flow_data->prealloc_array[i]), 
		0, 1);
	}
	else
	{
	    trove_callback_fn(&(flow_data->prealloc_array[i]), 1);
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
    return (-PVFS_ENOSYS);
}

int fp_multiqueue_service(flow_descriptor * flow_d)
{
    return (-PVFS_ENOSYS);
}

static void bmi_callback_fn(void *user_ptr,
		         bmi_size_t actual_size,
		         bmi_error_code_t error_code)
{
    /* TODO: real error handling */
    assert(error_code == 0);
    return;
}

static void trove_callback_fn(void *user_ptr,
		           bmi_error_code_t error_code)
{
    /* TODO: real error handling */
    assert(error_code == 0);
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

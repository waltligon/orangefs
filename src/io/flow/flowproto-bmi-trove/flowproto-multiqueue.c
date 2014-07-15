/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <sys/time.h>
#include <unistd.h>
#endif

#include "gossip.h"
#include "quicklist.h"
#include "src/io/flow/flowproto-support.h"
#include "gen-locks.h"
#include "bmi.h"
#include "trove.h"
#include "thread-mgr.h"
#include "pint-perf-counter.h"
#include "pvfs2-internal.h"

/* the following buffer settings are used by default if none are specified in
 * the flow descriptor
 */
#define BUFFERS_PER_FLOW 8
#define BUFFER_SIZE (256*1024)

#define MAX_REGIONS 64

#define FLOW_CLEANUP_CANCEL_PATH(__flow_data, __cancel_path)               \
do {                                                                       \
    struct flow_descriptor *__flow_d = (__flow_data)->parent;              \
    gossip_err("function(FLOW_CLEANUP_CANCEL_PATH):flow(%p):cancel(%d)\n"  \
               ,__flow_d,__cancel_path);                                   \
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, "flowproto completing %p\n",     \
                 __flow_d);                                                \
    cleanup_buffers(__flow_data);                                          \
    __flow_d = (__flow_data)->parent;                                      \
    free(__flow_data);                                                     \
    __flow_d->release(__flow_d);                                           \
    __flow_d->callback(__flow_d, __cancel_path);                           \
} while(0)

#define FLOW_CLEANUP(___flow_data) FLOW_CLEANUP_CANCEL_PATH(___flow_data, 0)

struct result_chain_entry
{
    PVFS_id_gen_t posted_id;
    char *buffer_offset;
    PINT_Request_result result;
    PVFS_size size_list[MAX_REGIONS];
    PVFS_offset offset_list[MAX_REGIONS];
    struct result_chain_entry *next;
    struct fp_queue_item *q_item;
    struct PINT_thread_mgr_trove_callback trove_callback;
    PVFS_size out_size; /* holds the number of bytes actually written for this entry.*/
};

/* fp_queue_item describes an individual buffer being used within the flow */
struct fp_queue_item
{
    PVFS_id_gen_t posted_id;
    struct fp_queue_item *replicas;
    int replica_count;
    struct fp_queue_item *replica_parent;
    int last;
    int seq;
    void *buffer;
    int buffer_in_use; /*used by replication*/
    replication_endpoint_status_t *res; /* used by replication to store status info */
    PVFS_size buffer_used;
    PVFS_size out_size;
    struct result_chain_entry result_chain;
    int result_chain_count;
    struct qlist_head list_link;
    flow_descriptor *parent;
    struct PINT_thread_mgr_bmi_callback bmi_callback;
};

/* fp_private_data is information specific to this flow protocol, stored
 * in flow descriptor but hidden from caller
 */
struct fp_private_data
{
    flow_descriptor *parent;
    struct fp_queue_item* prealloc_array;
    struct qlist_head list_link;
    PVFS_size total_bytes_processed;
    int next_seq;
    int next_seq_to_send;
    int dest_pending;
    int dest_last_posted;
    int initial_posts;
    void *tmp_buffer_list[MAX_REGIONS];
    void *intermediate;
    int cleanup_pending_count;
    int req_proc_done;

    struct qlist_head src_list;
    struct qlist_head dest_list;
    struct qlist_head empty_list;

    /* Additions for forwarding flows */
    int sends_pending;
    int recvs_pending;
    int writes_pending;
    int primary_recvs_throttled;

    PVFS_size total_bytes_req;
    PVFS_size total_bytes_recvd;
    PVFS_size total_bytes_forwarded;
    PVFS_size total_bytes_written;
};
#define PRIVATE_FLOW(target_flow)\
    ((struct fp_private_data*)(target_flow->flow_protocol_data))

static bmi_context_id global_bmi_context = -1;
static void cleanup_buffers(
    struct fp_private_data *flow_data);
static void handle_io_error(
    PVFS_error error_code,
    struct fp_queue_item *q_item,
    struct fp_private_data *flow_data);
static int cancel_pending_bmi(
    struct qlist_head *list);
static int cancel_pending_trove(
    struct qlist_head *list, 
    TROVE_coll_id coll_id);

typedef void (*bmi_recv_callback)(void *, PVFS_size, PVFS_error);
typedef void (*trove_write_callback)(void *, PVFS_error);

static void flow_bmi_recv(struct fp_queue_item* q_item,
                          bmi_recv_callback recv_callback);
void forwarding_bmi_recv_callback_fn(void *user_ptr,
					    PVFS_size actual_size,
					    PVFS_error error_code);
void server_bmi_recv_callback_fn(void *user_ptr,
                                 PVFS_size actual_size,
                                 PVFS_error error_code);
static int flow_process_request( struct fp_queue_item* q_item );
void flow_trove_write(struct fp_queue_item* q_item,
                      PVFS_size actual_size,
                      trove_write_callback write_callback);
void server_trove_write_callback_fn(void *user_ptr,
                                    PVFS_error error_code);
TROVE_context_id global_trove_context = -1;
void forwarding_trove_write_callback_fn(void *user_ptr,
                                        PVFS_error error_code);
int forwarding_is_flow_complete(struct fp_private_data* flow_data);
void forwarding_bmi_send_callback_fn(void *user_ptr,
			             PVFS_size actual_size,
				     PVFS_error error_code);
static void handle_forwarding_io_error(PVFS_error error_code,
                                      struct fp_queue_item* q_item,
                                      struct fp_private_data* flow_data);
static inline void server_write_flow_post_init(flow_descriptor *flow_d,
                                               struct fp_private_data *flow_data);
static inline void forwarding_flow_post_init(flow_descriptor* flow_d,
					     struct fp_private_data* flow_data);


#ifdef __PVFS2_TROVE_SUPPORT__
typedef struct
{
    TROVE_coll_id coll_id;
    int sync_mode;
    struct qlist_head link;
} id_sync_mode_t;

static QLIST_HEAD(s_id_sync_mode_list);
static gen_mutex_t id_sync_mode_mutex = GEN_MUTEX_INITIALIZER;




static void bmi_recv_callback_fn(void *user_ptr,
                                 PVFS_size actual_size,
                                 PVFS_error error_code);

static int bmi_send_callback_fn(void *user_ptr,
                                PVFS_size actual_size,
                                PVFS_error error_code,
                                int initial_call_flag);
static void trove_read_callback_fn(void *user_ptr,
                                   PVFS_error error_code);
static void trove_write_callback_fn(void *user_ptr,
                                    PVFS_error error_code);

static int get_data_sync_mode(TROVE_coll_id coll_id);

/* wrappers that let us acquire locks or use return values in different
 * ways, depending on if the function is triggered from an external thread
 * or in a direct invocation
 */
static inline void bmi_send_callback_wrapper(void *user_ptr,
                                             PVFS_size actual_size,
                                             PVFS_error error_code)
{
    struct fp_private_data *flow_data = 
        PRIVATE_FLOW(((struct fp_queue_item*)user_ptr)->parent);
    gen_mutex_lock(&flow_data->parent->flow_mutex);

    bmi_send_callback_fn(user_ptr, actual_size, error_code, 0);
    if(flow_data->parent->state == FLOW_COMPLETE)
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
        FLOW_CLEANUP(flow_data);
    }
    else
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
    }
}

static inline void bmi_recv_callback_wrapper(void *user_ptr,
                                             PVFS_size actual_size,
                                             PVFS_error error_code)
{
    struct fp_private_data *flow_data = 
        PRIVATE_FLOW(((struct fp_queue_item*)user_ptr)->parent);
    gen_mutex_lock(&flow_data->parent->flow_mutex);
    bmi_recv_callback_fn(user_ptr, actual_size, error_code);
    if(flow_data->parent->state == FLOW_COMPLETE)
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
        FLOW_CLEANUP(flow_data);
    }
    else
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
    }
}

static inline void trove_read_callback_wrapper(void *user_ptr,
                                               PVFS_error error_code)
{
    struct fp_private_data *flow_data = 
        PRIVATE_FLOW(((struct
        result_chain_entry*)user_ptr)->q_item->parent);
    gen_mutex_lock(&flow_data->parent->flow_mutex);
    trove_read_callback_fn(user_ptr, error_code);
    if(flow_data->parent->state == FLOW_COMPLETE)
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
        FLOW_CLEANUP(flow_data);
    }
    else
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
    }
}

static inline void trove_write_callback_wrapper(void *user_ptr,
                                                PVFS_error error_code)
{
    struct fp_private_data *flow_data = 
        PRIVATE_FLOW(((struct
                       result_chain_entry*)user_ptr)->q_item->parent);
    gen_mutex_lock(&flow_data->parent->flow_mutex);
    trove_write_callback_fn(user_ptr, error_code);
    if(flow_data->parent->state == FLOW_COMPLETE)
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
        FLOW_CLEANUP(flow_data);
    }
    else
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
    }
}

#endif

static void mem_to_bmi_callback_fn(void *user_ptr,
                                   PVFS_size actual_size,
                                   PVFS_error error_code);
static void bmi_to_mem_callback_fn(void *user_ptr,
                                   PVFS_size actual_size,
                                   PVFS_error error_code);

/* wrappers that let us acquire locks or use return values in different
 * ways, depending on if the function is triggered from an external thread
 * or in a direct invocation
 */
static void mem_to_bmi_callback_wrapper(void *user_ptr,
                                        PVFS_size actual_size,
                                        PVFS_error error_code)
{
    struct fp_private_data *flow_data = 
        PRIVATE_FLOW(((struct fp_queue_item*)user_ptr)->parent);
    gen_mutex_lock(&flow_data->parent->flow_mutex);
    mem_to_bmi_callback_fn(user_ptr, actual_size, error_code);
    if(flow_data->parent->state == FLOW_COMPLETE)
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
        FLOW_CLEANUP(flow_data);
    }
    else
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
    }
}

static void bmi_to_mem_callback_wrapper(void *user_ptr,
                                        PVFS_size actual_size,
                                        PVFS_error error_code)
{
    struct fp_private_data *flow_data = 
        PRIVATE_FLOW(((struct fp_queue_item*)user_ptr)->parent);

    assert(flow_data);
    assert(flow_data->parent);

    gen_mutex_lock(&flow_data->parent->flow_mutex);
    bmi_to_mem_callback_fn(user_ptr, actual_size, error_code);
    if(flow_data->parent->state == FLOW_COMPLETE)
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
        FLOW_CLEANUP(flow_data);
    }
    else
    {
        gen_mutex_unlock(&flow_data->parent->flow_mutex);
    }
}

/* interface prototypes */
static int fp_multiqueue_initialize(int flowproto_id);

static int fp_multiqueue_finalize(void);

static int fp_multiqueue_getinfo(flow_descriptor  *flow_d,
                                 int option,
                                 void *parameter);

static int fp_multiqueue_setinfo(flow_descriptor *flow_d,
                                 int option,
                                 void *parameter);

static int fp_multiqueue_post(flow_descriptor *flow_d);

static int fp_multiqueue_cancel(flow_descriptor *flow_d);

static char fp_multiqueue_name[] = "flowproto_multiqueue";

struct flowproto_ops fp_multiqueue_ops = {
    fp_multiqueue_name,
    fp_multiqueue_initialize,
    fp_multiqueue_finalize,
    fp_multiqueue_getinfo,
    fp_multiqueue_setinfo,
    fp_multiqueue_post,
    fp_multiqueue_cancel
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
    {
        id_sync_mode_t *cur_info = NULL;
        struct qlist_head *tmp_link = NULL, *scratch_link = NULL;

        PINT_thread_mgr_trove_stop();

        gen_mutex_lock(&id_sync_mode_mutex);
        qlist_for_each_safe(tmp_link, scratch_link, &s_id_sync_mode_list)
        {
            cur_info = qlist_entry(tmp_link, id_sync_mode_t, link);
            qlist_del(&cur_info->link);
            free(cur_info);
            cur_info = NULL;
        }
        gen_mutex_unlock(&id_sync_mode_mutex);
    }
#endif
    return (0);
}

/* fp_multiqueue_getinfo()
 *
 * retrieves runtime parameters from flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_getinfo(flow_descriptor *flow_d,
                          int option,
                          void *parameter)
{
    int *type;

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
    }
}

/* fp_multiqueue_setinfo()
 *
 * sets runtime parameters in flow protocol
 *
 * returns 0 on success, -PVFS_error on failure
 */
int fp_multiqueue_setinfo(flow_descriptor *flow_d,
                          int option,
                          void *parameter)
{
    int ret = -PVFS_ENOSYS;

    switch(option)
    {
#ifdef __PVFS2_TROVE_SUPPORT__
        case FLOWPROTO_DATA_SYNC_MODE:
        {
            TROVE_coll_id coll_id = 0, sync_mode = 0;
            id_sync_mode_t *new_id_mode = NULL;
            struct qlist_head* iterator = NULL;
            struct qlist_head* scratch = NULL;
            id_sync_mode_t *tmp_mode = NULL;

            assert(parameter && strlen(parameter));
            sscanf((const char *)parameter, "%d,%d",
                   &coll_id, &sync_mode);

            ret = -ENOMEM;

            new_id_mode = (id_sync_mode_t *)malloc(
                sizeof(id_sync_mode_t));
            if (new_id_mode)
            {
                gen_mutex_lock(&id_sync_mode_mutex);
                /* remove any old instances of this fs id */
                qlist_for_each_safe(iterator, scratch, &s_id_sync_mode_list)
                {
                    tmp_mode = qlist_entry(iterator, id_sync_mode_t, link);
                    assert(tmp_mode);
                    if(tmp_mode->coll_id == coll_id)
                    {
                        qlist_del(&tmp_mode->link);
                    }
                }

                /* add new instance */
                new_id_mode->coll_id = coll_id;
                new_id_mode->sync_mode = sync_mode;

                qlist_add_tail(&new_id_mode->link, &s_id_sync_mode_list);
                gen_mutex_unlock(&id_sync_mode_mutex);

                gossip_debug(
                    GOSSIP_FLOW_PROTO_DEBUG, "fp_multiqueue_setinfo: "
                    "data sync mode on coll_id %d set to %d\n",
                    coll_id, sync_mode);
                ret = 0;
            }
        }
        break;
#endif
        default:
            break;
    }
    return ret;
}

/* fp_multiqueue_cancel()
 *
 * cancels a previously posted flow
 *
 * returns 0 on success, 1 on immediate completion, -PVFS_error on failure
 */
int fp_multiqueue_cancel(flow_descriptor  *flow_d)
{
    struct fp_private_data *flow_data = PRIVATE_FLOW(flow_d);

    gossip_err("%s: flow proto cancel called on %p\n", __func__, flow_d);
    gen_mutex_lock(&flow_data->parent->flow_mutex);
    /*
      if the flow is already marked as complete, then there is nothing
      to do
    */
    if(flow_d->state != FLOW_COMPLETE)
    {
        gossip_debug(GOSSIP_CANCEL_DEBUG,
                     "%s: called on active flow, %lld bytes transferred.\n",
                     __func__, lld(flow_d->total_transferred));
        assert(flow_d->state == FLOW_TRANSMITTING);
        /* NOTE: set flow error class bit so that system interface understands
         * that this may be a retry-able error
         */
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(-(PVFS_ECANCEL|PVFS_ERROR_FLOW), NULL, flow_data);
        if(flow_data->parent->state == FLOW_COMPLETE)
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
            FLOW_CLEANUP_CANCEL_PATH(flow_data, 1);
            return(0);
        }
    }
    else
    {
        gossip_debug(GOSSIP_CANCEL_DEBUG,
                     "%s: called on already completed flow; doing nothing.\n",
                     __func__);
    }
    gen_mutex_unlock(&flow_data->parent->flow_mutex);

    return(0);
}

/* fp_multiqueue_post()
 *
 * posts a flow descriptor to begin work
 *
 * returns 0 on success, 1 on immediate completion, -PVFS_error on failure
 */
int fp_multiqueue_post(flow_descriptor  *flow_d)
{
    struct fp_private_data *flow_data = NULL;
    int i,j,ret;
    uint32_t *always_queue=NULL;

    gossip_err("Executing %s for flow(%p):handle(%llu)...\n",__func__,flow_d,llu(flow_d->dest.u.trove.handle));

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, "flowproto posting %p\n",
                 flow_d);

    assert((flow_d->src.endpoint_id == BMI_ENDPOINT && 
            flow_d->dest.endpoint_id == TROVE_ENDPOINT) ||
           (flow_d->src.endpoint_id == BMI_ENDPOINT &&
            flow_d->dest.endpoint_id == REPLICATION_ENDPOINT) ||
           (flow_d->src.endpoint_id == TROVE_ENDPOINT &&
            flow_d->dest.endpoint_id == BMI_ENDPOINT) ||
           (flow_d->src.endpoint_id == MEM_ENDPOINT &&
            flow_d->dest.endpoint_id == BMI_ENDPOINT) ||
           (flow_d->src.endpoint_id == BMI_ENDPOINT &&
            flow_d->dest.endpoint_id == MEM_ENDPOINT));

    flow_data = (struct fp_private_data*)malloc(sizeof(struct fp_private_data));
    if(!flow_data)
    {
        gossip_err("Error allocating memory for flow_data.\n");
        ret = -PVFS_ENOMEM;
        goto error_exit;
    }
    memset(flow_data, 0, sizeof(struct fp_private_data));
    
    flow_d->flow_protocol_data = flow_data;
    flow_d->state = FLOW_TRANSMITTING;
    flow_data->parent = flow_d;
    INIT_QLIST_HEAD(&flow_data->src_list);
    INIT_QLIST_HEAD(&flow_data->dest_list);
    INIT_QLIST_HEAD(&flow_data->empty_list);

    /* if a file datatype offset was specified, go ahead and skip ahead 
     * before doing anything else
     */
    if(flow_d->file_req_offset)
    {
        PINT_REQUEST_STATE_SET_TARGET(flow_d->file_req_state,
            flow_d->file_req_offset);
    }

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

    if(flow_d->buffer_size < 1)
    {
        flow_d->buffer_size = BUFFER_SIZE;
    }
    if(flow_d->buffers_per_flow < 1)
    {
        flow_d->buffers_per_flow = BUFFERS_PER_FLOW;
    }
        
    flow_data->prealloc_array = (struct fp_queue_item*)
                malloc(flow_d->buffers_per_flow*sizeof(struct fp_queue_item));
    if(!flow_data->prealloc_array)
    {
        gossip_err("Flow(%p):Error allocating memory for prealloc_array.\n",flow_d);
        ret = -PVFS_ENOMEM;
        goto error_exit;
    }
    memset(flow_data->prealloc_array,
           0,
           flow_d->buffers_per_flow*sizeof(struct fp_queue_item));
    for(i = 0; i < flow_d->buffers_per_flow; i++)
    {
        /* We allocate additional q_items per flow-buffer for the sending of data to the replicas. 
         * NOTE:  Using q_item (struct fp_queue_item) structures are required for the recovery processes.
         */
        if (flow_d->next_dest_count > 0)
        {
           flow_data->prealloc_array[i].replicas = calloc(flow_d->repl_d_repl_count
                                                         ,sizeof(*flow_data->prealloc_array[i].replicas));
           if (!flow_data->prealloc_array[i].replicas)
           {
               gossip_err("Flow(%p):Error allocating memory for q_item's replica information.\n",flow_d);
               ret = -PVFS_ENOMEM;
               goto error_exit;
           }
           flow_data->prealloc_array[i].replica_count = flow_d->repl_d_repl_count;
           for (j=0; j<flow_d->repl_d_repl_count; j++)
           {
              INIT_QLIST_HEAD(&flow_data->prealloc_array[i].replicas[j].list_link);
              flow_data->prealloc_array[i].replicas[j].parent = flow_d;
              flow_data->prealloc_array[i].replicas[j].replica_parent = &(flow_data->prealloc_array[i]);

              /* flow status for each replica is kept in the replica q-item, subordinate to the primary q-item */
              flow_data->prealloc_array[i].replicas[j].res = &(flow_d->repl_d[j].endpt_status);
              gossip_err("%s:flow_data->prealloc_array[%d].replicas[%d].res:(%p) flow_d->repl_d[%d].endpt_status:(%p) "
                         "flow_d->repl_d[%d].endpt_status.writes_completed_bytes(%p)\n"

                        ,__func__,i,j,flow_data->prealloc_array[i].replicas[j].res,j,&(flow_d->repl_d[j].endpt_status)
                        ,j,&(flow_d->repl_d[j].endpt_status.writes_completed_bytes));
           }

           /* The local flow status is kept in the primary q_item */
           flow_data->prealloc_array[i].res = &(flow_d->repl_d[flow_d->repl_d_local_flow_index].endpt_status);
           gossip_err("%s:flow_data->prealloc_array[%d].res:(%p) flow_d->repl_d[%d].endpt_status:(%p) "
                      "flow_d->repl_d[%d].endpt_status.writes_completed_bytes(%p)\n"
                     ,__func__,i,flow_data->prealloc_array[i].res,flow_d->repl_d_local_flow_index
                     ,&(flow_d->repl_d[flow_d->repl_d_local_flow_index].endpt_status)
                     ,flow_d->repl_d_local_flow_index,&(flow_d->repl_d[flow_d->repl_d_local_flow_index].endpt_status.writes_completed_bytes));
           gossip_err("%s:flow_data->prealloc_array[%d].res->state:(%s) error_code:(%d)\n"
                     ,__func__
                     ,i
                     ,get_replication_endpoint_state_as_string(flow_data->prealloc_array[i].res->state)
                     ,flow_data->prealloc_array[i].res->error_code);
        }/*end if*/
        flow_data->prealloc_array[i].parent = flow_d;
        flow_data->prealloc_array[i].bmi_callback.data = 
                        &(flow_data->prealloc_array[i]);
    }


    /* remaining setup depends on the endpoints we intend to use */
    if(flow_d->src.endpoint_id == BMI_ENDPOINT &&
        flow_d->dest.endpoint_id == MEM_ENDPOINT)
    {
        flow_data->prealloc_array[0].buffer = flow_d->dest.u.mem.buffer;
        flow_data->prealloc_array[0].bmi_callback.fn =
                        bmi_to_mem_callback_wrapper;
        /* put all of the buffers on empty list, we don't really do any
         * queueing for this type of flow
         */
        for(i=0; i<flow_d->buffers_per_flow; i++)
        {
            qlist_add_tail(&flow_data->prealloc_array[i].list_link,
                           &flow_data->empty_list);
        }
        gen_mutex_lock(&flow_data->parent->flow_mutex);
        bmi_to_mem_callback_fn(&(flow_data->prealloc_array[0]), 0, 0);
        if(flow_data->parent->state == FLOW_COMPLETE)
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
            FLOW_CLEANUP(flow_data);
        }
        else
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
        }
    }
    else if(flow_d->src.endpoint_id == MEM_ENDPOINT &&
            flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
        flow_data->prealloc_array[0].buffer = flow_d->src.u.mem.buffer;
        flow_data->prealloc_array[0].bmi_callback.fn =
                     mem_to_bmi_callback_wrapper;
        /* put all of the buffers on empty list, we don't really do any
         * queueing for this type of flow
         */
        for(i = 0; i < flow_d->buffers_per_flow; i++)
        {
            qlist_add_tail(&flow_data->prealloc_array[i].list_link,
                           &flow_data->empty_list);
        }
        gen_mutex_lock(&flow_data->parent->flow_mutex);
        mem_to_bmi_callback_fn(&(flow_data->prealloc_array[0]), 0, 0);
        if(flow_data->parent->state == FLOW_COMPLETE)
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
            FLOW_CLEANUP(flow_data);
        }
        else
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
        }
    }
#ifdef __PVFS2_TROVE_SUPPORT__
    else if(flow_d->src.endpoint_id  == BMI_ENDPOINT   &&
            flow_d->dest.endpoint_id == REPLICATION_ENDPOINT) 
    {
         /* Add hint to indicate that we want BMI_post_recv and BMI_post_send to NEVER complete
          * immediately but ALWAYS be queued.  In this way, we can post many sends and recvs
          * without executing the callback functions immediately.  The hope is that we will have more
          * data available to process and thus keep the server as busy as possible.
          */
         always_queue=malloc(sizeof(*always_queue));
         if (!always_queue)
         {
             gossip_err("%s:Error allocating memory for always_queue.\n",__func__);
             ret = -PVFS_ENOMEM;
             goto error_exit;
         }
         *always_queue = 1;
         ret=PVFS_hint_add(&(flow_d->hints),
                          PVFS_HINT_BMI_QUEUE_NAME,
                           sizeof(*always_queue),
                           always_queue);
         if (ret)
         {
            gossip_err("%s:Error adding hint(%d).\n",__func__,ret);
            ret = -PVFS_ENOMEM;
            goto error_exit;
         }

         /* Initiate BMI-RCV->multiple BMI-SENDs->trove-write loop.
          */
         gossip_err("flow(%p):Calling forwarding_flow_post_init...\n",flow_d);
         forwarding_flow_post_init(flow_d, flow_data);
    }
    else if(flow_d->src.endpoint_id == TROVE_ENDPOINT &&
            flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
        flow_data->initial_posts = flow_d->buffers_per_flow;
        gen_mutex_lock(&flow_data->parent->flow_mutex);
        for(i = 0; i < flow_d->buffers_per_flow; i++)
        {
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue forcing bmi_send_callback_fn.\n");

            bmi_send_callback_fn(&(flow_data->prealloc_array[i]), 0, 0, 1);
            if(flow_data->dest_last_posted)
            {
                break;
            }
        }
        if(flow_data->parent->state == FLOW_COMPLETE)
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
            FLOW_CLEANUP(flow_data);
        }
        else
        {
            gen_mutex_unlock(&flow_data->parent->flow_mutex);
        }
    }
    else if(flow_d->src.endpoint_id == BMI_ENDPOINT &&
            flow_d->dest.endpoint_id == TROVE_ENDPOINT)
    {
         /* Add hint to indicate that we want BMI_post_recv and BMI_post_send to NEVER complete
          * immediately but ALWAYS be queued.  In this way, we can post many sends and recvs
          * without executing the callback returns immediately.  The hope is that we will have more
          * data available to process and thus keep the server as busy as possible.
          */
         always_queue=malloc(sizeof(*always_queue));
         if (!always_queue)
         {
             gossip_lerr("%s:Error allocating memory for always_queue.\n",__func__);
             ret = -PVFS_ENOMEM;
             goto error_exit;
         }
         *always_queue = 1;
         ret=PVFS_hint_add(&(flow_d->hints),
                          PVFS_HINT_BMI_QUEUE_NAME,
                           sizeof(*always_queue),
                           always_queue);
         if (ret)
         {
            gossip_lerr("%s:Error adding hint(%d).\n",__func__,ret);
            ret = -PVFS_ENOMEM;
            goto error_exit;
         }
        /* Initiate BMI-rcv->trove-write loop for this server */
        gossip_err("flow(%p):Calling server_write_flow_post_init()...\n",flow_d);
        server_write_flow_post_init(flow_d,flow_data);
    }
#endif
    else
    {
        return(-ENOSYS);
    }

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, "flowproto posted %p\n",
                 flow_d);
    return (0);

error_exit:
    if (flow_data && flow_data->prealloc_array)
    {
       for (i=0; i<flow_d->buffers_per_flow; i++)
       {
           if (flow_data->prealloc_array[i].replicas)
           {
              free (flow_data->prealloc_array[i].replicas);
           }
       }
       free(flow_data->prealloc_array);
    }
    if (flow_data)
    {
       free(flow_data);
    }
    flow_d->flow_protocol_data = NULL;
    if (always_queue)
    {
        free(always_queue);
    }
    return (ret);
}/*end fp_multiqueue_post*/

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
    struct fp_queue_item *q_item = user_ptr;
    int ret;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    PVFS_size tmp_actual_size;
    struct result_chain_entry *result_tmp;
    struct result_chain_entry *old_result_tmp;
    PVFS_size bytes_processed = 0;
    void *tmp_buffer;
    void *tmp_user_ptr;
    int sync_mode = 0;

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
        "flowproto-multiqueue bmi_recv_callback_fn, error code: %d, flow: %p.\n",
        error_code, flow_data->parent);

    q_item->posted_id = 0;

    if(error_code != 0 || flow_data->parent->error_code != 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    /* remove from current queue */
    qlist_del(&q_item->list_link);
    /* add to dest queue */
    qlist_add_tail(&q_item->list_link, &flow_data->dest_list);
    result_tmp = &q_item->result_chain;
    do{
        assert(result_tmp->result.bytes);
        result_tmp->q_item = q_item;
        result_tmp->trove_callback.data = result_tmp;
        result_tmp->trove_callback.fn = trove_write_callback_wrapper;
        /* XXX: can someone confirm this avoids a segfault in the immediate
         * completion case? */
        tmp_user_ptr = result_tmp;
        assert(result_tmp->result.bytes);

        if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
        {
            /* This is the last write operation for this flow.  Set sync
             * flag if needed
             */ 
            sync_mode = get_data_sync_mode(
                q_item->parent->dest.u.trove.coll_id);
        }

        ret = trove_bstream_write_list(
            q_item->parent->dest.u.trove.coll_id,
            q_item->parent->dest.u.trove.handle,
            (char**)&result_tmp->buffer_offset,
            &result_tmp->result.bytes,
            1,
            result_tmp->result.offset_array,
            result_tmp->result.size_array,
            result_tmp->result.segs,
            &q_item->out_size,
            sync_mode,
            NULL,
            &result_tmp->trove_callback,
            global_trove_context,
            &result_tmp->posted_id,
            q_item->parent->hints);

        result_tmp = result_tmp->next;

        if(ret < 0)
        {
            gossip_err("%s: I/O error occurred\n", __func__);
            handle_io_error(ret, q_item, flow_data);
            return;
        }

        if(ret == 1)
        {
            /* immediate completion; trigger callback ourselves */
            trove_write_callback_fn(tmp_user_ptr, 0);
        }
    } while(result_tmp);

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
            q_item->buffer = BMI_memalloc(
                            q_item->parent->src.u.bmi.address,
                            q_item->parent->buffer_size, BMI_RECV);
            /* TODO: error handling */
            assert(q_item->buffer);
            q_item->bmi_callback.fn = bmi_recv_callback_wrapper;
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
                memset(result_tmp, 0, sizeof(struct result_chain_entry));
                old_result_tmp->next = result_tmp;
            }
            /* process request */
            result_tmp->result.offset_array = result_tmp->offset_list;
            result_tmp->result.size_array = result_tmp->size_list;
            result_tmp->result.bytemax = flow_data->parent->buffer_size - 
                                         bytes_processed;
            result_tmp->result.bytes = 0;
            result_tmp->result.segmax = MAX_REGIONS;
            result_tmp->result.segs = 0;
            result_tmp->buffer_offset = tmp_buffer;
            ret = PINT_process_request(q_item->parent->file_req_state,
                                       q_item->parent->mem_req_state,
                                       &q_item->parent->file_data,
                                       &result_tmp->result,
                                       PINT_SERVER);
            /* TODO: error handling */ 
            assert(ret >= 0);

            if(result_tmp->result.bytes == 0)
            {
                if(result_tmp != &q_item->result_chain)
                {
                    free(result_tmp);
                    old_result_tmp->next = NULL;
                }
                q_item->result_chain_count--;
            }
            else
            {
                old_result_tmp = result_tmp;
                result_tmp = result_tmp->next;
                tmp_buffer = (void*)((char*)tmp_buffer + old_result_tmp->result.bytes);
                bytes_processed += old_result_tmp->result.bytes;
            }
        } while(bytes_processed < flow_data->parent->buffer_size && 
                !PINT_REQUEST_DONE(q_item->parent->file_req_state));

        assert(bytes_processed <= flow_data->parent->buffer_size);
        if(bytes_processed == 0)
        {        
            qlist_del(&q_item->list_link);
            qlist_add_tail(&q_item->list_link, &flow_data->empty_list);
            return;
        }

        flow_data->total_bytes_processed += bytes_processed;

        gossip_debug(GOSSIP_DIRECTIO_DEBUG,
                     "offset %llu, buffer ptr: %p\n",
                     llu(q_item->result_chain.result.offset_array[0]),
                     q_item->buffer);

        /* TODO: what if we recv less than expected? */
        ret = BMI_post_recv(&q_item->posted_id,
                            q_item->parent->src.u.bmi.address,
                            ((char *)q_item->buffer),
                            flow_data->parent->buffer_size,
                            &tmp_actual_size,
                            BMI_PRE_ALLOC,
                            q_item->parent->tag,
                            &q_item->bmi_callback,
                            global_bmi_context,
                            (bmi_hint)q_item->parent->hints);

        if(ret < 0)
        {
            gossip_err("%s: I/O error occurred\n", __func__);
            handle_io_error(ret, q_item, flow_data);
            return;
        }

        if(ret == 1)
        {
            /* immediate completion; trigger callback ourselves */
            bmi_recv_callback_fn(q_item, tmp_actual_size, 0);
        }
    }

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
    int ret;
    struct result_chain_entry *result_tmp = user_ptr;
    struct fp_queue_item *q_item = result_tmp->q_item;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    struct result_chain_entry *old_result_tmp;
    int done = 0;
    struct qlist_head *tmp_link;

    q_item = result_tmp->q_item;

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
        "flowproto-multiqueue trove_read_callback_fn, error_code: %d, flow: %p.\n",
        error_code, flow_data->parent);

    result_tmp->posted_id = 0;

    if(error_code != 0 || flow_data->parent->error_code != 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    /* don't do anything until the last read completes */
    if(q_item->result_chain_count > 1)
    {
        q_item->result_chain_count--;
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
        {
            free(old_result_tmp);
        }
    } while(result_tmp);

    q_item->result_chain.next = NULL;
    q_item->result_chain_count = 0;

    /* while we hold dest lock, look for next seq no. to send */
    do{
        qlist_for_each(tmp_link, &flow_data->dest_list)
        {
            q_item = qlist_entry(tmp_link, struct fp_queue_item,
                                 list_link);
            if(q_item->seq == flow_data->next_seq_to_send)
            {
                break;
            }
        }

        if(q_item->seq == flow_data->next_seq_to_send)
        {
            flow_data->dest_pending++;
            assert(q_item->buffer_used);
            ret = BMI_post_send(&q_item->posted_id,
                                q_item->parent->dest.u.bmi.address,
                                q_item->buffer,
                                q_item->buffer_used,
                                BMI_PRE_ALLOC,
                                q_item->parent->tag,
                                &q_item->bmi_callback,
                                global_bmi_context,
                                (bmi_hint)q_item->parent->hints);
            flow_data->next_seq_to_send++;
            if(q_item->last)
            {
                flow_data->initial_posts = 0;
                flow_data->dest_last_posted = 1;
            }
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "%s: (post send time) ini posts: %d, pending: %d, last: %d\n",
                __func__,
                flow_data->initial_posts, flow_data->dest_pending,
                flow_data->dest_last_posted);
        }
        else
        {
            ret = 0;
            done = 1;
        }        

        if(ret < 0)
        {
            gossip_err("%s: I/O error occurred\n", __func__);
            handle_io_error(ret, q_item, flow_data);
            return;
        }

        if(ret == 1)
        {
            /* immediate completion; trigger callback ourselves */
            ret = bmi_send_callback_fn(q_item, q_item->buffer_used, 0, 0);
            /* if that callback finished the flow, then return now */
            if(ret == 1)
            {
                return;
            }
        }
    }
    while(!done);

    return;
}

/* bmi_send_callback_fn()
 *
 * function to be called upon completion of a BMI send operation
 *
 * returns 1 if flow completes, 0 otherwise
 */
static int bmi_send_callback_fn(void *user_ptr,
                                PVFS_size actual_size,
                                PVFS_error error_code,
                                int initial_call_flag)
{
    struct fp_queue_item *q_item = user_ptr;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    int ret;
    struct result_chain_entry *result_tmp;
    struct result_chain_entry *old_result_tmp;
    void *tmp_buffer;
    PVFS_size bytes_processed = 0;
    void *tmp_user_ptr = NULL;

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
        "flowproto-multiqueue bmi_send_callback_fn, error_code: %d, "
        "initial_call_flag: %d, flow: %p.\n", error_code, initial_call_flag,
        flow_data->parent);

    if(flow_data->parent->error_code != 0 && initial_call_flag)
    {
        /* cleanup path already triggered, don't do anything more */
        return(1);
    }

    q_item->posted_id = 0;

    if(error_code != 0 || flow_data->parent->error_code != 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(error_code, q_item, flow_data);
        if(flow_data->parent->state == FLOW_COMPLETE)
        {
            return(1);
        }
        else
        {
            return(0);
        }
    }

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_READ, 
                    actual_size, 
                    PINT_PERF_ADD);

    PINT_perf_count(PINT_server_pc,
                    PINT_PERF_FLOW_READ, 
                    actual_size, 
                    PINT_PERF_ADD);

    flow_data->parent->total_transferred += actual_size;

    if(initial_call_flag)
    {
        flow_data->initial_posts--;
    }
    else
    {
        flow_data->dest_pending--;
    }

#if 0
    gossip_err(
        "initial_posts: %d, dest_pending: %d, dest_last_posted: %d\n", 
        flow_data->initial_posts, flow_data->dest_pending,
        flow_data->dest_last_posted);
#endif

    /* if this was the last operation, then mark the flow as done */
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
        "(send callback time) ini posts: %d, pending: %d, last: %d, "
        "src_list emtpy: %s\n",
        flow_data->initial_posts, flow_data->dest_pending,
        flow_data->dest_last_posted,
        qlist_empty(&flow_data->src_list) ? "yes" : "no");
    if(flow_data->initial_posts == 0 &&
        flow_data->dest_pending == 0 && 
        flow_data->dest_last_posted &&
        qlist_empty(&flow_data->src_list))
    {
        /* we are in trouble if more than one callback function thinks that
         * it can trigger completion
         */
        assert(q_item->parent->state != FLOW_COMPLETE);
        q_item->parent->state = FLOW_COMPLETE;
        return(1);
    }
 
    /* if we have finished request processing then there is no need to try
     * to continue
     */
    if(flow_data->req_proc_done)
    {
        if(q_item->buffer)
        {
            qlist_del(&q_item->list_link);
        }
        return(0);
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
        q_item->buffer = BMI_memalloc(
                        q_item->parent->dest.u.bmi.address,
                        q_item->parent->buffer_size, BMI_SEND);

        /* TODO: error handling */
        assert(q_item->buffer);
        q_item->bmi_callback.fn = bmi_send_callback_wrapper;
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
        result_tmp->result.offset_array = result_tmp->offset_list;
        result_tmp->result.size_array = result_tmp->size_list;
        result_tmp->result.bytemax = q_item->parent->buffer_size 
                                     - bytes_processed;
        result_tmp->result.bytes = 0;
        result_tmp->result.segmax = MAX_REGIONS;
        result_tmp->result.segs = 0;
        result_tmp->buffer_offset = tmp_buffer;
        ret = PINT_process_request(q_item->parent->file_req_state,
                                   q_item->parent->mem_req_state,
                                   &q_item->parent->file_data,
                                   &result_tmp->result,
                                   PINT_SERVER);
        /* TODO: error handling */ 
        assert(ret >= 0);

        if(result_tmp->result.bytes == 0)
        {
            if(result_tmp != &q_item->result_chain)
            {
                free(result_tmp);
                old_result_tmp->next = NULL;
            }
            q_item->result_chain_count--;
        }
        else
        {
            old_result_tmp = result_tmp;
            result_tmp = result_tmp->next;
            tmp_buffer = (void*)
                            ((char*)tmp_buffer + old_result_tmp->result.bytes);
            bytes_processed += old_result_tmp->result.bytes;
            q_item->buffer_used += old_result_tmp->result.bytes;
        }

    } while(bytes_processed < flow_data->parent->buffer_size && 
            !PINT_REQUEST_DONE(q_item->parent->file_req_state));

    assert(bytes_processed <= flow_data->parent->buffer_size);

    /* important to update the sequence /after/ request processed */
    q_item->seq = flow_data->next_seq;
    flow_data->next_seq++;

    flow_data->total_bytes_processed += bytes_processed;
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
    {
        q_item->last = 1;
        assert(flow_data->req_proc_done == 0);
        flow_data->req_proc_done = 1;
        /* special case, we never have a "last" operation when there
         * is no work to do, trigger manually
         */
        if(flow_data->total_bytes_processed == 0)
        {
            flow_data->initial_posts = 0;
            flow_data->dest_last_posted = 1;
        }
    }

    if(bytes_processed == 0)
    {        
        if(q_item->buffer)
        {
            qlist_del(&q_item->list_link);
        }

        if(flow_data->dest_pending == 0 && qlist_empty(&flow_data->src_list))
        {
            /* we know 2 things: 
             *
             * 1) all the previously posted trove read and
             *    bmi send operations have completed.
             * 2) there aren't any more bytes to process and therefore
             *    no more trove reads to post.
             *
             * based on that we can complete the flow.
             */
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, 
                         "zero bytes processed.  no dests pending. "
                         "setting flow to done\n");
            assert(q_item->parent->state != FLOW_COMPLETE);
            q_item->parent->state = FLOW_COMPLETE;
            return 1;
        }
        else
        {
            /* no more bytes to process but qitems are still being
             * worked on, so we can only set that the last qitem
             * has been posted
             */
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                         "zero bytes processed, dests pending: %d, "
                         "src_list empty: %s\n",
                         flow_data->dest_pending,
                         qlist_empty(&flow_data->src_list) ? "yes" : "no");

            /* this allows a check in the fp_multiqueue_post function
             * to prevent further trying to start other qitems from being
             * posted
             */
            flow_data->initial_posts = 0;
            flow_data->dest_last_posted = 1;
            return 0;
        }
    }

    assert(q_item->buffer_used);

    result_tmp = &q_item->result_chain;
    do{
        assert(q_item->buffer_used);
        assert(result_tmp->result.bytes);
        result_tmp->q_item = q_item;
        result_tmp->trove_callback.data = result_tmp;
        result_tmp->trove_callback.fn = trove_read_callback_wrapper;
        /* XXX: can someone confirm this avoids a segfault in the immediate
         * completion case? */
        tmp_user_ptr = result_tmp;
        assert(result_tmp->result.bytes);

        ret = trove_bstream_read_list(q_item->parent->src.u.trove.coll_id,
                                      q_item->parent->src.u.trove.handle,
                                      (char**)&result_tmp->buffer_offset,
                                      &result_tmp->result.bytes,
                                      1,
                                      result_tmp->result.offset_array,
                                      result_tmp->result.size_array,
                                      result_tmp->result.segs,
                                      &q_item->out_size,
                                      0, /* get_data_sync_mode(
                                       q_item->parent->dest.u.trove.coll_id), */
                                      NULL,
                                      &result_tmp->trove_callback,
                                      global_trove_context,
                                      &result_tmp->posted_id,
                                      flow_data->parent->hints);

        result_tmp = result_tmp->next;

        if(ret < 0)
        {
            gossip_err("%s: I/O error occurred\n", __func__);
            handle_io_error(ret, q_item, flow_data);
            if(flow_data->parent->state == FLOW_COMPLETE)
            {
                return(1);
            }
            else
            {
                return(0);
            }
        }

        if(ret == 1)
        {
            /* immediate completion; trigger callback ourselves */
            trove_read_callback_fn(tmp_user_ptr, 0);
        }
    } while(result_tmp);

    return(0);
};

/* trove_write_callback_fn()
 *
 * function to be called upon completion of a trove write operation
 *
 * no return value
 */
static void trove_write_callback_fn(void *user_ptr, PVFS_error error_code)
{
    PVFS_size tmp_actual_size;
    int ret;
    struct result_chain_entry *result_tmp = user_ptr;
    struct fp_queue_item *q_item = result_tmp->q_item;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    struct result_chain_entry *old_result_tmp;
    void *tmp_buffer;
    PVFS_size bytes_processed = 0;

    gossip_debug(
        GOSSIP_FLOW_PROTO_DEBUG,
        "flowproto-multiqueue trove_write_callback_fn, error_code: %d, flow: %p.\n",
        error_code, flow_data->parent);

    result_tmp->posted_id = 0;

    if(error_code != 0 || flow_data->parent->error_code != 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    /* don't do anything until the last write completes */
    if(q_item->result_chain_count > 1)
    {
        q_item->result_chain_count--;
        return;
    }

    result_tmp = &q_item->result_chain;
    do{
        q_item->parent->total_transferred += result_tmp->result.bytes;

        PINT_perf_count( PINT_server_pc,
                         PINT_PERF_WRITE, 
                         result_tmp->result.bytes,
                         PINT_PERF_ADD);

        PINT_perf_count( PINT_server_pc,
                         PINT_PERF_FLOW_WRITE, 
                         result_tmp->result.bytes,
                         PINT_PERF_ADD);

        old_result_tmp = result_tmp;
        result_tmp = result_tmp->next;
        if(old_result_tmp != &q_item->result_chain)
        {
            free(old_result_tmp);
        }
    } while(result_tmp);
    q_item->result_chain.next = NULL;
    q_item->result_chain_count = 0;

    /* if this was the last operation, then mark the flow as done */
    if(flow_data->parent->total_transferred ==
        flow_data->total_bytes_processed &&
        PINT_REQUEST_DONE(flow_data->parent->file_req_state))
    {
        /* we are in trouble if more than one callback function thinks that
         * it can trigger completion
         */
        assert(q_item->parent->state != FLOW_COMPLETE);
        q_item->parent->state = FLOW_COMPLETE;
        return;
    }

    /* if there are no more receives to post, just return */
    if(PINT_REQUEST_DONE(flow_data->parent->file_req_state))
    {
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
                                      q_item->parent->buffer_size,
                                      BMI_RECV);
        /* TODO: error handling */
        assert(q_item->buffer);
        q_item->bmi_callback.fn = bmi_recv_callback_wrapper;
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
            result_tmp->result.offset_array = result_tmp->offset_list;
            result_tmp->result.size_array = result_tmp->size_list;
            result_tmp->result.bytemax = flow_data->parent->buffer_size 
                                         - bytes_processed;
            result_tmp->result.bytes = 0;
            result_tmp->result.segmax = MAX_REGIONS;
            result_tmp->result.segs = 0;
            result_tmp->buffer_offset = tmp_buffer;
            assert(!PINT_REQUEST_DONE(q_item->parent->file_req_state));
            ret = PINT_process_request(q_item->parent->file_req_state,
                                       q_item->parent->mem_req_state,
                                       &q_item->parent->file_data,
                                       &result_tmp->result,
                                       PINT_SERVER);
            /* TODO: error handling */ 
            assert(ret >= 0);

            if(result_tmp->result.bytes == 0)
            {
                if(result_tmp != &q_item->result_chain)
                {
                    free(result_tmp);
                    old_result_tmp->next = NULL;
                }
                q_item->result_chain_count--;
            }
            else
            {
                old_result_tmp = result_tmp;
                result_tmp = result_tmp->next;
                tmp_buffer = (void*)
                        ((char*)tmp_buffer + old_result_tmp->result.bytes);
                bytes_processed += old_result_tmp->result.bytes;
            }
        }while(bytes_processed < flow_data->parent->buffer_size && 
               !PINT_REQUEST_DONE(q_item->parent->file_req_state));

        assert(bytes_processed <= flow_data->parent->buffer_size);
 
        flow_data->total_bytes_processed += bytes_processed;

        if(bytes_processed == 0)
        {        
            if(flow_data->parent->total_transferred ==
                flow_data->total_bytes_processed &&
                PINT_REQUEST_DONE(flow_data->parent->file_req_state))
            {
                assert(q_item->parent->state != FLOW_COMPLETE);
                q_item->parent->state = FLOW_COMPLETE;
            }
            return;
        }

        gossip_debug(GOSSIP_DIRECTIO_DEBUG,
                     "offset %llu, buffer ptr: %p\n",
                     llu(q_item->result_chain.result.offset_array[0]),
                     q_item->buffer);

        /* TODO: what if we recv less than expected? */
        ret = BMI_post_recv(&q_item->posted_id,
                            q_item->parent->src.u.bmi.address,
                            ((char *)q_item->buffer),
                            flow_data->parent->buffer_size,
                            &tmp_actual_size,
                            BMI_PRE_ALLOC,
                            q_item->parent->tag,
                            &q_item->bmi_callback,
                            global_bmi_context,
                            (bmi_hint)q_item->parent->hints);

        if(ret < 0)
        {
            gossip_err("%s: I/O error occurred\n", __func__);
            handle_io_error(ret, q_item, flow_data);
            return;
        }

        if(ret == 1)
        {
            /* immediate completion; trigger callback ourselves */
            bmi_recv_callback_fn(q_item, tmp_actual_size, 0);
        }
    }
    else
    {
        qlist_add_tail(&q_item->list_link, &(flow_data->empty_list));
    }

    return;
};
#endif

/* cleanup_buffers()
 *
 * releases any resources consumed during flow processing
 *
 * no return value
 */
static void cleanup_buffers(struct fp_private_data *flow_data)
{
    int i;
    struct result_chain_entry *result_tmp;
    struct result_chain_entry *old_result_tmp;
    flow_descriptor *flow_d = flow_data->parent;

    if(  flow_d->src.endpoint_id  == BMI_ENDPOINT &&
        (flow_d->dest.endpoint_id == TROVE_ENDPOINT ||
         flow_d->dest.endpoint_id == REPLICATION_ENDPOINT) )
    {
        for(i=0; i<flow_d->buffers_per_flow; i++)
        {
            if(flow_data->prealloc_array[i].buffer)
            {
		    BMI_memfree(flow_d->src.u.bmi.address,
				    flow_data->prealloc_array[i].buffer,
				    flow_d->buffer_size,
				    BMI_RECV);
            }
            result_tmp = &(flow_data->prealloc_array[i].result_chain);
            do{
                old_result_tmp = result_tmp;
                result_tmp = result_tmp->next;
                if(old_result_tmp !=
                    &(flow_data->prealloc_array[i].result_chain))
                {
                    free(old_result_tmp);
                }
            } while(result_tmp);
            flow_data->prealloc_array[i].result_chain.next = NULL;
        }

        /* NOTE: hints are freed by the state machine processor in server_state_machine_complete().
         *       The new hint, PINT_HINT_BMI_QUEUE, introduced for replication will be deallocated then.
         */
    }
    else if(flow_data->parent->src.endpoint_id  == TROVE_ENDPOINT &&
            flow_data->parent->dest.endpoint_id == BMI_ENDPOINT)
    {
        for(i = 0; i < flow_data->parent->buffers_per_flow; i++)
        {
            if(flow_data->prealloc_array[i].buffer)
            {
                BMI_memfree(flow_data->parent->dest.u.bmi.address,
                            flow_data->prealloc_array[i].buffer,
                            flow_data->parent->buffer_size,
                            BMI_SEND);
            }
            result_tmp = &(flow_data->prealloc_array[i].result_chain);
            do{
                old_result_tmp = result_tmp;
                result_tmp = result_tmp->next;
                if(old_result_tmp !=
                    &(flow_data->prealloc_array[i].result_chain))
                {
                    free(old_result_tmp);
                }
            } while(result_tmp);
            flow_data->prealloc_array[i].result_chain.next = NULL;
        }
    }
    else if(flow_data->parent->src.endpoint_id  == MEM_ENDPOINT &&
            flow_data->parent->dest.endpoint_id == BMI_ENDPOINT)
    {
        if(flow_data->intermediate)
        {
            BMI_memfree(flow_data->parent->dest.u.bmi.address,
                        flow_data->intermediate,
                        flow_data->parent->buffer_size,
                        BMI_SEND);
        }
    }
    else if(flow_data->parent->src.endpoint_id  == BMI_ENDPOINT &&
            flow_data->parent->dest.endpoint_id == MEM_ENDPOINT)
    {
        if(flow_data->intermediate)
        {
            BMI_memfree(flow_data->parent->src.u.bmi.address,
                        flow_data->intermediate,
                        flow_data->parent->buffer_size,
                        BMI_RECV);
        }
    }
 
    /* we have an array of posted_id's when we are replicating and have only one
     * when we are not.
     */
    for (i=0; flow_data->prealloc_array && i<flow_d->buffers_per_flow; i++)
    {
        if (flow_data->prealloc_array[i].replicas)
        {
           free(flow_data->prealloc_array[i].replicas);
        }
    }

    if (flow_data->prealloc_array)
    {
       free(flow_data->prealloc_array);
    }
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
    struct fp_queue_item *q_item = user_ptr;
    int ret;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    int i;
    PVFS_size bytes_processed = 0;
    char *src_ptr, *dest_ptr;
    enum bmi_buffer_type buffer_type = BMI_EXT_ALLOC;

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
        "flowproto-multiqueue mem_to_bmi_callback_fn, error_code: %d, flow: %p.\n",
        error_code, flow_data->parent);

    q_item->posted_id = 0;

    if(error_code != 0 || flow_data->parent->error_code != 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    /* remove from current queue, empty or earlier send; add bmi active dest */
    qlist_del(&q_item->list_link);
    qlist_add_tail(&q_item->list_link, &flow_data->dest_list);

    flow_data->parent->total_transferred += actual_size;

    /* are we done? */
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
    {
        /* we are in trouble if more than one callback function thinks that
         * it can trigger completion
         */
        assert(q_item->parent->state != FLOW_COMPLETE);
        q_item->parent->state = FLOW_COMPLETE;
        return;
    }

    /* process request */
    q_item->result_chain.result.offset_array = 
                    q_item->result_chain.offset_list;
    q_item->result_chain.result.size_array = 
                    q_item->result_chain.size_list;
    q_item->result_chain.result.bytemax = flow_data->parent->buffer_size;
    q_item->result_chain.result.bytes = 0;
    q_item->result_chain.result.segmax = MAX_REGIONS;
    q_item->result_chain.result.segs = 0;
    q_item->result_chain.buffer_offset = NULL;

    ret = PINT_process_request(q_item->parent->file_req_state,
                               q_item->parent->mem_req_state,
                               &q_item->parent->file_data,
                               &q_item->result_chain.result,
                               PINT_CLIENT);

    /* TODO: error handling */ 
    assert(ret >= 0);

    /* was MAX_REGIONS enough to satisfy this step? */
    if(!PINT_REQUEST_DONE(flow_data->parent->file_req_state) &&
        q_item->result_chain.result.bytes < flow_data->parent->buffer_size)
    {
        /* create an intermediate buffer */
        if(!flow_data->intermediate)
        {
            flow_data->intermediate = BMI_memalloc(
                                      flow_data->parent->dest.u.bmi.address,
                                      flow_data->parent->buffer_size,
                                      BMI_SEND);
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
            q_item->result_chain.result.bytemax =
                            (flow_data->parent->buffer_size - bytes_processed);
            q_item->result_chain.result.bytes = 0;
            q_item->result_chain.result.segmax = MAX_REGIONS;
            q_item->result_chain.result.segs = 0;
            q_item->result_chain.buffer_offset = NULL;

            /* process ahead */
            ret = PINT_process_request(q_item->parent->file_req_state,
                                       q_item->parent->mem_req_state,
                                       &q_item->parent->file_data,
                                       &q_item->result_chain.result,
                                       PINT_CLIENT);
            /* TODO: error handling */
            assert(ret >= 0);

            /* copy what we have so far into intermediate buffer */
            for(i = 0; i < q_item->result_chain.result.segs; i++)
            {
                src_ptr = ((char*)q_item->parent->src.u.mem.buffer + 
                            q_item->result_chain.offset_list[i]);
                dest_ptr = ((char*)flow_data->intermediate + bytes_processed);
                memcpy(dest_ptr, src_ptr, q_item->result_chain.size_list[i]);
                bytes_processed += q_item->result_chain.size_list[i];
            }

        } while(bytes_processed < flow_data->parent->buffer_size &&
                !PINT_REQUEST_DONE(q_item->parent->file_req_state));

        assert (bytes_processed <= flow_data->parent->buffer_size);

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
            /* we are in trouble if more than one callback function thinks that
             * it can trigger completion
             */
            assert(q_item->parent->state != FLOW_COMPLETE);
            q_item->parent->state = FLOW_COMPLETE;
            return;
        }

        /* convert offsets to memory addresses */
        for(i = 0; i < q_item->result_chain.result.segs; i++)
        {
            flow_data->tmp_buffer_list[i] = 
                    (char*)(q_item->result_chain.result.offset_array[i] +
                    (char *)q_item->buffer);
        }
    }

    assert(q_item->result_chain.result.bytes);

    ret = BMI_post_send_list(&q_item->posted_id,
                             q_item->parent->dest.u.bmi.address,
                             (const void**)flow_data->tmp_buffer_list,
                             q_item->result_chain.result.size_array,
                             q_item->result_chain.result.segs,
                             q_item->result_chain.result.bytes,
                             buffer_type,
                             q_item->parent->tag,
                             &q_item->bmi_callback,
                             global_bmi_context,
                             (bmi_hint)q_item->parent->hints);

    if(ret < 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(ret, q_item, flow_data);
        return;
    }

    if(ret == 1)
    {
        mem_to_bmi_callback_fn(q_item, q_item->result_chain.result.bytes, 0);
    }
}


/* bmi_to_mem_callback()
 *
 * function to be called upon completion of bmi operations in bmi to
 * memory transfers
 * 
 * no return value
 */
static void bmi_to_mem_callback_fn(void *user_ptr,
                                   PVFS_size actual_size,
                                   PVFS_error error_code)
{
    struct fp_queue_item *q_item = user_ptr;
    int ret;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    int i;
    PVFS_size tmp_actual_size;
    PVFS_size *size_array;
    int segs;
    PVFS_size total_size;
    enum bmi_buffer_type buffer_type = BMI_EXT_ALLOC;
    PVFS_size bytes_processed = 0;
    char *src_ptr, *dest_ptr;
    PVFS_size region_size;

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
        "flowproto-multiqueue bmi_to_mem_callback_fn, error_code: %d, flow: %p.\n",
        error_code, flow_data->parent);

    q_item->posted_id = 0;

    if(error_code != 0 || flow_data->parent->error_code != 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    /* remove from current queue, empty or earlier send; add bmi active src */
    qlist_del(&q_item->list_link);
    qlist_add_tail(&q_item->list_link, &flow_data->src_list);

    flow_data->parent->total_transferred += actual_size;

    /* if this is the result of a receive into an intermediate buffer,
     * then we must copy out */
    if(flow_data->tmp_buffer_list[0] == flow_data->intermediate &&
        flow_data->intermediate != NULL)
    {
        /* copy out what we have so far */
        for(i=0; i<q_item->result_chain.result.segs; i++)
        {
            region_size = q_item->result_chain.size_list[i];
            src_ptr = (char*)((char *)flow_data->intermediate + 
                       bytes_processed);
            dest_ptr = (char*)(q_item->result_chain.offset_list[i]
                       + (char *)q_item->parent->dest.u.mem.buffer);
            memcpy(dest_ptr, src_ptr, region_size);
            bytes_processed += region_size;
        }

        do
        {
            q_item->result_chain.result.bytemax =
                        (q_item->parent->buffer_size - bytes_processed);
            q_item->result_chain.result.bytes = 0;
            q_item->result_chain.result.segmax = MAX_REGIONS;
            q_item->result_chain.result.segs = 0;
            q_item->result_chain.buffer_offset = NULL;

            /* process ahead */
            ret = PINT_process_request(q_item->parent->file_req_state,
                                       q_item->parent->mem_req_state,
                                       &q_item->parent->file_data,
                                       &q_item->result_chain.result,
                                       PINT_CLIENT);

            /* TODO: error handling */
            assert(ret >= 0);

            /* copy out what we have so far */
            for(i = 0; i < q_item->result_chain.result.segs; i++)
            {
                region_size = q_item->result_chain.size_list[i];
                src_ptr = (char*)((char *)flow_data->intermediate + 
                          bytes_processed);
                dest_ptr = (char*)(q_item->result_chain.offset_list[i]
                           + (char *)q_item->parent->dest.u.mem.buffer);
                memcpy(dest_ptr, src_ptr, region_size);
                bytes_processed += region_size;
            }
        } while(bytes_processed < flow_data->parent->buffer_size &&
                !PINT_REQUEST_DONE(q_item->parent->file_req_state));

        assert(bytes_processed <= flow_data->parent->buffer_size);
    }

    /* are we done? */
    if(PINT_REQUEST_DONE(q_item->parent->file_req_state))
    {
        /* we are in trouble if more than one callback function thinks
         * that it can trigger completion
         */
        assert(q_item->parent->state != FLOW_COMPLETE);
        q_item->parent->state = FLOW_COMPLETE;
        return;
    }

    /* process request */
    q_item->result_chain.result.offset_array = 
                    q_item->result_chain.offset_list;
    q_item->result_chain.result.size_array = 
                    q_item->result_chain.size_list;
    q_item->result_chain.result.bytemax = flow_data->parent->buffer_size;
    q_item->result_chain.result.bytes = 0;
    q_item->result_chain.result.segmax = MAX_REGIONS;
    q_item->result_chain.result.segs = 0;
    q_item->result_chain.buffer_offset = NULL;

    ret = PINT_process_request(q_item->parent->file_req_state,
                               q_item->parent->mem_req_state,
                               &q_item->parent->file_data,
                               &q_item->result_chain.result,
                               PINT_CLIENT);

    /* TODO: error handling */ 
    assert(ret >= 0);

    /* was MAX_REGIONS enough to satisfy this step? */
    if(!PINT_REQUEST_DONE(flow_data->parent->file_req_state) &&
        q_item->result_chain.result.bytes < flow_data->parent->buffer_size)
    {
        /* create an intermediate buffer */
        if(!flow_data->intermediate)
        {
            flow_data->intermediate = BMI_memalloc(
                            flow_data->parent->src.u.bmi.address,
                            flow_data->parent->buffer_size,
                            BMI_RECV);
            /* TODO: error handling */
            assert(flow_data->intermediate);
        }
        /* setup for BMI operation */
        flow_data->tmp_buffer_list[0] = flow_data->intermediate;
        buffer_type = BMI_PRE_ALLOC;
        q_item->buffer_used = flow_data->parent->buffer_size;
        total_size = flow_data->parent->buffer_size;
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
        for(i = 0; i < q_item->result_chain.result.segs; i++)
        {
            flow_data->tmp_buffer_list[i] = 
                       (void*)(q_item->result_chain.result.offset_array[i] +
                        (char *)q_item->buffer);
        }

        /* go ahead and return if there is nothing to do */
        if(q_item->result_chain.result.bytes == 0)
        {        
            /* we are in trouble if more than one callback function
             * thinks that it can trigger completion
             */
            assert(q_item->parent->state != FLOW_COMPLETE);
            q_item->parent->state = FLOW_COMPLETE;
            return;
        }
    }

    assert(total_size);
    ret = BMI_post_recv_list(&q_item->posted_id,
                             q_item->parent->src.u.bmi.address,
                             flow_data->tmp_buffer_list,
                             size_array,
                             segs,
                             total_size,
                             &tmp_actual_size,
                             buffer_type,
                             q_item->parent->tag,
                             &q_item->bmi_callback,
                             global_bmi_context,
                             (bmi_hint)q_item->parent->hints);

    if(ret < 0)
    {
        gossip_err("%s: I/O error occurred\n", __func__);
        handle_io_error(ret, q_item, flow_data);
        return;
    }

    if(ret == 1)
    {
        bmi_to_mem_callback_fn(q_item, tmp_actual_size, 0);
    }

    return;
}


/* handle_io_error()
 * 
 * called any time a BMI or Trove error code is detected, responsible
 * for safely cleaning up the associated flow
 *
 * NOTE: this function should always be called while holding the flow mutex!
 *
 * no return value
 */
static void handle_io_error(PVFS_error error_code,
                            struct fp_queue_item *q_item,
                            struct fp_private_data *flow_data)
{
    int ret;
    char buf[64] = {0};

    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, 
        "flowproto-multiqueue handle_io_error() called for flow %p.\n",
        flow_data->parent);

    /* is this the first error registered for this particular flow? */
    if(flow_data->parent->error_code == 0)
    {
        enum flow_endpoint_type src, dest;

        PVFS_strerror_r(error_code, buf, 64);
        gossip_err("%s: flow proto error cleanup started on %p: %s\n",
                   __func__, flow_data->parent, buf);

        flow_data->parent->error_code = error_code;
        if(q_item)
        {
            qlist_del(&q_item->list_link);
        }
        flow_data->cleanup_pending_count = 0;

        src = flow_data->parent->src.endpoint_id;
        dest = flow_data->parent->dest.endpoint_id;

        /* cleanup depending on what endpoints are in use */
        if (src == BMI_ENDPOINT && dest == MEM_ENDPOINT)
        {
            ret = cancel_pending_bmi(&flow_data->src_list);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d bmi-mem BMI ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
        }
        else if (src == MEM_ENDPOINT && dest == BMI_ENDPOINT)
        {
            ret = cancel_pending_bmi(&flow_data->dest_list);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d mem-bmi BMI ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
        }
        else if (src == TROVE_ENDPOINT && dest == BMI_ENDPOINT)
        {
            ret = cancel_pending_trove(&flow_data->src_list,
                                       flow_data->parent->src.u.trove.coll_id);
            flow_data->cleanup_pending_count += ret;
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d trove-bmi Trove ops.\n", ret);
            ret = cancel_pending_bmi(&flow_data->dest_list);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d trove-bmi BMI ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
        }
        else if (src == BMI_ENDPOINT && (dest == TROVE_ENDPOINT || dest == REPLICATION_ENDPOINT))
        {
            ret = cancel_pending_bmi(&flow_data->src_list);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d bmi-trove BMI ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
            ret = cancel_pending_trove(&flow_data->dest_list,
                                       flow_data->parent->dest.u.trove.coll_id);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d bmi-trove Trove ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
        }
        else
        {
            /* impossible condition */
            assert(0);
        }
        gossip_err("%s: flow proto %p canceled %d operations, will clean up.\n",
                   __func__, flow_data->parent,
                   flow_data->cleanup_pending_count);
    }
    else
    {
        /* one of the previous cancels came through */
        flow_data->cleanup_pending_count--;
    }
    
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, 
        "flowproto-multiqueue handle_io_error() pending count: %d\n",
        flow_data->cleanup_pending_count);

    if(flow_data->cleanup_pending_count == 0)
    {
        PVFS_strerror_r(flow_data->parent->error_code, buf, 64);
        gossip_err("%s: flow proto %p error cleanup finished: %s\n",
            __func__, flow_data->parent, buf);

        /* we are finished, make sure error is marked and state is set */
        assert(flow_data->parent->error_code);
        /* we are in trouble if more than one callback function thinks that
         * it can trigger completion
         */
        assert(flow_data->parent->state != FLOW_COMPLETE);
        flow_data->parent->state = FLOW_COMPLETE;
    }
}


/* cancel_pending_bmi()
 *
 * cancels any pending bmi operations on the given queue list
 *
 * returns the number of operations that were canceled 
 */
static int cancel_pending_bmi(struct qlist_head *list)
{
    struct qlist_head *tmp_link;
    struct fp_queue_item *q_item = NULL;
    int ret = 0;
    int count = 0;

    /* run down the chain of pending operations */
    qlist_for_each(tmp_link, list)
    {
        q_item = qlist_entry(tmp_link, struct fp_queue_item, list_link);
        /* skip anything that is in the queue but not actually posted */
        if(q_item->posted_id)
        {
           gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
               "flowprotocol cleanup: unposting BMI operation.\n");
           ret = PINT_thread_mgr_bmi_cancel(q_item->posted_id,
                                           &q_item->bmi_callback);
           if(ret < 0)
           {
               gossip_err("WARNING: BMI thread mgr cancel failed, "
                          "proceeding anyway.\n");
           }else
           {
              count++;
           }
        }/*end if*/
    }/*end for*/
    return (count);
}

/* cancel_pending_trove()
 *
 * cancels any pending trove operations on the given queue list
 *
 * returns the number of operations that were canceled 
 */
static int cancel_pending_trove(struct qlist_head *list, TROVE_coll_id coll_id)
{
    struct qlist_head *tmp_link;
    struct fp_queue_item *q_item = NULL;
    int count = 0;
    struct result_chain_entry *result_tmp;
    struct result_chain_entry *old_result_tmp;
    int ret;

    /* run down the chain of pending operations */
    qlist_for_each(tmp_link, list)
    {
        q_item = qlist_entry(tmp_link, struct fp_queue_item, list_link);

        result_tmp = &q_item->result_chain;
        do{
            old_result_tmp = result_tmp;
            result_tmp = result_tmp->next;

            if(old_result_tmp->posted_id)
            {
                ret = PINT_thread_mgr_trove_cancel(
                                       old_result_tmp->posted_id,
                                       coll_id,
                                       &old_result_tmp->trove_callback);
                if (ret == -TROVE_EOPNOTSUPP)
                {
                   ret=0;
                }else if (ret < 0)
                {
                    gossip_err("WARNING: Trove thread mgr cancel "
                               "failed, proceeding anyway.\n");
                    ret=0;
                    continue;
                }
                count++;
            }
        }while(result_tmp);
    }
    return (count);
}

#ifdef __PVFS2_TROVE_SUPPORT__
static int get_data_sync_mode(TROVE_coll_id coll_id)
{
    int mode = TROVE_SYNC;
    id_sync_mode_t *cur_info = NULL;
    struct qlist_head *tmp_link = NULL;

    gen_mutex_lock(&id_sync_mode_mutex);
    qlist_for_each(tmp_link, &s_id_sync_mode_list)
    {
        cur_info = qlist_entry(tmp_link, id_sync_mode_t, link);
        if (cur_info->coll_id == coll_id)
        {
            mode = cur_info->sync_mode;
            break;
        }
    }
    gen_mutex_unlock(&id_sync_mode_mutex);
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, "get_data_sync_mode "
                 "returning %d\n", mode);
    return mode;
}
#endif

static inline void server_write_flow_post_init(flow_descriptor *flow_d,
                                               struct fp_private_data *flow_data)
{
    int i;

    /* Generic flow initialization */
    flow_data->parent->total_transferred = 0;
    
    /* Iniitialize the pending counts */
    flow_data->recvs_pending = 0;
    flow_data->writes_pending = 0;
    flow_data->primary_recvs_throttled = 0;

    /* Initiailize progress counts */
    flow_data->total_bytes_req = flow_d->aggregate_size;
    flow_data->total_bytes_forwarded = 0;
    flow_data->total_bytes_recvd = 0;
    flow_data->total_bytes_written = 0;
    

    /* Initialize buffers */
    for (i = 0; i < flow_d->buffers_per_flow; i++)
    {
        /* Trove stuff I don't understand */
        flow_data->prealloc_array[i].result_chain.q_item = 
            &flow_data->prealloc_array[i];

        INIT_QLIST_HEAD(&flow_data->prealloc_array[i].list_link);
    }

    /* Post the initial receives */
    for (i = 0; i < flow_d->buffers_per_flow; i++)
    {
        /* If there is data to be received, perform the initial recv
           otherwise mark the flow complete */
        gen_mutex_lock(&flow_d->flow_mutex);
        if (!PINT_REQUEST_DONE(flow_data->parent->file_req_state))
        {

            /* Post the recv operation */
            gen_mutex_unlock(&flow_d->flow_mutex);
            flow_bmi_recv(&(flow_data->prealloc_array[i]),
                          server_bmi_recv_callback_fn);
        }
        else
        {
            gen_mutex_unlock(&flow_d->flow_mutex);
            gossip_err("Server flow posted all buffers on initial post.\n");
            break;
        }
    }

    return;
}/*end server_write_flow_post_init*/




static void flow_bmi_recv(struct fp_queue_item* q_item,
                          bmi_recv_callback recv_callback)
{
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    flow_descriptor *flow_d = flow_data->parent;

    PVFS_size tmp_actual_size;
    PVFS_size bytes_processed = 0;
    int ret;

    gossip_err("Executing %s...flow(%p):q_item(%p):buffer_in_use(%d)\n",__func__,flow_d,q_item,q_item->buffer_in_use);

    /* Create rest of qitem so that we can recv into it */
    if (0 == q_item->buffer)
    {
	q_item->buffer = BMI_memalloc(flow_d->src.u.bmi.address,
				      flow_d->buffer_size,
				      BMI_RECV);
    }
    assert(q_item->buffer);
    memset(q_item->buffer,0,flow_d->buffer_size);

    q_item->bmi_callback.fn = recv_callback;
    q_item->posted_id = 0;
    

    /* Process the request to determine mapping.  bytes_processed represents the total amount of bytes
     * that will be processed with this flow buffer. Could be equal to the flow buffer size or could be
     * something smaller than that.
     */
    bytes_processed = flow_process_request(q_item);

    gossip_err("%s:flow(%p):q_item(%p):q_item->result_chain_count(%d).\n",__func__
                                                                          ,flow_d
                                                                          ,q_item
                                                                          ,(int)q_item->result_chain_count);

    if (0 != bytes_processed)
    {
    
        gen_mutex_lock(&flow_d->flow_mutex);
           flow_data->recvs_pending++;
           q_item->buffer_in_use++;
           qlist_del(&q_item->list_link);
           qlist_add_tail(&q_item->list_link, &flow_data->src_list);
         gen_mutex_unlock(&flow_d->flow_mutex);
        /* TODO: what if we recv less than expected? */
        ret = BMI_post_recv(&q_item->posted_id,
                            flow_d->src.u.bmi.address,
                            q_item->buffer,
                            flow_d->buffer_size,
                            &tmp_actual_size,
                            BMI_PRE_ALLOC,
                            flow_d->tag,
                            &q_item->bmi_callback,
                            global_bmi_context,
                            flow_d->hints);

        gossip_err("flow(%p):q_item(%p):%s:return value from BMI_post_recv:(%d).\n"
                   ,flow_d,q_item,__func__,ret);
        if (ret == 0)
        {
           /* RECV posting has been queued. */
           return;
        }
        else if (ret == 1)
        {
           /* RECV posting has completed immediately */
           gossip_err("flow(%p):q_item(%p):%s:RECV posting has completed immediately.\n"
                      ,flow_d,q_item,__func__);
           recv_callback(q_item, tmp_actual_size, 0);
        }
        else if (ret < 0)
        {
            /* we want to stop the entire flow.
             * set flow_d->error_code = ret;
             * set flow_d->replica_state = FAILED_BMI_RECV_POST;
             */
            gen_mutex_lock(&flow_d->flow_mutex);
              q_item->res->state = FAILED_BMI_POST_RECV;
              q_item->res->error_code = ret;
              qlist_del(&q_item->list_link);
              gossip_err("flow(%p):q_item(%p):%s:ERROR: BMI_post_recv returned: %d!\n", flow_d,q_item,__func__,ret);
              handle_forwarding_io_error(ret, q_item, flow_data);
            gen_mutex_unlock(&flow_d->flow_mutex);
            return;
        }
    }
    else
    {
        gossip_lerr("ERROR: Zero size request processed!!??\n");
    }
} /*end flow_bmi_recv*/



#ifdef __PVFS2_TROVE_SUPPORT__
/* server_bmi_recv_callback_fn()
 *
 * Callback invoked when a BMI recv operation completes
 * no return value
 */
void server_bmi_recv_callback_fn(void *user_ptr,
                                 PVFS_size actual_size,
                                 PVFS_error error_code)
{
    struct fp_queue_item *q_item = user_ptr;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    flow_descriptor *flow_d = flow_data->parent;

    gossip_err("flow(%p):q_item(%p):Executing %s...\n",flow_d,q_item,__func__);

    /* Handle errors from recv */
    if(error_code != 0 || flow_d->error_code != 0)
    {
        gen_mutex_lock(&flow_d->flow_mutex);
        flow_data->recvs_pending--;
        gen_mutex_unlock(&flow_d->flow_mutex);
        gossip_lerr("ERROR occured on recv: %d\n", error_code);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    gen_mutex_lock(&flow_d->flow_mutex);
    /* Decrement recv pending count */
    flow_data->recvs_pending -= 1;
    flow_data->total_bytes_recvd += actual_size;
    
    /* Remove from src-list */
    qlist_del(&q_item->list_link);
    
    /* Write the data to trove. flow_trove_write puts q_item on the dest-list */
    flow_data->writes_pending += 1;

    /* Debug output */
    gossip_err("flow(%p):q_item(%p):%s: Total: %lld TotalAmtRecvd: %lld AmtRecvd: %lld PendingRecvs: %d PendingWrites: %d\n",
                 flow_d,q_item,__func__,
                 (long long int)flow_data->total_bytes_req,
                 (long long int)flow_data->total_bytes_recvd,
                 (long long int)actual_size,
                 flow_data->recvs_pending,
                 flow_data->writes_pending);
    gen_mutex_unlock(&flow_d->flow_mutex);

    flow_trove_write(q_item,
                     actual_size,
                     server_trove_write_callback_fn);
    
    /* At this point, either the trove write has been queued or it completed immediately. If 
     * immediate completion, then server_trove_write_callback_fn is called in flow_trove_write.
     */  
   return;
}/*end server_bmi_recv_callback_fn*/
#endif



#ifdef __PVFS2_TROVE_SUPPORT__
/**
 * Performs a trove write on data for a forwarding flow
 *
 * Remove q_item from current list
 * Adds q_item to dest_list
 * Sends data
 * Calls forwarding_bmi_send_callback on success
 *
 */
void flow_trove_write(struct fp_queue_item* q_item,
                      PVFS_size actual_size,
                      trove_write_callback write_callback)
{
    struct fp_private_data* flow_data = PRIVATE_FLOW(q_item->parent);
    flow_descriptor *flow_d = flow_data->parent;
    struct result_chain_entry* result_iter = 0;
    int data_sync_mode=0;
    int rc = 0;
    int i;

    gossip_err("flow(%p):q_item(%p):Executing %s...\n",flow_d,q_item,__func__);


    /* Retrieve the data sync mode */
    data_sync_mode = get_data_sync_mode(flow_d->dest.u.trove.coll_id);

    gossip_err("data sync mode (%d)\n",data_sync_mode);

    gen_mutex_lock(&flow_d->flow_mutex);
    /* Add qitem to dest_list */
    qlist_del(&q_item->list_link);
    qlist_add_tail(&q_item->list_link, &flow_data->dest_list);
    gen_mutex_unlock(&flow_d->flow_mutex);

    /* Perform a write to disk */
    result_iter = &q_item->result_chain;
    q_item->out_size=0; /* just to be sure */
    
    for (i=0; i<q_item->result_chain_count; i++)
    {
        /* Construct trove data structure */
        result_iter->q_item = q_item;
        result_iter->trove_callback.data = result_iter;
        result_iter->trove_callback.fn = write_callback;
        result_iter->out_size = 0;

        gossip_err("%s:flow(%p):q_item(%p):result_iter->result.bytes(%d).\n",__func__
                                                                             ,flow_d
                                                                             ,q_item
                                                                             ,(int)result_iter->result.bytes);

        rc = trove_bstream_write_list(flow_d->dest.u.trove.coll_id,
                                      flow_d->dest.u.trove.handle,
                                      (char**)&result_iter->buffer_offset,
                                      &result_iter->result.bytes,
                                      1,
                                      result_iter->result.offset_array,
                                      result_iter->result.size_array,
                                      result_iter->result.segs,
                                      &result_iter->out_size, /* need to have an out_size for each result_iter */
                                      data_sync_mode,
                                      NULL,
                                      &result_iter->trove_callback,
                                      global_trove_context,
                                      &result_iter->posted_id,
                                      NULL);
        
        /* if an error occurs, handle it
           else if immediate completion, trigger callback */
        if (rc < 0)
        {
            /* mutex lock 
             * qlist_del(&q_item->list_link)             
             * flow_data->writes_pending--
             * q_item->buffer_in_use--
             * flow_d->replication_status |= REPLICATION_TROVE_FAILURE;
             * q_item->res.replica_state = FAILED_TROVE_POST_WRITE;
             * q_item->res.replica_error_code = rc;
             * flow_data->attempted_write_amt = actual_size
             * mutex unlock
             * return;
             * 
             *
             * We want to continue receiving the flow and passing on data to the replicas, even though we can no longer
             * write to the local server.
             */
            gossip_err("Flow(%p): q_item(%p): %s: ERROR: Trove Write CATASTROPHIC FAILURE\n",flow_d,q_item,__func__);
            return;
        }
        else if (1 == rc)
        {
            gossip_err("Flow(%p):q_item(%p):%s:Immediate write completion. Executing callback.\n",flow_d,q_item,__func__);
            write_callback(result_iter, 0);
        }

        /* Increment iterator */
        result_iter = result_iter->next;

    }; /*end for*/

} /*end flow_trove_write*/
#endif


#ifdef __PVFS2_TROVE_SUPPORT__
/**
 * Callback invoked upon completion of a trove write operation
 */
void forwarding_trove_write_callback_fn(void *user_ptr,
  			                PVFS_error error_code)
{
    struct result_chain_entry *result_entry = user_ptr;
    struct fp_queue_item *q_item = result_entry->q_item;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    flow_descriptor *flow_d = flow_data->parent;
    int i;

    gossip_err("flow(%p):q_item(%p):error_code(%d):Executing %s...\n",flow_d,q_item,(int)error_code,__func__);

    gen_mutex_lock(&flow_d->flow_mutex);

    /* we are cancelling the entire flow */
    if (flow_d->error_code != 0 )
    {
       handle_forwarding_io_error(error_code, q_item, flow_data);
       gen_mutex_unlock(&flow_d->flow_mutex);
       return;
    }

    result_entry->posted_id = 0;
    q_item->result_chain_count--;
    gossip_err("flow(%p):q_item(%p):%s:result_chain_count(%d)\n",flow_d,q_item,__func__,q_item->result_chain_count);

    /* this error is for this particular result chain entry; there could be multiple entries. So,
     * we will keep the first error and let any others go.  Or, another q_item may have
     * already set the res->error_code.  In either case, we keep the original error and
     * throw away any others.
     */
    if (error_code != 0)
    {
       if (q_item->res->error_code == 0 )
       {
          q_item->res->state      = FAILED_TROVE_WRITE;
          q_item->res->error_code = error_code;
       }
       gossip_err("%s:flow(%p):q_item(%p):q_item->res->state(%s):q_item->res->error_code(%d).\n"
                 ,__func__
                 ,flow_d
                 ,q_item
                 ,get_replication_endpoint_state_as_string(q_item->res->state)
                 ,(int)q_item->res->error_code);
    }/*end if error code is not zero*/

    /* A zero chain count signifies the last time trove-write-callback will execute for this flow buffer. 
     * NOTE: we will have multiple chains when the given buffer contains data that is not to be written
     * sequentially; used primarily in user-defined data types in MPI.
     */
    if (q_item->result_chain_count == 0)
    {
        struct result_chain_entry* result_iter = &q_item->result_chain;

        qlist_del(&q_item->list_link); /* delete from the dest list */
        flow_data->writes_pending--;
        q_item->buffer_in_use--;
          
        while (result_iter)
        {
             struct result_chain_entry* re = result_iter;
             flow_data->total_bytes_written += result_iter->out_size;
             flow_d->total_transferred      += result_iter->out_size;
             q_item->res->writes_completed_bytes += result_iter->out_size;
             gossip_err("flow(%p):q_item(%p):%s:q_item->res(%p):q_item->res->writes_completed_bytes(%p).\n"
                        ,flow_d
                        ,q_item
                        ,__func__
                        ,q_item->res
                        ,&q_item->res->writes_completed_bytes);
             gossip_err("flow(%p):q_item(%p):%s:total-bytes-written(%d) \tresult_iter->out_size(%d)\n"
                       ,flow_d
                       ,q_item,__func__
                       ,(int)flow_data->total_bytes_written
                       ,(int)result_iter->out_size);
             PINT_perf_count(PINT_server_pc, 
                             PINT_PERF_WRITE,
                             result_iter->result.bytes, 
                             PINT_PERF_ADD);

             result_iter = result_iter->next;

             /* Free memory if this is not the chain head */
             if (re != &q_item->result_chain)
                 free(re);
        }/*end while*/
    
        /* Cleanup q_item memory */
        q_item->result_chain.next = NULL;
        q_item->result_chain_count = 0;

    }/*end if result-chain-count is zero*/

    /* Debug output */
    gossip_err(
     "FORWARDING-TROVE-WRITE-FINISHED: flow(%p):q_item(%p):%s:Total: %lld TotalAmtWritten: %lld "
     "AmtWritten: %lld PendingWrites: %d "
     "Throttled: %d\n",
        flow_d,q_item,__func__,
        (long long int)flow_data->total_bytes_req,
        (long long int)flow_data->total_bytes_written,
        (long long int)q_item->out_size,
        flow_data->writes_pending,
        flow_data->primary_recvs_throttled);

    gossip_err("flow(%p):q_item(%p):%s:buffer_in_use(%d)\n",flow_d
                                                            ,q_item,__func__
                                                            ,q_item->buffer_in_use);


    /* Determine if the flow is complete, regardless of error_code */
    if (forwarding_is_flow_complete(flow_data))
    {
         gossip_err("flow(%p):%s: Write finished\n",flow_d,__func__);
         if (flow_d->repl_d[flow_d->repl_d_local_flow_index].endpt_status.state == RUNNING)
         {
            assert(flow_data->total_bytes_recvd == flow_data->total_bytes_written);
         }
         assert(flow_d->state != FLOW_COMPLETE);
         FLOW_CLEANUP(flow_data);
         flow_d->state = FLOW_COMPLETE;
         gen_mutex_unlock(&flow_d->flow_mutex);
         return;
     }


     /* If this buffer is available, there is more data to receive, and at least one flow is still runnning,
      * then post another BMI-RECV.
      */
     if (q_item->buffer_in_use == 0 && !PINT_REQUEST_DONE(flow_d->file_req_state))
     {
         gossip_err("flow(%p):q_item(%p):%s:Starting recv. buffer_in_use(%d)\n"
                    ,flow_d,q_item,__func__,q_item->buffer_in_use);

         /* Post another recv operation as long as trove writes are still running or 
          * bmi sends are still running.
          */
         for (i=0; i<flow_d->repl_d_total_count; i++)
         {
             if (flow_d->repl_d[i].endpt_status.state == RUNNING)
             {
                break;
             }
         }
         gen_mutex_unlock(&flow_d->flow_mutex);
         if (i < flow_d->repl_d_total_count)
         {
            flow_bmi_recv(q_item,
                          forwarding_bmi_recv_callback_fn);
         }
     }
     else
     {
         gen_mutex_unlock(&flow_d->flow_mutex);
     }
  
    return;
}/*end forwarding_trove_write_callback_fn*/
#endif

/**
 * Marks the forwarding flow as complete when finished
 * Return 1 when the flow is complete, otherwise returns 0
 */
int forwarding_is_flow_complete(struct fp_private_data* flow_data)
{
    int is_flow_complete = 0;
    int i;
    flow_descriptor *flow_d = flow_data->parent;
 
    gossip_err("flow(%p):Executing %s...\n",flow_d,__func__);
    
    /* If there are no more recvs, check for completion
       else if there are recvs to go, start one if possible */
    if (PINT_REQUEST_DONE(flow_d->file_req_state))
    {
       gossip_err("flow(%p):PINT_REQUEST_DONE is true.\n",flow_d);
       gossip_err("flow(%p):sends_pending(%d):recvs_pending(%d):writes_pending(%d)\n"
                  ,flow_d
                  ,flow_data->sends_pending
                  ,flow_data->recvs_pending
                  ,flow_data->writes_pending);
        /* If all replicate operations are complete */
        if (0 == flow_data->sends_pending &&
            0 == flow_data->recvs_pending &&
            0 == flow_data->writes_pending)
        {
            gossip_err("flow(%p):Forwarding flow finished\n",flow_d);
            gossip_err("flow(%p):flow_data->total_bytes_recvd(%d) \ttotal_bytes_forwarded(%d) \tNumber of Copies(%d) "
                        "\tbytes_per_server(%d)\n"
                       ,flow_d
                       ,(int)flow_data->total_bytes_recvd
                       ,(int)flow_data->total_bytes_forwarded
                       ,(int)flow_d->next_dest_count
                                     /* NOTE: flow_d->next_dest_count is wrong. should use count of active destinations */
                       ,(int)flow_data->total_bytes_forwarded/(int)flow_d->next_dest_count);
            gossip_err("flow(%p):flow_data->total_bytes_recv(%d) \ttotal_bytes_written(%d)\n"
                       ,flow_d,(int)flow_data->total_bytes_recvd,(int)flow_data->total_bytes_written);
            for (i=0; i<flow_d->repl_d_total_count; i++)
            {
                replication_endpoint_status_print(&flow_d->repl_d[i].endpt_status,1);
            }
            assert(flow_data->total_bytes_recvd ==
                   ((int)flow_data->total_bytes_forwarded/(int)flow_d->next_dest_count));

            /* if the local trove call ended in error, then we cannot assume that the total_bytes_written 
             * will match the total_bytes_recvd.  Once a trove error is encountered with one flow buffer,
             * then all trove calls and updates to total_bytes_written are stopped, even though we are 
             * still receiving and forwarding data to other replicas.
             */
            if ( flow_d->repl_d[flow_d->repl_d_local_flow_index].endpt_status.state == RUNNING )
            {
               assert(flow_data->total_bytes_recvd == flow_data->total_bytes_written);
            }

            is_flow_complete = 1;
        }/*end if*/
    }/*end if*/

    return is_flow_complete;
}/*end forwarding_is_flow_complete*/


#ifdef __PVFS2_TROVE_SUPPORT__
/* forwarding_bmi_recv_callback_fn()
 *
 * Callback invoked when a BMI recv operation completes
 * no return value
 */
void forwarding_bmi_recv_callback_fn(void *user_ptr,
					    PVFS_size actual_size,
					    PVFS_error error_code)
{
    struct fp_queue_item *q_item = user_ptr;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    flow_descriptor *flow_d = flow_data->parent;
    int i, ret, running_replica_count=0;
    struct fp_queue_item *replica_q_item = NULL;

    gossip_err("flow(%p):q_item(%p):Executing %s...\n",flow_d,q_item,__func__);

    /* Handle errors from recv */
    /* mutex lock
     * if ( flow_d->replication_status & REPLICATION_STOP ) 
     *      qlist_del(&q_item->list_link)
     *      unlock mutex
     *      handle_forwarding_io_error()
     *      return; 
     * if ( error_code != 0 )
     *      qlist_del(&q_item->list_link)
     *      q_item->res.replica_state = FAILED_BMI_RECV
     *      q_item->res.replica_error_code = error_code;
     *      flow_d->replication_status |= REPLICATION_STOP
     *      flow_d->state = FLOW_COMPLETE;
     *      unlock mutex
     *      handle_forwarding_io_error()
     *      return;
     */ 
    gen_mutex_lock(&flow_d->flow_mutex);

    if (flow_d->error_code != 0)
    {
        gossip_lerr("ERROR occured on recv: error_code(%d), flow_d->error_code(%d)\n", error_code
                                                                                     , flow_d->error_code);
        /* if we can't receive data from the client, then we must cancel the
         * entire flow, allowing the client to restart the flow.
         */
        handle_forwarding_io_error(error_code, q_item, flow_data);
        gen_mutex_unlock(&flow_d->flow_mutex);
        return;
    }

    /* calculate how many replicas that we will attempt to send. We check the intersection of the replicas
     * contacted with those returning a successful response. 
     */
    for (i=0; i<flow_d->repl_d_repl_count && flow_d->repl_d[i].endpt_status.state == RUNNING; i++)
    {
        running_replica_count++;
    }

    /* Increment buffer_in_use and sends_pending by the number of replicas still RUNNING */
    q_item->buffer_in_use    += running_replica_count;
    flow_data->sends_pending += running_replica_count;

    /* Decrement recv pending count; we just got one from the client */
    flow_data->recvs_pending -= 1;

    /* Increment writes pending.  We MUST increment before sending to replica servers, because the BMI thread may execute before we
     * return from the post, which may possibly execute the callback for this send.  We want the callback to know that we have a 
     * write pending and thus the flow buffer is still in use and therefore the flow is not finished.  
     */
     if ( q_item->res->state == RUNNING )
     {
        flow_data->writes_pending += 1;

        /* Remove from src_list and add to dest_list */
        qlist_del(&q_item->list_link);
        qlist_add_tail(&q_item->list_link, &flow_data->dest_list);

        /* Reset the posted_id for the trove call */
        q_item->posted_id = 0;
     }
     else
     {
        q_item->buffer_in_use--;
        qlist_del(&q_item->list_link);
     }

    /* bytes received from the client.  This amount could be the size of a flow buffer or smaller. */
    flow_data->total_bytes_recvd += actual_size;
    
    /* Debug output */
    gossip_err("flow(%p):q_item(%p):RECV FINISHED: Total: %lld TotalRecvd: %lld RecvdNow: %lld AmtFwd: %lld PendingRecvs: %d "
                "PendingFwds: %d Throttled: %d\n",
                 flow_d,
                 q_item,
                 (long long int)flow_data->total_bytes_req,
                 (long long int)flow_data->total_bytes_recvd,
                 (long long int)actual_size,
                 (long long int)flow_data->total_bytes_forwarded,
                 flow_data->recvs_pending,
                 flow_data->sends_pending,
                 flow_data->primary_recvs_throttled);

    gen_mutex_unlock(&flow_d->flow_mutex);

    /* Send data to replica servers */
    for (i=0; i<flow_d->repl_d_repl_count && flow_d->repl_d[i].endpt_status.state == RUNNING; i++)
    {
     
        gossip_err("flow(%p):q_item(%p):buffer_in_use(%d)\n",flow_d,q_item,q_item->buffer_in_use);

        replica_q_item = &q_item->replicas[i];
        INIT_QLIST_HEAD(&replica_q_item->list_link);
        replica_q_item->parent = flow_d;
        replica_q_item->replica_parent = q_item;

        replica_q_item->bmi_callback.fn = forwarding_bmi_send_callback_fn;
        replica_q_item->bmi_callback.data = replica_q_item;
        replica_q_item->buffer = q_item->buffer;

        gen_mutex_lock(&flow_d->flow_mutex);
           /* NOTE: the replica q_item's are only included on the src-list, so that error handling
            * will process correctly.
            */
           qlist_add_tail(&replica_q_item->list_link, &flow_data->src_list);
        gen_mutex_unlock(&flow_d->flow_mutex);

        /* hints should contain BMI_QUEUE hint, so that sends are always queued. */
        gossip_err("%s:flow(%p):flow_d->next_dest[%d].u.bmi.address(%d) tag(%d)\n"
                   ,__func__,flow_d,i,(int)flow_d->next_dest[i].u.bmi.address
                                     ,(int)flow_d->next_dest[i].u.bmi.tag);
        ret = BMI_post_send( &replica_q_item->posted_id    
                            ,flow_d->next_dest[i].u.bmi.address
                            ,replica_q_item->buffer
                            ,actual_size
                            ,BMI_PRE_ALLOC
                            ,flow_d->next_dest[i].u.bmi.tag
                            ,&replica_q_item->bmi_callback
                            ,global_bmi_context
                            ,flow_d->hints );
        gossip_err("flow(%p):replica(%p):q_item(%p):%s:Value of BMI_post_send:(%d).\n"
                   ,flow_d,replica_q_item,q_item,__func__,ret);
        if (ret == 0)
        {
           /* BMI-send was queued. */
           continue;
        }
        else if (ret == 1)
        {
           /* BMI-send completed immediately, just in case */
           forwarding_bmi_send_callback_fn(replica_q_item, actual_size, 0);
           continue;
        }
        else if (ret < 0)
        {
           /* mutex lock
            * qlist_del(&replica_q_item->list_link);
            * flow_data->sends_pending--;
            * q_item->buffer_in_use--;
            * flow_d->replication_status |= REPLICATION_BMI_SEND_FAILURE;
            * replica_q_item->res.replica_state = FAILED_BMI_POST_SEND;
            * replica_q_item->res.replica_error_code = ret;
            * unlock mutex
            * handle_forwarding_io_error();
            * continue;
            */
           gossip_err("flow(%p):replica(%p):q_item(%p):%s:Error while sending to replica server:(%d)..\n"
                      ,flow_d,replica_q_item,q_item,__func__,ret);
           PVFS_perror("Error Code:",ret);

           gen_mutex_lock(&flow_d->flow_mutex);
           /* change this to handle_io_error ? For now, stop the entire flow*/
           handle_forwarding_io_error(ret, replica_q_item, flow_data);
           gen_mutex_unlock(&flow_d->flow_mutex);
           return;
        }
    }/*end for*/

    if (q_item->res->state == RUNNING)
    {
       flow_trove_write(q_item, actual_size,
                        forwarding_trove_write_callback_fn);
    }
   return; 
}/*end forwarding_bmi_recv_callback_fn*/
#endif


/**
 * Perform a process request for a q_item
 *
 * @return the number of bytes processed
 */
static int flow_process_request( struct fp_queue_item* q_item )
{
    struct result_chain_entry *result_tmp;
    struct result_chain_entry *old_result_tmp;
    flow_descriptor *flow_d = q_item->parent;
    PVFS_size bytes_processed = 0;
    void* tmp_buffer;

    result_tmp = &q_item->result_chain;
    old_result_tmp = result_tmp;
    tmp_buffer = q_item->buffer;
    
    /* protect the variables shared by the flow buffers */
    gen_mutex_lock(&flow_d->flow_mutex);

    do {
        int ret = 0;
        
        q_item->result_chain_count++;

        /* if no result chain exists, allocate one */
        if (!result_tmp)
        {
            result_tmp = (struct result_chain_entry*)malloc(
                sizeof(struct result_chain_entry));
            assert(result_tmp);
            memset(result_tmp, 0, sizeof(struct result_chain_entry));
            old_result_tmp->next = result_tmp;
        }
        
        /* process request */
        result_tmp->result.offset_array = result_tmp->offset_list;
        result_tmp->result.size_array = result_tmp->size_list;
        result_tmp->result.bytemax = q_item->parent->buffer_size - bytes_processed;
        result_tmp->result.bytes = 0;
        result_tmp->result.segmax = MAX_REGIONS;
        result_tmp->result.segs = 0;
        result_tmp->buffer_offset = tmp_buffer;

        ret = PINT_process_request(flow_d->file_req_state,
                                   flow_d->mem_req_state,
                                   &flow_d->file_data,
                                   &result_tmp->result,
                                   PINT_SERVER);
        
        /* TODO: error handling */ 
        assert(ret >= 0);

        /* No documnetation, figure out later */
        if (result_tmp->result.bytes == 0)
        {
            if (result_tmp != &q_item->result_chain)
            {
                free(result_tmp);
                old_result_tmp->next = NULL;
            }
            
            q_item->result_chain_count--;
        }
        else
        {
            old_result_tmp = result_tmp;
            result_tmp = result_tmp->next;
            tmp_buffer = (void*)((char*)tmp_buffer +
                                 old_result_tmp->result.bytes);
            bytes_processed += old_result_tmp->result.bytes;
        }
        
    } while (bytes_processed < flow_d->buffer_size && !PINT_REQUEST_DONE(flow_d->file_req_state));

    gen_mutex_unlock(&flow_d->flow_mutex);

    return bytes_processed;
}/*end flow_process_request*/

#ifdef __PVFS2_TROVE_SUPPORT__
/**
 * Callback invoked upon completion of a BMI send operation
 */
void forwarding_bmi_send_callback_fn(void *user_ptr,
                                     PVFS_size actual_size,
                                     PVFS_error error_code)
{
    /* Convert data into flow descriptor */
    struct fp_queue_item* replica_q_item = user_ptr;
    struct fp_private_data *flow_data = PRIVATE_FLOW(((struct fp_queue_item*)user_ptr)->parent);
    flow_descriptor *flow_d = flow_data->parent;
    struct fp_queue_item *q_item = replica_q_item->replica_parent;

    gossip_err("flow(%p):replica_q_item(%p):error_code(%d):Executing %s...\n",flow_d
                                                                              ,replica_q_item
                                                                              ,(int)error_code,__func__);

    gen_mutex_lock(&flow_d->flow_mutex);

    if (flow_d->error_code != 0)
    {
       gossip_err("function(%s):flow(%p):replica_q_item(%p):error_code(%d):Flow is being cancelled.\n"
                   ,__func__
                   ,flow_d
                   ,replica_q_item
                   ,(int)error_code);
       replica_q_item->res->state = FLOW_CANCELLED;
       replica_q_item->res->error_code = flow_d->error_code;
       qlist_del(&replica_q_item->list_link);
       /* if this q-item was counted? */
       handle_forwarding_io_error(flow_d->error_code, replica_q_item, flow_data);
       gen_mutex_unlock(&flow_d->flow_mutex);
       return;
    }

    /* Perform bookkeeping for data forward completion */
    flow_data->sends_pending -= 1;

    /* after all sends have completed, this value will represent the amount of data sent
     * times the number of copies.
     */
    //NOTE: don't want to add actual_size if error occurred.
    flow_data->total_bytes_forwarded += actual_size;

    /* NOTE: buffer_in_use is shared between forwarding_bmi_recv_callback_fn, forwarding_bmi_send_callback_fn,
     *       and forwarding_trove_write_callback_fn.
     */
    q_item->buffer_in_use--;

    /* Remove SEND request from the src-list */
    qlist_del(&replica_q_item->list_link);

    /* Debug output */
    gossip_err("FWD FINISHED: flow(%p): q_item(%p) parent q_item(%p): Total: %lld TotalRecvd: %lld TotalAmtFwd: %lld "
                "AmtFwdNow: %lld PendingRecvs: %d "
                "PendingFwds: %d Throttled: %d\n",
                 flow_d,
                 replica_q_item,
                 q_item,
                 (long long int)flow_data->total_bytes_req,
                 (long long int)flow_data->total_bytes_recvd,
                 (long long int)flow_data->total_bytes_forwarded,
                 (long long int)actual_size,
                 flow_data->recvs_pending,
                 flow_data->sends_pending,
                 flow_data->primary_recvs_throttled);

    if (error_code != 0)
    {
        gossip_err("function(%s):flow(%p):replica_q_item(%p):error_code(%d):Error sending data.\n"
                   ,__func__
                   ,flow_d
                   ,replica_q_item
                   ,(int)error_code);
        replica_q_item->res->state = FAILED_BMI_SEND;
        replica_q_item->res->error_code = error_code;
    }

    if (forwarding_is_flow_complete(flow_data))
    {
        gossip_err("flow(%p):q_item(%p):%s:Finished sending a buffer of data? %s\n",flow_d,q_item,__func__,error_code?"NO":"YES");
        if (flow_d->repl_d[flow_d->repl_d_local_flow_index].endpt_status.state == RUNNING)
        {
            assert(flow_data->total_bytes_recvd == flow_data->total_bytes_written);
        }
        assert(flow_d->state != FLOW_COMPLETE);
        FLOW_CLEANUP(flow_data);
        flow_d->state = FLOW_COMPLETE;
        gen_mutex_unlock(&flow_d->flow_mutex);
        return;
    }

    gossip_err("flow(%p): q_item(%p): buffer_in_use(%d)\n",flow_d,q_item,q_item->buffer_in_use);

    /* If this request needs to receive more data, then re-use the current flow buffer if its no longer in use. */
    if (q_item->buffer_in_use == 0 && !PINT_REQUEST_DONE(flow_d->file_req_state))
    {
          /* Post another recv operation */
          gossip_err("flow(%p): q_item(%p): %s: Starting recv. buffer_in_use(%d)\n"
                     ,flow_d
                     ,q_item,__func__
                     ,q_item->buffer_in_use);
          gen_mutex_unlock(&flow_d->flow_mutex);
          flow_bmi_recv(q_item,
                        forwarding_bmi_recv_callback_fn);
    }
    else
    {
       gen_mutex_unlock(&flow_d->flow_mutex);
    }

    return;
}/*end forwarding_bmi_send_callback_fn*/
#endif

#ifdef __PVFS2_TROVE_SUPPORT__
/**
 * Callback invoked upon completion of a trove write operation
 */
void server_trove_write_callback_fn(void *user_ptr,
                                    PVFS_error error_code)
{
    struct result_chain_entry* result_entry = user_ptr;
    struct fp_queue_item *q_item = result_entry->q_item;
    struct fp_private_data *flow_data = PRIVATE_FLOW(q_item->parent);
    flow_descriptor *flow_d = flow_data->parent;
    int bytes_written=0;

    gossip_err("flow(%p):q_item(%p):%s:Server Write Finished\n",flow_d,q_item,__func__);

    /* Handle trove errors */
    if(error_code != 0 || flow_d->error_code != 0)
    {
        gossip_err("flow(%p):q_item(%p):%s trove error_code(%d) flow_d->error_code(%d)\n"
                   ,flow_d,q_item,__func__,error_code,flow_d->error_code);
        gen_mutex_lock(&flow_d->flow_mutex);
        flow_data->writes_pending--;
        gen_mutex_unlock(&flow_d->flow_mutex);
        handle_io_error(error_code, q_item, flow_data);
        return;
    }

    /* Decrement result chain count */
    q_item->result_chain_count--;
    result_entry->posted_id = 0;

    /* If all results for this qitem are available continue */
    gossip_err("flow(%p):q_item(%p):%s:result_chain_count(%d).\n"
               ,flow_d,q_item,__func__,q_item->result_chain_count);
    if (0 == q_item->result_chain_count)
    {
        struct result_chain_entry* result_iter = &q_item->result_chain;

        gen_mutex_lock(&flow_d->flow_mutex);

        /* Decrement the number of pending writes */
        flow_data->writes_pending--;

        /* Aggregate results */
        while (0 != result_iter)
        {
            struct result_chain_entry* re = result_iter;
            bytes_written += result_iter->result.bytes;
            flow_data->total_bytes_written += result_iter->result.bytes;
            flow_d->total_transferred      += result_iter->result.bytes;
            PINT_perf_count(PINT_server_pc, 
                            PINT_PERF_WRITE,
                            result_iter->result.bytes, 
                            PINT_PERF_ADD);
            result_iter = result_iter->next;

            /* Free memory if this is not the chain head */
            if (re != &q_item->result_chain)
                free(re);
        }

        /* Debug output */
        gossip_err(
            "SERVER WRITE FINISHED: flow(%p):q_item(%p):%s  Total: %lld TotalAmtWritten: %lld "
            "AmtWritten: %lld PendingWrites: %d PendingRecvs: %d RequestDone: %s\n",
            flow_d,q_item,__func__,
            (long long int)flow_data->total_bytes_req,
            (long long int)flow_data->total_bytes_written,
            (long long int)bytes_written,
            flow_data->writes_pending,
            flow_data->recvs_pending,
            PINT_REQUEST_DONE(flow_d->file_req_state)?"YES":"NO");
    
        /* Cleanup q_item memory */
        q_item->result_chain.next = NULL;
        q_item->result_chain_count = 0;

        /* Remove q_item from dest list */
        qlist_del(&q_item->list_link);

        /* Determine if the flow is complete */
        if (PINT_REQUEST_DONE(flow_d->file_req_state) &&
            0 == flow_data->recvs_pending &&
            0 == flow_data->writes_pending)
        {
            gossip_err("flow(%p):q_item(%p):%s:Server Write flow finished\n",flow_d,q_item,__func__);
            assert(flow_data->total_bytes_recvd ==
                   flow_data->total_bytes_written);
            assert(flow_d->state != FLOW_COMPLETE);
            FLOW_CLEANUP(flow_data);
            flow_d->state = FLOW_COMPLETE;
            gen_mutex_unlock(&flow_d->flow_mutex);
            return;
        }

        /* if the request requires more flow buffers, then re-use the current q_item. */
        if (!PINT_REQUEST_DONE(flow_d->file_req_state) )
        {
            /* Post another recv operation */
            gossip_err("flow(%p):q_item(%p):Starting recv from server write callback.\n",flow_d,q_item);
            gen_mutex_unlock(&flow_d->flow_mutex);
            flow_bmi_recv(q_item,
                          server_bmi_recv_callback_fn);
        }
        else
        {
           gen_mutex_unlock(&flow_d->flow_mutex);
        }
    }/*end if*/
    return;
}/*end server_trove_write_callback_fn*/
#endif

/* handle_forwarding_io_error()
 * 
 */
static void handle_forwarding_io_error(PVFS_error error_code,
                                      struct fp_queue_item* q_item,
                                      struct fp_private_data* flow_data)
{
    flow_descriptor *flow_d = flow_data->parent;
    int ret_bmi,ret_trove;

    PVFS_perror("Error: ", error_code);
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, 
	"flowproto-multiqueue error cleanup path.\n");


    /* THIS FUNCTION HAS TO BE CALLED WITH FLOW_D->FLOW_MUTEX HELD. */

    if(flow_d->error_code == 0)
    {
	gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
	    "flowproto-multiqueue first failure.\n");
	flow_d->error_code = error_code;

        /* In this case, we were unable to receive a buffer of data from
         * the client; thus, we need to stop EVERYTHING: posted recvs,
         * posted sends, and posted writes.  Posted recvs and sends are
         * located on the src-list; posted writes are on the dest-list.
         */
        ret_bmi = cancel_pending_bmi(&flow_data->src_list);

        /* cancel_pending_bmi returns the number of q_items that were sent to the
         * bmi_cancel thread.
         */

        ret_trove = cancel_pending_trove(&flow_data->dest_list,flow_d->dest.u.trove.coll_id); 

        /* cancel_pending_trove returns the number of result chains that were sent to the
         * trove_cancel thread.  NOTE: there could be many result chains per one q_item.
         */
          
        flow_data->cleanup_pending_count = ret_bmi + ret_trove;
        gossip_err("function(%s):flow(%p):q_item:(%p):error_code(%d):initial cleanup_pending_count(%d).\n"
                   ,__func__
                   ,flow_d
                   ,q_item
                   ,(int)error_code
                   ,flow_data->cleanup_pending_count);
        if (flow_data->cleanup_pending_count > 0)
        {
           return;
        }
    }
    else
    {
	/* one of the previous cancels came through */
	flow_data->cleanup_pending_count--;
    }
    
    gossip_err("function(%s):flow(%p):q_item:(%p):error_code(%d):cleanup_pending_count(%d).\n"
                ,__func__
                ,flow_d
                ,q_item
                ,(int)error_code
                ,flow_data->cleanup_pending_count);
    
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, "cleanup_pending_count: %d\n",
	flow_data->cleanup_pending_count);

    if(flow_data->cleanup_pending_count == 0)
    {
        //At this point, all pending cancels have completed and we can now stop the flow and cleanup.
	/* we are finished, make sure error is marked and state is set */
        assert(flow_d->error_code);
	/* we are in trouble if more than one callback function thinks that
	 * it can trigger completion
	 */
	assert(flow_d->state != FLOW_COMPLETE);
        FLOW_CLEANUP(flow_data);
	flow_d->state = FLOW_COMPLETE;
    }

    return;
}/*end handle_forwarding_io_error*/

/**
 * Perform initialization steps before this forwarding flow can be posted
 */
static inline void forwarding_flow_post_init(flow_descriptor* flow_d,
					     struct fp_private_data* flow_data)
{
    gossip_err("flow(%p):Executing %s...\n",flow_d,__func__);
    int i;
    uint32_t *always_queue;

    /* Initialization */
    flow_d->total_transferred = 0;
    
    flow_data->recvs_pending  = 0;
    flow_data->sends_pending  = 0;
    flow_data->writes_pending = 0;
    flow_data->primary_recvs_throttled = 0;

    flow_data->total_bytes_req = flow_data->parent->aggregate_size;
    flow_data->total_bytes_forwarded = 0;
    flow_data->total_bytes_recvd     = 0;
    flow_data->total_bytes_written   = 0;
    
    always_queue=PINT_hint_get_value_by_type(flow_d->hints,
                                             PINT_HINT_BMI_QUEUE,
                                             NULL);
    gossip_err("always_queue(%d)\n",always_queue?*always_queue:0);
    
    flow_data->cleanup_pending_count = -1;


    /* Initialize buffers */
    for (i = 0; i < flow_d->buffers_per_flow; i++)
    {
        /* Required by TROVE */
        flow_data->prealloc_array[i].result_chain.q_item = 
            &flow_data->prealloc_array[i];

        INIT_QLIST_HEAD(&flow_data->prealloc_array[i].list_link);
    }

    /* Post the initial receives */
    for (i = 0; i < flow_d->buffers_per_flow; i++)
    {
        /* If there is data to be received, perform the initial recv
           otherwise mark the flow complete */
        gen_mutex_lock(&flow_d->flow_mutex);
        if ( (flow_d->error_code == 0) && (!PINT_REQUEST_DONE(flow_d->file_req_state)))
        {
            gossip_err("flow(%p):q_item(%p):buffer_in_use(%d):Calling flow_bmi_recv from forwarding_flow_post_init.\n"
                       ,flow_d,&flow_data->prealloc_array[i],flow_data->prealloc_array[i].buffer_in_use);
            gen_mutex_unlock(&flow_d->flow_mutex);
            flow_bmi_recv(&(flow_data->prealloc_array[i]),
                          forwarding_bmi_recv_callback_fn);
        }
        else
        {
            gen_mutex_unlock(&flow_d->flow_mutex);
            if (!flow_d->error_code)
            {
               gossip_err("Server flow posted all on initial post.\n");
            }
            break;
        }
    }

    return;
}/*end forwarding_flow_post_init*/



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

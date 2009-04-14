#include "src/io/flow/flowproto-support.h"
#include "thread-mgr.h"

/* the following buffer settings are used by default if none are specified in
 * the flow descriptor
 */
#define BUFFERS_PER_FLOW 8
#define BUFFER_SIZE (256*1024)

#define MAX_REGIONS 64

#define FLOW_CLEANUP(__flow_data)                                     \
do {                                                                  \
    struct flow_descriptor *__flow_d = (__flow_data)->parent;         \
    gossip_debug(GOSSIP_FLOW_PROTO_DEBUG, "flowproto completing %p\n",\
                 __flow_d);                                           \
    cleanup_buffers(__flow_data);                                     \
    __flow_d = (__flow_data)->parent;                                 \
    free(__flow_data);                                                \
} while(0)

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
};

/* fp_queue_item describes an individual buffer being used within the flow */
struct fp_queue_item
{
    PVFS_id_gen_t posted_id;
    int last;
    int seq;
    void *buffer;
    PVFS_size buffer_used;
    PVFS_size out_size;
    struct result_chain_entry result_chain;
    int result_chain_count;
  //struct qlist_head list_link;
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
  
  //struct qlist_head src_list;
  //struct qlist_head dest_list;
  //struct qlist_head empty_list;
};
#define PRIVATE_FLOW(target_flow)\
    ((struct fp_private_data*)(target_flow->flow_protocol_data))

static void cleanup_buffers(
    struct fp_private_data *flow_data);

#if 0

/* handle_io_error()
 * 
 * called any time a BMI or Trove error code is detected, responsible
 * for safely cleaning up the associated flow
 *
 * NOTE: this function should always be called while holding the flow mutex!
 *
 * no return value
 */
static void handle_io_error(
    PVFS_error error_code,
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
        gossip_err("%s: flow proto error cleanup started on %p: %s\n", __func__, flow_data->parent, buf);

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
            ret = cancel_pending_trove(&flow_data->src_list, flow_data->parent->src.u.trove.coll_id);
            flow_data->cleanup_pending_count += ret;
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d trove-bmi Trove ops.\n", ret);
            ret = cancel_pending_bmi(&flow_data->dest_list);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d trove-bmi BMI ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
        }
        else if (src == BMI_ENDPOINT && dest == TROVE_ENDPOINT)
        {
            ret = cancel_pending_bmi(&flow_data->src_list);
            gossip_debug(GOSSIP_FLOW_PROTO_DEBUG,
                "flowproto-multiqueue canceled %d bmi-trove BMI ops.\n", ret);
            flow_data->cleanup_pending_count += ret;
            ret = cancel_pending_trove(&flow_data->dest_list, flow_data->parent->dest.u.trove.coll_id);
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

    gossip_debug(GOSSIP_IO_DEBUG, "%s: \n", __func__);
    if(flow_data->parent->src.endpoint_id == BMI_ENDPOINT &&
        flow_data->parent->dest.endpoint_id == TROVE_ENDPOINT)
    {
        for(i=0; i<flow_data->parent->buffers_per_flow; i++)
        {
            if(flow_data->prealloc_array[i].buffer)
            {
	      BMI_memfree(flow_data->parent->src.u.bmi.address,
			  flow_data->prealloc_array[i].buffer,
			  flow_data->parent->buffer_size,
			  BMI_RECV);
            }
            result_tmp = &(flow_data->prealloc_array[i].result_chain);
            do{
                old_result_tmp = result_tmp;
                result_tmp = result_tmp->next;
                if(old_result_tmp !=
                    &(flow_data->prealloc_array[i].result_chain))
                    free(old_result_tmp);
            }while(result_tmp);
            flow_data->prealloc_array[i].result_chain.next = NULL;
        }
    }
    else if(flow_data->parent->src.endpoint_id == TROVE_ENDPOINT &&
        flow_data->parent->dest.endpoint_id == BMI_ENDPOINT)
    {
        for(i=0; i<flow_data->parent->buffers_per_flow; i++)
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
                    free(old_result_tmp);
            }while(result_tmp);
            flow_data->prealloc_array[i].result_chain.next = NULL;
        }
    }
    else if(flow_data->parent->src.endpoint_id == MEM_ENDPOINT &&
        flow_data->parent->dest.endpoint_id == BMI_ENDPOINT)
    {
        if(flow_data->intermediate)
        {
            BMI_memfree(flow_data->parent->dest.u.bmi.address,
                flow_data->intermediate, flow_data->parent->buffer_size, BMI_SEND);
        }
    }
    else if(flow_data->parent->src.endpoint_id == BMI_ENDPOINT &&
        flow_data->parent->dest.endpoint_id == MEM_ENDPOINT)
    {
        if(flow_data->intermediate)
        {
            BMI_memfree(flow_data->parent->src.u.bmi.address,
                flow_data->intermediate, flow_data->parent->buffer_size, BMI_RECV);
        }
    }

    free(flow_data->prealloc_array);
}


enum {DO_READ=3, DO_WRITE, LOOP};

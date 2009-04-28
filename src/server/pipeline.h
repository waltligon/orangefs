#ifndef __PIPELINE_H
#define __PIPELINE_H

/* the following buffer settings are used by default
 */
#define BUFFER_SIZE (256*1024)

enum {LOOP=3};

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


#endif /* __PIPELINE_H */

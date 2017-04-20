/*
 * This is the equivalent of openib.c for now. Eventually I believe this stuff
 * can be moved into rdma.c/rdma.h, just not sure where yet.
 *
 * TODO: If we still need to support ibv_get_devices(), which was replaced 
 *       by ibv_get_device_list(), then the #ifdef HAVE_IBV_GET_DEVICES 
 *       statements and corresponding code need to be added back in.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2006 Kyle Schochenmaier <kschoche@scl.ameslab.gov>
 * Copyright (C) 2016 David Reynolds <david@omnibond.com>
 *
 * See COPYING in top-level directory.
 */
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C       /* include definitions */
#include <src/io/bmi/bmi-byteswap.h>         /* bmitoh64 */
#include <src/common/misc/pvfs2-internal.h>  /* llu */
#include <infiniband/verbs.h>

#ifdef HAVE_VALGRIND_H
#   include <memcheck.h>
#else
#   define VALGRIND_MAKE_MEM_DEFINED(addr,len)
#endif

#include "rdma.h"

/*
 * RDMA-private device-wide state.
 */
struct rdma_device_priv
{
    struct ibv_context *ctx;   /* context used to reference everything */
    struct ibv_cq *nic_cq;     /* single completion queue for all QPs */
    struct ibv_pd *nic_pd;     /* single protection domain for all memory/QP */
    /* TODO: is the nic_lid field needed? */
    //uint16_t nic_lid;          /* local id (nic) */
    int nic_port;              /* port number */
    struct ibv_comp_channel *channel;
    
    /* max values as reported by NIC */
    int nic_max_sge;
    int nic_max_wr;
    
    /* MTU values reported by NIC port */
    int max_mtu;
    int active_mtu;
    
    /*
     * Temp array for filling scatter/gather lists to pass to RDMA functions,
     * allocated once at start to max size defined as reported by the qp.
     */
    struct ibv_sge *sg_tmp_array;
    uint32_t sg_max_len;
    
    /*
     * We use unsignaled sends.  They complete locally but we have no need to
     * hear about it since the ack protocol with the peer handles freeing up
     * buffers and whatever.  However, the completion queue _will_ fill up
     * even though poll returns no results.  Hence you must post a signaled
     * send every once in a while, per CQ, not per QP.  This tracks when we
     * need to do the next. 
     */
    unsigned int num_unsignaled_sends;
    unsigned int max_unsignaled_sends;
};

/*
 * Per-connection state.
 */
struct rdma_connection_priv
{
    /* connection id */
    struct rdma_cm_id *id;
    
    /* ibv local params */
    struct ibv_qp *qp;
    struct ibv_mr *eager_send_mr;
    struct ibv_mr *eager_recv_mr;
    
    /* TODO: are these still needed? */
    /* rdma remote params */
    //uint16_t remote_lid;
    //uint32_t remote_qp_num;
};

/* NOTE:  You have to be sure that ib_uverbs.ko is loaded, otherwise it
 * will segfault and/or not find the ib device.  
 * TODO:  Find some way to check and see if ib_uverbs.ko is loaded (if 
 *        needed for RDMA/RoCE)
 */

/* constants used to initialize infiniband device */
static const int IBV_PORT = 1;  /* TODO: what if it isn't Port 1? */
static const unsigned int IBV_NUM_CQ_ENTRIES = 1024;
/* TODO: can the MTU be bumped up? */
static const int IBV_MTU = IBV_MTU_1024;  /* dmtu, 1k good for mellanox */

/* function prototypes */
static void build_qp_init_attr(int *num_wr, struct ibv_qp_init_attr *attr);
static void build_conn_params(struct rdma_conn_param *params);
//static void init_connection_modify_qp(struct ibv_qp *qp,
//                                      uint32_t remote_qp_num,
//                                      int remote_lid);
static void rdma_post_rr(const rdma_connection_t *c, struct buf_head *bh);
static const char *async_event_type_string(enum ibv_event_type event_type);
static int return_active_nic_handle(struct rdma_device_priv *rd,
                                    struct ibv_port_attr *hca_port);
int int_rdma_initialize(void);
static void rdma_finalize(void);


/* 
 * build_qp_init_attr()
 *
 * Description:
 *  Set initial attributes for new queue pair before creating it.
 *
 * Params:
 *  [out] num_wr - requested number of work requests
 *  [out] attr   - initial QP attributes to be passed to rdma_create_qp()
 *
 * Returns:
 *  none
 */
static void build_qp_init_attr(int *num_wr,
                               struct ibv_qp_init_attr *attr)
{
    struct rdma_device_priv *rd = rdma_device->priv;
    
    memset(attr, 0, sizeof(*attr));
    
    attr->send_cq = rd->nic_cq;
    attr->recv_cq = rd->nic_cq;
    
    *num_wr = rdma_device->eager_buf_num + 50;   /* plus some rdmaw */
    if (*num_wr > rd->nic_max_wr)
    {
        *num_wr = rd->nic_max_wr;
    }
    
    attr->cap.max_recv_wr = *num_wr;
    attr->cap.max_send_wr = *num_wr;
    attr->cap.max_recv_sge = 16;
    attr->cap.max_send_sge = 16;
    
    if ((int) attr->cap.max_recv_sge > rd->nic_max_sge)
    {
        attr->cap.max_recv_sge = rd->nic_max_sge;
        attr->cap.max_send_sge = rd->nic_max_sge - 1;
        /* minus 1 to work around mellanox issue */
    }
    
    /* Reliable Connection */
    attr->qp_type = IBV_QPT_RC;
}

/*
 * build_conn_params()
 *
 * Description:
 *  Set RDMA connection parameters in preparation for calling rdma_connect().
 *
 * Params:
 *  [out] params - connection parameters to be passed to rdma_connect()
 *
 * Returns:
 *  none
 */
static void build_conn_params(struct rdma_conn_param *params)
{
    memset(params, 0, sizeof(*params));
    
    params->responder_resources = 1;
    params->initiator_depth = 1;
    params->retry_count = 7;
    params->rnr_retry_count = 7;
}

/*
 * rdma_new_connection()
 *
 * TODO: break into multiple functions
 *
 * Description:
 *  Build new connection.
 *
 * Params:
 *  [in/out] c - connection structure
 *  [in] id    - RDMA CM identifier new connection will be associated with
 *  [in] is_server - 0=client, 1=server
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int rdma_new_connection(rdma_connection_t *c,
                               struct rdma_cm_id *id,
                               int is_server)
{
    struct rdma_connection_priv *rc;
    struct rdma_device_priv *rd = rdma_device->priv;
    int i, ret;
    int num_wr = 0;
    size_t len;
    struct ibv_qp_init_attr attr;
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *event = NULL;
    struct rdma_cm_event event_copy;
    
    if (is_server)
    {
        debug(0, "%s: [SERVER] starting, channel=%d",
              __func__, id->channel->fd);
    }
    else
    {
        debug(0, "%s: [CLIENT] starting, channel=%d",
              __func__, id->channel->fd);
    }
    
    /* build connection priv */
    rc = malloc(sizeof(*rc));
    if (!rc)
    {
        /* TODO: is this the best way to handle it? exit? */
        error("%s: malloc %ld bytes failed", __func__, sizeof(*rc));
    }
    
    c->priv = rc;
    rc->id = id;
    
    /* register memory region, Recv side */
    len = rdma_device->eager_buf_num * rdma_device->eager_buf_size;
    
    debug(0, "%s: calling ibv_reg_mr recv side", __func__);
    rc->eager_recv_mr = ibv_reg_mr(rd->nic_pd,
                                   c->eager_recv_buf_contig,
                                   len,
                                   IBV_ACCESS_LOCAL_WRITE
                                       | IBV_ACCESS_REMOTE_WRITE
                                       | IBV_ACCESS_REMOTE_READ);
    if (!rc->eager_recv_mr)
    {
        error("%s: register_mr eager recv", __func__);
        return -ENOMEM;
    }
    
    /* register memory region, Send side */
    debug(0, "%s: calling ibv_reg_mr send side", __func__);
    rc->eager_send_mr = ibv_reg_mr(rd->nic_pd,
                                   c->eager_send_buf_contig,
                                   len,
                                   IBV_ACCESS_LOCAL_WRITE
                                       | IBV_ACCESS_REMOTE_WRITE
                                       | IBV_ACCESS_REMOTE_READ);
    if (!rc->eager_send_mr)
    {
        error("%s: register_mr eager send", __func__);
        return -ENOMEM;
    }
    
    /* create the main queue pair */
    debug(0, "%s: creating main queue pair", __func__);
    build_qp_init_attr(&num_wr, &attr);
    
    /* NOTE: rdma_create_qp() automatically transitions the QP through its
     * states; after allocation and connection it is ready to post receives
     */
    debug(0, "%s: calling rdma_create_qp", __func__);
    ret = rdma_create_qp(id, rd->nic_pd, &attr);
    if (ret)
    {
        error("%s: create QP", __func__);
        return -EINVAL;
    }
    
    rc->qp = id->qp;
    
    VALGRIND_MAKE_MEM_DEFINED(&attr, sizeof(attr));
    VALGRIND_MAKE_MEM_DEFINED(&rc->qp->qp_num, sizeof(rc->qp->qp_num));
    
    /* compare the caps that came back against what we already have */
    if (rd->sg_max_len == 0)
    {
        rd->sg_max_len = attr.cap.max_send_sge;
        if (attr.cap.max_recv_sge < rd->sg_max_len)
        {
            rd->sg_max_len = attr.cap.max_recv_sge;
        }
        
        rd->sg_tmp_array = malloc(rd->sg_max_len * sizeof(*rd->sg_tmp_array));
        if (!rd->sg_tmp_array)
        {
            /* TODO: is this the best way to handle it? exit? */
            error("%s: malloc %ld bytes failed",
                  __func__, (rd->sg_max_len * sizeof(*rd->sg_tmp_array)));
        }
    }
    else
    {
        if (attr.cap.max_send_sge < rd->sg_max_len)
        {
            error("%s: new conn has smaller send SG array size %d vs %d",
                  __func__, attr.cap.max_send_sge, rd->sg_max_len);
            return -EINVAL;
        }
        if (attr.cap.max_recv_sge < rd->sg_max_len)
        {
            error("%s: new conn has smaller recv SG array size %d vs %d",
                  __func__, attr.cap.max_recv_sge, rd->sg_max_len);
            return -EINVAL;
        }
    }
    
    if (rd->max_unsignaled_sends == 0)
    {
        rd->max_unsignaled_sends = attr.cap.max_send_wr;
    }
    else
    {
        if (attr.cap.max_send_wr < rd->max_unsignaled_sends)
        {
            error("%s: new conn has smaller max_send_wr, %d vs %d",
                  __func__, attr.cap.max_send_wr, rd->max_unsignaled_sends);
            return -EINVAL;
        }
    }
    
    /* verify we got what we asked for */
    if ((int) attr.cap.max_recv_wr < num_wr)
    {
        error("%s: asked for %d recv WRs on QP, got %d",
              __func__, num_wr, attr.cap.max_recv_wr);
        return -EINVAL;
    }
    if ((int) attr.cap.max_send_wr < num_wr)
    {
        error("%s: asked for %d send WRs on QP, got %d",
              __func__, num_wr, attr.cap.max_send_wr);
        return -EINVAL;
    }
    
    /* TODO: don't need exchange data anymore. What else needs to change? */
    
    /* On the client, setup connection parameters and connect */
    if (!is_server)
    {
        /* TODO: is this all that should be inside if statement? */
        /* TODO: should build_conn_params() be outside the if so it happens
         *       on the server too?
         */
        
        build_conn_params(&conn_param);
        
retry:
        ret = rdma_connect(id, &conn_param);
        if (ret)
        {
            if (errno == EINTR)
            {
                goto retry;
            }
            else
            {
                ret = -errno;
                warning("%s: connect to server %s: %m", __func__, c->peername);
                return ret;
            }
        }
    }
    
    /* TODO: is this the right place to do this for the server, too? */
    /* wait until the connection is established */
    ret = rdma_get_cm_event(id->channel, &event);
    if (!ret)
    {
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        
        /* TODO: will this always happen first/right away? should we loop? */
        if (event_copy.event != RDMA_CM_EVENT_ESTABLISHED)
        {
            error("%s: unexpected event: %s",
                  __func__, rdma_event_str(event_copy.event));
            return -EINVAL;
        }
        
        debug(4, "%s: connected, peername=%s", __func__, c->peername);
    }
    else
    {
        ret = -errno;
        error("%s: rdma_get_cm_event failed: errno = %s",
              __func__, strerror(errno));
        /* TODO: could use error_errno() instead, but there might be a problem
         *       with that function (in util.c). Does vsprintf() not have the
         *       potential to alter errno?
         */
        return ret;
    }
    
    /* TODO: is there anything that was previously taken care of in
     * init_connection_modify_qp() that still needs to be done?
     *    - the local ack timeout (previously attr.timeout = 14 in
     *      init_connection_modify_qp) is set from the subnet_timeout value
     *      from opensm when using rdma_cm. It is currently set to 18. You
     *      can use the command opensm -c <config-file> to dump the current
     *      opensm configuration to a file.
     */
    
    /* post initial RRs and RRs for acks */
    debug(0, "%s: entering for loop for rdma_post_rr", __func__);
    for (i = 0; i < rdma_device->eager_buf_num; i++)
    {
        rdma_post_rr(c, &c->eager_recv_buf_head_contig[i]);
    }
    
    return 0;
}

/*
 * The queue pair is automatically transitioned through the required states 
 * by rdma_create_qp(), so it is ready to post receives. All we need to do 
 * here is modify attributes.
 *
 * TODO: is this needed anymore?
 */
//static void init_connection_modify_qp()
//{
//}

/*
 * Close the QP associated with this connection.
 *
 * TODO: does replacing this entire function with rdma_disconnect() achieve
 *       the desired result?
 */
//static void rdma_drain_qp(rdma_connect_t *c)
//{
//}

/*
 * disconnect()
 *
 * TODO: This function is just a temporary wrapper for rdma_disconnect! It
 *       won't be needed when this code is merged into rdma.c. However,
 *       for the time being, rdma_disconnect() must be called from this file
 *       in order to acess the ID through the rdma_connection_priv.
 *
 * Description:
 *  Transitions the QP to the error state, which flushes any posted work
 *  requests, and disconnect.
 *
 * Params:
 *  [in] c - connection to disconnect
 *
 * Returns:
 *  none
 */
static void disconnect(rdma_connection_t *c)
{
    struct rdma_connection_priv *rc = c->priv;
    rdma_disconnect(rc->id);
}

/*
 * rdma_close_connection()
 *
 * Description:
 *  At an explicit BYE message, or at finalize time, shut down a connection.
 *  If descriptors are posted, defer and clean up the connection structures
 *  later.
 *
 * Params:
 *  [in] c - pointer to the connection to close
 *
 * Returns:
 *  none
 */
static void rdma_close_connection(rdma_connection_t *c)
{
    int ret;
    struct rdma_connection_priv *rc = c->priv;
    struct rdma_cm_event *event = NULL;
    struct rdma_cm_event event_copy;
    struct rdma_event_channel *channel = NULL;
    
    /* disconnect (also transfers associated QP to error state) */
    rdma_disconnect(rc->id);
    
    /* TODO: is this loop necessary? do we need to wait for the event? */
    /* wait for disconnected event */
    while (rdma_get_cm_event(rc->id->channel, &event) == 0)
    {
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        
        if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED)
        {
            break;
        }
        
        /* TODO: should I compare event_copy.id and rc->id to make sure
         *       what we expected is actually what disconnected?
         */
    }
    
    /* destroy the queue pair */
    if (rc->qp)
    {
        rdma_destroy_qp(rc->id);
        if (rc->qp != NULL)
        {
            error("%s: rdma_destroy_qp", __func__);
            goto out;
        }
    }
    
    /* destroy the memory regions */
    if (rc->eager_send_mr)
    {
        ret = ibv_dereg_mr(rc->eager_send_mr);
        if (ret < 0)
        {
            error_xerrno(ret, "%s: ibv_dereg_mr eager send", __func__);
            goto out;
        }
    }
    
    if (rc->eager_recv_mr)
    {
        ret = ibv_dereg_mr(rc->eager_recv_mr);
        if (ret < 0)
        {
            error_xerrno(ret, "%s: ibv_dereg_mr eager recv", __func__);
            goto out;
        }
    }
    
    /* destroy the id and event channel */
    if (rc->id)
    {
        channel = rc->id->channel;
        
        ret = rdma_destroy_id(rc->id);
        if (ret < 0)
        {
            error_xerrno(ret, "%s: rdma_destroy_id", __func__);
            goto out;
        }
        
        rdma_destroy_event_channel(channel);
        channel = NULL;
    }
    
out:
    free(rc);
}

/*
 * rdma_post_sr()
 *
 * Description:
 *  Simplify RDMA interface to post sends. Not RDMA, just SEND.
 *  Called for an eager send, rts send, or cts send.
 *
 * Params:
 *  [in] bh  - pointer to the buf_head of the buffer being posted
 *  [in] len - length of buffer being posted
 *
 * Returns:
 *  none
 */
static void rdma_post_sr(const struct buf_head *bh,
                         u_int32_t len)
{
    rdma_connection_t *c = bh->c;
    struct rdma_connection_priv *rc = c->priv;
    struct rdma_device_priv *rd = rdma_device->priv;
    int ret;
    struct ibv_sge sg =
    {
        .addr = int64_from_ptr(bh->buf),
        .length = len,
        .lkey = rc->eager_send_mr->lkey,
    };
    struct ibv_send_wr sr =
    {
        .next = NULL,
        .wr_id = int64_from_ptr(bh),
        .sg_list = &sg,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,  /* XXX: try unsignaled if possible */
    };
    struct ibv_send_wr *bad_wr;
    
    debug(4, "%s: %s bh %d len %u wr %d/%d",
          __func__,
          c->peername,
          bh->num,
          len,
          rd->num_unsignaled_sends,
          rd->max_unsignaled_sends);
    
    if (rd->num_unsignaled_sends + 10 == rd->max_unsignaled_sends)
    {
        rd->num_unsignaled_sends = 0;
    }
    else
    {
        ++rd->num_unsignaled_sends;
    }
    
    ret = ibv_post_send(rc->qp, &sr, &bad_wr);
    if (ret < 0)
    {
        error("%s: ibv_post_send (%d)", __func__, ret);
        /* TODO: error handling? */
    }
}

/*
 * rdma_post_rr()
 *
 * Description:
 *  Post one of the eager recv bufs for this connection.
 *
 * Params:
 *  [in] c  - pointer to the connection
 *  [in] bh - pointer to the buf_head of the buffer being posted
 *
 * Returns:
 *  none
 */
static void rdma_post_rr(const rdma_connection_t *c,
                         struct buf_head *bh)
{
    struct rdma_connection_priv *rc = c->priv;
    int ret;
    struct ibv_sge sg =
    {
        .addr = int64_from_ptr(bh->buf),
        .length = rdma_device->eager_buf_size,
        .lkey = rc->eager_recv_mr->lkey,
    };
    struct ibv_recv_wr rr =
    {
        .wr_id = int64_from_ptr(bh),
        .sg_list = &sg,
        .num_sge = 1,
        .next = NULL,
    };
    struct ibv_recv_wr *bad_wr;
    
    debug(4, "%s: %s bh %d", __func__, c->peername, bh->num);
    
    ret = ibv_post_recv(rc->qp, &rr, &bad_wr);
    if (ret)
    {
        error("%s: ibv_post_recv", __func__);
        /* TODO: error handling? */
    }
}

/*
 * rdma_post_sr_rdmaw()
 *
 * Description:
 *  Called only in response to receipt of a CTS on the sender.  RDMA write
 *  the big data to the other side.  A bit messy since an RDMA write may
 *  not scatter to the receiver, but can gather from the sender, and we may
 *  have a non-trivial buflist on both sides.  The mh_cts variable length
 *  fields must be decoded as we go.
 *
 * Params:
 *  [in] sq         - pointer to send work request
 *  [in] mh_cts     - pointer to MSG_CTS header
 *  [in] mh_cts_buf - pointer to receive buffer
 *
 * Returns:
 *  none
 */
static void rdma_post_sr_rdmaw(struct rdma_work *sq,
                               msg_header_cts_t *mh_cts,
                               void *mh_cts_buf)
{
    rdma_connection_t *c = sq->c;
    struct rdma_connection_priv *rc = c->priv;
    struct rdma_device_priv *rd = rdma_device->priv;
    struct ibv_send_wr sr;
    int done;
    
    int send_index = 0, recv_index = 0; /* working entry in buflist */
    int send_offset = 0;        /* byte offset in working send entry */
    u_int64_t *recv_bufp = (u_int64_t *) mh_cts_buf;
    u_int32_t *recv_lenp = (u_int32_t *) (recv_bufp + mh_cts->buflist_num);
    /* TODO: how is this the rkey? shouldn't the rkey be random and secure? */
    u_int32_t *recv_rkey = (u_int32_t *) (recv_lenp + mh_cts->buflist_num);
    u_int32_t recv_bytes_needed = 0;
    
    debug(2, "%s: sq %p totlen %d", __func__, sq, (int) sq->buflist.tot_len);
    
    /* constant things for every send */
    memset(&sr, 0, sizeof(sr));
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.sg_list = rd->sg_tmp_array;
    sr.next = NULL;
    
    done = 0;
    while (!done)
    {
        int ret;
        struct ibv_send_wr *bad_wr;
        
        if (recv_bytes_needed == 0)
        {
            /* new one, fresh numbers */
            sr.wr.rdma.remote_addr = bmitoh64(recv_bufp[recv_index]);
            recv_bytes_needed = bmitoh32(recv_lenp[recv_index]);
        }
        else
        {
            /* continuing into unfinished remote receive index */
            sr.wr.rdma.remote_addr += bmitoh32(recv_lenp[recv_index]) -
                                      recv_bytes_needed;
        }
        
        sr.wr.rdma.rkey = bmitoh32(recv_rkey[recv_index]);
        sr.num_sge = 0;
        
        debug(0, "%s: chunk to %s remote addr %llx rkey %x",
              __func__,
              c->peername,
              llu(sr.wr.rdma.remote_addr),
              sr.wr.rdma.rkey);
        
        /* Driven by recv elements.  Sizes have already been checked. */
        while (recv_bytes_needed > 0 && sr.num_sge < (int) rd->sg_max_len)
        {
            /* consume from send buflist to fill this one receive */
            u_int32_t send_bytes_offered = sq->buflist.len[send_index] -
                                           send_offset;
            u_int32_t this_bytes = send_bytes_offered;
            
            if (this_bytes > recv_bytes_needed)
            {
                this_bytes = recv_bytes_needed;
            }
            
            rd->sg_tmp_array[sr.num_sge].addr =
                    int64_from_ptr(sq->buflist.buf.send[send_index]) +
                    send_offset;
            rd->sg_tmp_array[sr.num_sge].length = this_bytes;
            rd->sg_tmp_array[sr.num_sge].lkey =
                    sq->buflist.memcache[send_index]->memkeys.lkey;
            
            debug(0, "%s: chunk %d local addr %llx len %d lkey %x",
                  __func__,
                  sr.num_sge,
                  (unsigned long long) rd->sg_tmp_array[sr.num_sge].addr,
                  rd->sg_tmp_array[sr.num_sge].length,
                  rd->sg_tmp_array[sr.num_sge].lkey);
            
            ++sr.num_sge;
            
            send_offset += this_bytes;
            if (send_offset == sq->buflist.len[send_index])
            {
                ++send_index;
                send_offset = 0;
                if (send_index == sq->buflist.num)
                {
                    done = 1;
                    break;      /* short send */
                }
            }
            recv_bytes_needed -= this_bytes;
        }
        
        /* done with the one we were just working on, is this the last recv? */
        if (recv_bytes_needed == 0)
        {
            ++recv_index;
            if (recv_index == (int) mh_cts->buflist_num)
            {
                done = 1;
            }
        }
        
        /* either filled the recv or exhausted the send */
        if (done)
        {
            sr.wr_id = int64_from_ptr(sq);     /* used to match in completion */
            sr.send_flags = IBV_SEND_SIGNALED; /* completion drives the unpin */
        }
        else
        {
            sr.wr_id = 0;
            sr.send_flags = 0;
        }
        
        ret = ibv_post_send(rc->qp, &sr, &bad_wr);
        if (ret < 0)
        {
            error("%s: ibv_post_send (%d)", __func__, ret);
            return;
        }
    }
}

/*
 * rdma_check_cq()
 *
 * Description: 
 *  Check for any completed work requests
 *
 * Params:
 *  [out] wc - pointer to work completion, if one was found
 *
 * Returns:
 *  0 if cq empty or if unhandled opcode found, 1 if completion found,
 *  otherwise -errno returned
 */
static int rdma_check_cq(struct bmi_rdma_wc *wc)
{
    struct rdma_device_priv *rd = rdma_device->priv;
    struct ibv_wc desc;
    int ret;
    
    memset(&desc, 0, sizeof(desc));
    
    /* poll the queue for a single completion */
    ret = ibv_poll_cq(rd->nic_cq, 1, &desc);
    if (ret < 0)
    {
        error("%s: ibv_poll_cq (%d)", __func__, ret);
        return -EINVAL;
    }
    else if (ret == 0)
    {
        /* empty */
        return 0;
    }
    
    /* convert to generic form */
    wc->id = desc.wr_id;
    wc->status = desc.status;
    wc->byte_len = desc.byte_len;
    switch (desc.opcode)
    {
        case IBV_WC_SEND:
            wc->opcode = BMI_RDMA_OP_SEND;
            break;
            
        case IBV_WC_RECV:
            wc->opcode = BMI_RDMA_OP_RECV;
            break;
            
        case IBV_WC_RDMA_WRITE:
            wc->opcode = BMI_RDMA_OP_RDMA_WRITE;
            break;
            
        case IBV_WC_RDMA_READ:
            warning("%s: unhandled IBV_WC_RDMA_READ opcode, id %llx status %d \
                    opcode %d",
                    __func__,
                    llu(desc.wr_id),
                    desc.status,
                    desc.opcode);
            warning("%s: vendor_err %d byte_len %d imm_data %d qp_num %d",
                    __func__,
                    desc.vendor_err,
                    desc.byte_len,
                    desc.imm_data,
                    desc.qp_num);
            warning("%s: src_qp %d wc_flags %d pkey_index %d slid %d",
                    __func__,
                    desc.src_qp,
                    desc.wc_flags,
                    desc.pkey_index,
                    desc.slid);
            warning("%s: sl %d dlid_path_bits %d",
                    __func__,
                    desc.sl,
                    desc.dlid_path_bits);
            return 0;
            
        case IBV_WC_COMP_SWAP:
            warning("%s: unhandled IBV_WC_COMP_SWAP opcode, id %llx status %d \
                    opcode %d",
                    __func__,
                    llu(desc.wr_id),
                    desc.status,
                    desc.opcode);
            warning("%s: vendor_err %d byte_len %d imm_data %d qp_num %d",
                    __func__,
                    desc.vendor_err,
                    desc.byte_len,
                    desc.imm_data,
                    desc.qp_num);
            warning("%s: src_qp %d wc_flags %d pkey_index %d slid %d",
                    __func__,
                    desc.src_qp,
                    desc.wc_flags,
                    desc.pkey_index,
                    desc.slid);
            warning("%s: sl %d dlid_path %d",
                    __func__,
                    desc.sl,
                    desc.dlid_path_bits);
            return 0;
            
        case IBV_WC_FETCH_ADD:
            warning("%s: unhandled IBV_WC_FETCH_ADD opcode, id %llx status %d \
                    opcode %d",
                    __func__,
                    llu(desc.wr_id),
                    desc.status,
                    desc.opcode);
            warning("%s: vendor_err %d byte_len %d imm_data %d qp_num %d",
                    __func__,
                    desc.vendor_err,
                    desc.byte_len,
                    desc.imm_data,
                    desc.qp_num);
            warning("%s: src_qp %d wc_flags %d pkey_index %d slid %d",
                    __func__,
                    desc.src_qp,
                    desc.wc_flags,
                    desc.pkey_index,
                    desc.slid);
            warning("%s: sl %d dlid_path_bits %d",
                    __func__,
                    desc.sl,
                    desc.dlid_path_bits);
            return 0;
            
        case IBV_WC_BIND_MW:
            warning("%s: unhandled IBV_WC_BIND_MW opcode, id %llx status %d \
                    opcode %d",
                    __func__,
                    llu(desc.wr_id),
                    desc.status,
                    desc.opcode);
            warning("%s: vendor_err %d byte_len %d imm_data %d qp_num %d",
                    __func__,
                    desc.vendor_err,
                    desc.byte_len,
                    desc.imm_data,
                    desc.qp_num);
            warning("%s: src_qp %d wc_flags %d pkey_index %d slid %d",
                    __func__,
                    desc.src_qp,
                    desc.wc_flags,
                    desc.pkey_index,
                    desc.slid);
            warning("%s: sl %d dlid_path_bits %d",
                    __func__,
                    desc.sl,
                    desc.dlid_path_bits);
            return 0;
            
        default:
            warning("%s: unknown wc opcode, id %llx status %d opcode %d",
                    __func__, llu(desc.wr_id),
                    desc.status,
                    desc.opcode);
            warning("%s: vendor_err %d byte_len %d imm_data %d qp_num %d",
                    __func__,
                    desc.vendor_err,
                    desc.byte_len,
                    desc.imm_data,
                    desc.qp_num);
            warning("%s: src_qp %d wc_flags %d pkey_index %d slid %d",
                    __func__,
                    desc.src_qp,
                    desc.wc_flags,
                    desc.pkey_index,
                    desc.slid);
            warning("%s: sl %d dlid_path_bits %d",
                    __func__,
                    desc.sl,
                    desc.dlid_path_bits);
            return 0;
    }
    VALGRIND_MAKE_MEM_DEFINED(wc, sizeof(*wc));
    
    return 1;
}

/*
 * rdma_prepare_cq_block()
 *
 * Description:
 *  Get the RDMA device's completion queue ready to block for activity by 
 *  requesting a completion notification on the completion queue and returning
 *  the file descriptors that need to be polled
 *
 * Params:
 *  [out] cq_fd    - pointer to file descriptor for RDMA device's comp. channel
 *  [out] async_fd - pointer to asynchronous file descriptor for RDMA 
 *                   device context
 *
 * Returns:
 *  none
 */
static void rdma_prepare_cq_block(int *cq_fd,
                                  int *async_fd)
{
    struct rdma_device_priv *rd = rdma_device->priv;
    int ret;
    
    /* ask for the next notification */
    ret = ibv_req_notify_cq(rd->nic_cq, 0);
    if (ret < 0)
    {
        error_xerrno(ret, "%s: ibv_req_notify_cq", __func__);
        return;
    }
    
    /* return the fd that can be fed to poll() */
    *cq_fd = rd->channel->fd;
    *async_fd = rd->ctx->async_fd;
}

/*
 * rdma_ack_cq_completion_event()
 *
 * Description:
 *  As poll says there is something to read, get the event, but
 *  ignore the contents as we only have one CQ. But ack it
 *  so that the count is correct and the CQ can be shutdown later.
 *
 * Params:
 *  none
 *
 * Returns:
 *  none
 */
static void rdma_ack_cq_completion_event(void)
{
    struct rdma_device_priv *rd = rdma_device->priv;
    struct ibv_cq *cq;
    void *cq_context;
    int ret;
    
    ret = ibv_get_cq_event(rd->channel, &cq, &cq_context);
    if (ret == 0)
    {
        ibv_ack_cq_events(cq, 1);
    }
    debug(0, "%s: ibv_get_cq_event ret=%d", __func__, ret);
}

#define CASE(e)  case e: s = #e; break

/*
 * rdma_wc_status_string()
 *
 * Description:
 *  Return string form of work completion status field.
 *
 * Params:
 *  [in] status - integer value of work completion status (ibv_wc_status)
 *
 * Returns:
 *  String representation of status
 */
static const char *rdma_wc_status_string(int status)
{
    const char *s = "(UNKNOWN)";
    
    switch (status)
    {
        CASE(IBV_WC_SUCCESS);
        CASE(IBV_WC_LOC_LEN_ERR);
        CASE(IBV_WC_LOC_QP_OP_ERR);
        CASE(IBV_WC_LOC_EEC_OP_ERR);
        CASE(IBV_WC_LOC_PROT_ERR);
        CASE(IBV_WC_WR_FLUSH_ERR);
        CASE(IBV_WC_MW_BIND_ERR);
        CASE(IBV_WC_BAD_RESP_ERR);
        CASE(IBV_WC_LOC_ACCESS_ERR);
        CASE(IBV_WC_REM_INV_REQ_ERR);
        CASE(IBV_WC_REM_ACCESS_ERR);
        CASE(IBV_WC_REM_OP_ERR);
        CASE(IBV_WC_RETRY_EXC_ERR);
        CASE(IBV_WC_RNR_RETRY_EXC_ERR);
        CASE(IBV_WC_LOC_RDD_VIOL_ERR);
        CASE(IBV_WC_REM_INV_RD_REQ_ERR);
        CASE(IBV_WC_REM_ABORT_ERR);
        CASE(IBV_WC_INV_EECN_ERR);
        CASE(IBV_WC_INV_EEC_STATE_ERR);
        CASE(IBV_WC_FATAL_ERR);
        CASE(IBV_WC_RESP_TIMEOUT_ERR);
        CASE(IBV_WC_GENERAL_ERR);
    }
    
    return s;
}

/*
 * rdma_wc_status_to_bmi()
 *
 * Description:
 *  Convert an IBV work completion status into a BMI error code.
 *
 *  Params:
 *   [in] status - integer value of work completion status (ibv_wc_status)
 *
 *  Returns:
 *   Integer value representing appropriate BMI error code
 */
static int rdma_wc_status_to_bmi(int status)
{
    int result = 0;

    switch (status)
    {
        case IBV_WC_SUCCESS;
            result = 0;
            break;

        case IBV_WC_RETRY_EXC_ERR:
            debug(0, "%s: converting IBV_WC_RETRY_EXC_ERR to BMI_EHOSTUNREACH",
                  __func__);
            result = -BMI_EHOSTUNREACH;
            break;

        default:
            warning("%s: unhandled wc status %s, error code unchanged",
                    __func__, rdma_wc_status_string(status));
            result = status;
            break;
    }

    return result;
}

/*
 * async_event_type_string()
 *
 * Description:
 *  Return string form of Infiniband verbs asynchronous event type.
 *
 * Params:
 *  [in] event_type - integer value of asynchronous event type (ibv_event_type)
 *
 * Returns:
 *  String representation of event_type
 */
static const char *async_event_type_string(enum ibv_event_type event_type)
{
    const char *s = "(UNKNOWN)";
    
    switch (event_type)
    {
            CASE(IBV_EVENT_CQ_ERR);
            CASE(IBV_EVENT_QP_FATAL);
            CASE(IBV_EVENT_QP_REQ_ERR);
            CASE(IBV_EVENT_QP_ACCESS_ERR);
            CASE(IBV_EVENT_COMM_EST);
            CASE(IBV_EVENT_SQ_DRAINED);
            CASE(IBV_EVENT_PATH_MIG);
            CASE(IBV_EVENT_PATH_MIG_ERR);
            CASE(IBV_EVENT_DEVICE_FATAL);
            CASE(IBV_EVENT_PORT_ACTIVE);
            CASE(IBV_EVENT_PORT_ERR);
            CASE(IBV_EVENT_LID_CHANGE);
            CASE(IBV_EVENT_PKEY_CHANGE);
            CASE(IBV_EVENT_SM_CHANGE);
            CASE(IBV_EVENT_SRQ_ERR);
            CASE(IBV_EVENT_SRQ_LIMIT_REACHED);
            CASE(IBV_EVENT_QP_LAST_WQE_REACHED);
            CASE(IBV_EVENT_CLIENT_REREGISTER);
            CASE(IBV_EVENT_GID_CHANGE);
            
            /* Experimental event types */
#ifdef HAVE_IBV_EXP_EVENT_DCT_KEY_VIOLATION
            CASE(IBV_EXP_EVENT_DCT_KEY_VIOLATION);
#endif
#ifdef HAVE_IBV_EXP_EVENT_DCT_ACCESS_ERR
            CASE(IBV_EXP_EVENT_DCT_ACCESS_ERR);
#endif
#ifdef HAVE_IBV_EXP_EVENT_DCT_REQ_ERR
            CASE(IBV_EXP_EVENT_DCT_REQ_ERR);
#endif
    }
    
    return s;
}

#undef CASE

/*
 * rdma_mem_register(), rdma_mem_deregister()
 *
 * Description:
 *  Memory registration and deregistration.  Used both by sender and
 *  receiver, vary if lkey or rkey = 0.
 *
 *  Pain because a s/g list requires lots of little allocations.  Needs
 *  wuj's clever discontig allocation stuff.
 *
 *  These two must be called holding the interface mutex since they
 *  make IB calls and that these may or may not be threaded under the hood.
 *
 * Params:
 *  [in/out] c - pointer to entry to register/deregister;
 *               c->memkeys info set on exit from register
 *
 * Returns:
 *  register   - 0 on success, -errno on failure
 *  deregister - none
 */
static int rdma_mem_register(memcache_entry_t *c)
{
    struct ibv_mr *mrh;
    struct rdma_device_priv *rd = rdma_device->priv;
    int tries = 0;
    
retry:
    mrh = ibv_reg_mr(rd->nic_pd,
                     c->buf,
                     c->len,
                     IBV_ACCESS_LOCAL_WRITE
                        | IBV_ACCESS_REMOTE_WRITE
                        | IBV_ACCESS_REMOTE_READ);
    
    if (!mrh && (errno == ENOMEM && tries < 1))
    {
        ++tries;
        
        /* Try to flush some cached entries, then try again. */
        memcache_cache_flush(rdma_device->memcache);
        goto retry;
    }
    
    if (!mrh)
    {
        /* Die horribly. Need registered memory. */
        warning("%s: ibv_reg_mr", __func__);
        return -errno;
    }
    
    c->memkeys.mrh = int64_from_ptr(mrh);   /* convert pointer to 64-bit int */
    c->memkeys.lkey = mrh->lkey;
    c->memkeys.rkey = mrh->rkey;
    
    debug(4, "%s: buf %p len %lld lkey %x rkey %x",
          __func__,
          c->buf,
          lld(c->len),
          c->memkeys.lkey,
          c->memkeys.rkey);
    
    return 0;
}

static void rdma_mem_deregister(memcache_entry_t *c)
{
    int ret;
    struct ibv_mr *mrh;
    
    mrh = ptr_from_int64(c->memkeys.mrh);   /* convert 64-bit int to pointer */
    ret = ibv_dereg_mr(mrh);
    if (ret)
    {
        error_xerrno(ret, "%s: ibv_dereg_mr", __func__);
        return;
    }
    
    debug(4, "%s: buf %p len %lld lkey %x rkey %x",
          __func__,
          c->buf,
          lld(c->len),
          c->memkeys.lkey,
          c->memkeys.rkey);
}

/* TODO: not needed anymore */
#if 0
//static struct ibv_device *get_nic_handle(void)
//{
//    struct ibv_device *nic_handle;
//    struct ibv_context **dev_list;
//    int num_devs;
//    
//    dev_list = rdma_get_devices(&num_devs);
//    if (num_devs == 0)
//    {
//        return NULL;
//    }
//    if (num_devs > 1)
//    {
//        warning("%s: found %d HCAs, choosing the first", __func__, num_devs);
//    }
//    
//    nic_handle = dev_list[0]->device;
//    rdma_free_devices(dev_list);
//    
//    return nic_handle;
//}
#endif

/*
 * rdma_check_async_events()
 *
 * Description:
 *  Check for an asynchronous event on the RDMA device context and ack it.
 *
 * Params:
 *  none
 *
 * Returns:
 *  0 if no event, 1 if an event was found, -errno on failure
 */
static int rdma_check_async_events(void)
{
    struct rdma_device_priv *rd = rdma_device->priv;
    int ret;
    struct ibv_async_event ev;
    
    ret = ibv_get_async_event(rd->ctx, &ev);
    if (ret < 0)
    {
        if (errno == EAGAIN)
        {
            return 0;
        }
        
        error_errno("%s: ibv_get_async_event", __func__);
        return -EINVAL;
    }
    
    debug(0, "%s: %s", __func__, async_event_type_string(ev.event_type));
    ibv_ack_async_event(&ev);
    return 1;
}

/* 
 * return_active_nic_handle()
 *
 * Description:
 *  This function returns the first active HCA device which returns a 
 *  valid IBV_PORT_ACTIVE.
 *
 * Params:
 *  [out] rd - preallocated from int_rdma_initialize(); rd->ctx is allocated 
 *             by rdma_get_devices() inside this function
 *  [out] hca_port - hca port attributes
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int return_active_nic_handle(struct rdma_device_priv *rd,
                                    struct ibv_port_attr *hca_port)
{
    int ret = 0, i = 0;
    struct ibv_device *nic_handle = NULL;
    struct ibv_context **dev_list;
    int num_devs = 0;
    struct ibv_context *ctx;
    
    /* make this configurable once we decide how
     * adding more than one HCA REALLY complicates the configurable
     * nature that we had discussed */
    rd->nic_port = IBV_PORT;
    
    dev_list = rdma_get_devices(&num_devs);
    if (num_devs <= 0)
    {
        error("%s : NO RDMA DEVICES FOUND ", __func__);
        return -ENOSYS;
    }
    else
    {
        /* return a device which is active */
        for (i = 0; i < num_devs; i++)
        {
            nic_handle = dev_list[i]->device;
            
            /* test the device to see if active */
            ctx = NULL;
            ctx = dev_list[i];
            rd->ctx = ctx;
            if (!rd->ctx || ctx == NULL || !ctx)
            {
                error("%s: rdma_get_devices", __func__);
                return -ENOSYS;
            }
            
            /* TODO: is ibv_query_port supported by RoCE/RDMA */
            ret = ibv_query_port(ctx, rd->nic_port, hca_port);
            if (ret)
            {
                error_xerrno(ret, "%s: ibv_query_port", __func__);
                return -ENOSYS;
            }
            
            if (hca_port->state != IBV_PORT_ACTIVE)
            {
                /* in this case, continue, delete old hca_port info */
                memset(hca_port, 0, sizeof(*hca_port));
                warning("%s: found an inactive device/port", __func__);
                
                /* if we get to num_devs, no valid devices found */
                if (i == (num_devs - 1))
                {
                    /* FATAL */
                    warning("%s: No Active IB ports/devices found", __func__);
                    return -ENOSYS;
                }
                
                continue;
            }
            else
            {
                /* if we get here, we had a valid device found,
                 * done searching */
                rd->max_mtu = hca_port->max_mtu;
                rd->active_mtu = hca_port->active_mtu;
                break;
            }
        }
    }
    
    VALGRIND_MAKE_MEM_DEFINED(ctx, sizeof(*ctx));
    
    /* cleanup */
    rdma_free_devices(dev_list);
    return 0;
}

/*
 * int_rdma_initialize()
 * 
 * Description:
 *  Startup, once per application. Equivalent of openib_ib_initialize().
 *
 * Params:
 *  none
 * 
 * Returns:
 *  0 on success, -errno on failure
 */
int int_rdma_initialize(void)
{
    /* TODO: do I need to lock/unlock the mutex before/after each error()
     *       call in this function? See note in BMI_rdma_initialize()
     */
    
    int flags, ret = 0;
    //struct ibv_device *nic_handle;
    //struct ibv_context *ctx;
    int cqe_num;    /* local variables, mainly for debug */
    struct rdma_device_priv *rd;
    struct ibv_port_attr hca_port;  /* TODO: compatible? */
    struct ibv_device_attr hca_cap;
    
    rd = malloc(sizeof(*rd));
    if (!rd)
    {
        /* TODO: is this the best way to handle it? */
        error("%s: malloc %ld bytes failed", __func__, sizeof(*rd));
        /* TODO: return? exit? */
    }
    rdma_device->priv = rd;
    
    ret = return_active_nic_handle(rd, &hca_port);
    if (ret)
    {
        return -ENOSYS;
    }
    
    /* TODO: is the nic_lid field still needed? */
    //rd->nic_lid = hca_port.lid;
    
    /* Query the device for the max_ requests and such */
    ret = ibv_query_device(rd->ctx, &hca_cap);
    if (ret)
    {
        error_xerrno(ret, "%s: ibv_query_device", __func__);
        return -ENOSYS;
    }
    VALGRIND_MAKE_MEM_DEFINED(&hca_cap, sizeof(hca_cap));
    
    /* set the function pointers for rdma */
    rdma_device->func.new_connection = rdma_new_connection;
    rdma_device->func.close_connection = rdma_close_connection;
    //rdma_device->func.drain_qp = rdma_drain_qp;
    rdma_device->func.disconnect = disconnect;
    rdma_device->func.rdma_initialize = int_rdma_initialize;
    rdma_device->func.rdma_finalize = rdma_finalize;
    rdma_device->func.post_sr = rdma_post_sr;
    rdma_device->func.post_rr = rdma_post_rr;
    rdma_device->func.post_sr_rdmaw = rdma_post_sr_rdmaw;
    rdma_device->func.check_cq = rdma_check_cq;
    rdma_device->func.prepare_cq_block = rdma_prepare_cq_block;
    rdma_device->func.ack_cq_completion_event = rdma_ack_cq_completion_event;
    rdma_device->func.wc_status_string = rdma_wc_status_string;
    rdma_device->func.wc_status_to_bmi = rdma_wc_status_to_bmi;
    rdma_device->func.mem_register = rdma_mem_register;
    rdma_device->func.mem_deregister = rdma_mem_deregister;
    rdma_device->func.check_async_events = rdma_check_async_events;
    
    debug(1, "%s: max %d completion queue entries", __func__, hca_cap.max_cq);
    cqe_num = IBV_NUM_CQ_ENTRIES;
    rd->nic_max_sge = hca_cap.max_sge;
    rd->nic_max_wr = hca_cap.max_qp_wr;
    
    if (hca_cap.max_cq < cqe_num)
    {
        cqe_num = hca_cap.max_cq;
        warning("%s: hardly enough completion queue entries %d, hoping for %d",
                __func__, hca_cap.max_cq, IBV_NUM_CQ_ENTRIES);
    }
    
    /* Allocate a Protection Domain (global) */
    rd->nic_pd = ibv_alloc_pd(rd->ctx);
    if (!rd->nic_pd)
    {
        error("%s: ibv_alloc_pd failed", __func__);
        return -ENOMEM;
    }
    
    /* Create a completion channel for blocking on CQ events */
    rd->channel = ibv_create_comp_channel(rd->ctx);
    if (!rd->channel)
    {
        error("%s: ibv_create_comp_channel failed", __func__);
        return -EINVAL;
    }
    
    /* Build a CQ (global), connected to this channel */
    rd->nic_cq = ibv_create_cq(rd->ctx, cqe_num, NULL, rd->channel, 0);
    if (!rd->nic_cq)
    {
        error("%s: ibv_create_cq failed", __func__);
        return -EINVAL;
    }
    
    /* Use non-blocking IO on the async fd and completion fd */
    flags = fcntl(rd->ctx->async_fd, F_GETFL);
    if (flags < 0)
    {
        error_errno("%s: get async fd flags", __func__);
        return -EINVAL;
    }
    
    if (fcntl(rd->ctx->async_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        error_errno("%s: set async fd nonblocking", __func__);
        return -EINVAL;
    }
    
    flags = fcntl(rd->channel->fd, F_GETFL);
    if (flags < 0)
    {
        error_errno("%s: get completion fd flags", __func__);
        return -EINVAL;
    }
    if (fcntl(rd->channel->fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        error_errno("%s: set completion fd nonblocking", __func__);
        return -EINVAL;
    }
    
    /* will be set on first connection */
    rd->sg_tmp_array = 0;
    rd->sg_max_len = 0;
    rd->num_unsignaled_sends = 0;
    rd->max_unsignaled_sends = 0;
    
    return 0;
}

/*
 * rdma_finalize()
 *
 * Description:
 *  Shutdown.
 *
 * Params:
 *  none
 *
 * Returns:
 *  none
 */
static void rdma_finalize(void)
{
    struct rdma_device_priv *rd = rdma_device->priv;
    int ret;
    
    if (rd->sg_tmp_array)
    {
        free(rd->sg_tmp_array);
    }
    
    ret = ibv_destroy_cq(rd->nic_cq);
    if (ret)
    {
        error_xerrno(ret, "%s: ibv_destroy_cq", __func__);
        goto out;
    }
    
    ret = ibv_destroy_comp_channel(rd->channel);
    if (ret)
    {
        error_xerrno(ret, "%s: ibv_destroy_comp_channel", __func__);
        goto out;
    }
    
    ret = ibv_dealloc_pd(rd->nic_pd);
    if (ret)
    {
        error_xerrno(ret, "%s: ibv_dealloc_pd", __func__);
        goto out;
    }
    
    /* TODO: How do I close the rdma_device/free rd->ctx? Do I need to? */
    
out:
    free(rd);
    rdma_device->priv = NULL;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

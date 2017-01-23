/*
 * RDMA BMI method.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2006 Kyle Schochenmaier <kschoche@scl.ameslab.gov>
 * Copyright (C) 2016 David Reynolds <david@omnibond.com>
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <arpa/inet.h>    /* inet_ntoa */
#include <sys/poll.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* include definitions */
#include <src/common/id-generator/id-generator.h>
#include <src/io/bmi/bmi-method-support.h>   /* bmi_method_ops ... */
#include <src/io/bmi/bmi-method-callback.h>  /* bmi_method_addr_reg_callback */
#include <src/common/gen-locks/gen-locks.h> /* gen_mutex_t ... */
#include <src/common/misc/pvfs2-internal.h>
#include <pthread.h>
#include "pint-hint.h"

#ifdef HAVE_VALGRIND_H
#   include <memcheck.h>
#else
#   define VALGRIND_MAKE_MEM_DEFINDED(addr, len)
#endif

#include "rdma.h"

static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;

/*
 * Handle given by upper layer, which must be handed back to create
 * method_addrs.
 */
static int bmi_rdma_method_id;

/*
 * Alloc space for the single device structure pointer;
 */
rdma_device_t *rdma_device __hidden = NULL;

/* RDMA CM listen backlog */
static int listen_backlog = 16384;

/* accept thread variables */
static gen_mutex_t accept_thread_mutex = GEN_MUTEX_INITIALIZER;
int accept_thread_shutdown = 0;
gen_thread_t accept_thread_id;
int accept_timeout_ms = 2000;

/* these all vector through the rdma_device */
#define new_connection rdma_device->func.new_connection
#define close_connection rdma_device->func.close_connection
//#define drain_qp rdma_device->func.drain_qp
#define disconnect rdma_device->func.disconnect
#define rdma_initialize rdma_device->func.rdma_initialize
#define rdma_finalize rdma_device->func.rdma_finalize
#define post_sr rdma_device->func.post_sr
#define post_sr_rdmaw rdma_device->func.post_sr_rdmaw
#define check_cq rdma_device->func.check_cq
#define prepare_cq_block rdma_device->func.prepare_cq_block
#define ack_cq_completion_event rdma_device->func.ack_cq_completion_event
#define wc_status_string rdma_device->func.wc_status_string
#define mem_register rdma_device->func.mem_register
#define mem_deregister rdma_device->func.mem_deregister
#define check_async_events rdma_device->func.check_async_events

static void encourage_send_incoming_cts(struct buf_head *bh,
                                        u_int32_t byte_len);
static void encourage_recv_incoming(struct buf_head *bh,
                                    msg_type_t type,
                                    u_int32_t byte_len);
static void encourage_rts_done_waiting_buffer(struct rdma_work *sq);
static int send_cts(struct rdma_work *rq);
static void rdma_close_connection(rdma_connection_t *c);
static int rdma_client_event_loop(struct rdma_event_channel *ec,
                                  rdma_method_addr_t *rdma_map,
                                  struct bmi_method_addr *remote_map,
                                  int timeout_ms);
static int rdma_client_connect(rdma_method_addr_t *rdma_map,
                               struct bmi_method_addr *remote_map);
static int rdma_block_for_activity(int timeout_ms);
void *rdma_server_accept_thread(void *arg);
void *rdma_server_process_client_thread(void *arg);


/* structure to hold RDMA connection info passed to the accept thread */
/*
 * TODO: do I even need all of this anymore? Isn't the point of rdma_cm to
 *       avoid having to pass info back and forth in order to setup the
 *       connection?
 */
struct rdma_conn
{
    struct rdma_cm_id *id;  /* connection identifier */
    int port;               /* peer's port number */
    char *hostname;         /* peer's addr */
    char peername[2048];
};


/*
 * wc_opcode_string()
 *
 * Description:
 *  Return string form of work completion opcode field.
 *
 * Params:
 *  [in] opcode - integer value of work completion opcode (ibv_wc_opcode)
 *
 * Returns:
 *  String representation of opcode.
 */
static const char *wc_opcode_string(int opcode)
{
    if (opcode == BMI_RDMA_OP_SEND)
    {
        return "SEND";
    }
    else if (opcode == BMI_RDMA_OP_RECV)
    {
        return "RECV";
    }
    else if (opcode == BMI_RDMA_OP_RDMA_WRITE)
    {
        return "RDMA WRITE";
    }
    else
    {
        return "(UNKNOWN)";
    }
}

/*
 * rdma_check_cq()
 *
 * Description:
 *  Wander through single completion queue, pulling off messages and
 *  sticking them on the proper connection queues.  Later you can
 *  walk the incomingq looking for things to do to them.
 * 
 * Params:
 *  none
 *
 * Returns:
 *  Number of new things that arrived.
 */
static int rdma_check_cq(void)
{
    int ret = 0;
    struct buf_head *bh = NULL;
    struct rdma_work *sq = NULL;
    
    for (;;)
    {
        struct bmi_rdma_wc wc;
        int vret;
        
        bh = NULL;
        sq = NULL;
        
        vret = check_cq(&wc);
        if (vret == 0 || wc.id == 0)
        {
            break;  /* empty */
        }
        
        debug(4, "%s: found something", __func__);
        
        if (wc.status != 0)
        {
            bh = ptr_from_int64(wc.id);
            
            /* opcode is not necessarily valid; only wr_id, status, qp_num,
             * and vendor_err can be relied upon */
            if (wc.opcode == BMI_RDMA_OP_SEND)
            {
                debug(0, "%s: entry id 0x%llx SEND error %s to %s",
                      __func__,
                      llu(wc.id),
                      wc_status_string(wc.status),
                      bh->c->peername);
                
                if (wc.id)
                {
                    rdma_connection_t *c = ptr_from_int64(wc.id);
                    if (c->cancelled)
                    {
                        debug(0,
                              "%s: ignoring send error on cancelled conn to %s",
                              __func__, bh->c->peername);
                    }
                }
            }
            else
            {
                warning("%s: entry id 0x%llx opcode %s error %s from %s",
                        __func__,
                        llu(wc.id),
                        wc_opcode_string(wc.opcode),
                        wc_status_string(wc.status),
                        bh->c->peername);
            }
            continue;
        }
        
        if (wc.opcode == BMI_RDMA_OP_RECV)
        {
            /* Remote side did a send to us */
            msg_header_common_t mh_common;
            u_int32_t byte_len = wc.byte_len;
            char *ptr = NULL;
            
            bh = ptr_from_int64(wc.id);
            if (!bh)
            {
                continue;
            }
            ptr = bh->buf;
            
            VALGRIND_MAKE_MEM_DEFINDED(ptr, byte_len);
            decode_msg_header_common_t(&ptr, &mh_common);
            bh->c->send_credit += mh_common.credit;
            
            debug(2, "%s: recv from %s len %d type %s credit %d",
                  __func__,
                  bh->c->peername,
                  byte_len,
                  msg_type_name(mh_common.type),
                  mh_common.credit);
            
            if (mh_common.type == MSG_CTS)
            {
                /* incoming CTS messages go to the send engine */
                encourage_send_incoming_cts(bh, byte_len);
            }
            else
            {
                /* something for the recv side, no known rq yet */
                encourage_recv_incoming(bh, mh_common.type, byte_len);
            }
        }
        else if (wc.opcode == BMI_RDMA_OP_RDMA_WRITE)
        {
            /* completion event for the rdma write we initiated, used
             * to signal memory unpin etc. */
            sq = ptr_from_int64(wc.id);
            
            sq->state.send = SQ_WAITING_RTS_DONE_BUFFER;
            
            memcache_deregister(rdma_device->memcache, &sq->buflist);

            debug(2, "%s: sq %p RDMA write done, now %s",
                  __func__, sq, sq_state_name(sq->state.send));
            
            encourage_rts_done_waiting_buffer(sq);
        }
        else if (wc.opcode == BMI_RDMA_OP_SEND)
        {
            bh = ptr_from_int64(wc.id);
            if (!bh)
            {
                continue;
            }
            
            sq = bh->sq;
            if (sq == NULL)
            {
                /* MSG_BYE or MSG_CREDIT */
                debug(2, "%s: MSG_BYE or MSG_CREDIT completed locally",
                      __func__);
            }
            else if (sq->type == BMI_SEND)
            {
                sq_state_t state = sq->state.send;
                
                if (state == SQ_WAITING_EAGER_SEND_COMPLETION)
                {
                    sq->state.send = SQ_WAITING_USER_TEST;
                }
                else if (state == SQ_WAITING_RTS_SEND_COMPLETION)
                {
                    sq->state.send = SQ_WAITING_CTS;
                }
                else if (state == SQ_WAITING_RTS_SEND_COMPLETION_GOT_CTS)
                {
                    sq->state.send = SQ_WAITING_DATA_SEND_COMPLETION;
                }
                else if (state == SQ_WAITING_RTS_DONE_SEND_COMPLETION)
                {
                    sq->state.send = SQ_WAITING_USER_TEST;
                }
                else if (state == SQ_CANCELLED)
                {
                    ;
                }
                else
                {
                    warning("%s: unknown send state %s (%d) of sq %p",
                            __func__,
                            sq_state_name(sq->state.send),
                            sq->state.send,
                            sq);
                    continue;
                }
                
                debug(2, "%s: send to %s completed locally: sq %p -> %s",
                      __func__,
                      bh->c->peername,
                      sq,
                      sq_state_name(sq->state.send));
            }
            else
            {
                struct rdma_work *rq = sq;  /* rename */
                rq_state_t state = rq->state.recv;
                
                if (state & RQ_RTS_WAITING_CTS_SEND_COMPLETION)
                {
                    rq->state.recv &= ~RQ_RTS_WAITING_CTS_SEND_COMPLETION;
                }
                else if (state == RQ_CANCELLED)
                {
                    ;
                }
                else
                {
                    warning("%s: unknown recv state %s of rq %p",
                            __func__, rq_state_name(rq->state.recv), rq);
                    continue;
                }
                
                debug(2, "%s: send to %s completed locally: rq %p -> %s",
                      __func__,
                      bh->c->peername,
                      rq,
                      rq_state_name(rq->state.recv));
            }
            
            ++ret;
            qlist_add_tail(&bh->list, &bh->c->eager_send_buf_free);
        }
        else
        {
            warning("%s: cq entry id 0x%llx opcode %d unexpected",
                    __func__, llu(wc.id), wc.opcode);
        }    
    }
    
    return ret;
}

/*
 * msg_header_init()
 *
 * Description:
 *  Initialize common header of all messages.
 *
 * Params:
 *  [out] mh_common - pointer to common message header
 *  [in/out] c - pointer to connection message belongs to
 *  [in] type  - message type
 *
 * Returns:
 *  none
 */
static void msg_header_init(msg_header_common_t *mh_common,
                            rdma_connection_t *c,
                            msg_type_t type)
{
    mh_common->type = type;
    mh_common->credit = c->return_credit;
    c->return_credit = 0;
}

/*
 * get_eager_buf()
 *
 * Description:
 *  Grab an empty buf head, if available.
 *
 * Params:
 *  [in/out] c - pointer to connection
 *
 * Returns:
 *  Pointer to empty buf_head or NULL if none available
 */
static struct buf_head *get_eager_buf(rdma_connection_t *c)
{
    struct buf_head *bh = NULL;
    
    if (c->send_credit > 0)
    {
        --c->send_credit;
        bh = qlist_try_del_head(&c->eager_send_buf_free);
        if (!bh)
        {
            error("%s: empty eager_send_buf_free list, peer %s",
                  __func__, c->peername);
            c->send_credit++;
            return NULL;
        }
    }
    return bh;
}

/*
 * post_rr()
 *
 * Description:
 *  Re-post a receive buffer, possibly returning credit to the peer.
 *
 * Params:
 *  [in/out] c  - pointer to connection
 *  [in/out] bh - pointer to buf_head of buffer to post
 *
 * Returns:
 *  none
 */
static void post_rr(rdma_connection_t *c,
                    struct buf_head *bh)
{
    rdma_device->func.post_rr(c, bh);
    ++c->return_credit;
    
    /* if credits are building up, explicitly send them over */
    if (c->return_credit > rdma_device->eager_buf_num - 4)
    {
        msg_header_common_t mh_common;
        char *ptr;
        
        /* one credit saved back for just this situation, do not check */
        --c->send_credit;
        bh = qlist_try_del_head(&c->eager_send_buf_free);
        if (!bh)
        {
            error("%s: empty eager_send_buf_free list", __func__);
            c->send_credit++;
            return;
        }
        bh->sq = NULL;
        debug(2, "%s: return %d credits to %s",
              __func__, c->return_credit, c->peername);
        msg_header_init(&mh_common, c, MSG_CREDIT);
        ptr = bh->buf;
        encode_msg_header_common_t(&ptr, &mh_common);
        post_sr(bh, sizeof(mh_common));
    }
}

/*
 * encourage_send_waiting_buffer()
 *
 * Description:
 *  Push a send message along its next step.  Called internally only.
 *
 * Params:
 *  [in/out] sq - pointer to send work request
 *
 * Returns:
 *  none
 */
static void encourage_send_waiting_buffer(struct rdma_work *sq)
{
    struct buf_head *bh;
    rdma_connection_t *c = sq->c;
    
    if (!sq)
    {
        error("%S: no sq", __func__);
        return;
    }
    
    debug(3, "%s: sq %p", __func__, sq);
    
    if (sq->state.send != SQ_WAITING_BUFFER)
    {
        error("%s: wrong send state %s",
              __func__, sq_state_name(sq->state.send));
        return;
    }
    
    bh = get_eager_buf(c);
    if (!bh)
    {
        debug(2, "%s: sq %p no free send buffers to %s",
              __func__, sq, c->peername);
        return;
    }
    sq->bh = bh;
    bh->sq = sq;    /* uplink for completion */
    
    if (sq->buflist.tot_len <= rdma_device->eager_buf_payload)
    {
        /*
         * Eager send.
         */
        msg_header_eager_t mh_eager;
        char *ptr = bh->buf;
        
        memset(&mh_eager, 0, sizeof(mh_eager));
        
        if (sq->is_unexpected)
        {
            msg_header_init(&mh_eager.c, c, MSG_EAGER_SENDUNEXPECTED);
        }
        else
        {
            msg_header_init(&mh_eager.c, c, MSG_EAGER_SEND);
        }
        
        mh_eager.bmi_tag = sq->bmi_tag;
        encode_msg_header_eager_t(&ptr, &mh_eager);
        
        memcpy_from_buflist(&sq->buflist,
                            (msg_header_eager_t *) bh->buf + 1);
        
        /* send the message */
        post_sr(bh, (u_int32_t) (sizeof(mh_eager) + sq->buflist.tot_len));
        
        /* wait for ack saying remote has received and recycled his buf */
        sq->state.send = SQ_WAITING_EAGER_SEND_COMPLETION;
        debug(2, "%s: sq %p send EAGER len %lld",
              __func__, sq, lld(sq->buflist.tot_len));
    }
    else
    {
        /*
         * Request to send, rendez-vous.  Include the mop id in the message
         * which will be returned to us in the CTS so we can look it up.
         */
        msg_header_rts_t mh_rts;
        char *ptr = bh->buf;
        
        memset(&mh_rts, 0, sizeof(mh_rts));
        msg_header_init(&mh_rts.c, c, MSG_RTS);
        mh_rts.bmi_tag = sq->bmi_tag;
        mh_rts.mop_id = sq->mop->op_id;
        mh_rts.tot_len = sq->buflist.tot_len;
        
        encode_msg_header_rts_t(&ptr, &mh_rts);
        
        post_sr(bh, sizeof(mh_rts));
        
        /* XXX: need to lock against receiver thread?  Could poll return
         * the CTS and start the data send before this completes? */
        memcache_register(rdma_device->memcache, &sq->buflist);
        
        sq->state.send = SQ_WAITING_RTS_SEND_COMPLETION;
        debug(2, "%s: sq %p send RTS mopid %llx len %lld",
              __func__,
              sq,
              llu(sq->mop->op_id),
              lld(sq->buflist.tot_len));
    }
}

/*
 * encourage_send_incoming_cts()
 * 
 * Description:
 *  Look at the incoming message which is a response to an earlier RTS
 *  from us, and start the real data send.
 *
 * Params:
 *  [in] bh  - pointer to buf_head of incoming message
 *  [in] len - message size for CTS (TODO: is this right?)
 *
 * Returns:
 *  none
 */
static void encourage_send_incoming_cts(struct buf_head *bh,
                                        u_int32_t byte_len)
{
    msg_header_cts_t mh_cts;
    struct rdma_work *sq, *sqt;
    u_int32_t want;
    char *ptr = bh->buf;
    
    decode_msg_header_cts_t(&ptr, &mh_cts);
    
    /*
     * Look through this CTS message to determine the owning sq.  Works
     * using the mop_id which was sent during the RTS, now returned to us.
     */
    sq = 0;
    qlist_for_each_entry(sqt, &rdma_device->sendq, list)
    {
        debug(8, "%s: looking for op_id 0x%llx, consider 0x%llx",
              __func__,
              llu(mh_cts.rts_mop_id),
              llu(sqt->mop->op_id));
        
        if (sqt->c == bh->c &&
            sqt->mop->op_id == (bmi_op_id_t) mh_cts.rts_mop_id)
        {
            sq = sqt;
            break;
        }
    }
    
    if (!sq)
    {
        error("%s: mop_id %llx in CTS message not found",
              __func__, llu(mh_cts.rts_mop_id));
        return;
    }
    
    debug(2, "%s: sq %p %s mopid %llx len %u",
          __func__,
          sq,
          sq_state_name(sq->state.send),
          llu(mh_cts.rts_mop_id),
          byte_len);
    
    if (sq->state.send != SQ_WAITING_CTS &&
        sq->state.send != SQ_WAITING_RTS_SEND_COMPLETION)
    {
        error("%s: wrong send state %s",
              __func__, sq_state_name(sq->state.send));
        return;
    }
    
    /* message; cts content; list of buffers, lengths, and keys */
    want = sizeof(mh_cts) +
           mh_cts.buflist_num *
           MSG_HEADER_CTS_BUFLIST_ENTRY_SIZE;
    
    /* 
     * TODO: What is the purpose of this?
     *       Why not just "if (byte_len != want)"?
     */
    if (bmi_rdma_unlikely(byte_len != want))
    {
        error("%s: wrong message size for CTS, got %u, want %u",
              __func__, byte_len, want);
        return;
    }
    
    /* start the big transfer */
    post_sr_rdmaw(sq, &mh_cts, (msg_header_cts_t *) bh->buf + 1);
    
    /* re-post our recv buf now that we have all the information from CTS */
    post_rr(sq->c, bh);
    
    if (sq->state.send == SQ_WAITING_CTS)
    {
        sq->state.send = SQ_WAITING_DATA_SEND_COMPLETION;
    }
    else
    {
        sq->state.send = SQ_WAITING_RTS_SEND_COMPLETION_GOT_CTS;
    }
    
    debug(0, "%s: sq %p now %s", __func__, sq, sq_state_name(sq->state.send));
}

/*
 * find_matching_recv()
 *
 * Description:
 *  See if anything was preposted that matches this.
 *
 * Params:
 *  [in] statemask - target recv state(s) to match
 *  [in] c - pointer to connection to match recv to
 *  [in] bmi_tag - message tag to match
 *
 * Returns:
 *  Pointer to matching recv queue entry, NULL if no match
 */
static struct rdma_work *find_matching_recv(rq_state_t statemask,
                                            const rdma_connection_t *c,
                                            bmi_msg_tag_t bmi_tag)
{
    struct rdma_work *rq;
    
    qlist_for_each_entry(rq, &rdma_device->recvq, list)
    {
        if ((rq->state.recv & statemask) &&
                rq->c == c &&
                rq->bmi_tag == bmi_tag)
        {
            return rq;
        }
    }
    return NULL;
}

/*
 * alloc_new_recv()
 *
 * Description:
 *  Init a new recvq entry from something that arrived on the wire.
 *
 * Params:
 *  [in/out] c - pointer to connection the new entry belongs to
 *  [in] bh - pointer to buf_head of incoming unexpected message; 
 *            could be NULL for eager, pre-post recv
 *
 * Returns:
 *  Pointer to new recv queue entry
 */
static struct rdma_work *alloc_new_recv(rdma_connection_t *c,
                                        struct buf_head *bh)
{
    struct rdma_work *rq = bmi_rdma_malloc(sizeof(*rq));
    rq->type = BMI_RECV;
    rq->c = c;
    ++rq->c->refcnt;
    rq->bh = bh;
    rq->mop = 0;    /* until user posts for it */
    rq->rts_mop_id = 0;
    qlist_add_tail(&rq->list, &rdma_device->recvq);
    return rq;
}

/*
 * encourage_recv_incoming()
 *
 * Description:
 *  Called from incoming message processing, except for the case
 *  of ack to a CTS, for which we know the rq (see below).
 *
 *  Unexpected receive, either no post or explicit sendunexpected.
 *
 * Params:
 *  [in] bh       - pointer to buf_head of incoming message
 *  [in] type     - incoming message type
 *  [in] byte_len - size of recv (TODO: is this right?)
 *
 * Returns:
 *  none
 */
static void encourage_recv_incoming(struct buf_head *bh,
                                    msg_type_t type,
                                    u_int32_t byte_len)
{
    rdma_connection_t *c = bh->c;
    struct rdma_work *rq;
    char *ptr = bh->buf;
    
    debug(4, "%s: incoming msg type %s", __func__, msg_type_name(type));
    
    if (type == MSG_EAGER_SEND)
    {
        msg_header_eager_t mh_eager;
        
        ptr = bh->buf;
        decode_msg_header_eager_t(&ptr, &mh_eager);
        
        debug(2, "%s: recv eager len %u", __func__, byte_len);
        
        rq = find_matching_recv(RQ_WAITING_INCOMING, c, mh_eager.bmi_tag);
        if (rq)
        {
            bmi_size_t len = byte_len - sizeof(mh_eager);
            if (len > rq->buflist.tot_len)
            {
                error("%s: EAGER received %lld too small for buffer %lld",
                      __func__,
                      lld(len),
                      lld(rq->buflist.tot_len));
                return;
            }
            
            memcpy_to_buflist(&rq->buflist,
                              (msg_header_eager_t *) bh->buf + 1,
                              len);
            
            /* re-post */
            post_rr(c, bh);
            
            rq->state.recv = RQ_EAGER_WAITING_USER_TEST;
            
            debug(2, "%s: matched rq %p now %s",
                  __func__,
                  rq,
                  rq_state_name(rq->state.recv));
            
            /* if a big receive was posted but only a small message came
             * through, unregister it now */
            if (rq->buflist.tot_len > rdma_device->eager_buf_payload)
            {
                debug(2, "%s: early registration not needed, dereg after eager",
                      __func__);
                memcache_deregister(rdma_device->memcache, &rq->buflist);
            }
        }
        else
        {
            /* didn't find a matching recv */
            rq = alloc_new_recv(c, bh);
            /* return value for when user does post_recv for this one */
            rq->bmi_tag = mh_eager.bmi_tag;
            rq->state.recv = RQ_EAGER_WAITING_USER_POST;
            /* do not repost or ack, keeping bh until user test */
            debug(2, "%s: new rq %p now %s",
                  __func__,
                  rq,
                  rq_state_name(rq->state.recv));
        }
        rq->actual_len = byte_len - sizeof(mh_eager);
    }
    else if (type == MSG_EAGER_SENDUNEXPECTED)
    {
        msg_header_eager_t mh_eager;
        
        ptr = bh->buf;
        decode_msg_header_eager_t(&ptr, &mh_eager);
        
        debug(2, "%s: recv eager unexpected len %u", __func__, byte_len);
        
        rq = alloc_new_recv(c, bh);
        /* return values for when user does testunexpected for this one */
        rq->bmi_tag = mh_eager.bmi_tag;
        rq->state.recv = RQ_EAGER_WAITING_USER_TESTUNEXPECTED;
        rq->actual_len = byte_len - sizeof(mh_eager);
        /* do not repost, keeping bh until user test */
        debug(2, "%s: new rq %p now %s",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
    }
    else if (type == MSG_RTS)
    {
        /*
         * Sender wants to send a big message, initiates rts/cts protocol.
         * Has the user posted a matching receive for it yet?
         */
        msg_header_rts_t mh_rts;
        
        ptr = bh->buf;
        decode_msg_header_rts_t(&ptr, &mh_rts);
        
        debug(2, "%s: recv RTS len %lld mopid %llx",
              __func__,
              lld(mh_rts.tot_len),
              llu(mh_rts.mop_id));
        
        rq = find_matching_recv(RQ_WAITING_INCOMING, c, mh_rts.bmi_tag);
        if (rq)
        {
            /* found matching receive */
            if ((int) mh_rts.tot_len > rq->buflist.tot_len)
            {
                error("%s: RTS received %llu too small for buffer %llu",
                      __func__,
                      llu(mh_rts.tot_len),
                      llu(rq->buflist.tot_len));
                return;
            }
            rq->state.recv = RQ_RTS_WAITING_CTS_BUFFER;
            debug(2, "%s: matched rq %p MSG_RTS now %s",
                  __func__,
                  rq,
                  rq_state_name(rq->state.recv));
        }
        else
        {
            /* didn't find matching receive */
            rq = alloc_new_recv(c, bh);
            /* return value for when user does post_recv for this one */
            rq->bmi_tag = mh_rts.bmi_tag;
            rq->state.recv = RQ_RTS_WAITING_USER_POST;
            debug(2, "%s: new rq %p MSG_RTS now %s",
                  __func__,
                  rq,
                  rq_state_name(rq->state.recv));
        }
        rq->actual_len = mh_rts.tot_len;
        rq->rts_mop_id = mh_rts.mop_id;
        
        post_rr(c, bh);
        
        if (rq->state.recv == RQ_RTS_WAITING_CTS_BUFFER)
        {
            int ret;
            ret = send_cts(rq);
            if (ret == 0)
            {
                rq->state.recv = RQ_RTS_WAITING_RTS_DONE |
                                 RQ_RTS_WAITING_CTS_SEND_COMPLETION |
                                 RQ_RTS_WAITING_USER_TEST;
            }
            /* else keep waiting until we can send that cts */
        }
    }
    else if (type == MSG_RTS_DONE)
    {
        msg_header_rts_done_t mh_rts_done;
        struct rdma_work *rqt;
        
        ptr = bh->buf;
        decode_msg_header_rts_done_t(&ptr, &mh_rts_done);
        
        debug(2, "%s: recv RTS_DONE mop_id %llx",
              __func__, llu(mh_rts_done.mop_id));
        
        rq = NULL;
        qlist_for_each_entry(rqt, &rdma_device->recvq, list)
        {
            if (rqt->c == c &&
                rqt->rts_mop_id == mh_rts_done.mop_id &&
                rqt->state.recv & RQ_RTS_WAITING_RTS_DONE)
            {
                rq = rqt;
                break;
            }
        }
        
        if (rq == NULL)
        {
            warning("%s: mop_id %llx in RTS_DONE message not found",
                    __func__, llu(mh_rts_done.mop_id));
        }
        else
        {
            memcache_deregister(rdma_device->memcache, &rq->buflist);
        }
        
        post_rr(c, bh);
        
        if (rq)
        {
            rq->state.recv &= ~RQ_RTS_WAITING_RTS_DONE;
        }
    }
    else if (type == MSG_BYE)
    {
        /*
         * Other side requests connection close.  Do it.
         */
        debug(2, "%s: recv BYE", __func__);
        post_rr(c, bh);
        rdma_close_connection(c);
    }
    else if (type == MSG_CREDIT)
    {
        /* already added the credit in check_cq */
        debug(2, "%s: recv CREDIT", __func__);
        post_rr(c, bh);
    }
    else
    {
        error("%s: unknown message header type %d len %u",
              __func__, type, byte_len);
        return;
    }
}

/*
 * encourage_rts_done_waiting_buffer()
 *
 * Description:
 *  We finished the RDMA write.  Send him a done message.
 *
 * Params:
 *  [in/out] sq - pointer to sendq entry
 *
 * Returns:
 *  none
 */
static void encourage_rts_done_waiting_buffer(struct rdma_work *sq)
{
    rdma_connection_t *c = sq->c;
    struct buf_head *bh;
    char *ptr;
    msg_header_rts_done_t mh_rts_done;
    
    bh = get_eager_buf(c);
    if (!bh)
    {
        debug(2, "%s: sq %p no free send buffers to %s",
              __func__, sq, c->peername);
        return;
    }
    sq->bh = bh;
    bh->sq = sq;
    ptr = bh->buf;
    
    msg_header_init(&mh_rts_done.c, c, MSG_RTS_DONE);
    mh_rts_done.mop_id = sq->mop->op_id;
    
    debug(2, "%s: sq %p sent RTS_DONE mopid %llx",
          __func__, sq, llu(sq->mop->op_id));
    
    encode_msg_header_rts_done_t(&ptr, &mh_rts_done);
    
    post_sr(bh, sizeof(mh_rts_done));
    sq->state.send = SQ_WAITING_RTS_DONE_SEND_COMPLETION;
}

/*
 * send_bye()
 *
 * Description:
 *  Send a BYE message to peer. 
 *
 *  Called by a client before disconnecting/closing connection(s) to server(s).
 *
 * Params:
 *  [in] c - pointer to connection
 *
 * Returns:
 *  none
 */
static void send_bye(rdma_connection_t *c)
{
    msg_header_common_t mh_common;
    struct buf_head *bh;
    char *ptr;
    
    debug(2, "%s: sending bye", __func__);
    bh = get_eager_buf(c);
    if (!bh)
    {
        debug(2, "%s: no free send buffers to %s", __func__, c->peername);
        /* if no messages available, let garbage collection on server deal */
        return;
    }
    bh->sq = NULL;
    ptr = bh->buf;
    msg_header_init(&mh_common, c, MSG_BYE);
    encode_msg_header_common_t(&ptr, &mh_common);
    
    post_sr(bh, sizeof(mh_common));
}

/*
 * send_cts()
 *
 * Description:
 *  Two places need to send a CTS in response to an RTS.  They both
 *  call this.  This handles pinning the memory, too.  Don't forget
 *  to unpin when done.
 *
 * Params:
 *  [in/out] rq - pointer to recvq entry
 *
 * Returns:
 *  0 on success, 1 on failure
 */
static int send_cts(struct rdma_work *rq)
{
    rdma_connection_t *c = rq->c;
    struct buf_head *bh;
    msg_header_cts_t mh_cts;
    u_int64_t *bufp;
    u_int32_t *lenp;
    u_int32_t *keyp;
    u_int32_t post_len;
    char *ptr;
    int i;
    
    debug(2, "%s: rq %p from %s mopid %llx len %lld",
          __func__,
          rq,
          rq->c->peername,
          llu(rq->rts_mop_id),
          lld(rq->buflist.tot_len));
    
    bh = get_eager_buf(c);
    if (!bh)
    {
        debug(2, "%s: rq %p no free send buffers to %s",
              __func__, rq, c->peername);
        return 1;
    }
    rq->bh = bh;
    bh->sq = (struct rdma_work *) rq;   /* uplink for completion */
    
    msg_header_init(&mh_cts.c, c, MSG_CTS);
    mh_cts.rts_mop_id = rq->rts_mop_id;
    mh_cts.buflist_tot_len = rq->buflist.tot_len;
    mh_cts.buflist_num = rq->buflist.num;
    
    ptr = bh->buf;
    encode_msg_header_cts_t(&ptr, &mh_cts);
    
    /* encode all the buflist entries */
    bufp = (u_int64_t *)((msg_header_cts_t *) bh->buf + 1);
    lenp = (u_int32_t *)(bufp + rq->buflist.num);
    keyp = (u_int32_t *)(lenp + rq->buflist.num);
    post_len = (char *)(keyp + rq->buflist.num) - (char *) bh->buf;
    if (post_len > rdma_device->eager_buf_size)
    {
        error("%s: too many (%d) recv buflist entries for buf",
              __func__, rq->buflist.num);
        return 1;
    }
    for (i = 0; i < rq->buflist.num; i++)
    {
        bufp[i] = htobmi64(int64_from_ptr(rq->buflist.buf.recv[i]));
        lenp[i] = htobmi32(rq->buflist.len[i]);
        keyp[i] = htobmi32(rq->buflist.memcache[i]->memkeys.rkey);
    }
    
    /* send the cts */
    post_sr(bh, post_len);
    
    return 0;
}

/*
 * ensure_connected()
 *
 * Description:
 *  Bring up the connection before posting a send or a receive on it.
 *
 * Params:
 *  [in] remote_map - peer address/connection info
 *
 * Returns:
 *  0 if connected, 1 if not connected
 */
static int ensure_connected(struct bmi_method_addr *remote_map)
{
    int ret = 0;
    rdma_method_addr_t *rdma_map = remote_map->method_data;
    
    if (!rdma_map->c && rdma_map->reconnect_flag)
    {
        ret = rdma_client_connect(rdma_map, remote_map);
    }
    else if (!rdma_map->c && !rdma_map->reconnect_flag)
    {
        ret = 1;    /* cannot actively connect */
    }
    else
    {
        ret = 0;
    }
    
    return ret;
}

/*
 * post_send()
 *
 * Description:
 *  Generic interface for both send and sendunexpected, list and non-list send.
 *
 * Params:
 *  [out] id           - operation identifier
 *  [in] remote_map    - peer address/connection info
 *  [in] numbufs       - counterintuitive but 0 indicates a single buffer for a
 *                       non-_list send; otherwise it is a _list send and 
 *                       numbufs is the number of buffers in the buflist
 *  [in] buffers       - array of buffers to send
 *  [in] sizes         - array of buffer sizes
 *  [in] total_size    - total size of data being sent (sum of all buffer sizes)
 *  [in] tag           - user-specified message tag
 *  [in] user_ptr      - user_ptr associated with this operation
 *  [in] context_id    - context identifier
 *  [in] is_unexpected - indicates whether the message is unexpected
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int post_send(bmi_op_id_t *id,
                     struct bmi_method_addr *remote_map,
                     int numbufs,
                     const void *const *buffers,
                     const bmi_size_t *sizes,
                     bmi_size_t total_size,
                     bmi_msg_tag_t tag,
                     void *user_ptr,
                     bmi_context_id context_id,
                     int is_unexpected)
{
    struct rdma_work *sq;
    struct method_op *mop;
    rdma_method_addr_t *rdma_map;
    int i;
    int ret = 0;
    
    gen_mutex_lock(&interface_mutex);
    ret = ensure_connected(remote_map);
    if (ret)
    {
        goto out;
    }
    rdma_map = remote_map->method_data;
    
    /* alloc and build new sendq structure */
    sq = bmi_rdma_malloc(sizeof(*sq));
    sq->type = BMI_SEND;
    sq->state.send = SQ_WAITING_BUFFER;
    
    debug(2, "%s: sq %p len %lld peer %s",
          __func__,
          sq,
          (long long) total_size,
          rdma_map->c->peername);
    
    /*
     * For a single buffer, store it inside the sq directly, else save
     * the pointer to the list the user built when calling a _list
     * function.  This case is indicated by the non-_list functions by
     * a zero in numbufs.
     */
    if (numbufs == 0)
    {
        sq->buflist_one_buf.send = *buffers;
        sq->buflist_one_len = *sizes;
        sq->buflist.num = 1;
        sq->buflist.buf.send = &sq->buflist_one_buf.send;
        sq->buflist.len = &sq->buflist_one_len;
    }
    else
    {
        sq->buflist.num = numbufs;
        sq->buflist.buf.send = buffers;
        sq->buflist.len = sizes;
    }
    
    sq->buflist.tot_len = 0;
    for (i = 0; i < sq->buflist.num; i++)
    {
        sq->buflist.tot_len += sizes[i];
    }
    
    /*
     * This passed-in total length field does not make much sense
     * to me, but I'll at least check it for accuracy.
     *
     * TODO: Why? Expected size vs. Actual Size? 
     *       Copied from bmi.c:BMI_post_send_list() (Phil Carns)?
     */
    if (sq->buflist.tot_len != total_size)
    {
        error("%s: user-provided tot len %lld"
              " does not match buffer list tot len %lld",
              __func__,
              lld(total_size),
              lld(sq->buflist.tot_len));
        ret = -EINVAL;
        goto out;
    }
    
    /* unexpected messages must fit inside an eager message */
    if (is_unexpected && sq->buflist.tot_len > rdma_device->eager_buf_payload)
    {
        error("%s: unexpected message is too large (tot len: %lld)",
              __func__,
              lld(sq->buflist.tot_len));
        free(sq);
        ret = -EINVAL;
        goto out;
    }
    
    sq->bmi_tag = tag;
    sq->c = rdma_map->c;
    ++sq->c->refcnt;
    sq->is_unexpected = is_unexpected;
    qlist_add_tail(&sq->list, &rdma_device->sendq);
    
    /* generate identifier used by caller to test for message later */
    mop = bmi_rdma_malloc(sizeof(*mop));
    id_gen_fast_register(&mop->op_id, mop);
    mop->addr = remote_map;  /* set of function pointers, essentially */
    mop->method_data = sq;
    mop->user_ptr = user_ptr;
    mop->context_id = context_id;
    *id = mop->op_id;
    sq->mop = mop;
    debug(3, "%s: new sq %p", __func__, sq);
    
    /* and start sending it if possible */
    encourage_send_waiting_buffer(sq);
    
out:
    
    gen_mutex_unlock(&interface_mutex);
    return ret;
}

/*
 * BMI_rdma_post_send()
 *
 * Description:
 *  Wrapper for an expected, single-buffer send.
 *
 * Params:
 *  [out] id        - operation identifier (generated in generic post_send)
 *  [in] remote_map - peer address/connection info
 *  [in] buffer     - address of buffer to send
 *  [in] total_size - size of buffer
 *  [-] buffer_flag - unused
 *  [in] tag        - user-specified message tag
 *  [in] user_ptr   - user_ptr associated with this operation
 *  [in] context_id - context identifier
 *  [-] hints       - unused
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_post_send(bmi_op_id_t *id,
                              struct bmi_method_addr *remote_map,
                              const void *buffer,
                              bmi_size_t total_size,
                              enum bmi_buffer_type buffer_flag __unused,
                              bmi_msg_tag_t tag,
                              void *user_ptr,
                              bmi_context_id context_id,
                              PVFS_hint hints __unused)
{
    return post_send(id,
                     remote_map,
                     0,     /* indicates single-buffer, non-_list send */
                     &buffer,
                     &total_size,
                     total_size,
                     tag,
                     user_ptr,
                     context_id,
                     0);    /* expected */
}

/*
 * BMI_rdma_post_send_list()
 *
 * Description:
 *  Wrapper for an expected, single- (TODO: right?) or multi-buffer _list send.
 *
 * Params:
 *  [out] id        - operation identifier (generated in generic post_send)
 *  [in] remote_map - peer address/connection info
 *  [in] buffers    - array of buffers to send
 *  [in] sizes      - array of buffer sizes
 *  [in] list_count - number of buffers
 *  [in] total_size - total size of data being sent (sum of all buffer sizes)
 *  [-] buffer_flag - unused
 *  [in] tag        - user-specified message tag
 *  [in] user_ptr   - user_ptr associated with this operation
 *  [in] context_id - context identifier
 *  [-] hints       - unused
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_post_send_list(bmi_op_id_t *id,
                                   struct bmi_method_addr *remote_map,
                                   const void *const *buffers,
                                   const bmi_size_t *sizes,
                                   int list_count,
                                   bmi_size_t total_size,
                                   enum bmi_buffer_type buffer_flag __unused,
                                   bmi_msg_tag_t tag,
                                   void *user_ptr,
                                   bmi_context_id context_id,
                                   PVFS_hint hints __unused)
{
    return post_send(id,
                     remote_map,
                     list_count,
                     buffers,
                     sizes,
                     total_size,
                     tag,
                     user_ptr,
                     context_id,
                     0);    /* expected */
}

/*
 * BMI_rdma_post_sendunexpected()
 *
 * Description:
 *  Wrapper for an unexpected, single-buffer send.
 *
 * Params:
 *  [out] id        - operation identifier (generated in generic post_send)
 *  [in] remote_map - peer address/connection info
 *  [in] buffer     - address of buffer to send
 *  [in] total_size - size of buffer
 *  [-] buffer_flag - unused
 *  [in] tag        - user-specified message tag
 *  [in] user_ptr   - user_ptr associated with this operation
 *  [in] context_id - context identifier
 *  [-] hints       - unused
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_post_sendunexpected(bmi_op_id_t *id,
                                    struct bmi_method_addr *remote_map,
                                    const void *buffer,
                                    bmi_size_t total_size,
                                    enum bmi_buffer_type buffer_flag __unused,
                                    bmi_msg_tag_t tag,
                                    void *user_ptr,
                                    bmi_context_id context_id,
                                    PVFS_hint hints __unused)
{
    return post_send(id,
                     remote_map,
                     0,     /* indicates single-buffer, non-_list send */
                     &buffer,
                     &total_size,
                     total_size,
                     tag,
                     user_ptr,
                     context_id,
                     1);    /* unexpected */
}

/*
 * BMI_rdma_post_sendunexpected_list()
 *
 * Description:
 *  Wrapper for an unexpected, single or multi-buffer _list send.
 *
 * Params:
 *  [out] id        - operation identifier (generated in generic post_send)
 *  [in] remote_map - peer address/connection info
 *  [in] buffers    - array of buffers to send
 *  [in] sizes      - array of buffer sizes
 *  [in] list_count - number of buffers
 *  [in] total_size - total size of data being sent (sum of all buffer sizes)
 *  [-] buffer_flag - unused
 *  [in] tag        - user-specified message tag
 *  [in] user_ptr   - user_ptr associated with this operation
 *  [in] context_id - context identifier
 *  [-] hints       - unused
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_post_sendunexpected_list(bmi_op_id_t *id,
                                    struct bmi_method_addr *remote_map,
                                    const void *const *buffers,
                                    const bmi_size_t *sizes,
                                    int list_count,
                                    bmi_size_t total_size,
                                    enum bmi_buffer_type buffer_flag __unused,
                                    bmi_msg_tag_t tag,
                                    void *user_ptr,
                                    bmi_context_id context_id,
                                    PVFS_hint hints __unused)
{
    return post_send(id,
                     remote_map,
                     list_count,
                     buffers,
                     sizes,
                     total_size,
                     tag,
                     user_ptr,
                     context_id,
                     1);    /* unexpected */
}

/*
 * post_recv()
 *
 * Description:
 *  Used by both recv and recv_list.
 *
 * Params:
 *  [out] id              - operation identifier
 *  [in] remote_map       - peer address/connection info
 *  [in] numbufs          - counterintuitive but 0 indicates a single buffer
 *                          for a non-_list recv; otherwise it is a _list recv
 *                          and numbufs is the number of buffers in the buflist
 *  [in] buffers          - array of buffers to receive data into
 *  [in] sizes            - array of buffer sizes
 *  [in] tot_expected_len - total expected size of data being received
 *  [in] tag              - user-specified message tag
 *  [in] user_ptr         - user_ptr associated with this operation
 *  [in] context_id       - context identifier
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int post_recv(bmi_op_id_t *id,
                     struct bmi_method_addr *remote_map,
                     int numbufs,
                     void *const *buffers,
                     const bmi_size_t *sizes,
                     bmi_size_t tot_expected_len,
                     bmi_msg_tag_t tag,
                     void *user_ptr,
                     bmi_context_id context_id)
{
    struct rdma_work *rq;
    struct method_op *mop;
    rdma_method_addr_t *rdma_map;
    rdma_connection_t *c;
    int i;
    int ret = 0;
    
    gen_mutex_lock(&interface_mutex);
    ret = ensure_connected(remote_map);
    if (ret)
    {
        goto out;
    }
    rdma_map = remote_map->method_data;
    c = rdma_map->c;
    
    /* poll interface first to save a few steps below */
    rdma_check_cq();
    
    /* check to see if matching recv is in the queue */
    rq = find_matching_recv(RQ_EAGER_WAITING_USER_POST |
                                RQ_RTS_WAITING_USER_POST,
                            c,
                            tag);
    if (rq)
    {
        debug(2, "%s: rq %p matches %s",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
    }
    else
    {
        /* alloc and build new recvq structure */
        rq = alloc_new_recv(c, NULL);
        rq->state.recv = RQ_WAITING_INCOMING;
        rq->bmi_tag = tag;
        debug(2, "%s: new rq %p", __func__, rq);
    }
    
    /*
     * For a single buffer, store it inside the rq directly, else save
     * the pointer to the list the user built when calling a _list
     * function.  This case is indicated by the non-_list functions by
     * a zero in numbufs.
     */
    if (numbufs == 0)
    {
        rq->buflist_one_buf.recv = *buffers;
        rq->buflist_one_len = *sizes;
        rq->buflist.num = 1;
        rq->buflist.buf.recv = &rq->buflist_one_buf.recv;
        rq->buflist.len = &rq->buflist_one_len;
    }
    else
    {
        rq->buflist.num = numbufs;
        rq->buflist.buf.recv = buffers;
        rq->buflist.len = sizes;
    }
    
    rq->buflist.tot_len = 0;
    for (i = 0; i < rq->buflist.num; i++)
    {
        rq->buflist.tot_len += sizes[i];
    }
    
    /*
     * This passed-in total length field does not make much sense
     * to me, but I'll at least check it for accuracy.
     *
     * TODO: Why? Expected size vs. Actual Size?
     *       Copied from bmi.c:BMI_post_recv_list() (Phil Carns)?
     */
    if (rq->buflist.tot_len != tot_expected_len)
    {
        error("%s: user-provided tot len %lld"
              " does not match buffer list tot len %lld",
              __func__,
              lld(tot_expected_len),
              lld(rq->buflist.tot_len));
        ret = -EINVAL;
        goto out;
    }
    
    /* generate identifier used by caller to test for message later */
    mop = bmi_rdma_malloc(sizeof(*mop));
    id_gen_fast_register(&mop->op_id, mop);
    mop->addr = remote_map;     /* set of function pointers, essentially */
    mop->method_data = rq;
    mop->user_ptr = user_ptr;
    mop->context_id = context_id;
    *id = mop->op_id;
    rq->mop = mop;
    
    /* handle the two "waiting for a local user post" states */
    if (rq->state.recv == RQ_EAGER_WAITING_USER_POST)
    {
        msg_header_eager_t mh_eager;
        char *ptr = rq->bh->buf;
        
        decode_msg_header_eager_t(&ptr, &mh_eager);
        
        debug(2, "%s: rq %p state %s finish eager directly",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
        
        if (rq->actual_len > tot_expected_len)
        {
            error("%s: received %lld matches too-small buffer %lld",
                  __func__,
                  lld(rq->actual_len),
                  lld(rq->buflist.tot_len));
            ret = -EINVAL;
            goto out;
        }
        
        memcpy_to_buflist(&rq->buflist,
                          (msg_header_eager_t *) rq->bh->buf + 1,
                          rq->actual_len);
        
        /* re-post */
        post_rr(rq->c, rq->bh);
        
        /* now just wait for user to test, never do "immediate completion" */
        rq->state.recv = RQ_EAGER_WAITING_USER_TEST;
        goto out;
    }
    else if (rq->state.recv == RQ_RTS_WAITING_USER_POST)
    {
        int sret;
        
        debug(2, "%s: rq %p %s send cts",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
        
        /* try to send, or wait for send buffer space */
        rq->state.recv = RQ_RTS_WAITING_CTS_BUFFER;

        memcache_register(rdma_device->memcache, &rq->buflist);

        sret = send_cts(rq);
        if (sret == 0)
        {
            rq->state.recv = RQ_RTS_WAITING_RTS_DONE |
                             RQ_RTS_WAITING_CTS_SEND_COMPLETION |
                             RQ_RTS_WAITING_USER_TEST;
        }
        
        goto out;
    }
    
    /* but remember that this might not be used if the other side sends
     * less than we posted for receive; that's legal */
    if (rq->buflist.tot_len > rdma_device->eager_buf_payload)
    {
        memcache_register(rdma_device->memcache, &rq->buflist);
    }
    
out:
    
    gen_mutex_unlock(&interface_mutex);
    return ret;
}

/*
 * BMI_rdma_post_recv()
 *
 * Description:
 *  Wrapper for a single-buffer recv.
 *
 * Params:
 *  [out] id          - operation identifier (generated in generic post_recv)
 *  [in] remote_map   - peer address/connection info
 *  [in] buffer       - address of buffer to receive data into
 *  [in] expected_len - expected size of data being received
 *  [-] actual_len    - unused
 *  [-] buffer_flag   - unused
 *  [in] tag          - user-specified message tag
 *  [in] user_ptr     - user_ptr associated with this operation
 *  [in] context_id   - context identifier
 *  [-] hints         - unused
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_post_recv(bmi_op_id_t *id,
                              struct bmi_method_addr *remote_map,
                              void *buffer,
                              bmi_size_t expected_len,
                              bmi_size_t *actual_len __unused,
                              enum bmi_buffer_type buffer_flag __unused,
                              bmi_msg_tag_t tag,
                              void *user_ptr,
                              bmi_context_id context_id,
                              PVFS_hint hints __unused)
{
    return post_recv(id,
                     remote_map,
                     0,     /* indicates single-buffer, non-_list recv */
                     &buffer,
                     &expected_len,
                     expected_len,
                     tag,
                     user_ptr,
                     context_id);
}

/*
 * BMI_rdma_post_recv_list()
 *
 * Description:
 *  Wrapper for a single- (TODO: right?) or multi-buffer _list send.
 *
 * Params:
 *  [out] id           - operation identifier (generated in generic post_send)
 *  [in] remote_map    - peer address/connection info
 *  [in] buffers       - array of buffers to receive into
 *  [in] sizes         - array of buffer sizes
 *  [in] list_count    - number of buffers
 *  [in] tot_expected_len - total expected size of data being received
 *  [-] tot_actual_len - unused
 *  [-] buffer_flag    - unused
 *  [in] tag           - user-specified message tag
 *  [in] user_ptr      - user_ptr associated with this operation
 *  [in] context_id    - context identifier
 *  [-] hints          - unused
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_post_recv_list(bmi_op_id_t *id,
                                   struct bmi_method_addr *remote_map,
                                   void *const *buffers,
                                   const bmi_size_t *sizes,
                                   int list_count,
                                   bmi_size_t tot_expected_len,
                                   bmi_size_t *tot_actual_len __unused,
                                   enum bmi_buffer_type buffer_flag __unused,
                                   bmi_msg_tag_t tag,
                                   void *user_ptr,
                                   bmi_context_id context_id,
                                   PVFS_hint hints __unused)
{
    return post_recv(id,
                     remote_map,
                     list_count,
                     buffers,
                     sizes,
                     tot_expected_len,
                     tag,
                     user_ptr,
                     context_id);
}

/*
 * test_sq()
 *
 * Description:
 *  Internal shared helper function to push send operations along. Tests/pushes
 *  a single sendq entry.
 *
 * Params:
 *  [in/out] sq    - pointer to sendq entry to test/push
 *  [out] outid    - operation identifier for op if completed
 *  [out] err      - BMI error code; set to 0 if op completed successfully
 *                   or -PVFS_ETIMEDOUT if op was cancelled
 *  [out] size     - size of sent data for completed operation
 *  [out] user_ptr - user_ptr associated with completed operation
 *  [in] complete  - indicates whether the operation has completed (TODO: is 
 *                   this the best way to describe this? Is it really whether
 *                   it HAS completed or is it whether it CAN complete if it is
 *                   in the appropriate state (see testcontext)
 *
 * Returns:
 *  0 if the operation hasn't completed, 1 if it has
 */
static int test_sq(struct rdma_work *sq,
                   bmi_op_id_t *outid,
                   bmi_error_code_t *err,
                   bmi_size_t *size,
                   void **user_ptr,
                   int complete)
{
    rdma_connection_t *c;
    
    debug(9, "%s: sq %p outid %p err %p size %p user_ptr %p complete %d",
          __func__, sq, outid, err, size, user_ptr, complete);
    
    if (sq->state.send == SQ_WAITING_USER_TEST)
    {
        if (complete)
        {
            debug(2, "%s: sq %p completed %lld to %s",
                  __func__,
                  sq,
                  lld(sq->buflist.tot_len),
                  sq->c->peername);
            
            *outid = sq->mop->op_id;
            *err = 0;
            *size = sq->buflist.tot_len;
            
            if (user_ptr)
            {
                *user_ptr = sq->mop->user_ptr;
            }
            
            qlist_del(&sq->list);
            id_gen_fast_unregister(sq->mop->op_id);
            c = sq->c;
            free(sq->mop);
            free(sq);
            --c->refcnt;
            
            if (c->closed || c->cancelled)
            {
                rdma_close_connection(c);
            }
            
            return 1;
        }
        /* this state needs help, push it (ideally would be triggered
         * when the resource is freed... XXX */
    }
    else if (sq->state.send == SQ_WAITING_BUFFER)
    {
        debug(2, "%s: sq %p %s, encouraging",
              __func__,
              sq,
              sq_state_name(sq->state.send));
        encourage_send_waiting_buffer(sq);
    }
    else if (sq->state.send == SQ_WAITING_RTS_DONE_BUFFER)
    {
        debug(2, "%s: sq %p %s, encouraging",
              __func__,
              sq,
              sq_state_name(sq->state.send));
        encourage_rts_done_waiting_buffer(sq);
    }
    else if (sq->state.send == SQ_CANCELLED && complete)
    {
        debug(2, "%s: sq %p cancelled", __func__, sq);
        *outid = sq->mop->op_id;
        *err = -PVFS_ETIMEDOUT;
        
        if (user_ptr)
        {
            *user_ptr = sq->mop->user_ptr;
        }
        
        qlist_del(&sq->list);
        id_gen_fast_unregister(sq->mop->op_id);
        c = sq->c;
        free(sq->mop);
        free(sq);
        --c->refcnt;
        
        if (c->closed || c->cancelled)
        {
            rdma_close_connection(c);
        }
        
        return 1;
    }
    else
    {
        debug(9, "%s: sq %p found, not done, state %s",
              __func__,
              sq,
              sq_state_name(sq->state.send));
    }
    
    return 0;
}

/*
 * test_rq()
 *
 * Description:
 *  Internal shared helper function to push recv operations along. Tests/pushes
 *  a single recvq entry. Note that rq->mop can be null for unexpected messages.
 *
 * Params:
 *  [in/out] rq    - pointer to recvq entry to test/push
 *  [out] outid    - operation identifier for op if completed
 *  [out] err      - BMI error code; set to 0 if op completed successfully
 *                   or -PVFS_ETIMEDOUT if op was cancelled
 *  [out] size     - actual size of data received if op completed (could be 
 *                   less than expected/posted)
 *  [out] user_ptr - user_ptr associated with op if completed
 *  [in] complete  - indicates whether the operation has completed (TODO: is
 *                   this the best way to describe this? Is it really whether
 *                   it HAS completed or is it whether it CAN complete if it is
 *                   in the appropriate state (see testcontext)
 *
 * Returns:
 *  0 if the operation hasn't completed, 1 if it has
 */
static int test_rq(struct rdma_work *rq,
                   bmi_op_id_t *outid,
                   bmi_error_code_t *err,
                   bmi_size_t *size,
                   void **user_ptr,
                   int complete)
{
    rdma_connection_t *c;
    
    debug(9, "%s: rq %p outid %p err %p size %p user_ptr %p complete %d",
          __func__, rq, outid, err, size, user_ptr, complete);
    
    if (rq->state.recv == RQ_EAGER_WAITING_USER_TEST ||
        rq->state.recv == RQ_RTS_WAITING_USER_TEST)
    {
        if (complete)
        {
            debug(2, "%s: rq %p completed %lld from %s",
                  __func__,
                  rq,
                  lld(rq->actual_len),
                  rq->c->peername);
            
            *err = 0;
            *size = rq->actual_len;
            
            if (rq->mop)
            {
                *outid = rq->mop->op_id;
                if (user_ptr)
                {
                    *user_ptr = rq->mop->user_ptr;
                }
                id_gen_fast_unregister(rq->mop->user_ptr);
                free(rq->mop);
            }
            
            qlist_del(&rq->list);
            c = rq->c;
            free(rq);
            --c->refcnt;
            
            if (c->closed || c->cancelled)
            {
                rdma_close_connection(c);
            }
            
            return 1;
        }
        /* this state needs help, push it (ideally would be triggered
         * when the resource is freed...) XXX */
    }
    else if (rq->state.recv == RQ_RTS_WAITING_CTS_BUFFER)
    {
        int ret;
        
        debug(2, "%s: rq %p %s, encouraging",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
        
        ret = send_cts(rq);
        if (ret == 0)
        {
            rq->state.recv = RQ_RTS_WAITING_RTS_DONE |
                             RQ_RTS_WAITING_CTS_SEND_COMPLETION |
                             RQ_RTS_WAITING_USER_TEST;
        } /* else keep waiting until we can send that cts */
        
        debug(2, "%s: rq %p now %s",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
    }
    else if (rq->state.recv == RQ_CANCELLED && complete)
    {
        debug(2, "%s: rq %p canelled", __func__, rq);
        *err = -PVFS_ETIMEDOUT;
        
        if (rq->mop)
        {
            *outid = rq->mop->op_id;
            if (user_ptr)
            {
                *user_ptr = rq->mop->user_ptr;
            }
            id_gen_fast_unregister(rq->mop->op_id);
            free(rq->mop);
        }
        
        qlist_del(&rq->list);
        c = rq->c;
        free(rq);
        --c->refcnt;
        
        if (c->closed || c->cancelled)
        {
            rdma_close_connection(c);
        }
        return 1;
    }
    else
    {
        debug(9, "%s: rq %p found, not done, state %s",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
    }
    
    return 0;
}

/*
 * BMI_rdma_testcontext()
 *
 * Description:
 *  Test for multiple completions matching a particular user context.
 *
 * Params:
 *  [in] incount    - max number of completed ops that can be reaped
 *  [out] outids    - array of operation identifiers for completed ops
 *  [out] outcount  - number of completed ops
 *  [out] errs      - array of BMI error codes corresponding to completed ops;
 *                    set by test_[sq,rq] (0 or -PVFS_ETIMEDOUT)
 *  [out] sizes     - array of sent/received data sizes for completed ops
 *  [out] user_ptrs - array of user_ptrs associated with completed ops
 *  [in] max_idle_time - max time (in ms) allowed to block while waiting for 
 *                       CQ events or new connections
 *  [in] context_id    - user context to look for completions in
 *
 * Returns:
 *  0 if okay, >0 if want another poll soon, <0 for error.
 */
static int BMI_rdma_testcontext(int incount,
                                bmi_op_id_t *outids,
                                int *outcount,
                                bmi_error_code_t *errs,
                                bmi_size_t *sizes,
                                void **user_ptrs,
                                int max_idle_time,
                                bmi_context_id context_id)
{
    struct qlist_head *l, *lnext;
    int n = 0;
    int complete = 0;
    int activity = 0;
    void **up = NULL;
    
    gen_mutex_lock(&interface_mutex);
    
restart:
    activity += rdma_check_cq();
    
    /*
     * Walk _all_ entries on sq, rq, marking them completed or
     * encouraging them as needed due to resource limitations.
     */
    for (l = rdma_device->sendq.next; l != &rdma_device->sendq; l = lnext)
    {
        struct rdma_work *sq = qlist_upcast(l);
        lnext = l->next;
        
        /* test them all, even if can't reap them, just to encourage */
        complete = (sq->mop->context_id == context_id) && (n < incount);
        
        if (user_ptrs)
        {
            up = &user_ptrs[n];
        }
        
        n += test_sq(sq, &outids[n], &errs[n], &sizes[n], up, complete);
    }
    
    for (l = rdma_device->recvq.next; l != &rdma_device->recvq; l = lnext)
    {
        struct rdma_work *rq = qlist_upcast(l);
        lnext = l->next;
        
        /* some receives have no mops:  unexpected */
        complete = rq->mop &&
                   (rq->mop->context_id == context_id) &&
                   (n < incount);
        
        if (user_ptrs)
        {
            up = &user_ptrs[n];
        }
        
        n += test_rq(rq, &outids[n], &errs[n], &sizes[n], up, complete);
    }
    
    /* drop lock before blocking on new connections below */
    gen_mutex_unlock(&interface_mutex);
    
    if (activity == 0 && n == 0 && max_idle_time > 0)
    {
        /*
         * Block if told to from above.
         */
        debug(8, "%s: last activity too long ago, blocking", __func__);
        activity = rdma_block_for_activity(max_idle_time);
        if (activity == 1)
        {
            /* RDMA action, go do it immediately */
            gen_mutex_lock(&interface_mutex);
            goto restart;
        }
    }
    
    *outcount = n;
    return activity + n;
}

/*
 * BMI_rdma_testunexpected()
 *
 * Description:
 *  Test to look for an incoming unexpected message. This is also where we 
 *  check for new connections, since those would show up as unexpected the 
 *  first time anything is sent. Checks for one at a time; returns as soon as
 *  it finds an unexpected message.
 *
 * TODO: since it only finds one at a time, is there a better name for outcount?
 * TODO: are the return values right?
 *
 * Params:
 *  [-] incount        - unused
 *  [out] outcount     - 0 if nothing found, 1 if unexpected message found
 *  [out] ui           - information about incoming unexpected message
 *  [in] max_idle_time - max time (in ms) allowed to block while waiting for
 *                       CQ events or new connections
 *
 * Returns:
 *  0 on success, >0 if want another poll soon, -1 on failure
 */
static int BMI_rdma_testunexpected(int incount __unused,
                                   int *outcount,
                                   struct bmi_method_unexpected_info *ui,
                                   int max_idle_time)
{
    struct qlist_head *l;
    int activity = 0;
    int n;
    
    gen_mutex_lock(&interface_mutex);
    
    /* Check CQ, then look for the first unexpected message. */
restart:
    activity += rdma_check_cq();
    
    n = 0;
    qlist_for_each(l, &rdma_device->recvq)
    {
        struct rdma_work *rq = qlist_upcast(l);
        if (rq->state.recv == RQ_EAGER_WAITING_USER_TESTUNEXPECTED)
        {
            msg_header_eager_t mh_eager;
            char *ptr = rq->bh->buf;
            rdma_connection_t *c = rq->c;
            
            decode_msg_header_eager_t(&ptr, &mh_eager);
            
            debug(2, "%s: found waiting testunexpected", __func__);
            ui->error_code = 0;
            ui->addr = c->remote_map;   /* hand back permanent method_addr */
            ui->buffer = bmi_rdma_malloc((unsigned long) rq->actual_len);
            ui->size = rq->actual_len;
            memcpy(ui->buffer,
                   (msg_header_eager_t *) rq->bh->buf + 1,
                   (size_t) ui->size);
            ui->tag = rq->bmi_tag;
            
            /* re-post the buffer in which it was sitting, just unexpecteds */
            debug(2, "%s: calling post_rr", __func__);
            post_rr(c, rq->bh);
            n = 1;
            qlist_del(&rq->list);
            free(rq);
            --c->refcnt;
            
            if (c->closed || c->cancelled)
            {
                rdma_close_connection(c);
            }
            
            goto out;
        }
    }
    
out:
    gen_mutex_unlock(&interface_mutex);
    
    if (activity == 0 && n == 0 && max_idle_time > 0)
    {
        /*
         * Block if told to from above, also polls listening ID.
         */
        debug(8, "%s: last activity too long ago, blocking", __func__);
        activity = rdma_block_for_activity(max_idle_time);
        if (activity == 1)
        {
            /* RDMA action, go do it immediately */
            gen_mutex_lock(&interface_mutex);
            goto restart;
        }
    }
    
    *outcount = n;
    return activity + n;
}

/*
 * No need to track these internally.  Just search the entire queue.
 *
 * TODO: if these are unused can we just remove them?
 */
static int BMI_rdma_open_context(bmi_context_id context_id __unused)
{
    return 0;
}

static void BMI_rdma_close_context(bmi_context_id context_id __unused)
{
}

/*
 * BMI_rdma_cancel()
 *
 * Description:
 *  Asynchronous call to destroy an in-progress operation. Can't just call 
 *  test since we don't want to reap the operation, just make sure it's 
 *  done or not.
 *
 * Params:
 *  [in] id - operation identifier of op to cancel
 *  [-] context_id - unused
 *
 * Returns:
 *  0 no matter what (TODO: should this change? void? error cases?)
 */
static int BMI_rdma_cancel(bmi_op_id_t id,
                           bmi_context_id context_id __unused)
{
    struct method_op *mop;
    struct rdma_work *tsq;
    rdma_connection_t *c = 0;
    
    gen_mutex_lock(&interface_mutex);
    rdma_check_cq();
    mop = id_gen_fast_lookup(id);
    tsq = mop->method_data;
    
    if (tsq->type == BMI_SEND)
    {
        /*
         * Cancelling completed operations is fine, they will be
         * tested later.  Any others trigger full shutdown of the
         * connection.
         */
        if (tsq->state.send != SQ_WAITING_USER_TEST)
        {
            c = tsq->c;
        }
    }
    else
    {
        /* actually a recv */
        struct rdma_work *rq = mop->method_data;
        
        if (!(rq->state.recv == RQ_EAGER_WAITING_USER_TEST ||
              rq->state.recv == RQ_RTS_WAITING_USER_TEST))
        {
            c = rq->c;
        }
    }
    
    if (c && !c->cancelled)
    {
        /*
         * In response to a cancel, forcibly close the connection.  Don't send
         * a bye message first since it may be the case that the peer is dead
         * anyway.  Do not close the connetion until all the sq/rq on it have
         * gone away.
         */
        struct qlist_head *l;
        
        c->cancelled = 1;
        disconnect(c);  /* TODO: temporary wrapper for rdma_disconnect() */
        //rdma_disconnect(((rdma_connection_priv *) c->priv)->id);
        /* 
         * TODO: does rdma_disconnect() accomplish everything that was
         * previously handled in drain_qp()?
         */
        
        qlist_for_each(l, &rdma_device->sendq)
        {
            struct rdma_work *sq = qlist_upcast(l);
            if (sq->c != c)
            {
                continue;
            }
            
            if (sq->state.send == SQ_WAITING_DATA_SEND_COMPLETION)
            {
                memcache_deregister(rdma_device->memcache, &sq->buflist);
            }
            
            /* pin when sending rts, so also must dereg in this state */
            if (sq->state.send == SQ_WAITING_RTS_SEND_COMPLETION ||
                sq->state.send == SQ_WAITING_RTS_SEND_COMPLETION_GOT_CTS ||
                sq->state.send == SQ_WAITING_CTS)
            {
                memcache_deregister(rdma_device->memcache, &sq->buflist);
            }

            if (sq->state.send != SQ_WAITING_USER_TEST)
            {
                sq->state.send = SQ_CANCELLED;
            }
        }
        
        qlist_for_each(l, &rdma_device->recvq)
        {
            struct rdma_work *rq = qlist_upcast(l);
            if (rq->c != c)
            {
                continue;
            }
            
            if (rq->state.recv & RQ_RTS_WAITING_RTS_DONE)
            {
                memcache_deregister(rdma_device->memcache, &rq->buflist);
            }
            
            /* pin on post, dereg all these */
            if (rq->state.recv == RQ_WAITING_INCOMING &&
                rq->buflist.tot_len > rdma_device->eager_buf_payload)
            {
                memcache_deregister(rdma_device->memcache, &rq->buflist);
            }

            if (!(rq->state.recv == RQ_EAGER_WAITING_USER_TEST ||
                  rq->state.recv == RQ_RTS_WAITING_USER_TEST))
            {
                rq->state.recv = RQ_CANCELLED;
            }
        }
    }
    
    gen_mutex_unlock(&interface_mutex);
    return 0;
}

/*
 * BMI_rdma_rev_lookup()
 *
 * Description:
 *  Get string representation of peername.
 *
 * Params:
 *  [in] meth - pointer to struct holding peer addr/connection info
 *
 * Returns:
 *  String representation of peername if connected, otherwise returns the
 *  string "(unconnected)".
 */
static const char *BMI_rdma_rev_lookup(struct bmi_method_addr *meth)
{
    rdma_method_addr_t *rdma_map = meth->method_data;
    
    if (!rdma_map->c)
    {
        return "(unconnected)";
    }
    else
    {
        return rdma_map->c->peername;
    }
}

/*
 * rdma_alloc_method_addr()
 *
 * Description:
 *  Build and fill a RDMA-specific method_addr structure.
 *
 * Params:
 *  [in] c              - pointer to connection new method_addr will belong to
 *  [in] hostname       - hostname of new method_addr
 *  [in] port           - port number of new method_addr
 *  [in] reconnect_flag - indicates whether to automatically connect/reconnect
 *                        if a send or recv is posted when disconnected
 *
 * Returns:
 *  Pointer to new method_addr structure with passed-in values filled in
 */
static struct bmi_method_addr *rdma_alloc_method_addr(rdma_connection_t *c,
                                                      char *hostname,
                                                      int port,
                                                      int reconnect_flag)
{
    struct bmi_method_addr *map;
    rdma_method_addr_t *rdma_map;
    
    map = bmi_alloc_method_addr(bmi_rdma_method_id,
                                (bmi_size_t) sizeof(*rdma_map));
    rdma_map = map->method_data;
    rdma_map->c = c;
    rdma_map->hostname = hostname;
    rdma_map->port = port;
    rdma_map->reconnect_flag = reconnect_flag;
    rdma_map->ref_count = 1;
    
    return map;
}

/*
 * BMI_rdma_method_addr_lookup()
 *
 * Description:
 *  Break up a method string like:
 *    rdma://hostname:port/filesystem
 *  into its constituent fields, storing them in an opaque type, which is 
 *  then returned.
 *
 *  XXX: I'm assuming that these actually return a _const_ pointer
 *  so that I can hand back an existing map.
 *
 * Params:
 *  [in] id - method address string
 *
 * Returns:
 *  Pointer to existing or newly created method_addr structure with the 
 *  hostname and port number found in the given method string; NULL on failure
 */
static struct bmi_method_addr *BMI_rdma_method_addr_lookup(const char *id)
{
    /* 
     * TODO: what string(s) do I need to match in this module?
     * What will addresses start with ("ib", "rdma", "roce")? 
     */
    
    char *s, *hostname, *cp, *cq;
    int port;
    struct bmi_method_addr *map = NULL;
    
    /* remove "rdma://" */
    s = string_key("rdma", id);  /* allocs a string */
    if (!s)
    {
        return 0;
    }
    
    /* Locate ':' character between hostname and port number */
    cp = strchr(s, ':');
    if (!cp)
    {
        error("%s: no ':' found", __func__);
        free(s);
        return NULL;
    }
    
    /* copy hostname and port number to permanent storage */
    hostname = bmi_rdma_malloc((unsigned long) (cp - s + 1));
    strncpy(hostname, s, (size_t) (cp - s));
    hostname[cp - s] = '\0';
    
    /* strip /filesystem */
    ++cp;
    cq = strchr(cp, '/');   /* locate '/' between port number and filesystem */
    if (cq)
    {
        *cq = 0;    /* replaces '/' with null character */
    }
    
    /* convert port number to unsigned long int */
    port = strtoul(cp, &cq, 10);
    if (cq == cp)
    {
        error("%s: invalid port number", __func__);
        free(s);
        return NULL;
    }
    if (*cq != '\0')
    {
        error("%s: extra characters after port number", __func__);
        free(s);
        return NULL;
    }
    free(s);
    
    /* lookup in known connections, if there are any */
    gen_mutex_lock(&interface_mutex);
    if (rdma_device)
    {
        struct qlist_head *l;
        qlist_for_each(l, &rdma_device->connection)
        {
            rdma_connection_t *c = qlist_upcast(l);
            rdma_method_addr_t *rdma_map = c->remote_map->method_data;
            if (rdma_map->port == port && !strcmp(rdma_map->hostname, hostname))
            {
                map = c->remote_map;
                rdma_map->ref_count++;
                break;
            }
        }
    }
    gen_mutex_unlock(&interface_mutex);
    
    if (map)
    {
        free(hostname);  /* found it */
    }
    else
    {
        /* Allocate a new method_addr and set the reconnect flag; we will be
         * acting as a client for this connection and will be responsible for
         * making sure that the connection is established
         */
        map = rdma_alloc_method_addr(0, hostname, port, 1);
        /* but don't call bmi_method_addr_reg_callback! */
    }
    
    return map;
}

/*
 * rdma_new_connection()
 *
 * Description:
 *  Build new connection.
 *
 * Params:
 *  [in] id - RDMA CM identifier new connection will be associated with
 *  [in] peername - remote peername for the connection
 *  [in] is_server - 0=client, 1=server
 *
 * Returns:
 *  Pointer to new connection structure on success, NULL on failure
 */
static rdma_connection_t *rdma_new_connection(struct rdma_cm_id *id,
                                              const char *peername,
                                              int is_server)
{
    rdma_connection_t *c;
    int i, ret;
    
    if (is_server)
    {
        debug(4, "%s: [SERVER] starting, peername=%s", __func__, peername);
    }
    else
    {
        debug(4, "%s: [CLIENT] starting, peername=%s", __func__, peername);
    }
    
    c = malloc(sizeof(*c));
    if (!c)
    {
        /* TODO: is this the best way to handle it? exit ?*/
        error("%s: malloc %ld bytes failed", __func__, sizeof(*c));
    }
    c->peername = strdup(peername);
    
    /* fill send and recv free lists and buf heads */
    c->eager_send_buf_contig = malloc(rdma_device->eager_buf_num *
                                      rdma_device->eager_buf_size);
    c->eager_recv_buf_contig = malloc(rdma_device->eager_buf_num *
                                      rdma_device->eager_buf_size);
    
    if (!c->eager_send_buf_contig || !c->eager_recv_buf_contig)
    {
        /* TODO: is this the best way to handle it? exit? */
        error("%s: malloc %ld bytes failed",
              __func__,
              rdma_device->eager_buf_num * rdma_device->eager_buf_size);
    }
    
    INIT_QLIST_HEAD(&c->eager_send_buf_free);
    INIT_QLIST_HEAD(&c->eager_recv_buf_free);
    
    c->eager_send_buf_head_contig = malloc(rdma_device->eager_buf_num *
                                        sizeof(*c->eager_send_buf_head_contig));
    c->eager_recv_buf_head_contig = malloc(rdma_device->eager_buf_num *
                                        sizeof(*c->eager_recv_buf_head_contig));
    
    if (!c->eager_send_buf_head_contig || !c->eager_recv_buf_head_contig)
    {
        /* TODO: is this the best way to handle it? exit? */
        error("%s: malloc failed", __func__);
    }
    
    for (i = 0; i < rdma_device->eager_buf_num; i++)
    {
        struct buf_head *ebs = &c->eager_send_buf_head_contig[i];
        struct buf_head *ebr = &c->eager_recv_buf_head_contig[i];
        
        INIT_QLIST_HEAD(&ebs->list);
        INIT_QLIST_HEAD(&ebr->list);
        ebs->c = c;
        ebr->c = c;
        ebs->num = i;
        ebr->num = i;
        ebs->buf = (char *) c->eager_send_buf_contig + i *
                   rdma_device->eager_buf_size;
        ebr->buf = (char *) c->eager_recv_buf_contig + i *
                   rdma_device->eager_buf_size;
        
        qlist_add_tail(&ebs->list, &c->eager_send_buf_free);
        qlist_add_tail(&ebr->list, &c->eager_recv_buf_free);
    }
    
    /* put it on the list */
    qlist_add(&c->list, &rdma_device->connection);
    
    /* other vars */
    c->remote_map = 0;
    c->cancelled = 0;
    c->refcnt = 0;
    c->closed = 0;
    
    /* save one credit back for emergency credit refill */
    c->send_credit = rdma_device->eager_buf_num - 1;
    c->return_credit = 0;
    
    if (is_server)
    {
        debug(4, "%s: [SERVER SIDE] calling new_connection, channel=%d",
              __func__, id->channel->fd);
    }
    else
    {
        debug(4, "%s: [CLIENT SIDE] calling new_connection, channel=%d",
              __func__, id->channel->fd);
    }
    
    ret = new_connection(c, id, is_server);
    if (ret)
    {
        rdma_close_connection(c);
        c = NULL;
    }
    
    return c;
}

/*
 * rdma_close_connection()
 *
 * Description:
 *  Try to close and free a connection, but only do it if refcnt has
 *  gone to zero.
 *
 * Params:
 *  [in/out] c - pointer to connection to close
 *
 * Returns:
 *  none
 */
static void rdma_close_connection(rdma_connection_t *c)
{
    debug(2, "%s: closing connection to %s", __func__, c->peername);
    c->closed = 1;
    if (c->refcnt != 0)
    {
        debug(1, "%s: refcnt non-zero %d, delaying free", __func__, c->refcnt);
        return;
    }
    
    close_connection(c);
    
    free(c->eager_send_buf_contig);
    free(c->eager_recv_buf_contig);
    free(c->eager_send_buf_head_contig);
    free(c->eager_recv_buf_head_contig);
    
    /* never free the remote map, for the life of the executable, just
     * mark it unconnected since BMI will always have this structure. */
    if (c->remote_map)
    {
        rdma_method_addr_t *rdma_map = c->remote_map->method_data;
        rdma_map->c = NULL;
    }
    
    free(c->peername);
    qlist_del(&c->list);
    free(c);
}

/*
 * rdma_client_event_loop()
 *
 * Description:
 *  Event handler for client-side RDMA Communication Manager events
 *
 * Params:
 *  [in] ec - event channel to watch for events on
 *  [out] rdma_map - rdma specific method data for remote_map
 *  [in] remote_map - peer address/connection info
 *  [in] timeout_ms - time rdma_resolve_route() will wait for resolution
 *
 * Returns:
 *   0 on success, -errno on failure
 *
 * TODO: should this be a thread? should it be broken into separate functions
 *       for the different events? how do we break out of the loop? should 
 *       we check for RDMA_CM_EVENT_DISCONNECTED here instead of wherever
 *       else we handle it?
 */
static int rdma_client_event_loop(struct rdma_event_channel *ec,
                                  rdma_method_addr_t *rdma_map,
                                  struct bmi_method_addr *remote_map,
                                  int timeout_ms)
{
    struct rdma_cm_event *event = NULL;
    char peername[2048];
    int ret = -1;
    
    while (rdma_get_cm_event(ec, &event) == 0)
    {
        struct rdma_cm_event event_copy;
        
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        
        if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED)
        {
            ret = rdma_resolve_route(event_copy.id, timeout_ms);
            if (ret)
            {
                warning("%s: cannot resolve RDMA route to dest: %m", __func__);
                return -errno;
            }
        }
        else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED)
        {
            sprintf(peername, "%s:%d",
                    inet_ntoa(event_copy.id->route.addr.dst_sin.sin_addr),
                    rdma_map->port);
            
            debug(4, "%s: connecting to peername=%s", __func__, peername);
            
            rdma_map->c = rdma_new_connection(event_copy.id, peername, 0);
            if (!rdma_map->c)
            {
                error("%s: rdma_new_connection failed", __func__);
                return -EINVAL; /* TODO: more appropriate error? */
            }
            rdma_map->c->remote_map = remote_map;
            
            debug(4, "%s: connection complete", __func__);

            break;
        }
    }

    return 0;
}

/*
 * rdma_client_connect()
 *
 * Description:
 *  Blocking connect initiated by a post_sendunexpected{,_list}, or post_recv*
 *
 * Params:
 *  [in/out] rdma_map - rdma specific method data for remote_map
 *  [in] remote_map - peer address/connection info
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int rdma_client_connect(rdma_method_addr_t *rdma_map,
                               struct bmi_method_addr *remote_map)
{
    int ret;
    char *port_str = NULL;
    int port_str_len = 0;
    struct rdma_addrinfo *addrinfo = NULL;
    struct rdma_cm_id *conn_id = NULL;
    struct rdma_event_channel *ec = NULL;
    int timeout_ms = 500;   /* TODO: choose optimized timeout value */
    
    debug(4, "%s: starting", __func__);
    
    /*
     * Convert the port number to a string.
     * NOTE: The first snprintf() call returns the number of characters in
     * the string representation of the port number. The second call 
     * actually writes the port number into the string. 
     */
    port_str_len = snprintf(NULL, 0, "%d", rdma_map->port);
    port_str = (char *) malloc(port_str_len + 1);
    if (!port_str)
    {
        ret = -BMI_ENOMEM;
        goto error_out;
    }
    snprintf(port_str, (port_str_len + 1), "%d", rdma_map->port);
    
    ret = rdma_getaddrinfo(rdma_map->hostname, port_str, NULL, &addrinfo);
    if (ret)
    {
        warning("%s: cannot get addrinfo for server %s: %m",
                __func__, rdma_map->hostname);
        ret = -errno;
        goto error_out;
    }
    
    free(port_str);
    port_str = NULL;
    
    ec = rdma_create_event_channel();
    if (!ec)
    {
        warning("%s: create rdma event channel: %m", __func__);
        ret = -errno;
        goto error_out;
    }
    
    ret = rdma_create_id(ec, &conn_id, NULL, RDMA_PS_TCP);
    /* TODO: RDMA_PS_TCP vs RDMA_PS_IB? */
    if (ret)
    {
        warning("%s: create rdma communication manager id: %m", __func__);
        ret = -errno;
        goto error_out;
    }
    
    ret = rdma_resolve_addr(conn_id, NULL, addrinfo->ai_dst_addr, timeout_ms);
    if (ret)
    {
        warning("%s: cannot resolve dest IP to RDMA address: %m", __func__);
        ret = -errno;
        goto error_out;
    }
    
    rdma_freeaddrinfo(addrinfo);
    
    ret = rdma_client_event_loop(ec, rdma_map, remote_map, timeout_ms);
    if (ret)
    {
        goto error_out;
    }
    
    return 0;
    
error_out:
    
    if (addrinfo)
    {
        if (ec)
        {
            if (conn_id)
            {
                rdma_destroy_id(conn_id);
                conn_id = NULL;
            }
            
            rdma_destroy_event_channel(ec);
            ec = NULL;
        }
        
        rdma_freeaddrinfo(addrinfo);
    }
    
    if (port_str)
    {
        free(port_str);
        port_str = NULL;
    }
    
    return bmi_errno_to_pvfs(ret);
}

/*
 * rdma_server_init_listener()
 *
 * Description:
 *  On a server, initialize a connection to listen for connect requests.
 *
 * Params:
 *  [in] addr - local address to listen for connect requests on
 *
 * Returns:
 *  none
 */
static void rdma_server_init_listener(struct bmi_method_addr *addr)
{
    int flags;
    struct sockaddr_in skin;
    rdma_method_addr_t *rc = addr->method_data;
    struct rdma_event_channel *ec = NULL;
    int *timeout_ms;
    int ret = 0;
    
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = AF_INET;
    skin.sin_port = htons(rc->port);
    
    ec = rdma_create_event_channel();
    if (!ec)
    {
        error_errno("%s: create event channel", __func__);
        exit(1);
    }
    
    ret = rdma_create_id(ec, &rdma_device->listen_id, NULL, RDMA_PS_TCP);
    /* TODO: RDMA_PS_TCP vs RDMA_PS_IB? */
    if (ret)
    {
        error_errno("%s: create RDMA_CM id", __func__);
    }

retry:
    ret = rdma_bind_addr(rdma_device->listen_id, (struct sockaddr *) &skin);
    if (ret)
    {
        if (errno == EINTR)
        {
            goto retry;
        }
        else
        {
            error_errno("%s: bind addr", __func__);
            exit(1);
        }
    }
    
    debug(4, "%s: binding on port %d", __func__, rc->port);
    
    ret = rdma_listen(rdma_device->listen_id, listen_backlog);
    if (ret)
    {
        error_errno("%s: listen for incoming connection requests", __func__);
        exit(1);
    }
    
    flags = fcntl(rdma_device->listen_id->channel->fd, F_GETFL);
    if (flags < 0)
    {
        error_errno("%s: fcntl getfl listen id", __func__);
        exit(1);
    }
    
    flags |= O_NONBLOCK;
    if (fcntl(rdma_device->listen_id->channel->fd, F_SETFL, flags) < 0)
    {
        error_errno("%s: fcntl setfl nonblock listen id", __func__);
        exit(1);
    }
    
    timeout_ms = (int *) malloc(sizeof(int));
    if (timeout_ms)
    {
        *timeout_ms = accept_timeout_ms;
        /* start the accept thread */
        debug(0, "%s: starting rdma_server_accept_thread", __func__);
        if (pthread_create(&accept_thread_id,
                           NULL,
                           &rdma_server_accept_thread,
                           timeout_ms))
        {
            error("%s: unable to start accept thread, errno=%d",
                  __func__, errno);
            exit(1);
        }
    }
    else
    {
        error("%s: unable to malloc memory for timeout_ms, errno=%d",
              __func__, errno);
        exit(1);
    }
}

/*
 * rdma_server_accept_thread()
 *
 * Description:
 *  Server thread that waits for a connection request event from the client to
 *  show up on the listen id's event channel and accepts it, then starts the
 *  server-side client thread.
 *
 * Params (TODO):
 *  [in] arg - timeout_ms passed as thread arg to pthread_create?
 *
 * Returns:
 *  TODO - doesn't? not until server is stopped??
 */
void *rdma_server_accept_thread(void *arg)
{
    struct rdma_cm_event *event = NULL;
    int ret = 0;
    int timeout_ms = 10000;
    struct rdma_conn *rc;
    //struct rdma_cm_id *conn_id;
    gen_thread_t thread;
    
    if (arg)
    {
        timeout_ms = *(int *) arg;
        free(arg);
    }
    
    debug(0, "%s: starting, timeout_ms=%d", __func__, timeout_ms);
    
    for (;;)
    {
        /* check for shutdown */
        gen_mutex_lock(&accept_thread_mutex);
        if (accept_thread_shutdown)
        {
            gen_mutex_unlock(&accept_thread_mutex);
            break;
        }
        gen_mutex_unlock(&accept_thread_mutex);
        
        /* wait for a connection request */
        while (rdma_get_cm_event(rdma_device->listen_id->channel, &event) == 0)
        {
            struct rdma_cm_event event_copy;
            
            memcpy(&event_copy, event, sizeof(*event));
            rdma_ack_cm_event(event);
            
            /* 
             * TODO: should I use a while loop here? Will a connect request
             *       always be the first event that happens on a server? 
             */
            if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST)
            {
                debug(4, "%s: received event: %s",
                      __func__, rdma_event_str(event_copy.event));
                
                /* TODO: do I need to build the connection/context here? */
                
                /* TODO: do I need to do pre-connection stuff here? */
                
                /* TODO: do I need to pass any connection parameters? */
                ret = rdma_accept(event_copy.id, NULL);
                if (ret)
                {
                    warning("%s: accept connection, errno=%d", __func__, errno);
                    continue;
                }
                
                /* TODO: is this where I need to wait for 
                 *       RDMA_CM_EVENT_ESTABLISHED to occur? Or at least 
                 *       somewhere around here, before starting the 
                 *       rdma_server_process_client_thread?
                 */
                
#if 1 /* TODO: can we get rid of rc? */
                rc = (struct rdma_conn *) malloc(sizeof(*rc));
                if (!rc)
                {
                    warning("%s: unable to malloc rc, errno=%d",
                            __func__, errno);
                    sleep(30);
                    continue;
                }
                
                rc->id = event_copy.id;
                rc->hostname = strdup(
                                inet_ntoa(rc->id->route.addr.dst_sin.sin_addr));
                rc->port = ntohs(rc->id->route.addr.dst_sin.sin_port);
                sprintf(rc->peername, "%s:%d", rc->hostname, rc->port);
                
                debug(0, "%s: starting rdma_server_process_client_thread "
                      "for channel=%d",
                      __func__, rc->id->channel->fd);
                
                /* start the client thread */
                if (pthread_create(&thread,
                                   NULL,
                                   &rdma_server_process_client_thread,
                                   rc))
                {
                    warning("%s: unable to create accept_client "
                            "thread, errno=%d",
                            __func__, errno);
                    free(rc);
                }
#else
//                conn_id = (struct rdma_cm_id *) malloc(sizeof(*conn_id));
//                if (!conn_id)
//                {
//                    warning("%s: unable to malloc conn_id, errno=%d",
//                            __func__, errno);
//                    sleep(30);
//                    continue;
//                }
//                
//                conn_id = event_copy.id;
//                
//                debug(0, "%s: starting rdma_server_process_client_thread "
//                      "for channel=%d",
//                      __func__, conn_id->channel->fd);
//                
//                /* start the client thread */
//                if (pthread_create(&thread,
//                                   NULL,
//                                   &rdma_server_process_client_thread,
//                                   conn_id))
//                {
//                    warning("%s: unable to create accept_client "
//                            "thread, errno=%d",
//                            __func__, errno);
//                    free(conn_id);
//                }
#endif
                
                
                /* TODO: break out of the loop or return? */
            }
            
            /* TODO: do I need to handle a disconnect event here too? */
        }
    }
    
    pthread_exit(0);
}

/*
 * rdma_server_process_client_thread()
 *
 * Description:
 *  Server-side thread that ... creates a new connection to the client???
 *
 * Params (TODO):
 *  [] arg -
 *
 * Returns:
 *  TODO
 *
 * TODO: why is this a thread? (many clients/connections?)
 *       why is it called the client thread? Or is it "process client thread"?
 */
void *rdma_server_process_client_thread(void *arg)
{
    rdma_connection_t *c;
    struct rdma_conn *rc;
    int ret;
    
    debug(0, "%s: starting", __func__);
    if (!arg)
    {
        error("%s: no id passed", __func__);
        return NULL;
    }
    rc = (struct rdma_conn *) arg;
    
    gen_mutex_lock(&interface_mutex);
    
    debug(0, "%s: calling rdma_new_connection for peername=%s on channel=%d",
          __func__, rc->peername, rc->id->channel->fd);
    c = rdma_new_connection(rc->id, rc->peername, 1);
    if (!c)
    {
        error_xerrno(EINVAL, "%s: new rdma connection failed", __func__);
        goto out;
    }
    debug(0, "%s: returned from rdma_new_connection", __func__);
    
    /* don't set reconnect flag on this addr; we are a server in this
     * case and the peer will be responsible for maintaining the
     * connection
     */
    c->remote_map = rdma_alloc_method_addr(c, rc->hostname, rc->port, 0);
    
    /* register this address with the method control layer */
    c->bmi_addr = bmi_method_addr_reg_callback(c->remote_map);
    if (c->bmi_addr == 0)
    {
        error_xerrno(ENOMEM, "%s: bmi_method_addr_reg_callback", __func__);
        goto out;
    }
    
    debug(0, "%s: accepted new connection %s at server",
          __func__, c->peername);
    ret = 1;
    
out:
    gen_mutex_unlock(&interface_mutex);
    /* TODO: equivalent of closing a socket? */
    
    /* TODO: !!! should we really be destroying the qp and id here?
     *   - maybe they were only closing the socket here before because
     *     the socket was only needed to setup the connection, but once
     *     connected it wasn't needed anymore. However, I believe the id
     *     and qp are still needed here.
     */
    if (rc)
    {
        if (rc->id)
        {
            if (rc->id->qp)
            {
                rdma_destroy_qp(rc->id);
            }
            
            rdma_destroy_id(rc->id);
        }
        if (rc->hostname)
        {
            free(rc->hostname);
        }
        free(rc);
    }
    
    return NULL;
}

/*
 * rdma_block_for_activity()
 *
 * Description:
 *  Ask the device to write to its FD if a CQ event happens, and poll on it
 *  as well as the listen_id for activity, but do not actually respond to
 *  anything.  A later rdma_check_cq will handle CQ events, and a later call to
 *  testunexpected will pick up new connections.
 *
 * Params:
 *  [in] timeout_ms - passed to poll; maximum time (in ms) allowed to block
 *
 * Returns:
 *  1 if RDMA device is ready, other >0 for some activity, else 0
 *
 * TODO: how is pfd[2] (accept socket) used here? Why is the error message 
 *       "poll listen sock"?
 */
static int rdma_block_for_activity(int timeout_ms)
{
    struct pollfd pfd[3];   /* cq fd, async fd, accept socket (id) */
    int numfd;
    int ret = 0;
    
    prepare_cq_block(&pfd[0].fd, &pfd[1].fd);
    pfd[0].events = POLLIN;
    pfd[1].events = POLLIN;
    numfd = 2;
    
    ret = poll(pfd, numfd, timeout_ms);
    if (ret > 0)
    {
        if (pfd[0].revents == POLLIN)
        {
            ack_cq_completion_event();
            return 1;
        }
        
        /* check others only if CQ was empty */
        ret = 2;
        if (pfd[1].revents == POLLIN)
        {
            check_async_events();
        }
    }
    else if (ret < 0)
    {
        if (errno == EINTR)
        {
            /* normal, ignore but break */
            ret = 0;
        }
        else
        {
            error_errno("%s: poll listen event channel", __func__);
            return -EINVAL;
        }
    }
    return ret;
}

/*
 * BMI_rdma_get_info()
 *
 * Description:
 *  Callers sometimes want to know odd pieces of information.  Satisfy them.
 *
 * Params:
 *  [in] option - BMI_CHECK_MAXSIZE or BMI_GET_UNEXP_SIZE
 *  [out] param - used to return the requested info to the caller
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_get_info(int option,
                             void *param)
{
    int ret = 0;
    
    switch (option)
    {
        case BMI_CHECK_MAXSIZE:
            /* reality is 2^31, but shrink to avoid negative int */
            *(int *) param = (1UL << 31) - 1;
            break;
            
        case BMI_GET_UNEXP_SIZE:
            *(int *) param = rdma_device->eager_buf_payload;
            break;
            
        default:
            ret = -ENOSYS;
    }
    
    return ret;
}

/*
 * BMI_rdma_set_info()
 *
 * Description:
 *  Used to set some optional parameters and random functions, like ioctl.
 *
 * Params:
 *  [in] option - BMI_DROP_ADDR or BMI_OPTIMISTIC_BUFFER_REG
 *  [-] param   - unused (TODO: whis is this "__unused"?? Looks used to me.)
 *
 * Returns:
 *  0 (TODO: no error?)
 */
static int BMI_rdma_set_info(int option,
                             void *param __unused)
{
    switch (option)
    {
        case BMI_DROP_ADDR:
        {
            struct bmi_method_addr *map = param;
            rdma_method_addr_t *rdma_map = map->method_data;
            rdma_map->ref_count--;
            if (rdma_map->ref_count == 0)
            {
                free(rdma_map->hostname);
                free(map);
            }
            break;
        }
            
        /* 
         * TODO: memcache_preregister() doesn't actually do anything anymore,
         * so this case should removed but it looks like sys-io.sm (and maybe
         * others) still calls BMI_set_info with this option so we need to see
         * what else removing this will effect.
         */
        case BMI_OPTIMISTIC_BUFFER_REG:
        {
            /* not guaranteed to work */
            const struct bmi_optimistic_buffer_info *binfo = param;
            memcache_preregister(rdma_device->memcache,
                                 binfo->buffer,
                                 binfo->len,
                                 binfo->rw);
            break;
        }
            
        default:
            /* Should return -ENOSYS, but return 0 for caller ease. */
            break;
    }
    
    return 0;
}

extern int int_rdma_initialize(void);

/*
 * BMI_rdma_initialize()
 *
 * Description:
 *  Startup, once per application.
 *
 * Params:
 *  [in] listen_addr - for a server, address to listen for connect requests on
 *  [in] method_id   - global identifier for rdma method
 *  [in] init_flags  - BMI method initialization flags; used to indicate 
 *                     whether server or client in this case
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_initialize(struct bmi_method_addr *listen_addr,
                               int method_id,
                               int init_flags)
{
    int ret;
    
    debug(0, "Initializing RDMA module");
    
    gen_mutex_lock(&interface_mutex);
    
    /* check params */
    if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER))
    {
        error("%s: error: BMI_INIT_SERVER requires non-null "
              "listen_addr and v.v", __func__);
        exit(1);
    }
    
    bmi_rdma_method_id = method_id;
    
    rdma_device = malloc(sizeof(*rdma_device));
    if (!rdma_device)
    {
        /* 
         * Release the mutex while calling error() because it calls
         * gossip_backtrace() which means there is a chance it could exit().
         */
        gen_mutex_unlock(&interface_mutex);
        /* TODO: is this the best way to handle it? */
        error("%s: malloc %ld bytes failed", __func__, sizeof(*rdma_device));
        gen_mutex_lock(&interface_mutex);
    }
    
    /* TODO: equivalent of openib_ib_initialize() and vapi_ib_initialize()? */
    ret = int_rdma_initialize();
    if (ret)
    {
        gen_mutex_unlock(&interface_mutex);
        return bmi_errno_to_pvfs(-BMI_ENODEV);
    }
    
    /* initialize memcache */
    rdma_device->memcache = memcache_init(mem_register, mem_deregister);
    
    /*
     * Setup connection.
     */
    if (init_flags & BMI_INIT_SERVER)
    {
        rdma_server_init_listener(listen_addr);
        rdma_device->listen_addr = listen_addr;
    }
    else
    {
        rdma_device->listen_id = NULL;
        rdma_device->listen_addr = NULL;
    }
    
    /*
     * Initialize data structures.
     */
    INIT_QLIST_HEAD(&rdma_device->connection);
    INIT_QLIST_HEAD(&rdma_device->sendq);
    INIT_QLIST_HEAD(&rdma_device->recvq);
    
    rdma_device->eager_buf_num = DEFAULT_EAGER_BUF_NUM;
    rdma_device->eager_buf_size = DEFAULT_EAGER_BUF_SIZE;
    rdma_device->eager_buf_payload = rdma_device->eager_buf_size -
                                     sizeof(msg_header_eager_t);
    
    gen_mutex_unlock(&interface_mutex);
    
    debug(0, "rdma module successfully initialized");
    return ret;
}

/*
 * BMI_rdma_finalize()
 *
 * Description:
 *  Shutdown.
 *
 * Params:
 *  none
 *
 * Returns:
 *  0 (TODO: no errors?)
 */
static int BMI_rdma_finalize(void)
{
    struct rdma_event_channel *channel = NULL;
    
    gen_mutex_lock(&interface_mutex);
    
    /* if client, send BYE to each connection and bring down the QP */
    if (rdma_device->listen_id == NULL)
    {
        struct qlist_head *l;
        qlist_for_each(l, &rdma_device->connection)
        {
            rdma_connection_t *c = qlist_upcast(l);
            if (c->cancelled)
            {
                continue;   /* already closed */
            }
            
            /* Send BYE message to servers, then disconnect */
            send_bye(c);
            disconnect(c);  /* TODO: temporary wrapper for rdma_disconnect() */
            //rdma_disconnect(c->priv->id);
            /* TODO: handle RDMA_CM_EVENT_DISCONNECTED event that will be
             * generated by rdma_disconnect() somewhere, not here though */
        }
    }
    
    /* if server, stop listening */
    if (rdma_device->listen_id)
    {
        rdma_method_addr_t *rdma_map = rdma_device->listen_addr->method_data;
        
        /* tell the accept thread to terminate */
        gen_mutex_lock(&accept_thread_mutex);
        accept_thread_shutdown = 1;
        gen_mutex_unlock(&accept_thread_mutex);
        
        /* wait for the accept thread to end */
        pthread_join(accept_thread_id, NULL);
        
        channel = rdma_device->listen_id->channel;
        rdma_destroy_id(rdma_device->listen_id);
        rdma_destroy_event_channel(channel);
        channel = NULL;
        /* TODO: make sure any QP that is associated with the listen ID is 
         * freed prior to destroying the listen ID */
        
        free(rdma_map->hostname);
        free(rdma_device->listen_addr);
    }
    
    /* destroy QPs and other connection structures */
    while (rdma_device->connection.next != &rdma_device->connection)
    {
        rdma_connection_t *c =
                (rdma_connection_t *) rdma_device->connection.next;
        rdma_close_connection(c);
    }
    
    memcache_shutdown(rdma_device->memcache);
    
    rdma_finalize();
    
    free(rdma_device);
    rdma_device = NULL;
    
    gen_mutex_unlock(&interface_mutex);
    debug(0, "BMI_rdma_finalize: RDMA module finalized.");
    return 0;
}

/* exported method interface */
const struct bmi_method_ops bmi_rdma_ops =
{
    .method_name = "bmi_rdma",
    .flags = 0,
    .initialize = BMI_rdma_initialize,
    .finalize = BMI_rdma_finalize,
    .set_info = BMI_rdma_set_info,
    .get_info = BMI_rdma_get_info,
    .post_send = BMI_rdma_post_send,
    .post_sendunexpected = BMI_rdma_post_sendunexpected,
    .post_recv = BMI_rdma_post_recv,
    .testcontext = BMI_rdma_testcontext,
    .testunexpected = BMI_rdma_testunexpected,
    .method_addr_lookup = BMI_rdma_method_addr_lookup,
    .post_send_list = BMI_rdma_post_send_list,
    .post_recv_list = BMI_rdma_post_recv_list,
    .post_sendunexpected_list = BMI_rdma_post_sendunexpected_list,
    .open_context = BMI_rdma_open_context,
    .close_context = BMI_rdma_close_context,
    .cancel = BMI_rdma_cancel,
    .rev_lookup_unexpected = BMI_rdma_rev_lookup,
    .query_addr_range = NULL,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * InfiniBand BMI method.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2006 Kyle Schochenmaier <kschoche@scl.ameslab.gov>
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
#include <arpa/inet.h>   /* inet_ntoa */
#include <netdb.h>       /* gethostbyname */
#include <sys/poll.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* include definitions */
#include <src/common/id-generator/id-generator.h>
#include <src/io/bmi/bmi-method-support.h>   /* bmi_method_ops ... */
#include <src/io/bmi/bmi-method-callback.h>  /* bmi_method_addr_reg_callback */
#include <src/common/gen-locks/gen-locks.h>  /* gen_mutex_t ... */
#include <src/common/misc/pvfs2-internal.h>
#include <pthread.h>
#include "pint-hint.h"

#ifdef HAVE_VALGRIND_H
#include <memcheck.h>
#else
#define VALGRIND_MAKE_MEM_DEFINED(addr,len)
#endif

#include "ib.h"

static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;

/*
 * Handle given by upper layer, which must be handed back to create
 * method_addrs.
 */
static int bmi_ib_method_id;

/* alloc space for the single device structure pointer; one for
 * vapi and one for openib */
ib_device_t *ib_device __hidden = NULL;

/* TCP listen backlog */
static int listen_backlog = 16384;

/* accept thread variables */
static gen_mutex_t accept_thread_mutex = GEN_MUTEX_INITIALIZER;
int accept_thread_shutdown = 0;
gen_thread_t accept_thread_id;
int accept_timeout_ms = 2000;

/* these all vector through the ib_device */
#define new_connection ib_device->func.new_connection
#define close_connection ib_device->func.close_connection
#define drain_qp ib_device->func.drain_qp
#define ib_initialize ib_device->func.ib_initialize
#define ib_finalize ib_device->func.ib_finalize
#define post_sr ib_device->func.post_sr
#define post_sr_rdmaw ib_device->func.post_sr_rdmaw
#define check_cq ib_device->func.check_cq
#define prepare_cq_block ib_device->func.prepare_cq_block
#define ack_cq_completion_event ib_device->func.ack_cq_completion_event
#define wc_status_string ib_device->func.wc_status_string
#define mem_register ib_device->func.mem_register
#define mem_deregister ib_device->func.mem_deregister
#define check_async_events ib_device->func.check_async_events

#if MEMCACHE_EARLY_REG
#if MEMCACHE_BOUNCEBUF
#error Not sensible to use bouncebuf with early reg.  First use of bouncebuf \
  will register it, thus no effect whether early reg or not.
#endif
#endif

#if MEMCACHE_BOUNCEBUF
static ib_buflist_t reg_send_buflist = { .num = 0 };
static ib_buflist_t reg_recv_buflist = { .num = 0 };
static void *reg_send_buflist_buf;
static void *reg_recv_buflist_buf;
static const bmi_size_t reg_send_buflist_len = 256 * 1024;
static const bmi_size_t reg_recv_buflist_len = 256 * 1024;
#endif

static void encourage_send_incoming_cts(struct buf_head *bh, 
                                        u_int32_t byte_len);
static void encourage_recv_incoming(struct buf_head *bh, 
                                    msg_type_t type,
                                    u_int32_t byte_len);
static void encourage_rts_done_waiting_buffer(struct ib_work *sq);
static int send_cts(struct ib_work *rq);
static void ib_close_connection(ib_connection_t *c);
static int ib_tcp_client_connect(ib_method_addr_t *ibmap,
                                 struct bmi_method_addr *remote_map);
static int ib_block_for_activity(int timeout_ms);

void *ib_tcp_server_accept_thread(void *arg);
void *ib_tcp_server_process_client_thread(void *arg);


/* structure to hold TCP info passed to the accept thread */
struct tcp_conn {
    int port;
    char *hostname;
    char peername[2048];
    int s;
};


/*
 * Return string form of work completion opcode field.
 */
static const char *wc_opcode_string(int opcode)
{
    if (opcode == BMI_IB_OP_SEND)
    {
        return "SEND";
    }
    else if (opcode == BMI_IB_OP_RECV)
    {
        return "RECV";
    }
    else if (opcode == BMI_IB_OP_RDMA_WRITE)
    {
        return "RDMA WRITE";
    }
    else
    {
        return "(UNKNOWN)";
    }
}

/*
 * Wander through single completion queue, pulling off messages and
 * sticking them on the proper connection queues.  Later you can
 * walk the incomingq looking for things to do to them.  Returns
 * number of new things that arrived.
 */
static int ib_check_cq(void)
{
    int ret = 0;
    struct buf_head *bh = NULL;
    struct ib_work *sq = NULL;

    for (;;) 
    {
        struct bmi_ib_wc wc;
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
            if (wc.opcode == BMI_IB_OP_SEND) 
            {
                debug(0, "%s: entry id 0x%llx SEND error %s to %s", 
                      __func__,
                      llu(wc.id), 
                      wc_status_string(wc.status), 
                      bh->c->peername);
                if (wc.id) 
                {
                    ib_connection_t *c = ptr_from_int64(wc.id);
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

        if (wc.opcode == BMI_IB_OP_RECV) 
        {
            /*
             * Remote side did a send to us.
             */
            msg_header_common_t mh_common;
            u_int32_t byte_len = wc.byte_len;
            char *ptr = NULL;

            bh = ptr_from_int64(wc.id);
            if (!bh)
            {
                continue;
            }
            ptr = bh->buf;

            VALGRIND_MAKE_MEM_DEFINED(ptr, byte_len);
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
        else if (wc.opcode == BMI_IB_OP_RDMA_WRITE) 
        {
            /* completion event for the rdma write we initiated, used
             * to signal memory unpin etc. */
            sq = ptr_from_int64(wc.id);

            sq->state.send = SQ_WAITING_RTS_DONE_BUFFER;

#if !MEMCACHE_BOUNCEBUF
            memcache_deregister(ib_device->memcache, &sq->buflist);
#endif
            debug(2, "%s: sq %p RDMA write done, now %s", 
                  __func__, sq, sq_state_name(sq->state.send));

            encourage_rts_done_waiting_buffer(sq);
        } 
        else if (wc.opcode == BMI_IB_OP_SEND) 
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
                struct ib_work *rq = sq;  /* rename */
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
 * Initialize common header of all messages.
 */
static void msg_header_init(msg_header_common_t *mh_common,
                            ib_connection_t *c, 
                            msg_type_t type)
{
    mh_common->type = type;
    mh_common->credit = c->return_credit;
    c->return_credit = 0;
}

/*
 * Grab an empty buf head, if available.
 */
static struct buf_head *get_eager_buf(ib_connection_t *c)
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
 * Re-post a receive buffer, possibly returning credit to the peer.
 */
static void post_rr(ib_connection_t *c, 
                    struct buf_head *bh)
{
    ib_device->func.post_rr(c, bh);
    ++c->return_credit;

    /* if credits are building up, explicitly send them over */
    if (c->return_credit > ib_device->eager_buf_num - 4) 
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
 * Push a send message along its next step.  Called internally only.
 */
static void encourage_send_waiting_buffer(struct ib_work *sq)
{
    struct buf_head *bh;
    ib_connection_t *c = sq->c;

    if (!sq)
    {
        error("%s: no sq", __func__);
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
    bh->sq = sq;  /* uplink for completion */

    if (sq->buflist.tot_len <= ib_device->eager_buf_payload) 
    {
        /*
         * Eager send.
         */
        msg_header_eager_t mh_eager;
        char *ptr = bh->buf;

        memset(&mh_eager, 0, sizeof(msg_header_eager_t));
        msg_header_init(&mh_eager.c, 
                        c, 
                        sq->is_unexpected ? 
                            MSG_EAGER_SENDUNEXPECTED : MSG_EAGER_SEND);
        mh_eager.bmi_tag = sq->bmi_tag;
        encode_msg_header_eager_t(&ptr, &mh_eager);

        memcpy_from_buflist(&sq->buflist,
	                    (msg_header_eager_t *) bh->buf + 1);

        /* send the message */
        post_sr(bh, (u_int32_t) (sizeof(mh_eager) + sq->buflist.tot_len));

        /* wait for ack saying remote has received and recycled his buf */
        sq->state.send = SQ_WAITING_EAGER_SEND_COMPLETION;
        debug(2, "%s: sq %p sent EAGER len %lld", 
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

        memset(&mh_rts, 0, sizeof(msg_header_rts_t));
        msg_header_init(&mh_rts.c, c, MSG_RTS);
        mh_rts.bmi_tag = sq->bmi_tag;
        mh_rts.mop_id = sq->mop->op_id;
        mh_rts.tot_len = sq->buflist.tot_len;

        encode_msg_header_rts_t(&ptr, &mh_rts);

        post_sr(bh, sizeof(mh_rts));

#if MEMCACHE_EARLY_REG
        /* XXX: need to lock against receiver thread?  Could poll return
         * the CTS and start the data send before this completes? */
        memcache_register(ib_device->memcache, &sq->buflist);
#endif

        sq->state.send = SQ_WAITING_RTS_SEND_COMPLETION;
        debug(2, "%s: sq %p sent RTS mopid %llx len %lld", 
              __func__, 
              sq, 
              llu(sq->mop->op_id), 
              lld(sq->buflist.tot_len));
    }
}

/*
 * Look at the incoming message which is a response to an earlier RTS
 * from us, and start the real data send.
 */
static void encourage_send_incoming_cts(struct buf_head *bh, 
                                        u_int32_t byte_len)
{
    msg_header_cts_t mh_cts;
    struct ib_work *sq, *sqt;
    u_int32_t want;
    char *ptr = bh->buf;

    decode_msg_header_cts_t(&ptr, &mh_cts);

    /*
     * Look through this CTS message to determine the owning sq.  Works
     * using the mop_id which was sent during the RTS, now returned to us.
     */
    sq = 0;
    qlist_for_each_entry(sqt, &ib_device->sendq, list) 
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

    if (bmi_ib_unlikely(byte_len != want))
    {
        error("%s: wrong message size for CTS, got %u, want %u", 
              __func__, byte_len, want);
        return;
    }

    /* start the big tranfser */
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
 * See if anything was preposted that matches this.
 */
static struct ib_work *find_matching_recv(rq_state_t statemask, 
                                          const ib_connection_t *c,
                                          bmi_msg_tag_t bmi_tag)
{
    struct ib_work *rq;

    qlist_for_each_entry(rq, &ib_device->recvq, list) 
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
 * Init a new recvq entry from something that arrived on the wire.
 */
static struct ib_work *alloc_new_recv(ib_connection_t *c, 
                                      struct buf_head *bh)
{
    struct ib_work *rq = bmi_ib_malloc(sizeof(*rq));
    rq->type = BMI_RECV;
    rq->c = c;
    ++rq->c->refcnt;
    rq->bh = bh;
    rq->mop = 0;  /* until user posts for it */
    rq->rts_mop_id = 0;
    qlist_add_tail(&rq->list, &ib_device->recvq);
    return rq;
}

/*
 * Called from incoming message processing, except for the case
 * of ack to a CTS, for which we know the rq (see below).
 *
 * Unexpected receive, either no post or explicit sendunexpected.
 */
static void encourage_recv_incoming(struct buf_head *bh, 
                                    msg_type_t type, 
                                    u_int32_t byte_len)
{
    ib_connection_t *c = bh->c;
    struct ib_work *rq;
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

#if MEMCACHE_EARLY_REG
            /* if a big receive was posted but only a small message came
             * through, unregister it now */
            if (rq->buflist.tot_len > ib_device->eager_buf_payload) 
            {
                debug(2, "%s: early registration not needed, dereg after eager",
                      __func__);
                memcache_deregister(ib_device->memcache, &rq->buflist);
            }
#endif
        } 
        else 
        {
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
        struct ib_work *rqt;

        ptr = bh->buf;
        decode_msg_header_rts_done_t(&ptr, &mh_rts_done);

        debug(2, "%s: recv RTS_DONE mop_id %llx", 
              __func__, llu(mh_rts_done.mop_id));

        rq = NULL;
        qlist_for_each_entry(rqt, &ib_device->recvq, list) 
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
#if MEMCACHE_BOUNCEBUF
            memcpy_to_buflist(&rq->buflist, 
                              reg_recv_buflist_buf,
                              rq->buflist.tot_len);
#else
            memcache_deregister(ib_device->memcache, &rq->buflist);
#endif
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
        ib_close_connection(c);
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
 * We finished the RDMA write.  Send him a done message.
 */
static void encourage_rts_done_waiting_buffer(struct ib_work *sq)
{
    ib_connection_t *c = sq->c;
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

static void send_bye(ib_connection_t *c)
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
 * Two places need to send a CTS in response to an RTS.  They both
 * call this.  This handles pinning the memory, too.  Don't forget
 * to unpin when done.
 */
static int send_cts(struct ib_work *rq)
{
    ib_connection_t *c = rq->c;
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
    bh->sq = (struct ib_work *) rq;  /* uplink for completion */

#if MEMCACHE_BOUNCEBUF
    if (reg_recv_buflist.num == 0) 
    {
        reg_recv_buflist.num = 1;
        reg_recv_buflist.buf.recv = &reg_recv_buflist_buf;
        reg_recv_buflist.len = &reg_recv_buflist_len;
        reg_recv_buflist.tot_len = reg_recv_buflist_len;
        reg_recv_buflist_buf = bmi_ib_malloc(reg_recv_buflist_len);
        memcache_register(ib_device->memcache, &reg_recv_buflist);
    }

    if (rq->buflist.tot_len > reg_recv_buflist_len)
    {
        error("%s: recv prereg buflist too small, need %lld", 
              __func__, lld(rq->buflist.tot_len));
        return 1;
    }

    ib_buflist_t save_buflist = rq->buflist;
    rq->buflist = reg_recv_buflist;
#else
#  if !MEMCACHE_EARLY_REG
    memcache_register(ib_device->memcache, &rq->buflist);
#  endif
#endif

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
    post_len = (char *)(keyp + rq->buflist.num) - (char *)bh->buf;
    if (post_len > ib_device->eager_buf_size)
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

#if MEMCACHE_BOUNCEBUF
    rq->buflist = save_buflist;
#endif

    return 0;
}

/*
 * Bring up the connection before posting a send or receive on it.
 */
static int ensure_connected(struct bmi_method_addr *remote_map)
{
    int ret = 0;
    ib_method_addr_t *ibmap = remote_map->method_data;

    if (!ibmap->c && ibmap->reconnect_flag)
    {
        ret = ib_tcp_client_connect(ibmap, remote_map);
    }
    else if(!ibmap->c && !ibmap->reconnect_flag)
    {
        ret = 1; /* cannot actively connect */
    }
    else
    {
        ret = 0;
    }

    return ret;
}

/*
 * Generic interface for both send and sendunexpected, list and non-list send.
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
    struct ib_work *sq;
    struct method_op *mop;
    ib_method_addr_t *ibmap;
    int i;
    int ret = 0;

    gen_mutex_lock(&interface_mutex);
    ret = ensure_connected(remote_map);
    if (ret) 
    {
        goto out;
    }
    ibmap = remote_map->method_data;

    /* alloc and build new sendq structure */
    sq = bmi_ib_malloc(sizeof(*sq));
    sq->type = BMI_SEND;
    sq->state.send = SQ_WAITING_BUFFER;

    debug(2, "%s: sq %p len %lld peer %s", 
          __func__, 
          sq, 
          (long long) total_size,
          ibmap->c->peername);

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
    if (is_unexpected && sq->buflist.tot_len > ib_device->eager_buf_payload) 
    {
        free(sq);
        ret = -EINVAL;
        goto out;
    }

    sq->bmi_tag = tag;
    sq->c = ibmap->c;
    ++sq->c->refcnt;
    sq->is_unexpected = is_unexpected;
    qlist_add_tail(&sq->list, &ib_device->sendq);

    /* generate identifier used by caller to test for message later */
    mop = bmi_ib_malloc(sizeof(*mop));
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

static int BMI_ib_post_send(bmi_op_id_t *id, 
                            struct bmi_method_addr *remote_map,
                            const void *buffer, 
                            bmi_size_t total_size,
                            enum bmi_buffer_type buffer_flag __unused,
                            bmi_msg_tag_t tag, 
                            void *user_ptr, 
                            bmi_context_id
                            context_id, 
                            PVFS_hint hints __unused)
{
    return post_send(id, 
                     remote_map, 
                     0, 
                     &buffer, 
                     &total_size,
                     total_size, 
                     tag, 
                     user_ptr, 
                     context_id, 
                     0);
}

static int BMI_ib_post_send_list(bmi_op_id_t *id, 
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
                     0);
}

static int BMI_ib_post_sendunexpected(bmi_op_id_t *id, 
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
                     0, 
                     &buffer, 
                     &total_size,
                     total_size, 
                     tag, 
                     user_ptr, 
                     context_id, 
                     1);
}

static int BMI_ib_post_sendunexpected_list(bmi_op_id_t *id, 
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
                     1);
}

/*
 * Used by both recv and recv_list.
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
    struct ib_work *rq;
    struct method_op *mop;
    ib_method_addr_t *ibmap;
    ib_connection_t *c;
    int i;
    int ret = 0;
    
    gen_mutex_lock(&interface_mutex);
    ret = ensure_connected(remote_map);
    if (ret)
    {
        goto out;
    }
    ibmap = remote_map->method_data;
    c = ibmap->c;

    /* poll interface first to save a few steps below */
    ib_check_cq();

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
    mop = bmi_ib_malloc(sizeof(*mop));
    id_gen_fast_register(&mop->op_id, mop);
    mop->addr = remote_map;  /* set of function pointers, essentially */
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
#if MEMCACHE_EARLY_REG
        memcache_register(ib_device->memcache, &rq->buflist);
#endif
        sret = send_cts(rq);
        if (sret == 0) 
        {
            rq->state.recv = RQ_RTS_WAITING_RTS_DONE |
                             RQ_RTS_WAITING_CTS_SEND_COMPLETION |
                             RQ_RTS_WAITING_USER_TEST;
        }
        goto out;
    }

#if MEMCACHE_EARLY_REG
    /* but remember that this might not be used if the other side sends
     * less than we posted for receive; that's legal */
    if (rq->buflist.tot_len > ib_device->eager_buf_payload)
    {
        memcache_register(ib_device->memcache, &rq->buflist);
    }
#endif

  out:
    gen_mutex_unlock(&interface_mutex);
    return ret;
}

static int BMI_ib_post_recv(bmi_op_id_t *id, 
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
                     0, 
                     &buffer, 
                     &expected_len,
                     expected_len, 
                     tag, 
                     user_ptr, 
                     context_id);
}

static int BMI_ib_post_recv_list(bmi_op_id_t *id, 
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
 * Internal shared helper function.  Return 1 if found something
 * completed.
 */
static int test_sq(struct ib_work *sq, 
                   bmi_op_id_t *outid, 
                   bmi_error_code_t *err,
                   bmi_size_t *size, 
                   void **user_ptr, 
                   int complete)
{
    ib_connection_t *c;

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
                ib_close_connection(c);
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
            ib_close_connection(c);
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
 * Internal shared helper function.  Return 1 if found something
 * completed.  Note that rq->mop can be null for unexpected
 * messages.
 */
static int test_rq(struct ib_work *rq, 
                   bmi_op_id_t *outid, 
                   bmi_error_code_t *err,
                   bmi_size_t *size, 
                   void **user_ptr, 
                   int complete)
{
    ib_connection_t *c;

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
                id_gen_fast_unregister(rq->mop->op_id);
                free(rq->mop);
            }

            qlist_del(&rq->list);
            c = rq->c;
            free(rq);
            --c->refcnt;

            if (c->closed || c->cancelled)
            {
                ib_close_connection(c);
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
        debug(2, "%s: rq %p cancelled", __func__, rq);
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
            ib_close_connection(c);
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
 * Test one message, send or receive.  Also used to test the send side of
 * messages sent using sendunexpected.
 */
static int BMI_ib_test(bmi_op_id_t id, 
                       int *outcount, 
                       bmi_error_code_t *err,
                       bmi_size_t *size, 
                       void **user_ptr, 
                       int max_idle_time __unused,
                       bmi_context_id context_id __unused)
{
    struct method_op *mop;
    struct ib_work *sq;
    int n;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();

    mop = id_gen_fast_lookup(id);
    sq = mop->method_data;
    n = 0;

    if (sq->type == BMI_SEND) 
    {
        if (test_sq(sq, &id, err, size, user_ptr, 1))
        {
            n = 1;
        }
    } 
    else 
    {
        /* actually a recv */
        struct ib_work *rq = mop->method_data;
        if (test_rq(rq, &id, err, size, user_ptr, 1))
        {
            n = 1;
        }
    }

    *outcount = n;
    gen_mutex_unlock(&interface_mutex);
    return 0;
}

/*
 * Test just the particular list of op ids, returning the list of indices
 * that completed.
 */
static int BMI_ib_testsome(int incount, 
                           bmi_op_id_t *ids, 
                           int *outcount,
                           int *index_array, 
                           bmi_error_code_t *errs, 
                           bmi_size_t *sizes, 
                           void **user_ptrs,
                           int max_idle_time __unused, 
                           bmi_context_id context_id __unused)
{
    struct method_op *mop;
    struct ib_work *sq;
    bmi_op_id_t tid;
    int i, n;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();

    n = 0;
    for (i = 0; i < incount; i++) 
    {
        if (!ids[i])
        {
             continue;
        }
        mop = id_gen_fast_lookup(ids[i]);
        sq = mop->method_data;

        if (sq->type == BMI_SEND) 
        {
            if (test_sq(sq, &tid, &errs[n], &sizes[n], &user_ptrs[n], 1)) 
            {
                index_array[n] = i;
                ++n;
            }
        } 
        else 
        {
            /* actually a recv */
            struct ib_work *rq = mop->method_data;
            if (test_rq(rq, &tid, &errs[n], &sizes[n], &user_ptrs[n], 1)) 
            {
                index_array[n] = i;
                ++n;
            }
        }
    }

    gen_mutex_unlock(&interface_mutex);

    *outcount = n;
    return 0;
}

/*
 * Test for multiple completions matching a particular user context.
 * Return 0 if okay, >0 if want another poll soon, negative for error.
 */
static int BMI_ib_testcontext(int incount, 
                              bmi_op_id_t *outids, 
                              int *outcount,
                              bmi_error_code_t *errs, 
                              bmi_size_t *sizes, 
                              void **user_ptrs,
                              int max_idle_time, 
                              bmi_context_id context_id)
{
    struct qlist_head *l, *lnext;
    int n = 0, complete, activity = 0;
    void **up = NULL;

    gen_mutex_lock(&interface_mutex);

restart:
    activity += ib_check_cq();

    /*
     * Walk _all_ entries on sq, rq, marking them completed or
     * encouraging them as needed due to resource limitations.
     */
    for (l = ib_device->sendq.next; l != &ib_device->sendq; l = lnext) 
    {
        struct ib_work *sq = qlist_upcast(l);
        lnext = l->next;
        /* test them all, even if can't reap them, just to encourage */
        complete = (sq->mop->context_id == context_id) && (n < incount);

        if (user_ptrs)
        {
            up = &user_ptrs[n];
        }

        n += test_sq(sq, &outids[n], &errs[n], &sizes[n], up, complete);
    }

    for (l = ib_device->recvq.next; l != &ib_device->recvq; l = lnext) 
    {
        struct ib_work *rq = qlist_upcast(l);
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
        activity = ib_block_for_activity(max_idle_time);
        if (activity == 1) 
        {   
            /* IB action, go do it immediately */
            gen_mutex_lock(&interface_mutex);
            goto restart;
	}
    }

    *outcount = n;
    return activity + n;
}

/*
 * Non-blocking test to look for any incoming unexpected messages.
 * This is also where we check for new connections on the TCP socket, since
 * those would show up as unexpected the first time anything is sent.
 * Return 0 for success, or -1 for failure; number of things in *outcount.
 * Return >0 if want another poll soon.
 */
static int BMI_ib_testunexpected(int incount __unused, 
                                 int *outcount,
                                 struct bmi_method_unexpected_info *ui, 
                                 int max_idle_time)
{
    struct qlist_head *l;
    int activity = 0, n;

    gen_mutex_lock(&interface_mutex);

    /* Check CQ, then look for the first unexpected message.  */
restart:
    activity += ib_check_cq();

    n = 0;
    qlist_for_each(l, &ib_device->recvq) 
    {
        struct ib_work *rq = qlist_upcast(l);
        if (rq->state.recv == RQ_EAGER_WAITING_USER_TESTUNEXPECTED) 
        {
            msg_header_eager_t mh_eager;
            char *ptr = rq->bh->buf;
            ib_connection_t *c = rq->c;

            decode_msg_header_eager_t(&ptr, &mh_eager);

            debug(2, "%s: found waiting testunexpected", __func__);
            ui->error_code = 0;
            ui->addr = c->remote_map;  /* hand back permanent method_addr */
            ui->buffer = bmi_ib_malloc((unsigned long) rq->actual_len);
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
                ib_close_connection(c);
            }
            goto out;
        }
    }

  out:
    gen_mutex_unlock(&interface_mutex);

    if (activity == 0 && n == 0 && max_idle_time > 0) 
    {
        /*
         * Block if told to from above, also polls TCP listening socket.
         */
        debug(8, "%s: last activity too long ago, blocking", __func__);
        activity = ib_block_for_activity(max_idle_time);
        if (activity == 1) 
        {   
            /* IB action, go do it immediately */
            gen_mutex_lock(&interface_mutex);
            goto restart;
        }
    }

    *outcount = n;
    return activity + n;
}

/*
 * No need to track these internally.  Just search the entire queue.
 */
static int BMI_ib_open_context(bmi_context_id context_id __unused)
{
    return 0;
}

static void BMI_ib_close_context(bmi_context_id context_id __unused)
{
}

/*
 * Asynchronous call to destroy an in-progress operation.
 * Can't just call test since we don't want to reap the operation,
 * just make sure it's done or not.
 */
static int BMI_ib_cancel(bmi_op_id_t id, 
                         bmi_context_id context_id __unused)
{
    struct method_op *mop;
    struct ib_work *tsq;
    ib_connection_t *c = 0;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();
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
        struct ib_work *rq = mop->method_data;
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
         * anyway.  Do not close the connection until all the sq/rq on it have
         * gone away.
         */
        struct qlist_head *l;

        c->cancelled = 1;
        drain_qp(c);
        qlist_for_each(l, &ib_device->sendq) 
        {
            struct ib_work *sq = qlist_upcast(l);
            if (sq->c != c) 
            {
                continue;
            }
#if !MEMCACHE_BOUNCEBUF
            if (sq->state.send == SQ_WAITING_DATA_SEND_COMPLETION)
            {
                memcache_deregister(ib_device->memcache, &sq->buflist);
            }
    #if MEMCACHE_EARLY_REG
            /* pin when sending rts, so also must dereg in this state */
            if (sq->state.send == SQ_WAITING_RTS_SEND_COMPLETION ||
                sq->state.send == SQ_WAITING_RTS_SEND_COMPLETION_GOT_CTS ||
                sq->state.send == SQ_WAITING_CTS)
            {
                memcache_deregister(ib_device->memcache, &sq->buflist);
            }
    #endif
#endif
            if (sq->state.send != SQ_WAITING_USER_TEST)
            {
                sq->state.send = SQ_CANCELLED;
            }
        }
        qlist_for_each(l, &ib_device->recvq) 
        {
            struct ib_work *rq = qlist_upcast(l);
            if (rq->c != c) 
            {
                continue;
            }
#if !MEMCACHE_BOUNCEBUF
            if (rq->state.recv & RQ_RTS_WAITING_RTS_DONE)
            {
                memcache_deregister(ib_device->memcache, &rq->buflist);
            }
    #if MEMCACHE_EARLY_REG
            /* pin on post, dereg all these */
            if (rq->state.recv == RQ_WAITING_INCOMING
              && rq->buflist.tot_len > ib_device->eager_buf_payload)
            {
                memcache_deregister(ib_device->memcache, &rq->buflist);
            }
    #endif
#endif
            if (!(rq->state.recv == RQ_EAGER_WAITING_USER_TEST 
               || rq->state.recv == RQ_RTS_WAITING_USER_TEST))
            {
                rq->state.recv = RQ_CANCELLED;
            }
        }
    }

    gen_mutex_unlock(&interface_mutex);
    return 0;
}

static const char *BMI_ib_rev_lookup(struct bmi_method_addr *meth)
{
    ib_method_addr_t *ibmap = meth->method_data;
    if (!ibmap->c)
    {
        return "(unconnected)";
    }
    else
    {
        return ibmap->c->peername;
    }
}

/*
 * Build and fill an IB-specific method_addr structure.
 */
static struct bmi_method_addr *ib_alloc_method_addr(ib_connection_t *c,
                                                    char *hostname, 
                                                    int port, 
                                                    int reconnect_flag)
{
    struct bmi_method_addr *map;
    ib_method_addr_t *ibmap;

    map = bmi_alloc_method_addr(bmi_ib_method_id, (bmi_size_t) sizeof(*ibmap));
    ibmap = map->method_data;
    ibmap->c = c;
    ibmap->hostname = hostname;
    ibmap->port = port;
    ibmap->reconnect_flag = reconnect_flag;
    ibmap->ref_count = 1;

    return map;
}

/*
 * Break up a method string like:
 *   ib://hostname:port/filesystem
 * into its constituent fields, storing them in an opaque
 * type, which is then returned.
 * XXX: I'm assuming that these actually return a _const_ pointer
 * so that I can hand back an existing map.
 */
static struct bmi_method_addr *BMI_ib_method_addr_lookup(const char *id)
{
    char *s, *hostname, *cp, *cq;
    int port;
    struct bmi_method_addr *map = NULL;

    /* parse hostname */
    s = string_key("ib", id);  /* allocs a string */
    if (!s)
    {
        return 0;
    }

    cp = strchr(s, ':');
    if (!cp)
    {
        error("%s: no ':' found", __func__);
        free(s);
        return NULL;
    }

    /* copy to permanent storage */
    hostname = bmi_ib_malloc((unsigned long) (cp - s + 1));
    strncpy(hostname, s, (size_t) (cp-s));
    hostname[cp-s] = '\0';

    /* strip /filesystem  */
    ++cp;
    cq = strchr(cp, '/');
    if (cq)
    {
        *cq = 0;
    }
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
    if (ib_device) 
    {
        struct qlist_head *l;
        qlist_for_each(l, &ib_device->connection) 
        {
            ib_connection_t *c = qlist_upcast(l);
            ib_method_addr_t *ibmap = c->remote_map->method_data;
            if (ibmap->port == port && !strcmp(ibmap->hostname, hostname)) 
            {
                map = c->remote_map;
                ibmap->ref_count++;
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
        /* set reconnect flag on this addr; we will be acting as a client
         * for this connection and will be responsible for making sure that
         * the connection is established
         */
        map = ib_alloc_method_addr(0, hostname, port, 1);  /* alloc new one */
        /* but don't call bmi_method_addr_reg_callback! */
    }

    return map;
}

static ib_connection_t *ib_new_connection(int sock, 
                                          const char *peername,
                                          int is_server)
{
    ib_connection_t *c;
    int i, ret;

    if (is_server)
    {
        debug(4, "%s: [SERVER] starting, peername=%s", __func__, peername);
    }
    else
    {
        debug(4, "%s: [CLIENT] starting, peername=%s", __func__, peername);
    }

    c = bmi_ib_malloc(sizeof(*c));
    c->peername = strdup(peername);

    /* fill send and recv free lists and buf heads */
    c->eager_send_buf_contig = bmi_ib_malloc(ib_device->eager_buf_num * 
                               ib_device->eager_buf_size);
    c->eager_recv_buf_contig = bmi_ib_malloc(ib_device->eager_buf_num * 
                               ib_device->eager_buf_size);
    INIT_QLIST_HEAD(&c->eager_send_buf_free);
    INIT_QLIST_HEAD(&c->eager_recv_buf_free);
    c->eager_send_buf_head_contig = bmi_ib_malloc(ib_device->eager_buf_num * 
                                    sizeof(*c->eager_send_buf_head_contig));
    c->eager_recv_buf_head_contig = bmi_ib_malloc(ib_device->eager_buf_num * 
                                    sizeof(*c->eager_recv_buf_head_contig));

    for (i = 0; i < ib_device->eager_buf_num; i++) 
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
                   ib_device->eager_buf_size;
	ebr->buf = (char *) c->eager_recv_buf_contig + i * 
                   ib_device->eager_buf_size;
        qlist_add_tail(&ebs->list, &c->eager_send_buf_free);
        qlist_add_tail(&ebr->list, &c->eager_recv_buf_free);
    }

    /* put it on the list */
    qlist_add(&c->list, &ib_device->connection);

    /* other vars */
    c->remote_map = 0;
    c->cancelled = 0;
    c->refcnt = 0;
    c->closed = 0;

    /* save one credit back for emergency credit refill */
    c->send_credit = ib_device->eager_buf_num - 1;
    c->return_credit = 0;

    if (is_server)
    {
        debug(4, "%s: [SERVER SIDE] calling new_connection, sock=%d",
              __func__, sock);
    }
    else
    {
        debug(4, "%s: [CLIENT SIDE] calling new_connection, sock=%d",
              __func__, sock);
    }

    ret = new_connection(c, sock, is_server);
    if (ret) 
    {
        ib_close_connection(c);
        c = NULL;
    }

    return c;
}

/*
 * Try to close and free a connection, but only do it if refcnt has
 * gone to zero.
 */
static void ib_close_connection(ib_connection_t *c)
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
	ib_method_addr_t *ibmap = c->remote_map->method_data;
	ibmap->c = NULL;
    }
    free(c->peername);
    qlist_del(&c->list);
    free(c);
}

/*
 * Blocking connect initiated by a post_sendunexpected{,_list}, or
 * post_recv*
 */
static int ib_tcp_client_connect(ib_method_addr_t *ibmap,
                                 struct bmi_method_addr *remote_map)
{
    int s;
    char peername[2048];
    struct hostent *hp;
    struct sockaddr_in skin;
    
    debug(4, "%s: starting", __func__);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
    {
        warning("%s: create tcp socket: %m", __func__);
        return bmi_errno_to_pvfs(-errno);
    }

    hp = gethostbyname(ibmap->hostname);
    if (!hp) 
    {
        warning("%s: cannot resolve server %s", __func__, ibmap->hostname);
        close(s);
        return -1;
    }

    memset(&skin, 0, sizeof(skin));
    skin.sin_family = hp->h_addrtype;
    memcpy(&skin.sin_addr, hp->h_addr_list[0], (size_t) hp->h_length);
    skin.sin_port = htons(ibmap->port);
    sprintf(peername, "%s:%d", inet_ntoa(skin.sin_addr), ibmap->port);

    debug(4, "%s: connecting to peername=%s, s=%d", __func__, peername, s);

  retry:
    if (connect(s, (struct sockaddr *) &skin, sizeof(skin)) < 0) 
    {
        if (errno == EINTR)
        {
            goto retry;
        }
        else
        {
            warning("%s: connect to server %s: %m", __func__, peername);
            return bmi_errno_to_pvfs(-errno);
        }
    }

    debug(4, "%s: connected, now calling ib_new_connection "
          "for peername=%s, s=%d", 
          __func__, peername, s);

    ibmap->c = ib_new_connection(s, peername, 0);
    if (!ibmap->c)
    {
        close(s);
        error("%s: ib_new_connection failed", __func__);
        return -EINVAL;
    }
    ibmap->c->remote_map = remote_map;

    debug(4, "%s: connection complete", __func__);

    if (close(s) < 0) 
    {
        warning("%s: close sock: %m", __func__);
        return bmi_errno_to_pvfs(-errno);
    }
    return 0;
}

/*
 * On a server, initialize a socket for listening for new connections.
 */
static void ib_tcp_server_init_listen_socket(struct bmi_method_addr *addr)
{
    int flags;
    struct sockaddr_in skin;
    ib_method_addr_t *ibc = addr->method_data;
    int *timeout_ms;

    ib_device->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ib_device->listen_sock < 0)
    {
        error_errno("%s: create tcp socket", __func__);
        exit(1);
    }
    flags = 1;
    if (setsockopt(ib_device->listen_sock, 
                   SOL_SOCKET, 
                   SO_REUSEADDR, 
                   &flags,
                   sizeof(flags)) < 0)
    {
        error_errno("%s: setsockopt REUSEADDR", __func__);
        exit(1);
    }
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = AF_INET;
    skin.sin_port = htons(ibc->port);

  retry:
    if (bind(ib_device->listen_sock, 
             (struct sockaddr *) &skin, 
             sizeof(skin)) < 0) 
    {
        if (errno == EINTR)
        {
            goto retry;
        }
        else
        {
            error_errno("%s: bind tcp socket", __func__);
            exit(1);
        }
    }

    debug(4, "%s: binding on tcp port %d", __func__, ibc->port);

    if (listen(ib_device->listen_sock, listen_backlog) < 0)
    {
        error_errno("%s: listen tcp socket", __func__);
        exit(1);
    }

    flags = fcntl(ib_device->listen_sock, F_GETFL);
    if (flags < 0)
    {
        error_errno("%s: fcntl getfl listen sock", __func__);
        exit(1);
    }
    flags |= O_NONBLOCK;
    if (fcntl(ib_device->listen_sock, F_SETFL, flags) < 0)
    {
        error_errno("%s: fcntl setfl nonblock listen sock", __func__);
        exit(1);
    }  

    timeout_ms = (int *) bmi_ib_malloc(sizeof(int));
    if (timeout_ms)
    {
        *timeout_ms = accept_timeout_ms;
        /* start the accept thread */
        debug(0, "%s: starting ib_tcp_server_accept_thread", __func__);
        if (pthread_create(&accept_thread_id,
                           NULL,
                           &ib_tcp_server_accept_thread,
                           timeout_ms))
        {
            error("%s: unable to to start accept thread, errno=%d",
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


void *ib_tcp_server_accept_thread(void *arg)
{
    struct pollfd pfd;
    struct sockaddr_in ssin;
    socklen_t len;
    int ret = 0;
    int timeout_ms = 10000;
    struct tcp_conn *tc;
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

        /* poll for activity on the listen socket */
        pfd.fd = ib_device->listen_sock;
        pfd.events = POLLIN;
        ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0)
        {
            warning("%s: poll error, errno=%d", __func__, errno);
            continue;
        }
        else if (ret == 0)
        {
            continue;
        }

        debug(4, "%s: poll ret %d rev0 %x", __func__, ret, pfd.revents);

        len = sizeof(ssin);
        tc = (struct tcp_conn *) bmi_ib_malloc(sizeof(struct tcp_conn));
        if (!tc)
        {
            warning("%s: unable to malloc tc, errno=%d", __func__, errno);
            sleep(30);
            continue;
        }

        /* accept a client connection */
        tc->s = accept(ib_device->listen_sock,
                       (struct sockaddr *) &ssin, 
                       &len);
        if (tc->s < 0)
        {
            warning("%s: accept listen sock, errno=%d", __func__, errno);
            free(tc);
            continue;
        }
        tc->hostname = strdup(inet_ntoa(ssin.sin_addr));
        tc->port = ntohs(ssin.sin_port);
        sprintf(tc->peername, "%s:%d", tc->hostname, tc->port);

        debug(0, "%s: starting ib_tcp_server_process_client_thread "
              "for socket=%d", __func__, tc->s);

        /* start the client thread */
        if (pthread_create(&thread,
                           NULL,
                           &ib_tcp_server_process_client_thread,
                           tc))
        {
            warning("%s: unable to create accept_client thread, errno=%d",
                    __func__, errno);
            free(tc);
        }
    }

    pthread_exit(0);
}


void *ib_tcp_server_process_client_thread(void *arg)
{
    ib_connection_t *c;
    struct tcp_conn *tc;
    int ret;

    debug(0, "%s: starting", __func__);
    if (!arg)
    {
        error("%s: no socket passed", __func__);
        return NULL;
    }
    tc = (struct tcp_conn *) arg;

    gen_mutex_lock(&interface_mutex);

    debug(0, "%s: calling ib_new_connection for peername=%s on socket=%d",
          __func__, tc->peername, tc->s);
    c = ib_new_connection(tc->s, tc->peername, 1);
    if (!c)
    {
        error_xerrno(EINVAL, "%s: new ib connection failed", __func__);
        goto out;
    }
    debug(0, "%s: returned from ib_new_connection", __func__);

    /* don't set reconnect flag on this addr; we are a server in this
    * case and the peer will be responsible for maintaining the
    * connection
    */
    c->remote_map = ib_alloc_method_addr(c, tc->hostname, tc->port, 0);

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
    if (close(tc->s) < 0)
    {
        error_errno("%s: close new sock", __func__);
    }
    if (tc)
    {
        if (tc->hostname)
        {
            free(tc->hostname);
        }
        free(tc);
    }

    return NULL;
}

/*
 * Ask the device to write to its FD if a CQ event happens, and poll on it
 * as well as the listen_sock for activity, but do not actually respond to
 * anything.  A later ib_check_cq will handle CQ events, and a later call to
 * testunexpected will pick up new connections.  Returns ==1 if IB device is
 * ready, other >0 for some activity, else 0.
 */
static int ib_block_for_activity(int timeout_ms)
{
    struct pollfd pfd[3];  /* cq fd, async fd, accept socket */
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
            error_errno("%s: poll listen sock", __func__);
            return -EINVAL;
        }
    }
    return ret;
}

static void *BMI_ib_memalloc(bmi_size_t len,
                             enum bmi_op_type send_recv __unused)
{
    return memcache_memalloc(ib_device->memcache, 
                             len,
                             ib_device->eager_buf_payload);
}

static int BMI_ib_memfree(void *buf, 
                          bmi_size_t len,
                          enum bmi_op_type send_recv __unused)
{
    return memcache_memfree(ib_device->memcache, buf, len);
}

static int BMI_ib_unexpected_free(void *buf)
{
    free(buf);
    return 0;
}

/*
 * Callers sometimes want to know odd pieces of information.  Satisfy
 * them.
 */
static int BMI_ib_get_info(int option, void *param)
{
    int ret = 0;

    switch (option) 
    {
        case BMI_CHECK_MAXSIZE:
            /* reality is 2^31, but shrink to avoid negative int */
            *(int *)param = (1UL << 31) - 1;
            break;

        case BMI_GET_UNEXP_SIZE:
            *(int *)param = ib_device->eager_buf_payload;
            break;

        default:
            ret = -ENOSYS;
    }
    return ret;
}

/*
 * Used to set some optional parameters and random functions, like ioctl.
 */
static int BMI_ib_set_info(int option, 
                           void *param __unused)
{
    switch (option) 
    {
        case BMI_DROP_ADDR: 
        {
            struct bmi_method_addr *map = param;
            ib_method_addr_t *ibmap = map->method_data;
            ibmap->ref_count--;
            if (ibmap->ref_count == 0)
            {
                free(ibmap->hostname);
                free(map);
            }
            break;
        }

        case BMI_OPTIMISTIC_BUFFER_REG: 
        {
            /* not guaranteed to work */
            const struct bmi_optimistic_buffer_info *binfo = param;
            memcache_preregister(ib_device->memcache, 
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

#ifdef OPENIB
extern int openib_ib_initialize(void);
#endif
#ifdef VAPI
extern int vapi_ib_initialize(void);
#endif

/*
 * Startup, once per application.
 */
static int BMI_ib_initialize(struct bmi_method_addr *listen_addr, 
                             int method_id,
                             int init_flags)
{
    int ret;

    debug(0, "Initializing IB module");

    gen_mutex_lock(&interface_mutex);

    /* check params */
    if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER))
    {
        error("%s: error: BMI_INIT_SERVER requires non-null "
              "listen_addr and v.v", __func__);
        exit(1);
    }

    bmi_ib_method_id = method_id;

    ib_device = bmi_ib_malloc(sizeof(*ib_device));

    /* try, in order, OpenIB then VAPI; set up function pointers */
    ret = 1;
#ifdef OPENIB
    ret = openib_ib_initialize();
#endif
#ifdef VAPI
    if (ret)
    {
        ret = vapi_ib_initialize();
    }
#endif
    if (ret)
    {
        gen_mutex_unlock(&interface_mutex);
        return -ENODEV;  /* neither found */
    }

    /* initialize memcache */
    ib_device->memcache = memcache_init(mem_register, mem_deregister);
#if 0
    /*
     * Need this for correctness.  Could use malloc/free hooks instead, but
     * they fight with mpich's use, for example.  Consider switching to dreg
     * kernel module.
     */
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    /*
     * Set up tcp socket to listen for connection requests.
     * The hostname is currently ignored; the port number is used to bind
     * the listening TCP socket which accepts new connections.
     */
    if (init_flags & BMI_INIT_SERVER) 
    {
        ib_tcp_server_init_listen_socket(listen_addr);
        ib_device->listen_addr = listen_addr;
    } 
    else 
    {
        ib_device->listen_sock = -1;
        ib_device->listen_addr = NULL;
    }

    /*
     * Initialize data structures.
     */
    INIT_QLIST_HEAD(&ib_device->connection);
    INIT_QLIST_HEAD(&ib_device->sendq);
    INIT_QLIST_HEAD(&ib_device->recvq);

    ib_device->eager_buf_num  = DEFAULT_EAGER_BUF_NUM;
    ib_device->eager_buf_size = DEFAULT_EAGER_BUF_SIZE;
    ib_device->eager_buf_payload = ib_device->eager_buf_size - 
                                   sizeof(msg_header_eager_t);

    gen_mutex_unlock(&interface_mutex);

    debug(0, "IB module successfully initialized");
    return ret;
}

/*
 * Shutdown.
 */
static int BMI_ib_finalize(void)
{
    gen_mutex_lock(&interface_mutex);

    /* if client, send BYE to each connection and bring down the QP */
    if (ib_device->listen_sock < 0) 
    {
        struct qlist_head *l;
        qlist_for_each(l, &ib_device->connection) 
        {
            ib_connection_t *c = qlist_upcast(l);
            if (c->cancelled)
            {
                continue;  /* already closed */
            }

            /* Send BYE message to servers, transition QP to drain state */
            send_bye(c);
            drain_qp(c);
        }
    }

    /* if server, stop listening */
    if (ib_device->listen_sock >= 0) 
    {
        ib_method_addr_t *ibmap = ib_device->listen_addr->method_data;
        /* tell the accept thread to terminate */
        gen_mutex_lock(&accept_thread_mutex);
        accept_thread_shutdown = 1;
        gen_mutex_unlock(&accept_thread_mutex);
        /* wait for the accept thread to end */
        pthread_join(accept_thread_id, NULL);
        close(ib_device->listen_sock);
        free(ibmap->hostname);
        free(ib_device->listen_addr);
    }

    /* destroy QPs and other connection structures */
    while (ib_device->connection.next != &ib_device->connection) 
    {
        ib_connection_t *c = (ib_connection_t *) ib_device->connection.next;
        ib_close_connection(c);
    }

#if MEMCACHE_BOUNCEBUF
    if (reg_send_buflist.num > 0) 
    {
        memcache_deregister(ib_device->memcache, &reg_send_buflist);
        reg_send_buflist.num = 0;
        free(reg_send_buflist_buf);
    }
    if (reg_recv_buflist.num > 0) 
    {
        memcache_deregister(ib_device->memcache, &reg_recv_buflist);
        reg_recv_buflist.num = 0;
        free(reg_recv_buflist_buf);
    }
#endif

    memcache_shutdown(ib_device->memcache);

    ib_finalize();

    free(ib_device);
    ib_device = NULL;

    gen_mutex_unlock(&interface_mutex);
    debug(0, "BMI_tcp_finalize: IB module finalized.");
    return 0;
}

const struct bmi_method_ops bmi_ib_ops = 
{
    .method_name = "bmi_ib",
    .flags = 0,
    .initialize = BMI_ib_initialize,
    .finalize = BMI_ib_finalize,
    .set_info = BMI_ib_set_info,
    .get_info = BMI_ib_get_info,
    .memalloc = BMI_ib_memalloc,
    .memfree = BMI_ib_memfree,
    .unexpected_free = BMI_ib_unexpected_free,
    .post_send = BMI_ib_post_send,
    .post_sendunexpected = BMI_ib_post_sendunexpected,
    .post_recv = BMI_ib_post_recv,
    .test = BMI_ib_test,
    .testsome = BMI_ib_testsome,
    .testcontext = BMI_ib_testcontext,
    .testunexpected = BMI_ib_testunexpected,
    .method_addr_lookup = BMI_ib_method_addr_lookup,
    .post_send_list = BMI_ib_post_send_list,
    .post_recv_list = BMI_ib_post_recv_list,
    .post_sendunexpected_list = BMI_ib_post_sendunexpected_list,
    .open_context = BMI_ib_open_context,
    .close_context = BMI_ib_close_context,
    .cancel = BMI_ib_cancel,
    .rev_lookup_unexpected = BMI_ib_rev_lookup,
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

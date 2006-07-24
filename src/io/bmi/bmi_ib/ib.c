/*
 * InfiniBand BMI method.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2006 Kyle Schochenmaier <kschoche@scl.ameslab.gov>
 *
 * See COPYING in top-level directory.
 *
 * $Id: ib.c,v 1.34.2.2 2006-07-24 17:20:28 slang Exp $
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

/* these all vector through the ib_device */
#define new_connection ib_device->func.new_connection
#define close_connection ib_device->func.close_connection
#define drain_qp ib_device->func.drain_qp
#define send_bye ib_device->func.send_bye
#define ib_initialize ib_device->func.ib_initialize
#define ib_finalize ib_device->func.ib_finalize
#define post_sr ib_device->func.post_sr
#define post_rr ib_device->func.post_rr
#define post_sr_ack ib_device->func.post_sr_ack
#define post_rr_ack ib_device->func.post_rr_ack
#define post_sr_rdmaw ib_device->func.post_sr_rdmaw
#define check_cq ib_device->func.check_cq
#define prepare_cq_block ib_device->func.prepare_cq_block
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

static void encourage_send_incoming_cts(buf_head_t *bh, u_int32_t byte_len);
static void encourage_recv_incoming(ib_connection_t *c, buf_head_t *bh,
                                    u_int32_t byte_len);
static void encourage_recv_incoming_cts_ack(ib_recv_t *rq);
static int send_cts(ib_recv_t *rq);
static void maybe_free_connection(ib_connection_t *c);
static void ib_close_connection(ib_connection_t *c);
#ifndef __PVFS2_SERVER__
static int ib_tcp_client_connect(ib_method_addr_t *ibmap,
                                 struct method_addr *remote_map);
#endif
static int ib_tcp_server_check_new_connections(void);
static int ib_block_for_activity(int timeout_ms);

/*
 * Return string form of work completion opcode field.
 */
static const char *wc_opcode_string(int opcode)
{
    if (opcode == BMI_IB_OP_SEND)
	return "SEND";
    else if (opcode == BMI_IB_OP_RECV)
	return "RECV";
    else if (opcode == BMI_IB_OP_RDMA_WRITE)
	return "RDMA WRITE";
    else
	return "(UNKNOWN)";
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

    for (;;) {
	struct bmi_ib_wc wc;
	int vret;

	vret = check_cq(&wc);
	if (vret == 0)
	    break;  /* empty */

	debug(4, "%s: found something", __func__);
	++ret;
	if (wc.status != 0) {
	    if (wc.opcode == BMI_IB_OP_SEND) {
		debug(0, "%s: entry id 0x%llx SEND error %s", __func__,
		  llu(wc.id), wc_status_string(wc.status));
		if (wc.id) {
		    ib_connection_t *c = ptr_from_int64(wc.id);
		    if (c->cancelled) {
			debug(0,
			  "%s: ignoring send error on cancelled conn to %s",
			  __func__, c->peername);
		    }
		}
	    } else {
		error("%s: entry id 0x%llx opcode %s error %s", __func__,
		  llu(wc.id), wc_opcode_string(wc.opcode),
		  wc_status_string(wc.status));
	    }
	}

	if (wc.opcode == BMI_IB_OP_RECV) {
	    /*
	     * Remote side did a send to us.  Filled one of the receive
	     * queue descriptors, either message or ack.
	     */
	    buf_head_t *bh = ptr_from_int64(wc.id);
	    u_int32_t byte_len = wc.byte_len;

	    if (byte_len == 0) {
		/*
		 * Acknowledgment message on qp_ack.
		 */
		int bufnum = bmitoh32(wc.imm_data);
		ib_send_t *sq;

		debug(3, "%s: ack message %s my bufnum %d", __func__,
		      bh->c->peername, bufnum);

		/*
		 * Do not get the sq from the bh that posted this because
		 * these do not necessarily come in order, in particular
		 * there is no explicit ACK for an RTS instead the CTS serves
		 * as the ACK.  Instead look up the bufnum in the static
		 * send array.  This sq will actually be an rq if the ack
		 * is of a CTS.
		 */
		sq = bh->c->eager_send_buf_head_contig[bufnum].sq;
		if (bmi_ib_unlikely(sq->type == BMI_RECV))
		    /* ack of a CTS sent by the receiver */
		    encourage_recv_incoming_cts_ack((ib_recv_t *)sq);
		else {
		    assert(sq->state == SQ_WAITING_EAGER_ACK,
		      "%s: unknown send state %s of eager send bh %d"
		      " received in eager recv bh %d", __func__,
		      sq_state_name(sq->state), bufnum, bh->num);

		    sq->state = SQ_WAITING_USER_TEST;
		    qlist_add_tail(&sq->bh->list, &sq->c->eager_send_buf_free);

		    debug(3, "%s: sq %p"
		      " SQ_WAITING_EAGER_ACK -> SQ_WAITING_USER_TEST",
		      __func__, sq);
		}

	    } else {
		/*
		 * Some other message: eager send, RTS, CTS, BYE.
		 */
		msg_header_common_t mh_common;
		char *ptr = bh->buf;

		decode_msg_header_common_t(&ptr, &mh_common);

		debug(3, "%s: found len %d at %s my bufnum %d type %s",
		  __func__, byte_len, bh->c->peername, bh->num,
		  msg_type_name(mh_common.type));
		if (mh_common.type == MSG_CTS) {
		    /* incoming CTS messages go to the send engine */
		    encourage_send_incoming_cts(bh, byte_len);
		} else {
		    /* something for the recv side, no known rq yet */
		    encourage_recv_incoming(bh->c, bh, byte_len);
		}
	    }

	} else if (wc.opcode == BMI_IB_OP_RDMA_WRITE) {

	    /* completion event for the rdma write we initiated, used
	     * to signal memory unpin etc. */
	    ib_send_t *sq = ptr_from_int64(wc.id);

	    debug(3, "%s: sq %p %s", __func__, sq, sq_state_name(sq->state));

	    assert(sq->state == SQ_WAITING_DATA_LOCAL_SEND_COMPLETE,
	      "%s: wrong send state %s", __func__, sq_state_name(sq->state));

	    /* ack his cts, signals rdma completed */
	    post_sr_ack(sq->c, sq->his_bufnum);

#if !MEMCACHE_BOUNCEBUF
	    memcache_deregister(ib_device->memcache, &sq->buflist);
#endif
	    sq->state = SQ_WAITING_USER_TEST;

	    debug(2, "%s: sq %p now %s", __func__, sq,
	      sq_state_name(sq->state));

	} else if (wc.opcode == BMI_IB_OP_SEND) {

	    /* periodic send queue flush, qp or qp_ack */
	    debug(2, "%s: send to %s completed locally", __func__,
	      ((ib_connection_t *) ptr_from_int64(wc.id))->peername);

	} else {
	    error("%s: cq entry id 0x%llx opcode %d unexpected", __func__,
	      llu(wc.id), wc.opcode);
	}
    }
    return ret;
}

/*
 * Push a send message along its next step.  Called internally only.
 */
static void
encourage_send_waiting_buffer(ib_send_t *sq)
{
    /*
     * Must get buffers both locally and remote to do an eager send
     * or to initiate an RTS.  Maybe pair these two allocations if it
     * happens frequently.
     */
    buf_head_t *bh;

    debug(3, "%s: sq %p", __func__, sq);
    assert(sq->state == SQ_WAITING_BUFFER, "%s: wrong send state %s",
      __func__, sq_state_name(sq->state));

    bh = qlist_try_del_head(&sq->c->eager_send_buf_free);
    if (!bh) {
	debug(2, "%s: sq %p no free send buffers", __func__, sq);
	return;
    }
    sq->bh = bh;
    bh->sq = sq;  /* uplink for completion */

    if (sq->buflist.tot_len <= ib_device->eager_buf_payload) {
	/*
	 * Eager send.
	 */
	msg_header_eager_t mh_eager;
	char *ptr = bh->buf;
	
	mh_eager.type = sq->is_unexpected
	  ? MSG_EAGER_SENDUNEXPECTED : MSG_EAGER_SEND;
	mh_eager.bmi_tag = sq->bmi_tag;
	mh_eager.bufnum = bh->num;

	encode_msg_header_eager_t(&ptr, &mh_eager);

	memcpy_from_buflist(&sq->buflist,
	                    (msg_header_eager_t *) bh->buf + 1);

	/* get ready to receive the ack */
	post_rr_ack(sq->c, bh);

	/* send the message */
	post_sr(bh, (u_int32_t) (sizeof(mh_eager) + sq->buflist.tot_len));

	/* wait for ack saying remote has received and recycled his buf */
	sq->state = SQ_WAITING_EAGER_ACK;
	debug(3, "%s: sq %p sent EAGER now %s", __func__, sq,
	  sq_state_name(sq->state));

    } else {
	/*
	 * Request to send, rendez-vous.  Include the mop id in the message
	 * which will be returned to us in the CTS so we can look it up.
	 */
	msg_header_rts_t mh_rts;
	char *ptr = bh->buf;

	mh_rts.type = MSG_RTS;
	mh_rts.bmi_tag = sq->bmi_tag;
	mh_rts.mop_id = sq->mop->op_id;
	mh_rts.tot_len = sq->buflist.tot_len;

	encode_msg_header_rts_t(&ptr, &mh_rts);

	/* do not expect an ack back from this ever (implicit with CTS) */
	post_sr(bh, sizeof(mh_rts));

#if MEMCACHE_EARLY_REG
	/* XXX: need to lock against receiver thread?  Could poll return
	 * the CTS and start the data send before this completes? */
	memcache_register(ib_device->memcache, &sq->buflist);
#endif

	sq->state = SQ_WAITING_CTS;
	debug(3, "%s: sq %p sent RTS now %s", __func__, sq,
	  sq_state_name(sq->state));
    }
}

/*
 * Look at the incoming message which is a response to an earlier RTS
 * from us, and start the real data send.
 */
static void
encourage_send_incoming_cts(buf_head_t *bh, u_int32_t byte_len)
{
    msg_header_cts_t mh_cts;
    ib_send_t *sq;
    u_int32_t want;
    list_t *l;
    char *ptr = bh->buf;

    decode_msg_header_cts_t(&ptr, &mh_cts);

    /*
     * Look through this CTS message to determine the owning sq.  Works
     * using the mop_id which was sent during the RTS, now returned to us.
     */
    sq = 0;
    qlist_for_each(l, &ib_device->sendq) {
	ib_send_t *sqt = (ib_send_t *) l;
	debug(8, "%s: looking for op_id 0x%llx, consider 0x%llx", __func__,
	  llu(mh_cts.rts_mop_id), llu(sqt->mop->op_id));
	if (sqt->mop->op_id == (bmi_op_id_t) mh_cts.rts_mop_id) {
	    sq = sqt;
	    break;
	}
    }
    if (!sq)
	error("%s: mop_id %llx in CTS message not found", __func__,
	  llu(mh_cts.rts_mop_id));

    debug(2, "%s: sq %p %s my bufnum %d his bufnum %d len %u", __func__,
      sq, sq_state_name(sq->state), bh->num, mh_cts.bufnum, byte_len);
    assert(sq->state == SQ_WAITING_CTS,
      "%s: wrong send state %s", __func__, sq_state_name(sq->state));

    /* message; cts content; list of buffers, lengths, and keys */
    want = sizeof(mh_cts)
      + mh_cts.buflist_num * MSG_HEADER_CTS_BUFLIST_ENTRY_SIZE;
    if (bmi_ib_unlikely(byte_len != want))
	error("%s: wrong message size for CTS, got %u, want %u", __func__,
          byte_len, want);

    /* the cts serves as an implicit ack of our rts, free that send buf */
    qlist_add_tail(&sq->bh->list, &sq->c->eager_send_buf_free);

    /* save the bufnum from his cts for later acking */
    sq->his_bufnum = mh_cts.bufnum;

    /* start the big tranfser */
    post_sr_rdmaw(sq, &mh_cts, (msg_header_cts_t *) bh->buf + 1);

    /* re-post our recv buf now that we have all the information from CTS,
     * but don't tell him this until the rdma is complete. */
    post_rr(sq->c, bh);

    sq->state = SQ_WAITING_DATA_LOCAL_SEND_COMPLETE;
    debug(2, "%s: sq %p now %s", __func__, sq, sq_state_name(sq->state));
}


/*
 * See if anything was preposted that matches this.
 */
static ib_recv_t *
find_matching_recv(rq_state_t statemask, const ib_connection_t *c,
  bmi_msg_tag_t bmi_tag)
{
    list_t *l;

    qlist_for_each(l, &ib_device->recvq) {
	ib_recv_t *rq = qlist_upcast(l);
	if ((rq->state & statemask) && rq->c == c && rq->bmi_tag == bmi_tag)
	    return rq;
    }
    return 0;
}

/*
 * Init a new recvq entry from something that arrived on the wire.
 */
static ib_recv_t *
alloc_new_recv(ib_connection_t *c, buf_head_t *bh)
{
    ib_recv_t *rq = Malloc(sizeof(*rq));
    rq->type = BMI_RECV;
    rq->c = c;
    ++rq->c->refcnt;
    rq->bh = bh;
    rq->mop = 0;  /* until user posts for it */
    qlist_add_tail(&rq->list, &ib_device->recvq);
    return rq;
}

/*
 * Called from incoming message processing, except for the case
 * of ack to a CTS, for which we know the rq (see below).
 *
 * Unexpected receive, either no post or explicit sendunexpected.
 */
static void
encourage_recv_incoming(ib_connection_t *c, buf_head_t *bh, u_int32_t byte_len)
{
    ib_recv_t *rq;
    msg_header_common_t mh_common;
    char *ptr = bh->buf;

    decode_msg_header_common_t(&ptr, &mh_common);

    debug(4, "%s: incoming msg type %s", __func__,
          msg_type_name(mh_common.type));

    if (mh_common.type == MSG_EAGER_SEND) {

	msg_header_eager_t mh_eager;

	ptr = bh->buf;
	decode_msg_header_eager_t(&ptr, &mh_eager);

	debug(2, "%s: recv eager my bufnum %d his bufnum %d len %u", __func__,
	  bh->num, mh_eager.bufnum, byte_len);

	rq = find_matching_recv(RQ_WAITING_INCOMING, c, mh_eager.bmi_tag);
	if (rq) {
	    bmi_size_t len = byte_len - sizeof(mh_eager);
	    if (len > rq->buflist.tot_len)
		error("%s: EAGER received %lld too small for buffer %lld",
		  __func__, lld(len), lld(rq->buflist.tot_len));

	    memcpy_to_buflist(&rq->buflist,
	                      (msg_header_eager_t *) bh->buf + 1,
			      len);

	    /* re-post */
	    post_rr(c, bh);
	    /* done with buffer, ack to remote */
	    post_sr_ack(c, mh_eager.bufnum);
	    rq->state = RQ_EAGER_WAITING_USER_TEST;
	    debug(2, "%s: matched rq %p now %s", __func__, rq,
	      rq_state_name(rq->state));
#if MEMCACHE_EARLY_REG
	    /* if a big receive was posted but only a small message came
	     * through, unregister it now */
	    if (rq->buflist.tot_len > ib_device->eager_buf_payload) {
		debug(2, "%s: early registration not needed, dereg after eager",
		  __func__);
		memcache_deregister(ib_device->memcache, &rq->buflist);
	    }
#endif

	} else {
	    rq = alloc_new_recv(c, bh);
	    /* return value for when user does post_recv for this one */
	    rq->bmi_tag = mh_eager.bmi_tag;
	    rq->state = RQ_EAGER_WAITING_USER_POST;
	    /* do not repost or ack, keeping bh until user test */
	    debug(2, "%s: new rq %p now %s", __func__, rq,
	      rq_state_name(rq->state));
	}
	rq->actual_len = byte_len - sizeof(mh_eager);

    } else if (mh_common.type == MSG_EAGER_SENDUNEXPECTED) {

	msg_header_eager_t mh_eager;

	ptr = bh->buf;
	decode_msg_header_eager_t(&ptr, &mh_eager);

	debug(2, "%s: recv eager unexpected my bufnum %d his bufnum %d len %u",
	  __func__, bh->num, mh_eager.bufnum, byte_len);

	rq = alloc_new_recv(c, bh);
	/* return values for when user does testunexpected for this one */
	rq->bmi_tag = mh_eager.bmi_tag;
	rq->state = RQ_EAGER_WAITING_USER_TESTUNEXPECTED;
	rq->actual_len = byte_len - sizeof(mh_eager);
	/* do not repost or ack, keeping bh until user test */
	debug(2, "%s: new rq %p now %s", __func__, rq,
	  rq_state_name(rq->state));

    } else if (mh_common.type == MSG_RTS) {
	/*
	 * Sender wants to send a big message, initiates rts/cts protocol.
	 * Has the user posted a matching receive for it yet?
	 */
	msg_header_rts_t mh_rts;

	ptr = bh->buf;
	decode_msg_header_rts_t(&ptr, &mh_rts);

	debug(2, "%s: recv RTS my bufnum %d len %u",
	  __func__, bh->num, byte_len);

	rq = find_matching_recv(RQ_WAITING_INCOMING, c, mh_rts.bmi_tag);
	if (rq) {
	    if ((int)mh_rts.tot_len > rq->buflist.tot_len) {
		error("%s: RTS received %llu too small for buffer %llu",
		  __func__, llu(mh_rts.tot_len), llu(rq->buflist.tot_len));
	    }
	    rq->state = RQ_RTS_WAITING_CTS_BUFFER;
	    debug(2, "%s: matched rq %p MSG_RTS now %s", __func__, rq,
	      rq_state_name(rq->state));
	} else {
	    rq = alloc_new_recv(c, bh);
	    /* return value for when user does post_recv for this one */
	    rq->bmi_tag = mh_rts.bmi_tag;
	    rq->state = RQ_RTS_WAITING_USER_POST;
	    debug(2, "%s: new rq %p MSG_RTS now %s", __func__, rq,
	      rq_state_name(rq->state));
	}
	rq->actual_len = mh_rts.tot_len;
	rq->rts_mop_id = mh_rts.mop_id;

	/* Do not ack his rts, later cts implicitly acks it.
	 * Done with our buffer though we won't tell him yet. */
	post_rr(c, bh);

	if (rq->state == RQ_RTS_WAITING_CTS_BUFFER) {
	    int ret;
	    ret = send_cts(rq);
	    if (ret == 0)
		rq->state = RQ_RTS_WAITING_DATA;
	    /* else keep waiting until we can send that cts */
	}

    } else if (mh_common.type == MSG_BYE) {
	/*
	 * Other side requests connection close.  Do it.
	 */
	debug(2, "%s: recv BYE my bufnum %d len %u",
	  __func__, bh->num, byte_len);

	ib_close_connection(c);

    } else {
	error("%s: unknown message header type %d my bufnum %d len %u",
	      __func__, mh_common.type, bh->num, byte_len);
    }
}

/*
 * Data has arrived, we know because we got the ack to the CTS
 * we sent out.  Serves to release remote cts buffer too.
 */
static void
encourage_recv_incoming_cts_ack(ib_recv_t *rq)
{
    debug(2, "%s: rq %p %s", __func__, rq, rq_state_name(rq->state));
    assert(rq->state == RQ_RTS_WAITING_DATA, "%s: CTS ack to rq wrong state %s",
      __func__, rq_state_name(rq->state));

    /* XXX: should be head for cache, but use tail for debugging */
    qlist_add_tail(&rq->bh->list, &rq->c->eager_send_buf_free);
#if MEMCACHE_BOUNCEBUF
    memcpy_to_buflist(&rq->buflist, reg_recv_buflist_buf, rq->buflist.tot_len);
#else
    memcache_deregister(ib_device->memcache, &rq->buflist);
#endif
    rq->state = RQ_RTS_WAITING_USER_TEST;

    debug(2, "%s: rq %p now %s", __func__, rq, rq_state_name(rq->state));
}

/*
 * Two places need to send a CTS in response to an RTS.  They both
 * call this.  This handles pinning the memory, too.  Don't forget
 * to unpin when done.
 */
static int
send_cts(ib_recv_t *rq)
{
    buf_head_t *bh;
    msg_header_cts_t mh_cts;
    u_int64_t *bufp;
    u_int32_t *lenp;
    u_int32_t *keyp;
    u_int32_t post_len;
    char *ptr;
    int i;

    debug(2, "%s: rq %p from %s opid 0x%llx len %lld",
      __func__, rq, rq->c->peername, llu(rq->rts_mop_id),
      lld(rq->buflist.tot_len));

    bh = qlist_try_del_head(&rq->c->eager_send_buf_free);
    if (!bh) {
	debug(2, "%s: no bh available", __func__);
	return 1;
    }
    rq->bh = bh;
    bh->sq = (ib_send_t *) rq;  /* uplink for completion */

#if MEMCACHE_BOUNCEBUF
    if (reg_recv_buflist.num == 0) {
	reg_recv_buflist.num = 1;
	reg_recv_buflist.buf.recv = &reg_recv_buflist_buf;
	reg_recv_buflist.len = &reg_recv_buflist_len;
	reg_recv_buflist.tot_len = reg_recv_buflist_len;
	reg_recv_buflist_buf = Malloc(reg_recv_buflist_len);
	memcache_register(ib_device->memcache, &reg_recv_buflist);
    }
    if (rq->buflist.tot_len > reg_recv_buflist_len)
	error("%s: recv prereg buflist too small, need %lld", __func__,
	  lld(rq->buflist.tot_len));

    ib_buflist_t save_buflist = rq->buflist;
    rq->buflist = reg_recv_buflist;
#else
#  if !MEMCACHE_EARLY_REG
    memcache_register(ib_device->memcache, &rq->buflist);
#  endif
#endif

    mh_cts.type = MSG_CTS;
    mh_cts.bufnum = bh->num;
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
	error("%s: too many (%d) recv buflist entries for buf",  __func__,
	  rq->buflist.num);
    for (i=0; i<rq->buflist.num; i++) {
	bufp[i] = htobmi64(int64_from_ptr(rq->buflist.buf.recv[i]));
	lenp[i] = htobmi32(rq->buflist.len[i]);
	keyp[i] = htobmi32(rq->buflist.memcache[i]->memkeys.rkey);
    }

    /* expect an ack for this cts; will come after he does the big RDMA write */
    post_rr_ack(rq->c, bh);
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
static int
ensure_connected(struct method_addr *remote_map)
{
    int ret = 0;
    ib_method_addr_t *ibmap = remote_map->method_data;

    if (!ibmap->c)
#ifdef __PVFS2_SERVER__
	/* cannot actively connect */
	ret = 1;
#else
	ret = ib_tcp_client_connect(ibmap, remote_map);
#endif
    return ret;
}

/*
 * Used by both send and sendunexpected.
 */
static int
generic_post_send(bmi_op_id_t *id, struct method_addr *remote_map,
  int numbufs, const void *const *buffers, const bmi_size_t *sizes,
  bmi_size_t total_size, bmi_msg_tag_t tag, void *user_ptr,
  bmi_context_id context_id, int is_unexpected)
{
    ib_send_t *sq;
    struct method_op *mop;
    ib_method_addr_t *ibmap;
    int i;
    int ret = 0;

    gen_mutex_lock(&interface_mutex);
    ret = ensure_connected(remote_map);
    if (ret)
    	goto out;
    ibmap = remote_map->method_data;

    /* alloc and build new sendq structure */
    sq = Malloc(sizeof(*sq));
    sq->type = BMI_SEND;
    sq->state = SQ_WAITING_BUFFER;

    /*
     * For a single buffer, store it inside the sq directly, else save
     * the pointer to the list the user built when calling a _list
     * function.  This case is indicated by the non-_list functions by
     * a zero in numbufs.
     */
    if (numbufs == 0) {
	sq->buflist_one_buf = *buffers;
	sq->buflist_one_len = *sizes;
	sq->buflist.num = 1;
	sq->buflist.buf.send = &sq->buflist_one_buf;
	sq->buflist.len = &sq->buflist_one_len;
    } else {
	sq->buflist.num = numbufs;
	sq->buflist.buf.send = buffers;
	sq->buflist.len = sizes;
    }
    sq->buflist.tot_len = 0;
    for (i=0; i<sq->buflist.num; i++)
	sq->buflist.tot_len += sizes[i];

    /*
     * This passed-in total length field does not make much sense
     * to me, but I'll at least check it for accuracy.
     */
    if (sq->buflist.tot_len != total_size)
	error("%s: user-provided tot len %lld"
	  " does not match buffer list tot len %lld",
	  __func__, lld(total_size), lld(sq->buflist.tot_len));

    /* unexpected messages must fit inside an eager message */
    if (is_unexpected && sq->buflist.tot_len > ib_device->eager_buf_payload) {
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
    mop = Malloc(sizeof(*mop));
    id_gen_safe_register(&mop->op_id, mop);
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

static int
BMI_ib_post_send(bmi_op_id_t *id, struct method_addr *remote_map,
  const void *buffer, bmi_size_t size,
  enum bmi_buffer_type buffer_flag __unused,
  bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
    debug(3, "%s: len %d tag %d", __func__, (int) size, tag);
    /* references here will not be saved after this func returns */
    return generic_post_send(id, remote_map, 0, &buffer, &size, size,
      tag, user_ptr, context_id, 0);
}

static int
BMI_ib_post_send_list(bmi_op_id_t *id, struct method_addr *remote_map,
  const void *const *buffers, const bmi_size_t *sizes, int list_count,
  bmi_size_t total_size, enum bmi_buffer_type buffer_flag __unused,
  bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
    debug(2, "%s: listlen %d tag %d", __func__, list_count, tag);
    if (list_count < 1)
	error("%s: list count must be positive", __func__);
    return generic_post_send(id, remote_map, list_count, buffers, sizes,
      total_size, tag, user_ptr, context_id, 0);
}

static int
BMI_ib_post_sendunexpected(bmi_op_id_t *id, struct method_addr *remote_map,
  const void *buffer, bmi_size_t size,
  enum bmi_buffer_type buffer_flag __unused,
  bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
    debug(2, "%s: len %d tag %d", __func__, (int) size, tag);
    /* references here will not be saved after this func returns */
    return generic_post_send(id, remote_map, 0, &buffer, &size, size, tag,
      user_ptr, context_id, 1);
}

static int
BMI_ib_post_sendunexpected_list(bmi_op_id_t *id, struct method_addr *remote_map,
  const void *const *buffers, const bmi_size_t *sizes, int list_count,
  bmi_size_t total_size, enum bmi_buffer_type buffer_flag __unused,
  bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
    debug(2, "%s: listlen %d tag %d", __func__, list_count, tag);
    if (list_count < 1)
	error("%s: list count must be positive", __func__);
    /* references here will not be saved after this func returns */
    return generic_post_send(id, remote_map, list_count, buffers, sizes,
      total_size, tag, user_ptr, context_id, 1);
}

/*
 * Used by both recv and recv_list.
 */
static int
generic_post_recv(bmi_op_id_t *id, struct method_addr *remote_map,
  int numbufs, void *const *buffers, const bmi_size_t *sizes,
  bmi_size_t tot_expected_len, bmi_msg_tag_t tag,
  void *user_ptr, bmi_context_id context_id)
{
    ib_recv_t *rq;
    struct method_op *mop;
    ib_method_addr_t *ibmap;
    ib_connection_t *c;
    int i;
    int ret = 0;
    
    gen_mutex_lock(&interface_mutex);
    ret = ensure_connected(remote_map);
    if (ret)
    	goto out;
    ibmap = remote_map->method_data;
    c = ibmap->c;

    /* poll interface first to save a few steps below */
    ib_check_cq();

    /* check to see if matching recv is in the queue */
    rq = find_matching_recv(
      RQ_EAGER_WAITING_USER_POST | RQ_RTS_WAITING_USER_POST, c, tag);
    if (rq) {
	debug(2, "%s: rq %p matches %s", __func__, rq,
	  rq_state_name(rq->state));
    } else {
	/* alloc and build new recvq structure */
	rq = alloc_new_recv(c, NULL);
	rq->state = RQ_WAITING_INCOMING;
	rq->bmi_tag = tag;
	debug(2, "%s: new rq %p", __func__, rq);
    }

    if (numbufs == 0) {
	rq->buflist_one_buf = *buffers;
	rq->buflist_one_len = *sizes;
	rq->buflist.num = 1;
	rq->buflist.buf.recv = &rq->buflist_one_buf;
	rq->buflist.len = &rq->buflist_one_len;
    } else {
	rq->buflist.num = numbufs;
	rq->buflist.buf.recv = buffers;
	rq->buflist.len = sizes;
    }
    rq->buflist.tot_len = 0;
    for (i=0; i<rq->buflist.num; i++)
	rq->buflist.tot_len += sizes[i];

    /*
     * This passed-in total length field does not make much sense
     * to me, but I'll at least check it for accuracy.
     */
    if (rq->buflist.tot_len != tot_expected_len)
	error("%s: user-provided tot len %lld"
	  " does not match buffer list tot len %lld",
	  __func__, lld(tot_expected_len), lld(rq->buflist.tot_len));

    /* generate identifier used by caller to test for message later */
    mop = Malloc(sizeof(*mop));
    id_gen_safe_register(&mop->op_id, mop);
    mop->addr = remote_map;  /* set of function pointers, essentially */
    mop->method_data = rq;
    mop->user_ptr = user_ptr;
    mop->context_id = context_id;
    *id = mop->op_id;
    rq->mop = mop;

    /* handle the two "waiting for a local user post" states */
    if (rq->state == RQ_EAGER_WAITING_USER_POST) {

	msg_header_eager_t mh_eager;
	char *ptr = rq->bh->buf;

	decode_msg_header_eager_t(&ptr, &mh_eager);

	debug(2, "%s: rq %p state %s finish eager directly", __func__,
	  rq, rq_state_name(rq->state));
	if (rq->actual_len > tot_expected_len) {
	    error("%s: received %lld matches too-small buffer %lld",
	      __func__, lld(rq->actual_len), lld(rq->buflist.tot_len));
	}

	memcpy_to_buflist(&rq->buflist,
	                  (msg_header_eager_t *) rq->bh->buf + 1,
	                  rq->actual_len);

	/* re-post */
	post_rr(rq->c, rq->bh);
	/* done with buffer, ack to remote */
	post_sr_ack(rq->c, mh_eager.bufnum);

	/* now just wait for user to test, never do "immediate completion" */
	rq->state = RQ_EAGER_WAITING_USER_TEST;
	goto out;

    } else if (rq->state == RQ_RTS_WAITING_USER_POST) {
	int sret;
	debug(2, "%s: rq %p %s send cts", __func__, rq,
	  rq_state_name(rq->state));
	/* try to send, or wait for send buffer space */
	rq->state = RQ_RTS_WAITING_CTS_BUFFER;
#if MEMCACHE_EARLY_REG
	memcache_register(ib_device->memcache, &rq->buflist);
#endif
	sret = send_cts(rq);
	if (sret == 0)
	    rq->state = RQ_RTS_WAITING_DATA;
	goto out;
    }

#if MEMCACHE_EARLY_REG
    /* but remember that this might not be used if the other side sends
     * less than we posted for receive; that's legal */
    if (rq->buflist.tot_len > ib_device->eager_buf_payload)
	memcache_register(ib_device->memcache, &rq->buflist);
#endif

  out:
    gen_mutex_unlock(&interface_mutex);
    return ret;
}

static int
BMI_ib_post_recv(bmi_op_id_t *id, struct method_addr *remote_map,
  void *buffer, bmi_size_t expected_len, bmi_size_t *actual_len __unused,
  enum bmi_buffer_type buffer_flag __unused, bmi_msg_tag_t tag, void *user_ptr,
  bmi_context_id context_id)
{
    debug(2, "%s: expected len %d tag %d", __func__, (int) expected_len, tag);
    return generic_post_recv(id, remote_map, 0, &buffer, &expected_len,
      expected_len, tag, user_ptr, context_id);
}

static int
BMI_ib_post_recv_list(bmi_op_id_t *id, struct method_addr *remote_map,
  void *const *buffers, const bmi_size_t *sizes, int list_count,
  bmi_size_t tot_expected_len, bmi_size_t *tot_actual_len __unused,
  enum bmi_buffer_type buffer_flag __unused, bmi_msg_tag_t tag, void *user_ptr,
  bmi_context_id context_id)
{
    debug(2, "%s: tot expected len %d tag %d", __func__,
      (int) tot_expected_len, tag);
    if (list_count < 1)
	error("%s: list count must be positive", __func__);
    return generic_post_recv(id, remote_map, list_count, buffers, sizes,
      tot_expected_len, tag, user_ptr, context_id);
}

/*
 * Internal shared helper function.  Return 1 if found something
 * completed.
 */
static int
test_sq(ib_send_t *sq, bmi_op_id_t *outid, bmi_error_code_t *err,
  bmi_size_t *size, void **user_ptr, int complete)
{
    ib_connection_t *c;

    debug(9, "%s: sq %p outid %p err %p size %p user_ptr %p complete %d",
      __func__, sq, outid, err, size, user_ptr, complete);

    if (sq->state == SQ_WAITING_USER_TEST) {
	if (complete) {
	    debug(2, "%s: sq %p completed %lld to %s", __func__,
	      sq, lld(sq->buflist.tot_len), sq->c->peername);
	    *outid = sq->mop->op_id;
	    *err = 0;
	    *size = sq->buflist.tot_len;
	    if (user_ptr)
		*user_ptr = sq->mop->user_ptr;
	    qlist_del(&sq->list);
	    id_gen_safe_unregister(sq->mop->op_id);
	    c = sq->c;
	    free(sq->mop);
	    free(sq);
	    --c->refcnt;
	    if (c->closed)
		ib_close_connection(c);
	    return 1;
	}
    /* this state needs help, push it (ideally would be triggered
     * when the resource is freed... XXX */
    } else if (sq->state == SQ_WAITING_BUFFER) {
	debug(2, "%s: sq %p %s, encouraging", __func__, sq,
	  sq_state_name(sq->state));
	encourage_send_waiting_buffer(sq);
    } else if (sq->state == SQ_CANCELLED && complete) {
	debug(2, "%s: sq %p cancelled", __func__, sq);
	*outid = sq->mop->op_id;
	*err = -PVFS_ETIMEDOUT;
	if (user_ptr)
	    *user_ptr = sq->mop->user_ptr;
	qlist_del(&sq->list);
	id_gen_safe_unregister(sq->mop->op_id);
	c = sq->c;
	free(sq->mop);
	free(sq);
	--c->refcnt;
	maybe_free_connection(c);
	if (c->closed)
	    ib_close_connection(c);
	return 1;
    } else {
	debug(9, "%s: sq %p found, not done, state %s", __func__,
	  sq, sq_state_name(sq->state));
    }
    return 0;
}

/*
 * Internal shared helper function.  Return 1 if found something
 * completed.  Note that rq->mop can be null for unexpected
 * messages.
 */
static int
test_rq(ib_recv_t *rq, bmi_op_id_t *outid, bmi_error_code_t *err,
  bmi_size_t *size, void **user_ptr, int complete)
{
    ib_connection_t *c;

    debug(9, "%s: rq %p outid %p err %p size %p user_ptr %p complete %d",
      __func__, rq, outid, err, size, user_ptr, complete);

    if (rq->state == RQ_EAGER_WAITING_USER_TEST 
      || rq->state == RQ_RTS_WAITING_USER_TEST) {
	if (complete) {
	    debug(2, "%s: rq %p completed %lld from %s", __func__,
	      rq, lld(rq->actual_len), rq->c->peername);
	    *err = 0;
	    *size = rq->actual_len;
	    if (rq->mop) {
		*outid = rq->mop->op_id;
		if (user_ptr)
		    *user_ptr = rq->mop->user_ptr;
		id_gen_safe_unregister(rq->mop->op_id);
		free(rq->mop);
	    }
	    qlist_del(&rq->list);
	    c = rq->c;
	    free(rq);
	    --c->refcnt;
	    if (c->closed)
		ib_close_connection(c);
	    return 1;
	}
    /* this state needs help, push it (ideally would be triggered
     * when the resource is freed... XXX */
    } else if (rq->state == RQ_RTS_WAITING_CTS_BUFFER) {
	int ret;
	debug(2, "%s: rq %p %s, encouraging", __func__, rq,
	  rq_state_name(rq->state));
	ret = send_cts(rq);
	if (ret == 0)
	    rq->state = RQ_RTS_WAITING_DATA;
	/* else keep waiting until we can send that cts */
	debug(2, "%s: rq %p now %s", __func__, rq, rq_state_name(rq->state));
    } else if (rq->state == RQ_CANCELLED && complete) {
	debug(2, "%s: rq %p cancelled", __func__, rq);
	*err = -PVFS_ETIMEDOUT;
	if (rq->mop) {
	    *outid = rq->mop->op_id;
	    if (user_ptr)
		*user_ptr = rq->mop->user_ptr;
	    id_gen_safe_unregister(rq->mop->op_id);
	    free(rq->mop);
	}
	qlist_del(&rq->list);
	c = rq->c;
	free(rq);
	maybe_free_connection(c);
	--c->refcnt;
	if (c->closed)
	    ib_close_connection(c);
	return 1;
    } else {
	debug(9, "%s: rq %p found, not done, state %s", __func__,
	  rq, rq_state_name(rq->state));
    }
    return 0;
}

/*
 * Test one message, send or receive.  Also used to test the send side of
 * messages sent using sendunexpected.
 */
static int
BMI_ib_test(bmi_op_id_t id, int *outcount, bmi_error_code_t *err,
  bmi_size_t *size, void **user_ptr, int max_idle_time __unused,
  bmi_context_id context_id __unused)
{
    struct method_op *mop;
    ib_send_t *sq;
    int n;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();

    mop = id_gen_safe_lookup(id);
    sq = mop->method_data;
    n = 0;
    if (sq->type == BMI_SEND) {
	if (test_sq(sq, &id, err, size, user_ptr, 1))
	    n = 1;
    } else {
	/* actually a recv */
	ib_recv_t *rq = mop->method_data;
	if (test_rq(rq, &id, err, size, user_ptr, 1))
	    n = 1;
    }
    *outcount = n;
    gen_mutex_unlock(&interface_mutex);
    return 0;
}

/*
 * Test just the particular list of op ids, returning the list of indices
 * that completed.
 */
static int BMI_ib_testsome(int incount, bmi_op_id_t *ids, int *outcount,
  int *index_array, bmi_error_code_t *errs, bmi_size_t *sizes, void **user_ptrs,
  int max_idle_time __unused, bmi_context_id context_id __unused)
{
    struct method_op *mop;
    ib_send_t *sq;
    bmi_op_id_t tid;
    int i, n;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();

    n = 0;
    for (i=0; i<incount; i++) {
	if (!ids[i])
	    continue;
	mop = id_gen_safe_lookup(ids[i]);
	sq = mop->method_data;

	if (sq->type == BMI_SEND) {
	    if (test_sq(sq, &tid, &errs[n], &sizes[n], &user_ptrs[n], 1)) {
		index_array[n] = i;
		++n;
	    }
	} else {
	    /* actually a recv */
	    ib_recv_t *rq = mop->method_data;
	    if (test_rq(rq, &tid, &errs[n], &sizes[n], &user_ptrs[n], 1)) {
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
 * Used by the test functions to block if not much is going on
 * since the timeouts at the BMI job layer are too coarse.
 */
static struct timeval last_action = { 0, 0 };

/*
 * Test for multiple completions matching a particular user context.
 */
static int
BMI_ib_testcontext(int incount, bmi_op_id_t *outids, int *outcount,
  bmi_error_code_t *errs, bmi_size_t *sizes, void **user_ptrs,
  int max_idle_time, bmi_context_id context_id)
{
    list_t *l, *lnext;
    int n, complete;
    void **up = 0;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();

    /*
     * Walk _all_ entries on sq, rq, marking them completed or
     * encouraging them as needed due to resource limitations.
     */
    n = 0;
    for (l=ib_device->sendq.next; l != &ib_device->sendq; l=lnext) {
	ib_send_t *sq = qlist_upcast(l);
	lnext = l->next;
	/* test them all, even if can't reap them, just to encourage */
	complete = (sq->mop->context_id == context_id) && (n < incount);
	if (user_ptrs)
	    up = &user_ptrs[n];
	n += test_sq(sq, &outids[n], &errs[n], &sizes[n], up, complete);
    }

    for (l=ib_device->recvq.next; l != &ib_device->recvq; l=lnext) {
	ib_recv_t *rq = qlist_upcast(l);
	lnext = l->next;

	/* some receives have no mops:  unexpected */
	complete = rq->mop &&
	  (rq->mop->context_id == context_id) && (n < incount);
	if (user_ptrs)
	    up = &user_ptrs[n];
	n += test_rq(rq, &outids[n], &errs[n], &sizes[n], up, complete);
    }

    /* drop lock before blocking on new connections below */
    gen_mutex_unlock(&interface_mutex);

    *outcount = n;

    if (n > 0) {
	gettimeofday(&last_action, 0);  /* remember this action */
    } else if (max_idle_time > 0) {
	/*
	 * Spin for an interval after some activity, then go to a
	 * blocking interface by using poll() on the IB completion channel
	 * and TCP listen socket.
	 */
	struct timeval now;

	gettimeofday(&now, 0);
	timersub(&now, &last_action, &now);

	/* if time since last activity is > 10ms, block */
	if (now.tv_sec > 0 || now.tv_usec > 10000) {
	    /* block */
	    n = ib_block_for_activity(max_idle_time);
	    if (n)
		gettimeofday(&last_action, 0);  /* had some action */
	} else {
	    /* whee, spin */
	    /* totally helps on Lee's old 2.4.21 machine, but may cause
	     * big delays on modern kernels; do not make default */
	    /* sched_yield(); */
	    ;
	}
    }
    return 0;
}

/*
 * Non-blocking test to look for any incoming unexpected messages.
 * This is also where we check for new connections on the TCP socket, since
 * those would show up as unexpected the first time anything is sent.
 * Return 0 for success, or -1 for failure; number of things in *outcount.
 */
static int
BMI_ib_testunexpected(int incount __unused, int *outcount,
  struct method_unexpected_info *ui, int max_idle_time __unused)
{
    int num_action;
    list_t *l;

    gen_mutex_lock(&interface_mutex);

    /* Check CQ, then look for the first unexpected message.  */
    num_action = ib_check_cq();

    *outcount = 0;
    qlist_for_each(l, &ib_device->recvq) {
	ib_recv_t *rq = qlist_upcast(l);
	if (rq->state == RQ_EAGER_WAITING_USER_TESTUNEXPECTED) {
	    msg_header_eager_t mh_eager;
	    char *ptr = rq->bh->buf;
	    ib_connection_t *c;

	    decode_msg_header_eager_t(&ptr, &mh_eager);

	    debug(2, "%s: found waiting testunexpected", __func__);
	    ui->error_code = 0;
	    ui->addr = rq->c->remote_map;  /* hand back permanent method_addr */
	    ui->buffer = Malloc((unsigned long) rq->actual_len);
	    ui->size = rq->actual_len;
	    memcpy(ui->buffer,
	           (msg_header_eager_t *) rq->bh->buf + 1,
	           (size_t) ui->size);
	    ui->tag = rq->bmi_tag;
	    /* re-post the buffer in which it was sitting, just unexpecteds */
	    post_rr(rq->c, rq->bh);
	    /* freed our eager buffer, ack it */
	    post_sr_ack(rq->c, mh_eager.bufnum);
	    *outcount = 1;
	    qlist_del(&rq->list);
	    c = rq->c;
	    free(rq);
	    --c->refcnt;
	    if (c->closed)
		ib_close_connection(c);
	    goto out;
	}
    }

    /* check for new incoming connections */
    num_action += ib_tcp_server_check_new_connections();

    /* look for async events on the IB port */
    num_action += check_async_events();

    if (num_action)
	gettimeofday(&last_action, 0);

  out:
    gen_mutex_unlock(&interface_mutex);
    return 0;
}

/*
 * No need to track these internally.  Just search the entire queue.
 */
static int
BMI_ib_open_context(bmi_context_id context_id __unused)
{
    return 0;
}

static void
BMI_ib_close_context(bmi_context_id context_id __unused)
{
}

/*
 * Asynchronous call to destroy an in-progress operation.
 * Can't just call test since we don't want to reap the operation,
 * just make sure it's done or not.
 */
static int
BMI_ib_cancel(bmi_op_id_t id, bmi_context_id context_id __unused)
{
    struct method_op *mop;
    ib_send_t *tsq;
    ib_connection_t *c = 0;

    gen_mutex_lock(&interface_mutex);
    ib_check_cq();
    mop = id_gen_safe_lookup(id);
    tsq = mop->method_data;
    if (tsq->type == BMI_SEND) {
	/*
	 * Cancelling completed operations is fine, they will be
	 * tested later.  Any others trigger full shutdown of the
	 * connection.
	 */
	if (tsq->state != SQ_WAITING_USER_TEST)
	    c = tsq->c;
    } else {
	/* actually a recv */
	ib_recv_t *rq = mop->method_data;
	if (!(rq->state == RQ_EAGER_WAITING_USER_TEST 
	   || rq->state == RQ_RTS_WAITING_USER_TEST))
	    c = rq->c;
    }

    if (c && !c->cancelled) {
	/*
	 * In response to a cancel, forcibly close the connection.  Don't send
	 * a bye message first since it may be the case that the peer is dead
	 * anyway.  Do not close the connection until all the sq/rq on it have
	 * gone away.
	 */
	list_t *l;

	c->cancelled = 1;
	drain_qp(c);
	qlist_for_each(l, &ib_device->sendq) {
	    ib_send_t *sq = qlist_upcast(l);
	    if (sq->c != c) continue;
#if !MEMCACHE_BOUNCEBUF
	    if (sq->state == SQ_WAITING_DATA_LOCAL_SEND_COMPLETE)
		memcache_deregister(ib_device->memcache, &sq->buflist);
#  if MEMCACHE_EARLY_REG
	    /* pin when sending rts, so also must dereg in this state */
	    if (sq->state == SQ_WAITING_CTS)
		memcache_deregister(ib_device->memcache, &sq->buflist);
#  endif
#endif
	    if (sq->state != SQ_WAITING_USER_TEST)
		sq->state = SQ_CANCELLED;
	}
	qlist_for_each(l, &ib_device->recvq) {
	    ib_recv_t *rq = qlist_upcast(l);
	    if (rq->c != c) continue;
#if !MEMCACHE_BOUNCEBUF
	    if (rq->state == RQ_RTS_WAITING_DATA)
		memcache_deregister(ib_device->memcache, &rq->buflist);
#  if MEMCACHE_EARLY_REG
	    /* pin on post, dereg all these */
	    if (rq->state == RQ_RTS_WAITING_CTS_BUFFER)
		memcache_deregister(ib_device->memcache, &rq->buflist);
	    if (rq->state == RQ_WAITING_INCOMING
	      && rq->buflist.tot_len > ib_device->eager_buf_payload)
		memcache_deregister(ib_device->memcache, &rq->buflist);
#  endif
#endif
	    if (!(rq->state == RQ_EAGER_WAITING_USER_TEST 
	       || rq->state == RQ_RTS_WAITING_USER_TEST))
		rq->state = RQ_CANCELLED;
	}
    }

    gen_mutex_unlock(&interface_mutex);
    return 0;
}

/*
 * For connections that are being cancelled, maybe delete them if no
 * more send or recvq entries remain.
 */
static void
maybe_free_connection(ib_connection_t *c)
{
    list_t *l;

    if (!c->cancelled)
	return;
    qlist_for_each(l, &ib_device->sendq) {
	ib_send_t *sq = qlist_upcast(l);
	if (sq->c == c) return;
    }
    qlist_for_each(l, &ib_device->recvq) {
	ib_recv_t *rq = qlist_upcast(l);
	if (rq->c == c) return;
    }
    ib_close_connection(c);
}

static const char *
BMI_ib_rev_lookup(struct method_addr *meth)
{
    ib_method_addr_t *ibmap = meth->method_data;
    if (!ibmap->c)
	return "(unconnected)";
    else
	return ibmap->c->peername;
}

/*
 * Build and fill an IB-specific method_addr structure.
 */
static struct method_addr *ib_alloc_method_addr(ib_connection_t *c,
                                                const char *hostname, int port)
{
    struct method_addr *map;
    ib_method_addr_t *ibmap;

    map = alloc_method_addr(bmi_ib_method_id, (bmi_size_t) sizeof(*ibmap));
    ibmap = map->method_data;
    ibmap->c = c;
    ibmap->hostname = hostname;
    ibmap->port = port;

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
static struct method_addr *BMI_ib_method_addr_lookup(const char *id)
{
    char *s, *hostname, *cp, *cq;
    int port;
    struct method_addr *map = 0;

    /* parse hostname */
    s = string_key("ib", id);  /* allocs a string */
    if (!s)
	return 0;
    cp = strchr(s, ':');
    if (!cp)
	error("%s: no ':' found", __func__);

    /* copy to permanent storage */
    hostname = Malloc((unsigned long) (cp - s + 1));
    strncpy(hostname, s, (size_t) (cp-s));
    hostname[cp-s] = '\0';

    /* strip /filesystem  */
    ++cp;
    cq = strchr(cp, '/');
    if (cq)
	*cq = 0;
    port = strtoul(cp, &cq, 10);
    if (cq == cp)
	error("%s: invalid port number", __func__);
    if (*cq != '\0')
	error("%s: extra characters after port number", __func__);
    free(s);

    /* lookup in known connections, if there are any */
    gen_mutex_lock(&interface_mutex);
    if (ib_device) {
	list_t *l;
	qlist_for_each(l, &ib_device->connection) {
	    ib_connection_t *c = qlist_upcast(l);
	    ib_method_addr_t *ibmap = c->remote_map->method_data;
	    if (ibmap->port == port && !strcmp(ibmap->hostname, hostname)) {
	       map = c->remote_map;
	       break;
	    }
	}
    }
    gen_mutex_unlock(&interface_mutex);

    if (map)
	free(hostname);  /* found it */
    else
	map = ib_alloc_method_addr(0, hostname, port);  /* alloc new one */
	/* but don't call method_addr_reg_callback! */

    return map;
}

static ib_connection_t *ib_new_connection(int sock, const char *peername,
                                          int is_server)
{
    ib_connection_t *c;
    int i, ret;

    c = Malloc(sizeof(*c));
    c->peername = strdup(peername);

    /* fill send and recv free lists and buf heads */
    c->eager_send_buf_contig = Malloc(ib_device->eager_buf_num
      * ib_device->eager_buf_size);
    c->eager_recv_buf_contig = Malloc(ib_device->eager_buf_num
      * ib_device->eager_buf_size);
    INIT_QLIST_HEAD(&c->eager_send_buf_free);
    INIT_QLIST_HEAD(&c->eager_recv_buf_free);
    c->eager_send_buf_head_contig = Malloc(ib_device->eager_buf_num
      * sizeof(*c->eager_send_buf_head_contig));
    c->eager_recv_buf_head_contig = Malloc(ib_device->eager_buf_num
      * sizeof(*c->eager_recv_buf_head_contig));
    for (i=0; i<ib_device->eager_buf_num; i++) {
	buf_head_t *ebs = &c->eager_send_buf_head_contig[i];
	buf_head_t *ebr = &c->eager_recv_buf_head_contig[i];
	INIT_QLIST_HEAD(&ebs->list);
	INIT_QLIST_HEAD(&ebr->list);
	ebs->c = c;
	ebr->c = c;
	ebs->num = i;
	ebr->num = i;
	ebs->buf = (char *) c->eager_send_buf_contig
		 + i * ib_device->eager_buf_size;
	ebr->buf = (char *) c->eager_recv_buf_contig
		 + i * ib_device->eager_buf_size;
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

    ret = new_connection(c, sock, is_server);
    if (ret) {
	ib_close_connection(c);
	c = NULL;
    }

    return c;
}

static void ib_close_connection(ib_connection_t *c)
{
    ib_method_addr_t *ibmap;

    debug(2, "%s: closing connection to %s", __func__, c->peername);
    c->closed = 1;
    if (c->refcnt != 0) {
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
    ibmap = c->remote_map->method_data;
    ibmap->c = NULL;
    free(c->peername);
    qlist_del(&c->list);
    free(c);
}

#ifndef __PVFS2_SERVER__
/*
 * Blocking connect initiated by a post_sendunexpected{,_list}, or
 * post_recv*
 */
static int ib_tcp_client_connect(ib_method_addr_t *ibmap,
                                 struct method_addr *remote_map)
{
    int s;
    char peername[2048];
    struct hostent *hp;
    struct sockaddr_in skin;
    
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
	warning("%s: create tcp socket: %m", __func__);
	return bmi_errno_to_pvfs(errno);
    }
    hp = gethostbyname(ibmap->hostname);
    if (!hp) {
	warning("%s: cannot resolve server %s", __func__, ibmap->hostname);
	return -1;
    }
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = hp->h_addrtype;
    memcpy(&skin.sin_addr, hp->h_addr_list[0], (size_t) hp->h_length);
    skin.sin_port = htons(ibmap->port);
    sprintf(peername, "%s:%d", ibmap->hostname, ibmap->port);
  retry:
    if (connect(s, (struct sockaddr *) &skin, sizeof(skin)) < 0) {
	if (errno == EINTR)
	    goto retry;
	else {
	    warning("%s: connect to server %s: %m", __func__, peername);
	    return bmi_errno_to_pvfs(errno);
	}
    }
    ibmap->c = ib_new_connection(s, peername, 0);
    if (!ibmap->c)
	error("%s: ib_new_connection failed", __func__);
    ibmap->c->remote_map = remote_map;

    if (close(s) < 0) {
	warning("%s: close sock: %m", __func__);
	return bmi_errno_to_pvfs(errno);
    }
    return 0;
}
#endif

/*
 * On a server, initialize a socket for listening for new connections.
 */
static void ib_tcp_server_init_listen_socket(struct method_addr *addr)
{
    int flags;
    struct sockaddr_in skin;
    ib_method_addr_t *ibc = addr->method_data;

    ib_device->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ib_device->listen_sock < 0)
	error_errno("%s: create tcp socket", __func__);
    flags = 1;
    if (setsockopt(ib_device->listen_sock, SOL_SOCKET, SO_REUSEADDR, &flags,
      sizeof(flags)) < 0)
	error_errno("%s: setsockopt REUSEADDR", __func__);
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = AF_INET;
    skin.sin_port = htons(ibc->port);
  retry:
    if (bind(ib_device->listen_sock, (struct sockaddr *) &skin, sizeof(skin)) < 0) {
	if (errno == EINTR)
	    goto retry;
	else
	    error_errno("%s: bind tcp socket", __func__);
    }
    if (listen(ib_device->listen_sock, 1024) < 0)
	error_errno("%s: listen tcp socket", __func__);
    flags = fcntl(ib_device->listen_sock, F_GETFL);
    if (flags < 0)
	error_errno("%s: fcntl getfl listen sock", __func__);
    flags |= O_NONBLOCK;
    if (fcntl(ib_device->listen_sock, F_SETFL, flags) < 0)
	error_errno("%s: fcntl setfl nonblock listen sock", __func__);
}

/*
 * Check for new connections.  The listening socket is left nonblocking
 * so this test can be quick.  Returns >0 if an accept worked.
 */
static int ib_tcp_server_check_new_connections(void)
{
    struct sockaddr_in ssin;
    socklen_t len;
    int s, ret = 0;

    len = sizeof(ssin);
    s = accept(ib_device->listen_sock, (struct sockaddr *) &ssin, &len);
    if (s < 0) {
	if (!(errno == EAGAIN))
	    error_errno("%s: accept listen sock", __func__);
    } else {
	char peername[2048];
	ib_connection_t *c;

	char *hostname = strdup(inet_ntoa(ssin.sin_addr));
	int port = ntohs(ssin.sin_port);
	sprintf(peername, "%s:%d", hostname, port);

	c = ib_new_connection(s, peername, 1);
	if (!c) {
	    free(hostname);
	    close(s);
	    return 0;
	}

	c->remote_map = ib_alloc_method_addr(c, hostname, port);
	/* register this address with the method control layer */
	ret = bmi_method_addr_reg_callback(c->remote_map);
	if (ret < 0)
	    error_xerrno(ret, "%s: bmi_method_addr_reg_callback", __func__);

	debug(2, "%s: accepted new connection %s at server", __func__,
	  c->peername);
	if (close(s) < 0)
	    error_errno("%s: close new sock", __func__);
	ret = 1;
    }
    return ret;
}

/*
 * Ask the device to write to its FD if a CQ event happens, and poll on it
 * as well as the listen_sock for activity, but do not actually respond to
 * anything.  A later ib_check_cq will handle CQ events, and a later call to
 * testunexpected will pick up new connections.  Returns >0 if something is
 * ready.
 */
static int ib_block_for_activity(int timeout_ms)
{
    struct pollfd pfd[2];
    int numfd;
    int ret;

    pfd[0].fd = prepare_cq_block();
    pfd[0].events = POLLIN;
    numfd = 1;
    if (ib_device->listen_sock >= 0) {
	pfd[1].fd = ib_device->listen_sock;
	pfd[1].events = POLLIN;
	numfd = 2;
    }
    ret = poll(pfd, numfd, timeout_ms);
    if (ret < 0) {
	if (errno == EINTR)  /* normal, ignore but break */
	    ret = 0;
	else
	    error_errno("%s: poll listen sock", __func__);
    }
    debug(4, "%s: ret %d rev0 0x%x", __func__, ret, pfd[0].revents);
    return ret;
}

static void * BMI_ib_memalloc(bmi_size_t len,
                              enum bmi_op_type send_recv __unused)
{
    return memcache_memalloc(ib_device->memcache, len,
                             ib_device->eager_buf_payload);
}

static int BMI_ib_memfree(void *buf, bmi_size_t len,
                          enum bmi_op_type send_recv __unused)
{
    return memcache_memfree(ib_device->memcache, buf, len);
}

/*
 * Callers sometimes want to know odd pieces of information.  Satisfy
 * them.
 */
static int BMI_ib_get_info(int option, void *param)
{
    int ret = 0;

    switch (option) {
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
 * Used to set some optional parameters.  Just ignore.
 */
static int BMI_ib_set_info(int option __unused, void *param __unused)
{
    /* XXX: should return -ENOSYS, but 0 now until callers handle
     * that correctly. */
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
static int BMI_ib_initialize(struct method_addr *listen_addr, int method_id,
                             int init_flags)
{
    int ret;

    debug(0, "%s: init", __func__);

    gen_mutex_lock(&interface_mutex);

    /* check params */
    if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER))
	error("%s: error: BMI_INIT_SERVER requires non-null listen_addr"
	  " and v.v", __func__);

    bmi_ib_method_id = method_id;

    ib_device = Malloc(sizeof(*ib_device));

    /* try, in order, OpenIB then VAPI; set up function pointers */
    ret = 1;
#ifdef OPENIB
    ret = openib_ib_initialize();
#endif
#ifdef VAPI
    if (ret)
	ret = vapi_ib_initialize();
#endif
    if (ret)
	return -ENODEV;  /* neither found */

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
	ib_tcp_server_init_listen_socket(listen_addr);
    else
	ib_device->listen_sock = -1;

    /*
     * Initialize data structures.
     */
    INIT_QLIST_HEAD(&ib_device->connection);
    INIT_QLIST_HEAD(&ib_device->sendq);
    INIT_QLIST_HEAD(&ib_device->recvq);

    ib_device->eager_buf_num  = DEFAULT_EAGER_BUF_NUM;
    ib_device->eager_buf_size = DEFAULT_EAGER_BUF_SIZE;
    ib_device->eager_buf_payload = ib_device->eager_buf_size - sizeof(msg_header_eager_t);

    gen_mutex_unlock(&interface_mutex);

    debug(0, "%s: done", __func__);
    return ret;
}

/*
 * Shutdown.
 */
static int BMI_ib_finalize(void)
{
    gen_mutex_lock(&interface_mutex);

    /* if client, send BYE to each connection and bring down the QP */
    if (ib_device->listen_sock < 0) {
	list_t *l;
	qlist_for_each(l, &ib_device->connection) {
	    ib_connection_t *c = qlist_upcast(l);
	    if (c->cancelled)
		continue;  /* already closed */
	    /* Send BYE message to servers, transition QP to drain state */
	    send_bye(c);
	    drain_qp(c);
	}
    }
    /* if server, stop listening */
    if (ib_device->listen_sock >= 0)
	close(ib_device->listen_sock);

    /* destroy QPs and other connection structures */
    while (ib_device->connection.next != &ib_device->connection) {
	ib_connection_t *c = (ib_connection_t *) ib_device->connection.next;
	ib_close_connection(c);
    }

#if MEMCACHE_BOUNCEBUF
    if (reg_send_buflist.num > 0) {
	memcache_deregister(ib_device->memcache, &reg_send_buflist);
	reg_send_buflist.num = 0;
	free(reg_send_buflist_buf);
    }
    if (reg_recv_buflist.num > 0) {
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
    return 0;
}

const struct bmi_method_ops bmi_ib_ops = 
{
    .method_name = "bmi_ib",
    .BMI_meth_initialize = BMI_ib_initialize,
    .BMI_meth_finalize = BMI_ib_finalize,
    .BMI_meth_set_info = BMI_ib_set_info,
    .BMI_meth_get_info = BMI_ib_get_info,
    .BMI_meth_memalloc = BMI_ib_memalloc,
    .BMI_meth_memfree = BMI_ib_memfree,
    .BMI_meth_post_send = BMI_ib_post_send,
    .BMI_meth_post_sendunexpected = BMI_ib_post_sendunexpected,
    .BMI_meth_post_recv = BMI_ib_post_recv,
    .BMI_meth_test = BMI_ib_test,
    .BMI_meth_testsome = BMI_ib_testsome,
    .BMI_meth_testcontext = BMI_ib_testcontext,
    .BMI_meth_testunexpected = BMI_ib_testunexpected,
    .BMI_meth_method_addr_lookup = BMI_ib_method_addr_lookup,
    .BMI_meth_post_send_list = BMI_ib_post_send_list,
    .BMI_meth_post_recv_list = BMI_ib_post_recv_list,
    .BMI_meth_post_sendunexpected_list = BMI_ib_post_sendunexpected_list,
    .BMI_meth_open_context = BMI_ib_open_context,
    .BMI_meth_close_context = BMI_ib_close_context,
    .BMI_meth_cancel = BMI_ib_cancel,
    .BMI_meth_rev_lookup_unexpected = BMI_ib_rev_lookup,
};


/*
 * InfiniBand BMI method.
 *
 * Copyright (C) 2003 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * $Id: ib.c,v 1.5 2004-01-30 16:26:57 pw Exp $
 */
#include <stdio.h>  /* just for NULL for id-generator.h */
#include <src/common/id-generator/id-generator.h>
#include <src/common/quicklist/quicklist.h>
#include <src/io/bmi/bmi-method-support.h>
#include <src/common/gen-locks/gen-locks.h>
#include <vapi.h>
#include <vapi_common.h>
#include "ib.h"

static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;

/* alloc space for shared variables */
bmi_size_t EAGER_BUF_PAYLOAD __hidden;
VAPI_hca_hndl_t nic_handle __hidden;
VAPI_cq_hndl_t nic_cq __hidden;
int listen_sock __hidden;
list_t connection __hidden;
list_t sendq __hidden;
list_t recvq __hidden;
list_t incomingq __hidden;
VAPI_sg_lst_entry_t *sg_tmp_array __hidden;
int sg_max_len __hidden;

static int send_cts(ib_recv_t *rq);
static void post_sr(const buf_head_t *bh, u_int32_t len);
/* post_rr declared externally */
static void post_sr_ack(const ib_connection_t *c, const buf_head_t *bh);
static void post_rr_ack(const ib_connection_t *c, const buf_head_t *bh);
static void post_sr_rdmaw(ib_send_t *sq, msg_header_cts_t *mh_cts);
static void encourage_send(ib_send_t *sq, incoming_t *in);
static void encourage_recv(ib_recv_t *rq, incoming_t *in);

/*
 * Look through this CTS message to determine the owning sq.  Works
 * using the mop_id which was sent during the RTS, now returned to us.
 */
static ib_send_t *
find_cts_owner(msg_header_cts_t *mh_cts)
{
    ib_send_t *sq = 0;
    list_t *l;

    /* we sent him our sq->mop_id->op_id, go looking for that */
    qlist_for_each(l, &sendq) {
	ib_send_t *sqt = (ib_send_t *) l;
	if (sqt->mop->op_id == (bmi_op_id_t) mh_cts->rts_mop_id) {
	    sq = sqt;
	    break;
	}
    }
    if (!sq)
	error("%s: mop_id %Lx in CTS message not found", __func__,
	  mh_cts->rts_mop_id);
    return sq;
}

/*
 * Wander through single completion queue, pulling off messages and
 * sticking them on the proper connection queues.  Later you can
 * walk the incomingq looking for things to do to them.  Returns
 * number of new things that arrived.
 */
static int
check_cq(void)
{
    VAPI_wc_desc_t desc;
    int ret = 0;

    memset(&desc, -1, sizeof(desc));
    for (;;) {
	incoming_t *in;

	int vret = VAPI_poll_cq(nic_handle, nic_cq, &desc);
	if (vret < 0) {
	    if (vret == VAPI_CQ_EMPTY)
		break;
	    error_verrno(ret, "%s: VAPI_poll_cq", __func__);
	}

	debug(2, "%s: found something", __func__);
	if (desc.status != VAPI_SUCCESS)
	    error("%s: entry id 0x%Lx opcode %s error %s", __func__,
	      desc.id, VAPI_cqe_opcode_sym(desc.opcode),
	      VAPI_wc_status_sym(desc.status));

	if (desc.opcode == VAPI_CQE_RQ_SEND_DATA) {
	    /*
	     * Remote side did a send to us.  Filled one of the receive
	     * queue descriptors, either message or ack.
	     */
	    buf_head_t *bh = ptr_from_int64(desc.id);
	    ib_connection_t *c = bh->c;
	    in = qlist_del_head(&c->incoming_free);
	    in->bh = bh;
	    in->byte_len = desc.byte_len;

	    if (in->byte_len == 0) {
		/*
		 * Acknowledgment message on qp_ack.
		 */
		int bufnum = desc.imm_data;
		assert(desc.imm_data_valid, "%s: immediate data is not valid",
		  __func__);
		debug(2, "%s: acknowledgment message, buf %d",
		  __func__, bufnum);
		assert(bufnum == bh->num, "%s: ack out of sequence, got %d"
		  " in descriptor for buffer %d", __func__, bufnum, bh->num);
		/*
		 * Do not get the sq from the bh that posted this, just in case
		 * it ever becomes okay to do out-of-order.  Instead look up
		 * the bufnum in the static send array.  This sq will actually
		 * be an rq if the ack is of a CTS.
		 */
		in->sq = c->eager_send_buf_head_contig[bufnum].sq;

	    } else {
		/*
		 * Some other message: eager send, RTS, CTS, BYE.
		 */
		msg_header_t *mh = bh->buf;

		debug(2, "%s: found len %d at bufnum %d type %s",
		  __func__, in->byte_len, bh->num, msg_type_name(mh->type));
		if (mh->type == MSG_CTS) {
		    /* incoming CTS messages go to the send engine */
		    debug(2, "%s: found cts message", __func__);
		    in->sq = find_cts_owner((void *)(mh+1));
		} else {
		    /* something for the recv side, no known rq yet */
		    debug(2, "%s: found message for receive engine", __func__);
		    in->sq = 0;
		}
	    }
	} else if (desc.opcode == VAPI_CQE_SQ_RDMA_WRITE) {

	    /* completion event for the rdma write we initiated, used
	     * to signal memory unpin etc. */
	    ib_send_t *sq = ptr_from_int64(desc.id);
	    in = qlist_del_head(&sq->c->incoming_free);
	    in->sq = sq;

	} else {
	    const char *ops = VAPI_cqe_opcode_sym(desc.opcode);
	    if (!ops)
		ops = "(null)";
	    error("%s: cq entry id 0x%Lx opcode %s (%d) unexpected", __func__,
	      desc.id, ops, desc.opcode);
	}

	qlist_add_tail(&in->list, &incomingq);
	++ret;
    }
    return ret;
}

/*
 * Move things off incoming into the state of the send/recv queues, and
 * push them toward the next step of their processing.
 */
static void
process_cq(void)
{
    list_t *l;
    incoming_t *in;

    /* walk list statelessly */
    for (;;) {
	l = incomingq.next;
	if (l == &incomingq)
	    break;
	in = qlist_upcast(l);
	qlist_del(l);
	qlist_add(l, &in->c->incoming_free);
	if (in->sq == 0) {
	    encourage_recv(0, in);
	} else if (in->sq->type == TYPE_RECV) {
	    /* ack of a CTS send by the receiver */
	    encourage_recv((ib_recv_t *) in->sq, in);
	} else {
	    assert(in->sq->type == TYPE_SEND, "%s: expecting sendq item",
	      __func__);
	    encourage_send(in->sq, in);
	}
    }
}

/*
 * Push a send message along its next step.
 */
static void
encourage_send(ib_send_t *sq, incoming_t *in)
{
    if (sq->state == SQ_WAITING_BUFFER) {
	/*
	 * Must get buffers both locally and remote to do an eager send
	 * or to initiate an RTS.  Maybe pair these two allocations if it
	 * happens frequently.
	 */
	buf_head_t *bh;
	msg_header_t *mh;

	debug(2, "%s: sq %p %s in %p", __func__, sq, sq_state_name(sq->state),
	  in);
	assert(!in, "%s: state %s yet incoming non-null", __func__,
	  sq_state_name(sq->state));
	bh = qlist_try_del_head(&sq->c->eager_send_buf_free);
	if (!bh) {
	    debug(2, "%s: sq %p %s no free send buffers", __func__, sq,
	      sq_state_name(sq->state));
	    return;
	}
	sq->bh = bh;
	bh->sq = sq;  /* uplink for completion */

	if (sq->buflist.tot_len <= EAGER_BUF_PAYLOAD) {
	    /*
	     * Eager send.
	     */
	    mh = bh->buf;
	    mh->type = sq->is_unexpected
	      ? MSG_EAGER_SENDUNEXPECTED : MSG_EAGER_SEND;
	    mh->bmi_tag = sq->bmi_tag;

	    memcpy_from_buflist(&sq->buflist, mh + 1);

	    /* get ready to receive the ack */
	    post_rr_ack(sq->c, bh);

	    /* send the message */
	    post_sr(bh, sizeof(*mh) + sq->buflist.tot_len);

	    /* wait for ack saying remote has received and recycled his buf */
	    sq->state = SQ_WAITING_EAGER_ACK;
	    debug(2, "%s: sq %p sent EAGER now state %s", __func__, sq,
	      sq_state_name(sq->state));

	} else {
	    /*
	     * Request to send, rendez-vous.  Include the mop id in the message
	     * which will be returned to us in the CTS so we can look it up.
	     */
	    msg_header_rts_t *mh_rts;

	    mh = bh->buf;
	    mh->type = MSG_RTS;
	    mh->bmi_tag = sq->bmi_tag;
	    mh_rts = (void*)((char *) bh->buf + sizeof(*mh));
	    mh_rts->mop_id = sq->mop->op_id;
	    mh_rts->tot_len = sq->buflist.tot_len;

	    /* get ready to receive the ack */
	    post_rr_ack(sq->c, bh);

	    post_sr(bh, sizeof(*mh) + sizeof(*mh_rts));

	    sq->state = SQ_WAITING_RTS_ACK;
	    debug(2, "%s: sq %p sent RTS now state %s", __func__, sq,
	      sq_state_name(sq->state));
	}

    } else if (sq->state == SQ_WAITING_EAGER_ACK) {

	debug(2, "%s: sq %p %s in %p", __func__, sq, sq_state_name(sq->state),
	  in);
	assert(in, "%s: state %s yet incoming null", __func__,
	  sq_state_name(sq->state));

	qlist_add_tail(&sq->bh->list, &sq->c->eager_send_buf_free);
	sq->state = SQ_WAITING_USER_TEST;

    } else if (sq->state == SQ_WAITING_RTS_ACK) {

	debug(2, "%s: sq %p %s in %p", __func__, sq, sq_state_name(sq->state),
	  in);
	assert(in, "%s: state %s yet incoming null", __func__,
	  sq_state_name(sq->state));

	qlist_add_tail(&sq->bh->list, &sq->c->eager_send_buf_free);
	sq->state = SQ_WAITING_CTS;

    } else if (sq->state == SQ_WAITING_CTS) {

	msg_header_t *mh;
	msg_header_cts_t *mh_cts;
	u_int32_t want;

	debug(2, "%s: sq %p %s in %p", __func__, sq, sq_state_name(sq->state),
	  in);
	assert(in, "%s: state %s yet incoming null", __func__,
	  sq_state_name(sq->state));
	/*
	 * Look at the incoming message, it had better be a CTS, and
	 * start the real data send.  Actually it is known this is a cts
	 * since the matching above found the sq corresponding to it!
	 */
	mh = in->bh->buf;
	mh_cts = (void *)(mh + 1);

	assert(mh->type == MSG_CTS, "%s: expecting CTS", __func__);

	/* message, cts content, list of buffers and lengths */
	want = sizeof(*mh) + sizeof(*mh_cts) + mh_cts->buflist_num * (8+4+4);
	assert(in->byte_len == want,
	  "%s: wrong message size for CTS, got %u, want %u", __func__,
	  in->byte_len, want);

	/* save the bh which received the CTS for later acking */
	sq->bh = in->bh;

	/* start the big tranfser */
	post_sr_rdmaw(sq, mh_cts);

	sq->state = SQ_WAITING_DATA_LOCAL_SEND_COMPLETE;

    } else if (sq->state == SQ_WAITING_DATA_LOCAL_SEND_COMPLETE) {

	/* re-post and ack cts saved above, signals rdma completed */
	post_rr(sq->c, sq->bh);
	post_sr_ack(sq->c, sq->bh);

	ib_mem_deregister(&sq->buflist);
	sq->state = SQ_WAITING_USER_TEST;

    } else {
	error("%s: unknown send state %s", __func__, sq_state_name(sq->state));
    }
}

/*
 * See if anything was preposted that matches this.
 */
static ib_recv_t *
find_matching_recv(const ib_connection_t *c, bmi_msg_tag_t bmi_tag)
{
    list_t *l;

    qlist_for_each(l, &recvq) {
	ib_recv_t *rq = (ib_recv_t *) l;
	if (rq->state == RQ_WAITING_INCOMING && rq->c == c
	  && rq->bmi_tag == bmi_tag)
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
    rq->type = TYPE_RECV;
    rq->c = c;
    rq->bh = bh;
    qlist_add_tail(&rq->list, &recvq);
    return rq;
}

/*
 * Called from two places.  First in response to an incoming message in
 * which case rq == 0, in != 0, or for ack of a CTS, rq != 0, in != 0.
 * Second internally by the state machine with rq != 0, in == 0.
 */
static void
encourage_recv(ib_recv_t *rq, incoming_t *in)
{
    if (rq == 0) {
	/*
	 * Unexpected receive, either no post or explicit sendunexpected.
	 */
	msg_header_t *mh = in->bh->buf;
	ib_connection_t *c = in->c;

	debug(2, "%s: null rq, incoming %p type %s", __func__, in,
	  msg_type_name(mh->type));

	if (mh->type == MSG_EAGER_SEND) {

	    rq = find_matching_recv(c, mh->bmi_tag);
	    if (rq) {
		int len = in->byte_len - sizeof(*mh);
		if (len > rq->buflist.tot_len)
		    error("%s: EAGER received %d too small for buffer "
		      FORMAT_BMI_SIZE_T,
		      __func__, len, rq->buflist.tot_len);

		memcpy_to_buflist(&rq->buflist,
		  (char *) in->bh->buf + sizeof(*mh), len);

		/* re-post */
		post_rr(c, in->bh);
		/* done with buffer, ack to remote */
		post_sr_ack(c, in->bh);
		rq->state = RQ_EAGER_WAITING_USER_TEST;

	    } else {
		rq = alloc_new_recv(c, in->bh);
		/* return value for when user does post_recv for this one */
		rq->bmi_tag = mh->bmi_tag;
		rq->state = RQ_EAGER_WAITING_USER_POST;
		/* do not repost or ack, keeping bh until user test */
	    }
	    rq->actual_len = in->byte_len - sizeof(*mh);

	} else if (mh->type == MSG_EAGER_SENDUNEXPECTED) {

	    rq = alloc_new_recv(c, in->bh);
	    /* return values for when user does testunexpected for this one */
	    rq->bmi_tag = mh->bmi_tag;
	    rq->state = RQ_EAGER_WAITING_USER_TESTUNEXPECTED;
	    rq->actual_len = in->byte_len - sizeof(*mh);
	    /* do not repost or ack, keeping bh until user test */

	} else if (mh->type == MSG_RTS) {
	    /*
	     * Sender wants to send a big message, initiates rts/cts protocol.
	     * Has the user posted a matching receive for it yet?
	     */
	    msg_header_rts_t *mh_rts = (void *)(mh + 1);

	    rq = find_matching_recv(c, mh->bmi_tag);
	    if (rq) {
		if ((int)mh_rts->tot_len > rq->buflist.tot_len) {
		    error("%s: RTS received " FORMAT_U_INT64_T
		      " too small for buffer " FORMAT_U_INT64_T,
		      __func__, mh_rts->tot_len, rq->buflist.tot_len);
		}
		debug(2, "%s: rq %p MSG_RTS in %p, will send cts next",
		  __func__, rq, in);
		rq->state = RQ_RTS_WAITING_CTS_BUFFER;
	    } else {
		debug(2, "%s: MSG_RTS in %p, new recv alloced, waiting user",
		  __func__, in);
		rq = alloc_new_recv(c, in->bh);
		/* return value for when user does post_recv for this one */
		rq->bmi_tag = mh->bmi_tag;
		rq->state = RQ_RTS_WAITING_USER_POST;
	    }
	    rq->actual_len = mh_rts->tot_len;
	    rq->rts_mop_id = mh_rts->mop_id;

	    /* ack his rts for simplicity */
	    debug(2, "%s: refill our recv and ack his RTS", __func__);
	    post_rr(c, in->bh);
	    post_sr_ack(c, in->bh);

	    if (rq->state == RQ_RTS_WAITING_CTS_BUFFER) {
		/* about 24 lines down, handle cts_buffer */
		debug(2, "%s: jumping to push state %s", __func__,
		  rq_state_name(rq->state));
		goto continue_with_cts_buffer;
	    }

	} else if (mh->type == MSG_BYE) {
	    /*
	     * Other side requests connection close.  Do it.
	     */
	    ib_close_connection(c);

	} else {
	    error("%s: unknown message header type %d", __func__, mh->type);
	}

    } else {
	/*
	 * The rq was matched during the receive, or this was called from
	 * elsewhere.  Do something with it.
	 */
	if (rq->state == RQ_RTS_WAITING_CTS_BUFFER) {

	    int ret;
	  continue_with_cts_buffer:
	    debug(2, "%s: rq %p state %s", __func__, rq,
	      rq_state_name(rq->state));
	    ret = send_cts(rq);
	    if (ret == 0)
		rq->state = RQ_RTS_WAITING_DATA;
		/* do not jump there, must wait for data to arrive */

	} else if (rq->state == RQ_RTS_WAITING_DATA) {

	    /* Data has arrived, we know because we got the ack to the CTS
	     * we sent out.  Serves to release remote cts buffer too */
	    debug(2, "%s: rq %p state %s", __func__, rq,
	      rq_state_name(rq->state));

	    /* XXX: should be head for cache, but use tail for debugging */
	    qlist_add_tail(&rq->bh->list, &rq->c->eager_send_buf_free);
	    ib_mem_deregister(&rq->buflist);
	    rq->state = RQ_RTS_WAITING_USER_TEST;

	} else {
	    error("%s: unknown state %s", __func__, rq_state_name(rq->state));
	}
    }
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
    msg_header_t *mh;
    msg_header_cts_t *mh_cts;
    u_int64_t *bufp;
    u_int32_t *lenp;
    u_int32_t *keyp;
    u_int32_t post_len;
    int i;

    debug(2, "%s: sending on an rq, offering to recv len %Ld", __func__,
      Ld(rq->buflist.tot_len));

    bh = qlist_try_del_head(&rq->c->eager_send_buf_free);
    if (!bh) {
	debug(2, "%s: no bh available", __func__);
	return 1;
    }
    rq->bh = bh;
    bh->sq = (ib_send_t *) rq;  /* uplink for completion */

    ib_mem_register(&rq->buflist, TYPE_RECV);

    /* expect an ack for this cts */
    post_rr_ack(rq->c, bh);

    mh = bh->buf;
    mh->type = MSG_CTS;
    /* XXX: mh->bmi_tag unused, consider a more primitive union */
    mh_cts = (void *)((char *) bh->buf + sizeof(*mh));
    mh_cts->rts_mop_id = rq->rts_mop_id;
    mh_cts->buflist_num = rq->buflist.num;
    mh_cts->buflist_tot_len = rq->buflist.tot_len;
    /* encode all the buflist entries */
    bufp = (u_int64_t *)(mh_cts + 1);
    lenp = (u_int32_t *)(bufp + rq->buflist.num);
    keyp = (u_int32_t *)(lenp + rq->buflist.num);
    post_len = (char *)(keyp + rq->buflist.num) - (char *)mh;
    if (post_len > EAGER_BUF_SIZE)
	error("%s: too many (%d) recv buflist entries for buf",  __func__,
	  rq->buflist.num);
    for (i=0; i<rq->buflist.num; i++) {
	bufp[i] = int64_from_ptr(rq->buflist.buf.recv[i]);
	lenp[i] = rq->buflist.len[i];
	keyp[i] = rq->buflist.rkey[i];
    }

    post_sr(bh, post_len);
    return 0;
}


/*
 * Simplify VAPI interface to post sends.  Not RDMA, just SEND.
 * Called for an eager send, rts send, or cts send.  Local send
 * completion is ignored.
 */
static void
post_sr(const buf_head_t *bh, u_int32_t len)
{
    VAPI_sg_lst_entry_t sg;
    VAPI_sr_desc_t sr;
    int ret;

    debug(2, "%s: bh %d len %u", __func__, bh->num, len);
    sg.addr = int64_from_ptr(bh->buf);
    sg.len = len;
    sg.lkey = bh->c->eager_send_lkey;

    memset(&sr, 0, sizeof(sr));
    sr.opcode = VAPI_SEND;
    sr.comp_type = VAPI_UNSIGNALED;  /* == 1 */
    sr.sg_lst_p = &sg;
    sr.sg_lst_len = 1;
    ret = VAPI_post_sr(nic_handle, bh->c->qp, &sr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_post_sr", __func__);
}

/*
 * Post one of the eager recv bufs for this connection.
 */
void
post_rr(const ib_connection_t *c, buf_head_t *bh)
{
    VAPI_sg_lst_entry_t sg;
    VAPI_rr_desc_t rr;
    int ret;

    debug(2, "%s: bh %d", __func__, bh->num);
    sg.addr = int64_from_ptr(bh->buf);
    sg.len = EAGER_BUF_SIZE;
    sg.lkey = c->eager_recv_lkey;

    memset(&rr, 0, sizeof(rr));
    rr.opcode = VAPI_RECEIVE;
    rr.id = int64_from_ptr(bh);
    rr.sg_lst_p = &sg;
    rr.sg_lst_len = 1;
    ret = VAPI_post_rr(nic_handle, c->qp, &rr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_post_rr", __func__);
}

/*
 * Explicitly return a credit.  Immediate data says for which local
 * buffer on the sender is this ack.  Buffers are tied together, so
 * we use our local bufnum which is the same as his.
 */
static void
post_sr_ack(const ib_connection_t *c, const buf_head_t *bh)
{
    VAPI_sr_desc_t sr;
    int ret;

    debug(2, "%s: bh %d", __func__, bh->num);
    memset(&sr, 0, sizeof(sr));
    sr.opcode = VAPI_SEND_WITH_IMM;
    sr.comp_type = VAPI_UNSIGNALED;  /* == 1 */
    sr.imm_data = bh->num;
    sr.sg_lst_len = 0;
    ret = VAPI_post_sr(nic_handle, c->qp_ack, &sr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_post_sr", __func__);
}

/*
 * Put another receive entry on the list for an ack.  These have no
 * data, so require no local buffers.  Just add a descriptor to the
 * NIC list.  We do keep the .id pointing to the bh which is the originator
 * of the eager (or RTS or whatever) send, just as a consistency check
 * that when the ack comes in, it is for the outgoing message we expected.
 *
 * In the future they could be out-of-order, though, so perhaps that will
 * go away.
 *
 * Could prepost a whole load of these and just replenish them without
 * thinking.
 */
static void
post_rr_ack(const ib_connection_t *c, const buf_head_t *bh)
{
    VAPI_rr_desc_t rr;
    int ret;

    debug(2, "%s: bh %d", __func__, bh->num);
    memset(&rr, 0, sizeof(rr));
    rr.opcode = VAPI_RECEIVE;
    rr.id = int64_from_ptr(bh);
    ret = VAPI_post_rr(nic_handle, c->qp_ack, &rr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_post_rr", __func__);
}

/*
 * Called only in response to receipt of a CTS on the sender.  RDMA write
 * the big data to the other side.  A bit messy since an RDMA write may
 * not scatter to the receiver, but can gather from the sender, and we may
 * have a non-trivial buflist on both sides.
 */
static void
post_sr_rdmaw(ib_send_t *sq, msg_header_cts_t *mh_cts)
{
    VAPI_sr_desc_t sr;
    int done;

    int send_index = 0, recv_index = 0;    /* working entry in buflist */
    int send_offset = 0;  /* byte offset in working send entry */
    u_int64_t *recv_bufp = (u_int64_t *)(mh_cts + 1);
    u_int32_t *recv_lenp = (u_int32_t *)(recv_bufp + mh_cts->buflist_num);
    u_int32_t *recv_rkey = (u_int32_t *)(recv_lenp + mh_cts->buflist_num);

    debug(2, "%s: sq %p totlen %d", __func__, sq, (int) sq->buflist.tot_len);

    ib_mem_register(&sq->buflist, TYPE_SEND);

    /* constant things for every send */
    memset(&sr, 0, sizeof(sr));
    sr.opcode = VAPI_RDMA_WRITE;
    sr.comp_type = VAPI_UNSIGNALED;
    sr.sg_lst_p = sg_tmp_array;

    done = 0;
    while (!done) {
	int ret;
	u_int32_t recv_bytes_needed;

	/*
	 * Driven by recv elements.  Sizes have already been checked
	 * (hopefully).
	 */
	sr.remote_addr = recv_bufp[recv_index];
	sr.r_key = recv_rkey[recv_index];
	sr.sg_lst_len = 0;
	recv_bytes_needed = recv_lenp[recv_index];
	while (recv_bytes_needed > 0) {
	    /* consume from send buflist to fill this one receive */
	    u_int32_t send_bytes_offered
	      = sq->buflist.len[send_index] - send_offset;
	    u_int32_t this_bytes = send_bytes_offered;
	    if (this_bytes > recv_bytes_needed)
		this_bytes = recv_bytes_needed;

	    sg_tmp_array[sr.sg_lst_len].addr =
	      int64_from_ptr(sq->buflist.buf.send[send_index])
	      + send_offset;
	    sg_tmp_array[sr.sg_lst_len].len = this_bytes;
	    sg_tmp_array[sr.sg_lst_len].lkey = sq->buflist.lkey[send_index];
	    ++sr.sg_lst_len;
	    if ((int)sr.sg_lst_len > sg_max_len)
		error("%s: send buflist len %d bigger than max %d", __func__,
		  sr.sg_lst_len, sg_max_len);

	    send_offset += this_bytes;
	    if (send_offset == sq->buflist.len[send_index]) {
		++send_index;
		if (send_index == sq->buflist.num) {
		    done = 1;
		    break;  /* short send */
		}
	    }
	    recv_bytes_needed -= this_bytes;
	}

	/* done with the one we were just working on, is this the last recv? */
	++recv_index;
	if (++recv_index == (int)mh_cts->buflist_num)
	    done = 1;

	/* either filled the recv or exhausted the send */
	if (done) {
	    sr.id = int64_from_ptr(sq);    /* used to match in completion */
	    sr.comp_type = VAPI_SIGNALED;  /* completion drives the unpin */
	}
	ret = VAPI_post_sr(nic_handle, sq->c->qp, &sr);
	if (ret < 0)
	    error_verrno(ret, "%s: VAPI_post_sr", __func__);
    }
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
    ib_method_addr_t *ibmap = remote_map->method_data;
    int i;
    int ret = 0;

    gen_mutex_lock(&interface_mutex);

    /* alloc and build new sendq structure */
    sq = Malloc(sizeof(*sq));
    sq->type = TYPE_SEND;
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
	error("%s: user-provided tot len " FORMAT_BMI_SIZE_T
	  " does not match buffer list tot len " FORMAT_BMI_SIZE_T,
	  __func__, total_size, sq->buflist.tot_len);

    /* unexpected messages must fit inside an eager message */
    if (is_unexpected && sq->buflist.tot_len > EAGER_BUF_PAYLOAD) {
	free(sq);
	ret = -EINVAL;
	goto out;
    }

    sq->bmi_tag = tag;
    sq->c = ibmap->c;
    sq->is_unexpected = is_unexpected;
    qlist_add_tail(&sq->list, &sendq);

    /* generate identifier used by caller to test for message later */
    mop = Malloc(sizeof(*mop));
    id_gen_fast_register(&mop->op_id, mop);
    mop->addr = remote_map;  /* set of function pointers, essentially */
    mop->method_data = sq;
    mop->user_ptr = user_ptr;
    mop->context_id = context_id;
    *id = mop->op_id;
    sq->mop = mop;

    /* and start sending it if possible */
    encourage_send(sq, 0);
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
    debug(2, "%s: len %d tag %d", __func__, (int) size, tag);
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
    ib_method_addr_t *ibmap = remote_map->method_data;

    debug(2, "%s: len %d tag %d", __func__, (int) size, tag);
    gen_mutex_lock(&interface_mutex);
    if (!ibmap->c)
	ib_tcp_client_connect(ibmap, remote_map);
    gen_mutex_unlock(&interface_mutex);
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
    ib_method_addr_t *ibmap = remote_map->method_data;

    debug(2, "%s: listlen %d tag %d", __func__, list_count, tag);
    if (list_count < 1)
	error("%s: list count must be positive", __func__);
    gen_mutex_lock(&interface_mutex);
    if (!ibmap->c)
	ib_tcp_client_connect(ibmap, remote_map);
    gen_mutex_unlock(&interface_mutex);
    /* references here will not be saved after this func returns */
    return generic_post_send(id, remote_map, list_count, buffers, sizes,
      total_size, tag, user_ptr, context_id, 1);
}

/*
 * Used by both recv and recv_list.
 */
static void
generic_post_recv(bmi_op_id_t *id, struct method_addr *remote_map,
  int numbufs, void *const *buffers, const bmi_size_t *sizes,
  bmi_size_t tot_expected_len, bmi_msg_tag_t tag,
  void *user_ptr, bmi_context_id context_id)
{
    ib_recv_t *rq;
    struct method_op *mop;
    ib_method_addr_t *ibmap = remote_map->method_data;
    list_t *l;
    int i;
    
    gen_mutex_lock(&interface_mutex);
    /* XXX: maybe check recvq first, just an optimization... */

    /* check to see if matching recv is in the queue */
    qlist_for_each(l, &recvq) {
	rq = qlist_upcast(l);
	if (rq->state == RQ_EAGER_WAITING_USER_POST && rq->bmi_tag == tag) {
	    debug(2, "%s: matches pre-arrived eager", __func__);
	    goto build_buflist;
	}
	if (rq->state == RQ_RTS_WAITING_USER_POST && rq->bmi_tag == tag) {
	    debug(2, "%s: matches pre-arrived rts, sending cts", __func__);
	    goto build_buflist;
	}
    }
    
    /* alloc and build new recvq structure */
    rq = Malloc(sizeof(*rq));
    rq->type = TYPE_RECV;
    rq->state = RQ_WAITING_INCOMING;
    rq->bmi_tag = tag;
    rq->c = ibmap->c;
    qlist_add_tail(&rq->list, &recvq);

  build_buflist:
    debug(2, "%s: new recv matches nothing on queue", __func__);
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
	error("%s: user-provided tot len " FORMAT_BMI_SIZE_T
	  " does not match buffer list tot len " FORMAT_BMI_SIZE_T,
	  __func__, tot_expected_len, rq->buflist.tot_len);

    /* generate identifier used by caller to test for message later */
    mop = Malloc(sizeof(*mop));
    id_gen_fast_register(&mop->op_id, mop);
    mop->addr = remote_map;  /* set of function pointers, essentially */
    mop->method_data = rq;
    mop->user_ptr = user_ptr;
    mop->context_id = context_id;
    *id = mop->op_id;
    rq->mop = mop;

    /* encourage these directly; XXX: more generally make progress */
    if (rq->state == RQ_EAGER_WAITING_USER_POST) {

	debug(2, "%s: state %s finish eager directly", __func__,
	  rq_state_name(rq->state));
	if (rq->actual_len > tot_expected_len) {
	    error("%s: received " FORMAT_BMI_SIZE_T
	      " matches too-small buffer " FORMAT_BMI_SIZE_T,
	      __func__, rq->actual_len, rq->buflist.tot_len);
	}

	memcpy_to_buflist(&rq->buflist,
	  (char *) rq->bh->buf + sizeof(msg_header_t),
	  rq->actual_len);

	/* re-post */
	post_rr(rq->c, rq->bh);
	/* done with buffer, ack to remote */
	post_sr_ack(rq->c, rq->bh);

	/* now just wait for user to test, never do "immed complet" */
	rq->state = RQ_EAGER_WAITING_USER_TEST;
    }
    if (rq->state == RQ_RTS_WAITING_USER_POST) {
	int ret;

	debug(2, "%s: state %s send cts", __func__, rq_state_name(rq->state));
	ret = send_cts(rq);
	if (ret == 0)
	    rq->state = RQ_RTS_WAITING_DATA;
	else
	    rq->state = RQ_RTS_WAITING_CTS_BUFFER;
    }
    gen_mutex_unlock(&interface_mutex);
}

static int
BMI_ib_post_recv(bmi_op_id_t *id, struct method_addr *remote_map,
  void *buffer, bmi_size_t expected_len, bmi_size_t *actual_len __unused,
  enum bmi_buffer_type buffer_flag __unused, bmi_msg_tag_t tag, void *user_ptr,
  bmi_context_id context_id)
{
    debug(2, "%s: expected len %d tag %d", __func__, (int) expected_len, tag);
    generic_post_recv(id, remote_map, 0, &buffer, &expected_len,
      expected_len, tag, user_ptr, context_id);
    return 0;
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
    generic_post_recv(id, remote_map, list_count, buffers, sizes,
      tot_expected_len, tag, user_ptr, context_id);
    return 0;
}

/*
 * Test one message, send or receive.  Also used to test the send side of
 * messages sent using sendunexpected.
 */
static int
BMI_ib_test(bmi_op_id_t id, int *outcount, bmi_error_code_t *error_code,
  bmi_size_t *actual_size, void **user_ptr, int max_idle_time __unused,
  bmi_context_id context_id __unused)
{
    struct method_op *mop;
    ib_send_t *sq;
    int ret = 0;

    gen_mutex_lock(&interface_mutex);
    /* poke cq */
    (void) check_cq();
    process_cq();

    mop = id_gen_fast_lookup(id);
    sq = mop->method_data;
    if (sq->type == TYPE_SEND) {
	if (sq->state == SQ_WAITING_USER_TEST) {
	    debug(2, "%s: send done, freeing and returning true", __func__);
	    ret = 1;
	    *error_code = 0;
	    *actual_size = sq->buflist.tot_len;
	    *user_ptr = mop->user_ptr;
	    qlist_del(&sq->list);
	    free(mop);
	    free(sq);
	} else if (sq->state == SQ_WAITING_BUFFER) {
	    debug(2, "%s: send state %s, encouraging",
	      __func__, sq_state_name(sq->state));
	    encourage_send(sq, 0);
	} else {
	    debug(9, "%s: send found, not done, state %s", __func__,
	      sq_state_name(sq->state));
	}
    } else {
	/* actually a recv */
	ib_recv_t *rq = mop->method_data;
	assert(rq->type == TYPE_RECV, "%s: type not send or recv", __func__);
	if (rq->state == RQ_EAGER_WAITING_USER_TEST
	 || rq->state == RQ_RTS_WAITING_USER_TEST) {
	    debug(2, "%s: recv done from state %s", __func__,
	      rq_state_name(rq->state));
	    ret = 1;
	    *error_code = 0;
	    *actual_size = rq->actual_len;
	    if (user_ptr)
		*user_ptr = mop->user_ptr;
	    qlist_del(&rq->list);
	    free(mop);
	    free(rq);
	} else if (rq->state == RQ_RTS_WAITING_CTS_BUFFER) {
	    debug(2, "%s: recv state %s, encouraging",
	      __func__, rq_state_name(rq->state));
	    encourage_recv(rq, 0);
	} else {
	    debug(9, "%s: recv found, not done, state %s", __func__,
	      rq_state_name(rq->state));
	}
    }
    *outcount = ret;
    gen_mutex_unlock(&interface_mutex);
    return ret;
}

/*
 * Like testsome, but searches for completions associated with a particular
 * context.
 */
static int
BMI_ib_testcontext(int incount, bmi_op_id_t *outids, int *outcount,
  bmi_error_code_t *errs, bmi_size_t *sizes, void **user_ptrs,
  int max_idle_time __unused, bmi_context_id context_id)
{
    list_t *l, *lnext;

    gen_mutex_lock(&interface_mutex);
    /* poke cq */
    (void) check_cq();
    process_cq();

    /* walk sq, rq, marking completed or encouraging next step */
    *outcount = 0;
    for (l=sendq.next; l != &sendq; l=lnext) {
	ib_send_t *sq = qlist_upcast(l);
	lnext = l->next;

	if (sq->state == SQ_WAITING_USER_TEST) {
	    /* if context id matches and leftover output room */
	    if (sq->mop->context_id == context_id && *outcount < incount) {
		debug(2, "%s: send found completed", __func__);
		outids[*outcount] = sq->mop->op_id;
		errs[*outcount] = 0;
		sizes[*outcount] = sq->buflist.tot_len;
		user_ptrs[*outcount] = sq->mop->user_ptr;
		++*outcount;
		qlist_del(&sq->list);
		free(sq->mop);
		free(sq);
	    }
	/* always encourage other operations */
	} else if (sq->state == SQ_WAITING_BUFFER) {
	    debug(2, "%s: send state %s, encouraging", __func__,
	      sq_state_name(sq->state));
	    encourage_send(sq, 0);
	}
    }

    for (l=recvq.next; l != &recvq; l=lnext) {
	ib_recv_t *rq = qlist_upcast(l);
	lnext = l->next;

	if (rq->state == RQ_EAGER_WAITING_USER_TEST 
	  || rq->state == RQ_RTS_WAITING_USER_TEST) {
	    if (rq->mop->context_id == context_id && *outcount < incount) {
		debug(2, "%s: recv found completed", __func__);
		outids[*outcount] = rq->mop->op_id;
		errs[*outcount] = 0;
		sizes[*outcount] = rq->actual_len;
		user_ptrs[*outcount] = rq->mop->user_ptr;
		++*outcount;
		qlist_del(&rq->list);
		free(rq->mop);
		free(rq);
	    }
	} else if (rq->state == RQ_RTS_WAITING_CTS_BUFFER) {
	    debug(2, "%s: recv state %s, encouraging",
	      __func__, rq_state_name(rq->state));
	    encourage_recv(rq, 0);
	}
    }
    gen_mutex_unlock(&interface_mutex);
    return 0;
}

/*
 * Non-blocking test to look for any incoming unexpected messages.
 * This is also where we check for new connections on the TCP socket.
 */
static int
BMI_ib_testunexpected(int incount __unused, int *outcount,
  struct method_unexpected_info *ui, int max_idle_time __unused)
{
    int ret = 0;
    int new_cqe;
    list_t *l;

    gen_mutex_lock(&interface_mutex);
    /*
     * Check CQ, then look for the first unexpected message.
     */
    new_cqe = check_cq();
    process_cq();
    qlist_for_each(l, &recvq) {
	ib_recv_t *rq = qlist_upcast(l);
	if (rq->state == RQ_EAGER_WAITING_USER_TESTUNEXPECTED) {
	    debug(2, "%s: found waiting testunexpected", __func__);
	    ui->error_code = 0;
	    ui->addr = rq->c->remote_map;  /* hand back permanent method_addr */
	    ui->buffer = Malloc(rq->actual_len);
	    ui->size = rq->actual_len;
	    memcpy(ui->buffer, (char *) rq->bh->buf + sizeof(msg_header_t),
	      ui->size);
	    ui->tag = rq->bmi_tag;
	    /* re-post the buffer in which it was sitting, just unexpecteds */
	    post_rr(rq->c, rq->bh);
	    /* freed our eager buffer, ack it */
	    post_sr_ack(rq->c, rq->bh);
	    ret = 1;
	    qlist_del(&rq->list);
	    free(rq);
	    goto out;
	}
    }

    ib_tcp_server_check_new_connections();

  out:
    *outcount = ret;
    gen_mutex_unlock(&interface_mutex);
    return ret;
}

/*
 * Do not care about memory allocation.  Send/recv functions will pin as
 * necessary.
 */
static void *
BMI_ib_memalloc(bmi_size_t size,
  enum bmi_op_type send_recv __unused)
{
    return malloc((size_t) size);
}

static int
BMI_ib_memfree(void *buf, bmi_size_t size __unused,
  enum bmi_op_type send_recv __unused)
{
    free(buf);
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


/* exported method interface */
struct bmi_method_ops bmi_ib_ops = 
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
    .BMI_meth_testsome = 0,
    .BMI_meth_testcontext = BMI_ib_testcontext,
    .BMI_meth_testunexpected = BMI_ib_testunexpected,
    .BMI_meth_method_addr_lookup = BMI_ib_method_addr_lookup,
    .BMI_meth_post_send_list = BMI_ib_post_send_list,
    .BMI_meth_post_recv_list = BMI_ib_post_recv_list,
    .BMI_meth_post_sendunexpected_list = BMI_ib_post_sendunexpected_list,
    .BMI_meth_open_context = BMI_ib_open_context,
    .BMI_meth_close_context = BMI_ib_close_context,
};

/* vi: set tags+=/home/pw/src/infiniband/mellanox/include/tags: */

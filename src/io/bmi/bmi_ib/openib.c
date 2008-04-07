/*
 * OpenIB-specific calls.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2006 Kyle Schochenmaier <kschoche@scl.ameslab.gov>
 *
 * See COPYING in top-level directory.
 */
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* include definitions */
#include <src/io/bmi/bmi-byteswap.h>  /* bmitoh64 */
#include <src/common/misc/pvfs2-internal.h>  /* llu */
#include <infiniband/verbs.h>

#ifdef HAVE_VALGRIND_H
#include <memcheck.h>
#else
#define VALGRIND_MAKE_MEM_DEFINED(addr,len)
#endif

#include "ib.h"

/*
 * OpenIB-private device-wide state.
 */
struct openib_device_priv {
    struct ibv_context *ctx;  /* context used to reference everything */
    struct ibv_cq *nic_cq;  /* single completion queue for all QPs */
    struct ibv_pd *nic_pd;  /* single protection domain for all memory/QP */
    uint16_t nic_lid;  /* local id (nic) */
    int nic_port; /* port number */
    struct ibv_comp_channel *channel;

    /* max values as reported by NIC */
    int nic_max_sge;
    int nic_max_wr;

    /*
     * Temp array for filling scatter/gather lists to pass to IB functions,
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
struct openib_connection_priv {
    /* ibv local params */
    struct ibv_qp *qp;
    struct ibv_mr *eager_send_mr;
    struct ibv_mr *eager_recv_mr;
    /* in the past, did this per-connection, not per-device
     * unsigned int num_unsignaled_wr;
     */
    /* ib remote params */
    uint16_t remote_lid;
    uint32_t remote_qp_num;
};

/* NOTE:  You have to be sure that ib_uverbs.ko is loaded, otherwise it
 * will segfault and/or not find the ib device.  Find some way to check
 * and see if ib_uverbs.ko is loaded.
 */

/* constants used to initialize infiniband device */
static const int IBV_PORT = 1;
static const unsigned int IBV_NUM_CQ_ENTRIES = 1024;
static const int IBV_MTU = IBV_MTU_1024;  /* dmtu, 1k good for mellanox */

static int exchange_data(int sock, int is_server, void *xin, void *xout,
                         size_t len);
static void init_connection_modify_qp(struct ibv_qp *qp,
                                      uint32_t remote_qp_num, int remote_lid);
static void openib_post_rr(const ib_connection_t *c, struct buf_head *bh);
int openib_ib_initialize(void);
static void openib_ib_finalize(void);

/*
 * Build new conneciton.
 */
static int openib_new_connection(ib_connection_t *c, int sock, int is_server)
{
    struct openib_connection_priv *oc;
    struct openib_device_priv *od = ib_device->priv;
    int i, ret;
    int num_wr;
    size_t len;
    struct ibv_qp_init_attr att;

    /*
     * Values passed through TCP to permit IB connection.  These
     * are transformed to appear in network byte order (big endian)
     * on the network.  The lid is pushed up to 32 bits to avoid struct
     * alignment issues.
     */
    struct {
	uint32_t lid;
	uint32_t qp_num;
    } ch_in, ch_out;

    /* build new connection/context */
    oc = bmi_ib_malloc(sizeof(*oc));
    c->priv = oc;

    /* register memory region, Recv side */
    len = ib_device->eager_buf_num * ib_device->eager_buf_size;

    oc->eager_recv_mr = ibv_reg_mr(od->nic_pd, c->eager_recv_buf_contig, len,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE
	| IBV_ACCESS_REMOTE_READ);
    if (!oc->eager_recv_mr)
	error("%s: register_mr eager recv", __func__);


    /* register memory region, Send side */
    oc->eager_send_mr = ibv_reg_mr(od->nic_pd, c->eager_send_buf_contig, len,
	IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE
	| IBV_ACCESS_REMOTE_READ);
    if (!oc->eager_send_mr)
	error("%s: register_mr eager send", __func__);

    /* create the main queue pair */
    memset(&att, 0, sizeof(att));
    att.send_cq = od->nic_cq;
    att.recv_cq = od->nic_cq;
    num_wr = ib_device->eager_buf_num + 50;  /* plus some rdmaw */
    if (num_wr > od->nic_max_wr)
	num_wr = od->nic_max_wr;
    att.cap.max_recv_wr = num_wr;
    att.cap.max_send_wr = num_wr;
    att.cap.max_recv_sge = 16;
    att.cap.max_send_sge = 16;
    if ((int) att.cap.max_recv_sge > od->nic_max_sge) {
	att.cap.max_recv_sge = od->nic_max_sge;
	/* -1 to work around mellanox issue */
	att.cap.max_send_sge = od->nic_max_sge - 1;
    }
    att.qp_type = IBV_QPT_RC;
    oc->qp = ibv_create_qp(od->nic_pd, &att);
    if (!oc->qp)
	error("%s: create QP", __func__);
    VALGRIND_MAKE_MEM_DEFINED(&att, sizeof(att));
    VALGRIND_MAKE_MEM_DEFINED(&oc->qp->qp_num, sizeof(oc->qp->qp_num));

    /* compare the caps that came back against what we already have */
    if (od->sg_max_len == 0) {
	od->sg_max_len = att.cap.max_send_sge;
	if (att.cap.max_recv_sge < od->sg_max_len)
	    od->sg_max_len = att.cap.max_recv_sge;
	od->sg_tmp_array = bmi_ib_malloc(od->sg_max_len *
					 sizeof(*od->sg_tmp_array));
    } else {
	if (att.cap.max_send_sge < od->sg_max_len)
	    error("%s: new conn has smaller send SG array size %d vs %d",
		  __func__, att.cap.max_send_sge, od->sg_max_len);
	if (att.cap.max_recv_sge < od->sg_max_len)
	    error("%s: new conn has smaller recv SG array size %d vs %d",
		  __func__, att.cap.max_recv_sge, od->sg_max_len);
    }

    if (od->max_unsignaled_sends == 0)
	od->max_unsignaled_sends = att.cap.max_send_wr;
    else
	if (att.cap.max_send_wr < od->max_unsignaled_sends)
	    error("%s: new connection has smaller max_send_wr, %d vs %d",
	          __func__, att.cap.max_send_wr, od->max_unsignaled_sends);

    /* verify we got what we asked for */
    if ((int) att.cap.max_recv_wr < num_wr)
	error("%s: asked for %d recv WRs on QP, got %d", __func__, num_wr,
	      att.cap.max_recv_wr);
    if ((int) att.cap.max_send_wr < num_wr)
	error("%s: asked for %d send WRs on QP, got %d", __func__, num_wr,
	      att.cap.max_send_wr);

    /* exchange data, converting info to network order and back */
    ch_out.lid = htobmi32(od->nic_lid);
    ch_out.qp_num = htobmi32(oc->qp->qp_num);

    ret = exchange_data(sock, is_server, &ch_in, &ch_out, sizeof(ch_in));
    if (ret)
	goto out;

    oc->remote_lid = bmitoh32(ch_in.lid);
    oc->remote_qp_num = bmitoh32(ch_in.qp_num);

    /* bring the two QPs up to RTR */
    init_connection_modify_qp(oc->qp, oc->remote_qp_num, oc->remote_lid);

    /* post initial RRs and RRs for acks */
    for (i=0; i<ib_device->eager_buf_num; i++)
	openib_post_rr(c, &c->eager_recv_buf_head_contig[i]);

    /* final sychronization to ensure both sides have posted RRs */
    ret = exchange_data(sock, is_server, &ret, &ret, sizeof(ret));

  out:
    return ret;
}

/*
 * Exchange information: server reads first, then writes; client opposite.
 */
static int exchange_data(int sock, int is_server, void *xin, void *xout,
                         size_t len)
{
    int i;
    int ret;

    for (i=0; i<2; i++) {
	if (i ^ is_server) {
	    ret = read_full(sock, xin, len);
	    if (ret < 0) {
		warning_errno("%s: read", __func__);
		goto out;
	    }
	    if (ret != (int) len) {
		ret = 1;
		warning("%s: partial read, %d/%d bytes", __func__, ret,
		                                        (int) len);
		goto out;
	    }
	} else {
	    ret = write_full(sock, xout, len);
	    if (ret < 0) {
		warning_errno("%s: write", __func__);
		goto out;
	    }
	}
    }

    ret = 0;

  out:
    return ret;
}

/*
 * Perform the many steps required to bring up both sides of an IB connection.
 * We first transition the QP into an INIT state, make some changes, then
 * transition the QP into Ready-to-Receive & Ready-to-Send states before
 * leaving.
 * NOTE: OpenIB's documentation doesnt properly explain how return values
 * are decided for the ibv_modify_qp(...) function.  If everything is
 * working properly, it should return 0, which is the amount of bytes
 * written out by the write() system call buried in the call.  If the
 * return is !0, an error ocurred, or bytes were actually written, which
 * is not what you want to do here.
 */
static void init_connection_modify_qp(struct ibv_qp *qp, uint32_t remote_qp_num,
                                      int remote_lid)
{
    struct openib_device_priv *od = ib_device->priv;
    int ret;
    enum ibv_qp_attr_mask mask;
    struct ibv_qp_attr attr;

    /* Transition QP to Init */
    mask =
       IBV_QP_STATE
     | IBV_QP_ACCESS_FLAGS
     | IBV_QP_PKEY_INDEX
     | IBV_QP_PORT;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
	    IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    attr.pkey_index = 0;
    attr.port_num = od->nic_port;
    ret = ibv_modify_qp(qp, &attr, mask);
    if (ret)
	error_xerrno(ret, "%s: ibv_modify_qp -> INIT", __func__);

    /* Transition QP to Ready-to-Receive (RTR) */
    mask =
       IBV_QP_STATE
     | IBV_QP_MAX_DEST_RD_ATOMIC
     | IBV_QP_AV
     | IBV_QP_PATH_MTU
     | IBV_QP_RQ_PSN
     | IBV_QP_DEST_QPN
     | IBV_QP_MIN_RNR_TIMER;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.max_dest_rd_atomic = 1;
    attr.ah_attr.dlid = remote_lid;
    attr.ah_attr.port_num = od->nic_port;
    attr.path_mtu = IBV_MTU;
    attr.rq_psn = 0;
    attr.dest_qp_num = remote_qp_num;
    attr.min_rnr_timer = 31;
    ret = ibv_modify_qp(qp, &attr, mask);
    if (ret)
	error_xerrno(ret, "%s: ibv_modify_qp INIT -> RTR", __func__);

    /* transition qp to ready-to-send */
    mask =
       IBV_QP_STATE
     | IBV_QP_SQ_PSN
     | IBV_QP_MAX_QP_RD_ATOMIC
     | IBV_QP_TIMEOUT
     | IBV_QP_RETRY_CNT
     | IBV_QP_RNR_RETRY;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    attr.timeout = 26;  /* 4.096us * 2^26 = 5 min */
    attr.retry_cnt = 20;
    attr.rnr_retry = 20;
    ret = ibv_modify_qp(qp, &attr, mask);
    if (ret)
	error_xerrno(ret, "%s: ibv_modify_qp RTR -> RTS", __func__);

}

/*
 * Close the QP associated with this connection.
 */
static void openib_drain_qp(ib_connection_t *c)
{
    struct openib_connection_priv *oc = c->priv;
    struct ibv_qp *qp = oc->qp;
    int ret;
    struct ibv_qp_attr attr;
    enum ibv_qp_attr_mask mask;

    /* transition to drain */
    mask = IBV_QP_STATE;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_SQD;
    ret = ibv_modify_qp(qp, &attr, mask);
    if (ret < 0)
	error_xerrno(ret, "%s: ibv_modify_qp RTS -> SQD", __func__);
}

/*
 * At an explicit BYE message, or at finalize time, shut down a connection.
 * If descriptors are posted, defer and clean up the connection structures
 * later.
 */
static void openib_close_connection(ib_connection_t *c)
{
    int ret;
    struct openib_connection_priv *oc = c->priv;

    /* destroy the queue pair */
    if (oc->qp) {
	ret = ibv_destroy_qp(oc->qp);
	if (ret < 0)
	    error_xerrno(ret, "%s: ibv_destroy_qp", __func__);
    }

    /* destroy the memory regions */
    if (oc->eager_send_mr) {
	ret = ibv_dereg_mr(oc->eager_send_mr);
	if (ret < 0)
	    error_xerrno(ret, "%s: ibv_deregister_mr eager send", __func__);
    }
    if (oc->eager_recv_mr) {
	ret = ibv_dereg_mr(oc->eager_recv_mr);
	if (ret < 0)
	    error_xerrno(ret, "%s: ibv_deregister_mr eager recv", __func__);
    }

    free(oc);
}

/*
 * Simplify IB interface to post sends.  Not RDMA, just SEND.
 * Called for an eager send, rts send, or cts send.
 */
static void openib_post_sr(const struct buf_head *bh, u_int32_t len)
{
    ib_connection_t *c = bh->c;
    struct openib_connection_priv *oc = c->priv;
    struct openib_device_priv *od = ib_device->priv;
    int ret;
    struct ibv_sge sg = {
        .addr = int64_from_ptr(bh->buf),
        .length = len,
        .lkey = oc->eager_send_mr->lkey,
    };
    struct ibv_send_wr sr = {
        .next = NULL,
        .wr_id = int64_from_ptr(bh),
        .sg_list = &sg,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,  /* XXX: try unsignaled if possible */
    };
    struct ibv_send_wr *bad_wr;

    debug(4, "%s: %s bh %d len %u wr %d/%d", __func__, c->peername, bh->num,
          len, od->num_unsignaled_sends, od->max_unsignaled_sends);

    if (od->num_unsignaled_sends + 10 == od->max_unsignaled_sends)
        od->num_unsignaled_sends = 0;
    else
        ++od->num_unsignaled_sends;

    ret = ibv_post_send(oc->qp, &sr, &bad_wr);
    if (ret < 0)
        error("%s: ibv_post_send (%d)", __func__, ret);
}

/*
 * Post one of the eager recv bufs for this connection.
 */
static void openib_post_rr(const ib_connection_t *c, struct buf_head *bh)
{
    struct openib_connection_priv *oc = c->priv;
    int ret;
    struct ibv_sge sg = {
        .addr = int64_from_ptr(bh->buf),
        .length = ib_device->eager_buf_size,
        .lkey = oc->eager_recv_mr->lkey,
    };
    struct ibv_recv_wr rr = {
        .wr_id = int64_from_ptr(bh),
        .sg_list = &sg,
        .num_sge = 1,
        .next = NULL,
    };
    struct ibv_recv_wr *bad_wr;

    debug(4, "%s: %s bh %d", __func__, c->peername, bh->num);
    ret = ibv_post_recv(oc->qp, &rr, &bad_wr);
    if (ret)
        error("%s: ibv_post_recv", __func__);
}

/*
 * Called only in response to receipt of a CTS on the sender.  RDMA write
 * the big data to the other side.  A bit messy since an RDMA write may
 * not scatter to the receiver, but can gather from the sender, and we may
 * have a non-trivial buflist on both sides.  The mh_cts variable length
 * fields must be decoded as we go.
 */
static void openib_post_sr_rdmaw(struct ib_work *sq, msg_header_cts_t *mh_cts,
                                 void *mh_cts_buf)
{
    ib_connection_t *c = sq->c;
    struct openib_connection_priv *oc = c->priv;
    struct openib_device_priv *od = ib_device->priv;
    struct ibv_send_wr sr;
    int done;


    int send_index = 0, recv_index = 0; /* working entry in buflist */
    int send_offset = 0;        /* byte offset in working send entry */
    u_int64_t *recv_bufp = (u_int64_t *) mh_cts_buf;
    u_int32_t *recv_lenp = (u_int32_t *) (recv_bufp + mh_cts->buflist_num);
    u_int32_t *recv_rkey = (u_int32_t *) (recv_lenp + mh_cts->buflist_num);
    u_int32_t recv_bytes_needed = 0;

    debug(2, "%s: sq %p totlen %d", __func__, sq, (int) sq->buflist.tot_len);

#if MEMCACHE_BOUNCEBUF
    if (reg_send_buflist.num == 0) {
        reg_send_buflist.num = 1;
        reg_send_buflist.buf.recv = &reg_send_buflist_buf;
        reg_send_buflist.len = &reg_send_buflist_len;
        reg_send_buflist.tot_len = reg_send_buflist_len;
	reg_send_buflist_buf = bmi_ib_malloc(reg_send_buflist_len);
        memcache_register(ib_device->memcache, &reg_send_buflist, BMI_SEND);
    }
    if (sq->buflist.tot_len > reg_send_buflist_len)
        error("%s: send prereg buflist too small, need %lld", __func__,
              lld(sq->buflist.tot_len));
    memcpy_from_buflist(&sq->buflist, reg_send_buflist_buf);

    ib_buflist_t save_buflist = sq->buflist;
    sq->buflist = reg_send_buflist;

#else
#if !MEMCACHE_EARLY_REG
    memcache_register(ib_device->memcache, &sq->buflist, BMI_SEND);
#endif
#endif

    /* constant things for every send */
    memset(&sr, 0, sizeof(sr));
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.sg_list = od->sg_tmp_array;
    sr.next = NULL;

    done = 0;
    while (!done) {
        int ret;
        struct ibv_send_wr *bad_wr;

        if (recv_bytes_needed == 0) {
            /* new one, fresh numbers */
            sr.wr.rdma.remote_addr = bmitoh64(recv_bufp[recv_index]);
            recv_bytes_needed = bmitoh32(recv_lenp[recv_index]);
        } else {
            /* continuing into unfinished remote receive index */
            sr.wr.rdma.remote_addr +=
                bmitoh32(recv_lenp[recv_index]) - recv_bytes_needed;
        }

        sr.wr.rdma.rkey = bmitoh32(recv_rkey[recv_index]);
        sr.num_sge = 0;

        debug(4, "%s: chunk to %s remote addr %llx rkey %x",
              __func__, c->peername, llu(sr.wr.rdma.remote_addr),
              sr.wr.rdma.rkey);

        /*
         * Driven by recv elements.  Sizes have already been checked.
         */
        while (recv_bytes_needed > 0 && sr.num_sge < (int) od->sg_max_len) {
            /* consume from send buflist to fill this one receive */
            u_int32_t send_bytes_offered
                = sq->buflist.len[send_index] - send_offset;
            u_int32_t this_bytes = send_bytes_offered;
            if (this_bytes > recv_bytes_needed)
                this_bytes = recv_bytes_needed;

            od->sg_tmp_array[sr.num_sge].addr =
                int64_from_ptr(sq->buflist.buf.send[send_index]) + send_offset;
            od->sg_tmp_array[sr.num_sge].length = this_bytes;
            od->sg_tmp_array[sr.num_sge].lkey =
                sq->buflist.memcache[send_index]->memkeys.lkey;

            debug(4, "%s: chunk %d local addr %llx len %d lkey %x",
                  __func__, sr.num_sge,
                  (unsigned long long) od->sg_tmp_array[sr.num_sge].
                  addr, od->sg_tmp_array[sr.num_sge].length,
                  od->sg_tmp_array[sr.num_sge].lkey);

            ++sr.num_sge;

            send_offset += this_bytes;
            if (send_offset == sq->buflist.len[send_index]) {
                ++send_index;
                send_offset = 0;
                if (send_index == sq->buflist.num) {
                    done = 1;
                    break;      /* short send */
                }
            }
            recv_bytes_needed -= this_bytes;
        }
        /* done with the one we were just working on, is this the last recv? */
        if (recv_bytes_needed == 0) {
            ++recv_index;
            if (recv_index == (int) mh_cts->buflist_num)
                done = 1;
        }

        /* either filled the recv or exhausted the send */
        if (done) {
            sr.wr_id = int64_from_ptr(sq);     /* used to match in completion */
            sr.send_flags = IBV_SEND_SIGNALED; /* completion drives the unpin */
        } else {
            sr.wr_id = 0;
            sr.send_flags = 0;
        }

        ret = ibv_post_send(oc->qp, &sr, &bad_wr);
        if (ret < 0)
            error("%s: ibv_post_send (%d)", __func__, ret);
    }

#if MEMCACHE_BOUNCEBUF
    sq->buflist = save_buflist;
#endif
}

static int openib_check_cq(struct bmi_ib_wc *wc)
{
    struct openib_device_priv *od = ib_device->priv;
    struct ibv_wc desc;
    int ret;

    ret = ibv_poll_cq(od->nic_cq, 1, &desc);
    if (ret < 0)
	error("%s: ibv_poll_cq (%d)", __func__, ret);
    if (ret == 0) {  /* empty */
	return 0;
    }

    /* convert to generic form */
    wc->id = desc.wr_id;
    wc->status = desc.status;
    wc->byte_len = desc.byte_len;
    if (desc.opcode == IBV_WC_SEND)
	wc->opcode = BMI_IB_OP_SEND;
    else if (desc.opcode == (IBV_WC_SEND | IBV_WC_RECV))
	wc->opcode = BMI_IB_OP_RECV;
    else if (desc.opcode == IBV_WC_RDMA_WRITE)
	wc->opcode = BMI_IB_OP_RDMA_WRITE;
    else {
	debug(0, "%s: unknown opcode, id %llx status %d opcode %d",
	      __func__, llu(desc.wr_id), desc.status, desc.opcode);
	debug(0, "%s: vendor_err %d byte_len %d imm_data %d qp_num %d",
	      __func__, desc.vendor_err, desc.byte_len, desc.imm_data,
	      desc.qp_num);
	debug(0, "%s: src_qp %d wc_flags %d pkey_index %d slid %d",
	      __func__, desc.src_qp, desc.wc_flags, desc.pkey_index, desc.slid);
	debug(0, "%s: sl %d dlid_path_bits %d",
	      __func__, desc.sl, desc.dlid_path_bits);
	error("%s: unknown opcode %d", __func__, desc.opcode);
    }
    VALGRIND_MAKE_MEM_DEFINED(wc, sizeof(*wc));

    return 1;
}

static void openib_prepare_cq_block(int *cq_fd, int *async_fd)
{
    struct openib_device_priv *od = ib_device->priv;
    int ret;

    /* ask for the next notfication */
    ret = ibv_req_notify_cq(od->nic_cq, 0);
    if (ret < 0)
	error_xerrno(ret, "%s: ibv_req_notify_cq", __func__);

    /* return the fd that can be fed to poll() */
    *cq_fd = od->channel->fd;
    *async_fd = od->ctx->async_fd;
}

/*
 * As poll says there is something to read, get the event, but
 * ignore the contents as we only have one CQ.  But ack it
 * so that the count is correct and the CQ can be shutdown later.
 */
static void openib_ack_cq_completion_event(void)
{
    struct openib_device_priv *od = ib_device->priv;
    struct ibv_cq *cq;
    void *cq_context;
    int ret;

    ret = ibv_get_cq_event(od->channel, &cq, &cq_context);
    if (ret == 0)
	ibv_ack_cq_events(cq, 1);
}

/*
 * Return string form of work completion status field.
 */
#define CASE(e)  case e: s = #e; break
static const char *openib_wc_status_string(int status)
{
    const char *s = "(UNKNOWN)";

    switch (status) {
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
	CASE(IBV_WC_GENERAL_ERR);
    }
    return s;
}

static const char *openib_port_state_string(enum ibv_port_state state)
{
    const char *s = "(UNKNOWN)";

    switch (state) {
	CASE(IBV_PORT_NOP);
	CASE(IBV_PORT_DOWN);
	CASE(IBV_PORT_INIT);
	CASE(IBV_PORT_ARMED);
	CASE(IBV_PORT_ACTIVE);
	CASE(IBV_PORT_ACTIVE_DEFER);
    }
    return s;
}

static const char *async_event_type_string(enum ibv_event_type event_type)
{
    const char *s = "(UNKNOWN)";

    switch (event_type) {
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
#ifdef HAVE_IBV_EVENT_CLIENT_REREGISTER
	CASE(IBV_EVENT_CLIENT_REREGISTER);
#endif
    }
    return s;
}
#undef CASE

/*
 * Memory registration and deregistration.  Used both by sender and
 * receiver, vary if lkey or rkey = 0.
 *
 * Pain because a s/g list requires lots of little allocations.  Needs
 * wuj's clever discontig allocation stuff.
 *
 * These two must be called holding the interface mutex since they
 * make IB calls and that these may or may not be threaded under the hood.
 * Returns -errno on error.
 */
static int openib_mem_register(memcache_entry_t *c)
{
    struct ibv_mr *mrh;
    struct openib_device_priv *od = ib_device->priv;
    int tries = 0;

retry:
    mrh = ibv_reg_mr(od->nic_pd, c->buf, c->len,
                     IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE
                     | IBV_ACCESS_REMOTE_READ);
    if (!mrh && (errno == ENOMEM && tries < 1)) {
	++tries;

	/*
	 * Try to flush some cached entries, then try again.
	 */
	memcache_cache_flush(ib_device->memcache);
	goto retry;
    }

    /*
     * Die horribly.  Need registered memory.
     */
    if (!mrh) {
	warning("%s: ibv_register_mr", __func__);
	return -errno;
    }

    c->memkeys.mrh = int64_from_ptr(mrh);  /* convert pointer to 64-bit int */
    c->memkeys.lkey = mrh->lkey;
    c->memkeys.rkey = mrh->rkey;
    debug(4, "%s: buf %p len %lld lkey %x rkey %x", __func__,
          c->buf, lld(c->len), c->memkeys.lkey, c->memkeys.rkey);
    return 0;
}

static void openib_mem_deregister(memcache_entry_t *c)
{
    int ret;
    struct ibv_mr *mrh;

    mrh = ptr_from_int64(c->memkeys.mrh);  /* convert 64-bit int to pointer */
    ret = ibv_dereg_mr(mrh);
    if (ret)
	error_xerrno(ret, "%s: ibv_dereg_mr", __func__);
    debug(4, "%s: buf %p len %lld lkey %x rkey %x", __func__,
      c->buf, lld(c->len), c->memkeys.lkey, c->memkeys.rkey);
}

static struct ibv_device *get_nic_handle(void)
{
    struct ibv_device *nic_handle;

#ifdef HAVE_IBV_GET_DEVICES
    /* use old interface */
    struct dlist *dev_list;

    dev_list = ibv_get_devices();
    if (!dev_list)
	return NULL;

    /* just pick first nic */
    dlist_start(dev_list);
    nic_handle = dlist_next(dev_list);
    if (!nic_handle)
	return NULL;
    if (dlist_next(dev_list) != NULL)
	warning("%s: found multiple HCAs, choosing the first", __func__);
#else
    struct ibv_device **hca_list;
    int num_devs;

    hca_list = ibv_get_device_list(&num_devs);
    if (num_devs == 0)
	return NULL;
    if (num_devs > 1)
	warning("%s: found %d HCAs, choosing the first", __func__, num_devs);
    nic_handle = hca_list[0];
    ibv_free_device_list(hca_list);
#endif

    return nic_handle;
}

static int openib_check_async_events(void)
{
    struct openib_device_priv *od = ib_device->priv;
    int ret;
    struct ibv_async_event ev;

    ret = ibv_get_async_event(od->ctx, &ev);
    if (ret < 0) {
	if (errno == EAGAIN)
	    return 0;
	error_errno("%s: ibv_get_async_event", __func__);
    }
    warning("%s: %s", __func__, async_event_type_string(ev.event_type));
    ibv_ack_async_event(&ev);
    return 1;
}


/*
 * Startup, once per application.
 */
int openib_ib_initialize(void)
{
    int flags, ret = 0;
    struct ibv_device *nic_handle;
    struct ibv_context *ctx;
    int cqe_num; /* local variables, mainly for debug */
    struct openib_device_priv *od;
    struct ibv_port_attr hca_port;
    struct ibv_device_attr hca_cap;

    debug(1, "%s: init", __func__);

    nic_handle = get_nic_handle();
    if (!nic_handle) {
	warning("%s: no NIC found", __func__);
	return -ENOSYS;
    }

    /* open the device */
    ctx = ibv_open_device(nic_handle);
    if (!ctx) {
	warning("%s: ibv_open_device", __func__);
	return -ENOSYS;
    }
    VALGRIND_MAKE_MEM_DEFINED(ctx, sizeof(*ctx));

    od = bmi_ib_malloc(sizeof(*od));
    ib_device->priv = od;

    /* set the function pointers for openib */
    ib_device->func.new_connection = openib_new_connection;
    ib_device->func.close_connection = openib_close_connection;
    ib_device->func.drain_qp = openib_drain_qp;
    ib_device->func.ib_initialize = openib_ib_initialize;
    ib_device->func.ib_finalize = openib_ib_finalize;
    ib_device->func.post_sr = openib_post_sr;
    ib_device->func.post_rr = openib_post_rr;
    ib_device->func.post_sr_rdmaw = openib_post_sr_rdmaw;
    ib_device->func.check_cq = openib_check_cq;
    ib_device->func.prepare_cq_block = openib_prepare_cq_block;
    ib_device->func.ack_cq_completion_event = openib_ack_cq_completion_event;
    ib_device->func.wc_status_string = openib_wc_status_string;
    ib_device->func.mem_register = openib_mem_register;
    ib_device->func.mem_deregister = openib_mem_deregister;
    ib_device->func.check_async_events = openib_check_async_events;

    od->ctx = ctx;
    od->nic_port = IBV_PORT;  /* maybe let this be configurable */

    /* get the lid and verify port state */
    ret = ibv_query_port(od->ctx, od->nic_port, &hca_port);
    if (ret)
	error_xerrno(ret, "%s: ibv_query_port", __func__);
    VALGRIND_MAKE_MEM_DEFINED(&hca_port, sizeof(hca_port));

    od->nic_lid = hca_port.lid;

    if (hca_port.state != IBV_PORT_ACTIVE)
	error("%s: port state is %s but should be ACTIVE; check subnet manager",
	      __func__, openib_port_state_string(hca_port.state));

    /* Query the device for the max_ requests and such */
    ret = ibv_query_device(od->ctx, &hca_cap);
    if (ret)
	error_xerrno(ret, "%s: ibv_query_device", __func__);
    VALGRIND_MAKE_MEM_DEFINED(&hca_cap, sizeof(hca_cap));

    debug(1, "%s: max %d completion queue entries", __func__, hca_cap.max_cq);
    cqe_num = IBV_NUM_CQ_ENTRIES;
    od->nic_max_sge = hca_cap.max_sge;
    od->nic_max_wr = hca_cap.max_qp_wr;

    if (hca_cap.max_cq < cqe_num) {
	cqe_num = hca_cap.max_cq;
	warning("%s: hardly enough completion queue entries %d, hoping for %d",
	        __func__, hca_cap.max_cq, cqe_num);
    }

    /* Allocate a Protection Domain (global) */
    od->nic_pd = ibv_alloc_pd(od->ctx);
    if (!od->nic_pd)
	error("%s: ibv_alloc_pd", __func__);

    /* create completion channel for blocking on CQ events */
    od->channel = ibv_create_comp_channel(od->ctx);
    if (!od->channel)
	error("%s: ibv_create_comp_channel failed", __func__);

    /* build a CQ (global), connected to this channel */
    od->nic_cq = ibv_create_cq(od->ctx, cqe_num, NULL, od->channel, 0);
    if (!od->nic_cq)
	error("%s: ibv_create_cq failed", __func__);

    /* use non-blocking IO on the async fd and completion fd */
    flags = fcntl(ctx->async_fd, F_GETFL);
    if (flags < 0)
	error_errno("%s: get async fd flags", __func__);
    if (fcntl(ctx->async_fd, F_SETFL, flags | O_NONBLOCK) < 0)
	error_errno("%s: set async fd nonblocking", __func__);

    flags = fcntl(od->channel->fd, F_GETFL);
    if (flags < 0)
	error_errno("%s: get completion fd flags", __func__);
    if (fcntl(od->channel->fd, F_SETFL, flags | O_NONBLOCK) < 0)
	error_errno("%s: set completion fd nonblocking", __func__);

    /* will be set on first connection */
    od->sg_tmp_array = 0;
    od->sg_max_len = 0;
    od->num_unsignaled_sends = 0;
    od->max_unsignaled_sends = 0;

    return 0;
}

/*
 * Shutdown.
 */
static void openib_ib_finalize(void)
{
    struct openib_device_priv *od = ib_device->priv;
    int ret;

    if (od->sg_tmp_array)
	free(od->sg_tmp_array);
    ret = ibv_destroy_cq(od->nic_cq);
    if (ret)
	error_xerrno(ret, "%s: ibv_destroy_cq", __func__);
    ret = ibv_destroy_comp_channel(od->channel);
    if (ret)
	error_xerrno(ret, "%s: ibv_destroy_comp_channel", __func__);
    ret = ibv_dealloc_pd(od->nic_pd);
    if (ret)
	error_xerrno(ret, "%s: ibv_dealloc_pd", __func__);
    ret = ibv_close_device(od->ctx);
    if (ret)
	error_xerrno(ret, "%s: ibv_close_device", __func__);

    free(od);
    ib_device->priv = NULL;
}


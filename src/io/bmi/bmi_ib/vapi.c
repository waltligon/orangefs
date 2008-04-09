/*
 * VAPI-specific functions.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>  /* htonl */
#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* include definitions */
#include <src/io/bmi/bmi-method-support.h>   /* struct bmi_method_addr */
#include <src/common/misc/pvfs2-internal.h>
#include <src/io/bmi/bmi-byteswap.h>  /* bmitoh64 */

#include "pvfs2-config.h" /* HAVE_IB_WRAP_COMMON_H configure symbol */

#include <dlfcn.h>        /* look in mosal for syms */

/* otherwise undefined things in mtl_log.h */
#define MAX_TRACE 0
#define MAX_DEBUG 0
#define MAX_ERROR 0

#include <vapi.h>
#include <vapi_common.h>
#ifdef HAVE_IB_WRAP_COMMON_H
#include <wrap_common.h>  /* reinit_mosal externs */
#endif

#include "ib.h"

/*
 * VAPI-private device-wide state.
 */
struct vapi_device_priv {
    VAPI_hca_hndl_t nic_handle;  /* NIC reference */
    VAPI_cq_hndl_t nic_cq;  /* single completion queue for all QPs */
    VAPI_pd_hndl_t nic_pd;  /* single protection domain for all memory/QP */
    IB_lid_t nic_lid;  /* my lid */

    /*
     * Temp array for filling scatter/gather lists to pass to IB functions,
     * allocated once at start to max size defined as reported by the qp.
     */
    VAPI_sg_lst_entry_t *sg_tmp_array;
    unsigned int sg_max_len;

    /*
     * Maximum number of outstanding work requests in the NIC, same for both
     * SQ and RQ.  Used to decide when to use a SIGNALED completion on a send
     * to avoid WQE buildup.
     */
    unsigned int max_outstanding_wr;

    /* async events */
    EVAPI_async_handler_hndl_t nic_async_event_handler;
    int async_event_pipe[2];

    /* completion channel events */
    EVAPI_compl_handler_hndl_t nic_cq_event_handler;
    int cq_event_pipe[2];
};

/*
 * Per-connection state.
 */
struct vapi_connection_priv {
    /* ib local params */
    VAPI_qp_hndl_t qp;
    VAPI_qp_num_t qp_num;
    VAPI_mr_hndl_t eager_send_mr;
    VAPI_mr_hndl_t eager_recv_mr;
    VAPI_lkey_t eager_send_lkey;  /* for post_sr */
    VAPI_lkey_t eager_recv_lkey;  /* for post_rr */
    unsigned int num_unsignaled_wr;  /* keep track of outstanding WRs */
    /* ib remote params */
    IB_lid_t remote_lid;
    VAPI_qp_num_t remote_qp_num;
};

/* constants used to initialize infiniband device */
static const int VAPI_PORT = 1;
static const unsigned int VAPI_NUM_CQ_ENTRIES = 1024;
static const int VAPI_MTU = MTU1024;  /* default mtu, 1k best on mellanox */

static int exchange_data(int sock, int is_server, void *xin, void *xout,
                         size_t len);
static void verify_prop_caps(VAPI_qp_cap_t *cap);
static void init_connection_modify_qp(VAPI_qp_hndl_t qp,
  VAPI_qp_num_t remote_qp_num, int remote_lid);
static void vapi_post_rr(const ib_connection_t *c, struct buf_head *bh);
static void __attribute__((noreturn,format(printf,2,3)))
  error_verrno(int ecode, const char *fmt, ...);
int vapi_ib_initialize(void);
static void vapi_ib_finalize(void);

/*
 * Build new conneciton and do the QP bringup dance.
 */
static int vapi_new_connection(ib_connection_t *c, int sock, int is_server)
{
    struct vapi_connection_priv *vc;
    struct vapi_device_priv *vd = ib_device->priv;
    int i, ret;
    VAPI_mr_t mr, mr_out;
    VAPI_qp_init_attr_t qp_init_attr;
    VAPI_qp_prop_t prop;
    /* for connection handshake with peer */
    struct {
	IB_lid_t lid;
	VAPI_qp_num_t qp_num;
    } ch_in, ch_out;

    vc = bmi_ib_malloc(sizeof(*vc));
    c->priv = vc;

    /* register memory region, recv */
    mr.type = VAPI_MR;
    mr.start = int64_from_ptr(c->eager_recv_buf_contig);
    mr.size = ib_device->eager_buf_num * ib_device->eager_buf_size;
    mr.pd_hndl = vd->nic_pd;
    mr.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
    ret = VAPI_register_mr(vd->nic_handle, &mr, &vc->eager_recv_mr, &mr_out);
    if (ret < 0)
	error_verrno(ret, "%s: register_mr eager recv", __func__);
    vc->eager_recv_lkey = mr_out.l_key;

    /* register memory region, send */
    mr.type = VAPI_MR;
    mr.start = int64_from_ptr(c->eager_send_buf_contig);
    mr.size = ib_device->eager_buf_num * ib_device->eager_buf_size;
    mr.pd_hndl = vd->nic_pd;
    mr.acl = VAPI_EN_LOCAL_WRITE;
    ret = VAPI_register_mr(vd->nic_handle, &mr, &vc->eager_send_mr, &mr_out);
    if (ret < 0)
	error_verrno(ret, "%s: register_mr bounce", __func__);
    vc->eager_send_lkey = mr_out.l_key;

    /* common qp properites */
    qp_init_attr.cap.max_oust_wr_sq = 5000;  /* outstanding WQEs */
    qp_init_attr.cap.max_oust_wr_rq = 5000;
    qp_init_attr.cap.max_sg_size_sq = 20;  /* scatter/gather entries */
    qp_init_attr.cap.max_sg_size_rq = 20;
    qp_init_attr.pd_hndl            = vd->nic_pd;
    qp_init_attr.rdd_hndl           = 0;
    /* wire both send and recv to the same CQ */
    qp_init_attr.sq_cq_hndl         = vd->nic_cq;
    qp_init_attr.rq_cq_hndl         = vd->nic_cq;
    /* only generate completion queue entries if requested */
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    /* build main qp */
    ret = VAPI_create_qp(vd->nic_handle, &qp_init_attr, &vc->qp, &prop);
    if (ret < 0)
	error_verrno(ret, "%s: create QP", __func__);
    vc->qp_num = prop.qp_num;
    verify_prop_caps(&prop.cap);

    /* initialize for post_sr */
    vc->num_unsignaled_wr = 0;

    /* share connection information across TCP */
    /* sanity check sizes of things (actually only 24 bits in qp_num) */
    bmi_ib_assert(sizeof(ch_in.lid) == sizeof(uint16_t),
		  "%s: connection_handshake.lid size %d expecting %d",
		  __func__, (int) sizeof(ch_in.lid), (int) sizeof(u_int16_t));
    bmi_ib_assert(sizeof(ch_in.qp_num) == sizeof(uint32_t),
		  "%s: connection_handshake.qp_num size %d expecting %d",
		  __func__, (int) sizeof(ch_in.qp_num), (int) sizeof(uint32_t));

    /* convert all to network order and back */
    ch_out.lid = htobmi16(vd->nic_lid);
    ch_out.qp_num = htobmi32(vc->qp_num);

    ret = exchange_data(sock, is_server, &ch_in, &ch_out, sizeof(ch_in));
    if (ret)
	goto out;

    vc->remote_lid = bmitoh16(ch_in.lid);
    vc->remote_qp_num = bmitoh32(ch_in.qp_num);

    /* bring the two QPs up to RTR */
    init_connection_modify_qp(vc->qp, vc->remote_qp_num, vc->remote_lid);

    /* post initial RRs */
    for (i=0; i<ib_device->eager_buf_num; i++)
	vapi_post_rr(c, &c->eager_recv_buf_head_contig[i]);

    /* final sychronize to ensure both sides have posted RRs */
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
 * If not set, set them.  Otherwise verify that none of our assumed global
 * limits are different for this new connection.
 */
static void verify_prop_caps(VAPI_qp_cap_t *cap)
{
    struct vapi_device_priv *vd = ib_device->priv;

    if (vd->sg_max_len == 0) {
	vd->sg_max_len = cap->max_sg_size_sq;
	if (cap->max_sg_size_rq < vd->sg_max_len)
	    vd->sg_max_len = cap->max_sg_size_rq;
	vd->sg_tmp_array = bmi_ib_malloc(vd->sg_max_len *
					 sizeof(*vd->sg_tmp_array));
    } else {
	if (cap->max_sg_size_sq < vd->sg_max_len)
	    error(
	      "%s: new connection has smaller send scatter/gather array size,"
	      " %d vs %d", __func__, cap->max_sg_size_sq, vd->sg_max_len);
	if (cap->max_sg_size_rq < vd->sg_max_len)
	    error(
	      "%s: new connection has smaller recv scatter/gather array size,"
	      " %d vs %d", __func__, cap->max_sg_size_rq, vd->sg_max_len);
    }

    if (vd->max_outstanding_wr == 0) {
	vd->max_outstanding_wr = cap->max_oust_wr_sq;
    } else {
	if (cap->max_oust_wr_sq < vd->max_outstanding_wr)
	    error(
	      "%s: new connection has smaller max_oust_wr_sq size, %d vs %d",
	      __func__, cap->max_oust_wr_sq, vd->max_outstanding_wr);
    }
}

/*
 * Perform the many steps required to bring up both sides of an IB connection.
 */
static void init_connection_modify_qp(VAPI_qp_hndl_t qp,
                                      VAPI_qp_num_t remote_qp_num,
				      int remote_lid)
{
    struct vapi_device_priv *vd = ib_device->priv;
    int ret;
    VAPI_qp_attr_t attr;
    VAPI_qp_attr_mask_t mask;
    VAPI_qp_cap_t cap;

    /* see HCA/vip/qpm/qp_xition.h for important settings */
    /* transition qp to init */
    QP_ATTR_MASK_CLR_ALL(mask);
    QP_ATTR_MASK_SET(mask,
       QP_ATTR_QP_STATE
     | QP_ATTR_REMOTE_ATOMIC_FLAGS
     | QP_ATTR_PKEY_IX
     | QP_ATTR_PORT);
    attr.qp_state = VAPI_INIT;
    attr.remote_atomic_flags = VAPI_EN_REM_WRITE;
    attr.pkey_ix = 0;
    attr.port = VAPI_PORT;
    ret = VAPI_modify_qp(vd->nic_handle, qp, &attr, &mask, &cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_modify_qp RST -> INIT", __func__);

    /* transition qp to ready-to-receive */
    QP_ATTR_MASK_CLR_ALL(mask);
    QP_ATTR_MASK_SET(mask,
       QP_ATTR_QP_STATE
     | QP_ATTR_QP_OUS_RD_ATOM
     | QP_ATTR_AV
     | QP_ATTR_PATH_MTU
     | QP_ATTR_RQ_PSN
     | QP_ATTR_DEST_QP_NUM
     | QP_ATTR_MIN_RNR_TIMER);
    attr.qp_state = VAPI_RTR;
    attr.qp_ous_rd_atom = 0;
    memset(&attr.av, 0, sizeof(attr.av));
    attr.av.dlid = remote_lid;
    attr.path_mtu = VAPI_MTU;
    attr.rq_psn = 0;
    attr.dest_qp_num = remote_qp_num;
    attr.min_rnr_timer = IB_RNR_NAK_TIMER_491_52;
    ret = VAPI_modify_qp(vd->nic_handle, qp, &attr, &mask, &cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_modify_qp INIT -> RTR", __func__);

    /* transition qp to ready-to-send */
    QP_ATTR_MASK_CLR_ALL(mask);
    QP_ATTR_MASK_SET(mask,
       QP_ATTR_QP_STATE
     | QP_ATTR_SQ_PSN
     | QP_ATTR_OUS_DST_RD_ATOM
     | QP_ATTR_TIMEOUT
     | QP_ATTR_RETRY_COUNT
     | QP_ATTR_RNR_RETRY
     );
    attr.qp_state = VAPI_RTS;
    attr.sq_psn = 0;
    attr.ous_dst_rd_atom = 0;
    attr.timeout = 26;  /* 4.096us * 2^26 = 5 min */
    attr.retry_count = 20;
    attr.rnr_retry = 20;
    ret = VAPI_modify_qp(vd->nic_handle, qp, &attr, &mask, &cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_modify_qp RTR -> RTS", __func__);
}

/*
 * Close the QP associated with this connection.  Used to wait for drain to
 * finish, but many seconds pass before the adapter tells us about it via an
 * asynch event.  Perhaps there is a way to do it via polling.
 */
static void vapi_drain_qp(ib_connection_t *c)
{
    struct vapi_connection_priv *vc = c->priv;
    struct vapi_device_priv *vd = ib_device->priv;
    VAPI_qp_hndl_t qp = vc->qp;
    int ret;
    /* int trips; */
    VAPI_qp_attr_t attr;
    VAPI_qp_attr_mask_t mask;
    VAPI_qp_cap_t cap;

    /* transition to drain */
    QP_ATTR_MASK_CLR_ALL(mask);
    QP_ATTR_MASK_SET(mask,
       QP_ATTR_QP_STATE);
     /* | QP_ATTR_EN_SQD_ASYN_NOTIF); */
    attr.qp_state = VAPI_SQD;
    /* attr.en_sqd_asyn_notif = 1; */
    ret = VAPI_modify_qp(vd->nic_handle, qp, &attr, &mask, &cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_modify_qp RTS -> SQD", __func__);
}

/*
 * At an explicit BYE message, or at finalize time, shut down a connection.
 * If descriptors are posted, defer and clean up the connection structures
 * later.
 */
static void vapi_close_connection(ib_connection_t *c)
{
    int ret;
    struct vapi_connection_priv *vc = c->priv;
    struct vapi_device_priv *vd = ib_device->priv;

    ret = VAPI_destroy_qp(vd->nic_handle, vc->qp);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_destroy_qp", __func__);
    ret = VAPI_deregister_mr(vd->nic_handle, vc->eager_send_mr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_deregister_mr eager send", __func__);
    ret = VAPI_deregister_mr(vd->nic_handle, vc->eager_recv_mr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_deregister_mr eager recv", __func__);

    free(vc);
}

/*
 * VAPI interface to post sends.  Not RDMA, just SEND.
 * Called for an eager send, rts send, or cts send.
 */
static void vapi_post_sr(const struct buf_head *bh, u_int32_t len)
{
    VAPI_sg_lst_entry_t sg;
    VAPI_sr_desc_t sr;
    int ret;
    ib_connection_t *c = bh->c;
    struct vapi_connection_priv *vc = c->priv;
    struct vapi_device_priv *vd = ib_device->priv;

    debug(2, "%s: %s bh %d len %u wr %d/%d", __func__, c->peername, bh->num,
      len, vc->num_unsignaled_wr, vd->max_outstanding_wr);
    sg.addr = int64_from_ptr(bh->buf);
    sg.len = len;
    sg.lkey = vc->eager_send_lkey;

    memset(&sr, 0, sizeof(sr));
    sr.opcode = VAPI_SEND;
    sr.id = int64_from_ptr(bh);
    sr.comp_type = VAPI_SIGNALED;
/* no unsignaled anymore, see openib.c
    if (++vc->num_unsignaled_wr + 100 == vd->max_outstanding_wr) {
	vc->num_unsignaled_wr = 0;
    } else
	sr.comp_type = VAPI_UNSIGNALED;
 */
    sr.sg_lst_p = &sg;
    sr.sg_lst_len = 1;
    ret = VAPI_post_sr(vd->nic_handle, vc->qp, &sr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_post_sr", __func__);
}

/*
 * Post one of the eager recv bufs for this connection.
 */
static void vapi_post_rr(const ib_connection_t *c, struct buf_head *bh)
{
    VAPI_sg_lst_entry_t sg;
    VAPI_rr_desc_t rr;
    int ret;
    struct vapi_connection_priv *vc = c->priv;
    struct vapi_device_priv *vd = ib_device->priv;

    debug(2, "%s: %s bh %d", __func__, c->peername, bh->num);
    sg.addr = int64_from_ptr(bh->buf);
    sg.len = ib_device->eager_buf_size;
    sg.lkey = vc->eager_recv_lkey;

    memset(&rr, 0, sizeof(rr));
    rr.opcode = VAPI_RECEIVE;
    rr.id = int64_from_ptr(bh);
    rr.sg_lst_p = &sg;
    rr.sg_lst_len = 1;
    ret = VAPI_post_rr(vd->nic_handle, vc->qp, &rr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_post_rr", __func__);
}

/*
 * Called only in response to receipt of a CTS on the sender.  RDMA write
 * the big data to the other side.  A bit messy since an RDMA write may
 * not scatter to the receiver, but can gather from the sender, and we may
 * have a non-trivial buflist on both sides.  The mh_cts variable length
 * fields must be decoded as we go.
 */
static void vapi_post_sr_rdmaw(struct ib_work *sq, msg_header_cts_t *mh_cts,
                               void *mh_cts_buf)
{
    VAPI_sr_desc_t sr;
    int done;
    ib_connection_t *c = sq->c;
    struct vapi_connection_priv *vc = c->priv;
    struct vapi_device_priv *vd = ib_device->priv;

    int send_index = 0, recv_index = 0;    /* working entry in buflist */
    int send_offset = 0;  /* byte offset in working send entry */
    u_int64_t *recv_bufp = (u_int64_t *) mh_cts_buf;
    u_int32_t *recv_lenp = (u_int32_t *)(recv_bufp + mh_cts->buflist_num);
    u_int32_t *recv_rkey = (u_int32_t *)(recv_lenp + mh_cts->buflist_num);
    u_int32_t recv_bytes_needed = 0;

    debug(2, "%s: sq %p totlen %d", __func__, sq, (int) sq->buflist.tot_len);

#if MEMCACHE_BOUNCEBUF
    if (reg_send_buflist.num == 0) {
	reg_send_buflist.num = 1;
	reg_send_buflist.buf.recv = &reg_send_buflist_buf;
	reg_send_buflist.len = &reg_send_buflist_len;
	reg_send_buflist.tot_len = reg_send_buflist_len;
	reg_send_buflist_buf = bmi_ib_malloc(reg_send_buflist_len);
	memcache_register(ib_device->memcache, &reg_send_buflist);
    }
    if (sq->buflist.tot_len > reg_send_buflist_len)
	error("%s: send prereg buflist too small, need %lld", __func__,
	  lld(sq->buflist.tot_len));
    memcpy_from_buflist(&sq->buflist, reg_send_buflist_buf);

    ib_buflist_t save_buflist = sq->buflist;
    sq->buflist = reg_send_buflist;

#else
#if !MEMCACHE_EARLY_REG
    memcache_register(ib_device->memcache, &sq->buflist);
#endif
#endif

    /* constant things for every send */
    memset(&sr, 0, sizeof(sr));
    sr.opcode = VAPI_RDMA_WRITE;
    sr.comp_type = VAPI_UNSIGNALED;
    sr.sg_lst_p = vd->sg_tmp_array;

    done = 0;
    while (!done) {
	int ret;

	if (recv_bytes_needed == 0) {
	    /* new one, fresh numbers */
	    sr.remote_addr = bmitoh64(recv_bufp[recv_index]);
	    recv_bytes_needed = bmitoh32(recv_lenp[recv_index]);
	} else {
	    /* continuing into unfinished remote receive index */
	    sr.remote_addr +=
		bmitoh32(recv_lenp[recv_index]) - recv_bytes_needed;
	}

	sr.r_key = bmitoh32(recv_rkey[recv_index]);
	sr.sg_lst_len = 0;

	debug(4, "%s: chunk to %s remote addr %llx rkey %x",
	  __func__, c->peername, llu(sr.remote_addr), sr.r_key);

	/*
	 * Driven by recv elements.  Sizes have already been checked.
	 */
	while (recv_bytes_needed > 0 && sr.sg_lst_len < vd->sg_max_len) {
	    /* consume from send buflist to fill this one receive */
	    u_int32_t send_bytes_offered
	      = sq->buflist.len[send_index] - send_offset;
	    u_int32_t this_bytes = send_bytes_offered;
	    if (this_bytes > recv_bytes_needed)
		this_bytes = recv_bytes_needed;

	    vd->sg_tmp_array[sr.sg_lst_len].addr =
	      int64_from_ptr(sq->buflist.buf.send[send_index])
	      + send_offset;
	    vd->sg_tmp_array[sr.sg_lst_len].len = this_bytes;
	    vd->sg_tmp_array[sr.sg_lst_len].lkey =
	      sq->buflist.memcache[send_index]->memkeys.lkey;

	    debug(4, "%s: chunk %d local addr %llx len %d lkey %x",
	      __func__, sr.sg_lst_len,
	      llu(vd->sg_tmp_array[sr.sg_lst_len].addr),
	      vd->sg_tmp_array[sr.sg_lst_len].len,
	      vd->sg_tmp_array[sr.sg_lst_len].lkey);

	    ++sr.sg_lst_len;
	    send_offset += this_bytes;
	    if (send_offset == sq->buflist.len[send_index]) {
		++send_index;
		send_offset = 0;
		if (send_index == sq->buflist.num) {
		    done = 1;
		    break;  /* short send */
		}
	    }
	    recv_bytes_needed -= this_bytes;
	}

	/* done with the one we were just working on, is this the last recv? */
	if (recv_bytes_needed == 0) {
	    ++recv_index;
	    if (recv_index == (int)mh_cts->buflist_num)
		done = 1;
	}

	/* either filled the recv or exhausted the send */
	if (done) {
	    sr.id = int64_from_ptr(sq);    /* used to match in completion */
	    sr.comp_type = VAPI_SIGNALED;  /* completion drives the unpin */
	} else {
	    sr.id = 0;
	    sr.comp_type = VAPI_UNSIGNALED;
	}
	ret = VAPI_post_sr(vd->nic_handle, vc->qp, &sr);
	if (ret < 0)
	    error_verrno(ret, "%s: VAPI_post_sr", __func__);
    }
#if MEMCACHE_BOUNCEBUF
    sq->buflist = save_buflist;
#endif
}

/*
 * Get one entry from completion queue, return 1 if found something, 0
 * if CQ empty.  Die if some error.
 */
static int vapi_check_cq(struct bmi_ib_wc *wc)
{
    int ret;
    VAPI_wc_desc_t desc;
    struct vapi_device_priv *vd = ib_device->priv;
    
    ret = VAPI_poll_cq(vd->nic_handle, vd->nic_cq, &desc);
    if (ret < 0) {
	if (ret == VAPI_CQ_EMPTY)
	    return 0;
	error_verrno(ret, "%s: VAPI_poll_cq", __func__);
    }

    /* convert to generic form */
    wc->id = desc.id;
    wc->status = desc.status;
    wc->byte_len = desc.byte_len;
    if (desc.opcode == VAPI_CQE_SQ_SEND_DATA)
	wc->opcode = BMI_IB_OP_SEND;
    else if (desc.opcode == VAPI_CQE_RQ_SEND_DATA)
	wc->opcode = BMI_IB_OP_RECV;
    else if (desc.opcode == VAPI_CQE_SQ_RDMA_WRITE)
	wc->opcode = BMI_IB_OP_RDMA_WRITE;
    else
	error("%s: unknown opcode %d", __func__, desc.opcode);
    return 1;
}

static void vapi_prepare_cq_block(int *cq_fd, int *async_fd)
{
    struct vapi_device_priv *vd = ib_device->priv;
    int ret;

    /* ask for the next notfication */
    ret = VAPI_req_comp_notif(vd->nic_handle, vd->nic_cq, VAPI_NEXT_COMP);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_req_comp_notif", __func__);

    /* return the fd that can be fed to poll() */
    *cq_fd = vd->cq_event_pipe[0];
    *async_fd = vd->async_event_pipe[0];
}

/*
 * Read an event that tells us there is some action on the CQ.  In
 * reality, just read the int from the pipe that connects us to the
 * event handler thread.
 */
static void vapi_ack_cq_completion_event(void)
{
    struct vapi_device_priv *vd = ib_device->priv;
    int i, ret;

    ret = read(vd->cq_event_pipe[0], &i, sizeof(i));
    if (ret != sizeof(i))
	error_errno("%s: read cq event pipe", __func__);
}

/*
 * Return string form of work completion status field.
 */
static const char *vapi_wc_status_string(int status)
{
    return VAPI_wc_status_sym(status);
}

#define CASE(e)  case e: s = #e; break
static const char *vapi_port_state_string(IB_port_state_t state)
{
    const char *s = "(UNKNOWN)";

    switch (state) {
	CASE(PORT_NOP);
	CASE(PORT_DOWN);
	CASE(PORT_INITIALIZE);
	CASE(PORT_ARMED);
	CASE(PORT_ACTIVE);
    }
    return s;
}
#undef CASE


/*
 * Memory registration and deregistration.  Used both by sender and
 * receiver, vary if lkey or rkey = 0.
 */
static int vapi_mem_register(memcache_entry_t *c)
{
    struct vapi_device_priv *vd = ib_device->priv;
    VAPI_mrw_t mrw, mrw_out;
    VAPI_mr_hndl_t mrh;
    int ret;

    /* always turn on local write and write even if just BMI_SEND */
    mrw.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
    mrw.type = VAPI_MR;
    mrw.pd_hndl = vd->nic_pd;
    mrw.start = int64_from_ptr(c->buf);
    mrw.size = c->len;
    ret = VAPI_register_mr(vd->nic_handle, &mrw, &mrh, &mrw_out);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_register_mr", __func__);
    c->memkeys.mrh = mrh;  /* store in 64-bit int */
    c->memkeys.lkey = mrw_out.l_key;
    c->memkeys.rkey = mrw_out.r_key;
    debug(4, "%s: buf %p len %lld", __func__, c->buf, lld(c->len));
    return 0;
}

static void vapi_mem_deregister(memcache_entry_t *c)
{
    struct vapi_device_priv *vd = ib_device->priv;
    VAPI_mr_hndl_t mrh;
    int ret;
    
    mrh = c->memkeys.mrh;  /* retrieve 32-bit from 64-bit int */
    ret = VAPI_deregister_mr(vd->nic_handle, mrh);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_deregister_mr", __func__);
    debug(4, "%s: buf %p len %lld lkey %x rkey %x", __func__,
      c->buf, lld(c->len), c->memkeys.lkey, c->memkeys.rkey);
}

/*
 * Format vapi-specific error code.
 */
static void __attribute__((noreturn,format(printf,2,3)))
error_verrno(int ecode, const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Error: %s: %s\n", s, VAPI_strerror(ecode));  /* adds a dot */
    exit(1);
}

/*
 * Catch errors from IB.  This is invoked in its own thread created
 * by libvapi.  Just ship the event down an fd and read it later when
 * we want to get it, like the OpenIB model.
 */
static void
async_event_handler(VAPI_hca_hndl_t nic_handle_in __attribute__((unused)),
  VAPI_event_record_t *e, void *private_data __attribute__((unused)) )
{
    struct vapi_device_priv *vd = ib_device->priv;
    int ret;

    ret = write(vd->async_event_pipe[1], e, sizeof(*e));
    if (ret != sizeof(*e))
	error_errno("%s: write async event pipe", __func__);
}

/*
 * To deal with blocking completion events.  Just write down a pipe
 * that something is ready to go; signaling to poll that an event is
 * ready.
 */
static void cq_event_handler(VAPI_hca_hndl_t hca __attribute__((unused)),
                             VAPI_cq_hndl_t cq __attribute__((unused)),
		             void *private_data __attribute__((unused)))
{
    struct vapi_device_priv *vd = ib_device->priv;
    int i = 0, ret;

    ret = write(vd->cq_event_pipe[1], &i, sizeof(i));
    if (ret != sizeof(i))
	error_errno("%s: write cq event pipe", __func__);
}

#ifdef HAVE_IB_WRAP_COMMON_H
extern int mosal_fd;
#endif

/*
 * Hack to work around fork in daemon mode which confuses kernel
 * state.  I wish they did not have an _init constructor function in
 * libmosal.so.  It calls into MOSAL_user_lib_init().
 * This just breaks its saved state and reinitializes.  (task->mm
 * changes due to fork after init, hash lookup on that fails.)
 *
 * Seems to work even in the case of a non-backgrounded server too,
 * fortunately.
 *
 * Note that even with shared libraries you do not have protection
 * against wandering headers.  The thca distributions have in the
 * past been eager to change critical #defines like VAPI_CQ_EMPTY
 * so libpvfs2.so is more or less tied to the vapi.h against which
 * it was compiled.
 */
static void reinit_mosal(void)
{
    void *dlh;
    int (*mosal_ioctl_open)(void);
    int (*mosal_ioctl_close)(void);

    dlh = dlopen("libmosal.so", RTLD_LAZY);
    if (!dlh)
	error("%s: cannot open libmosal shared library", __func__);

#ifdef HAVE_IB_WRAP_COMMON_H
    {
    /*
     * What's happening here is we probe the internals of the mosal library
     * to get it to return a structure that has the current fd and state
     * of the connection to /dev/mosal.  We close it, reset the state, and
     * force it to reinitialize itself.  Icky, but effective.  Only necessary
     * for older thca distributions that install the needed header and
     * have this symbol in the library.
     *
     * Else fall through to the easier method.
     */
    call_result_t (*_dev_mosal_init_lib)(t_lib_descriptor **pp_t_lib);
    const char *errmsg;

    _dev_mosal_init_lib = dlsym(dlh, "_dev_mosal_init_lib");
    errmsg = dlerror();
    if (errmsg == NULL) {
	t_lib_descriptor *desc;
	int ret;

	ret = (*_dev_mosal_init_lib)(&desc);
	debug(2, "%s: mosal init ret %d, desc %p", __func__, ret, desc);
	debug(2, "%s: desc->fd %d", __func__, desc->os_lib_desc_st.fd);
	close(desc->os_lib_desc_st.fd);
	/* both these state items protect against a reinit */
	desc->state = 0;
	mosal_fd = -1;
	MOSAL_user_lib_init();
	return;
    }
    }
#endif

    /*
     * Recent thca distros and the 2.6 openib tree do not seem to permit
     * any way to "trick" the library as above, but there's no need for
     * the hack now that they export a "finalize" function to undo the init.
     */
    mosal_ioctl_open = dlsym(dlh, "mosal_ioctl_open");
    if (dlerror())
	error("%s: mosal_ioctl_open not found in libmosal", __func__);
    mosal_ioctl_close = dlsym(dlh, "mosal_ioctl_close");
    if (dlerror())
	error("%s: mosal_ioctl_close not found in libmosal", __func__);

    (*mosal_ioctl_close)();
    (*mosal_ioctl_open)();
}

/*
 * Just catch and report the events, do not try to do anything with
 * them.
 */
static int vapi_check_async_events(void)
{
    struct vapi_device_priv *vd = ib_device->priv;
    int ret;
    VAPI_event_record_t ev;

    ret = read(vd->async_event_pipe[0], &ev, sizeof(ev));
    if (ret < 0) {
	if (errno == EAGAIN)
	    return 0;
	error_errno("%s: read async event pipe", __func__);
    }
    warning("%s: %s", __func__, VAPI_event_record_sym(ev.type));
    return 1;
}

/*
 * VAPI-specific startup.
 */
int vapi_ib_initialize(void)
{
    int ret, flags;
    u_int32_t num_hcas;
    VAPI_hca_id_t hca_ids[10];
    VAPI_hca_port_t nic_port_props;
    VAPI_hca_vendor_t vendor_cap;
    VAPI_hca_cap_t hca_cap;
    VAPI_cqe_num_t cqe_num, cqe_num_out;
    struct vapi_device_priv *vd;

    reinit_mosal();

    /* look for exactly one and take it */
    ret = EVAPI_list_hcas(sizeof(hca_ids)/sizeof(hca_ids[0]), &num_hcas,
                          hca_ids);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_list_hcas", __func__);
    if (num_hcas == 0) {
	warning("%s: no hcas detected", __func__);
	return -ENODEV;
    }
    if (num_hcas > 1)
	warning("%s: found %d HCAs, choosing the first", __func__, num_hcas);

    vd = bmi_ib_malloc(sizeof(*vd));
    ib_device->priv = vd;

    /* set the function pointers for vapi */
    ib_device->func.new_connection = vapi_new_connection;
    ib_device->func.close_connection = vapi_close_connection;
    ib_device->func.drain_qp = vapi_drain_qp;
    ib_device->func.ib_initialize = vapi_ib_initialize;
    ib_device->func.ib_finalize = vapi_ib_finalize;
    ib_device->func.post_sr = vapi_post_sr;
    ib_device->func.post_rr = vapi_post_rr;
    ib_device->func.post_sr_rdmaw = vapi_post_sr_rdmaw;
    ib_device->func.check_cq = vapi_check_cq;
    ib_device->func.prepare_cq_block = vapi_prepare_cq_block;
    ib_device->func.ack_cq_completion_event = vapi_ack_cq_completion_event;
    ib_device->func.wc_status_string = vapi_wc_status_string;
    ib_device->func.mem_register = vapi_mem_register;
    ib_device->func.mem_deregister = vapi_mem_deregister;
    ib_device->func.check_async_events = vapi_check_async_events;

    /*
     * Apparently VAPI_open_hca() is a once-per-machine sort of thing, and
     * users are not expected to call it.  It returns EBUSY every time.
     * This call initializes the per-process user resources and starts up
     * all the threads.  Discard const char* for silly mellanox prototype;
     * it really is treated as constant.
     */
    ret = EVAPI_get_hca_hndl(hca_ids[0], &vd->nic_handle);
    if (ret < 0)
	error("%s: could not get HCA handle", __func__);

    /* connect an asynchronous event handler to look for weirdness */
    ret = EVAPI_set_async_event_handler(vd->nic_handle, async_event_handler, 0,
                                        &vd->nic_async_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_set_async_event_handler", __func__);

    /* get the lid and verify port state */
    /* ignore different-width-prototype warning here, cannot pass u8 */
    ret = VAPI_query_hca_port_prop(vd->nic_handle, VAPI_PORT, &nic_port_props);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_query_hca_port_prop", __func__);
    vd->nic_lid = nic_port_props.lid;

    if (nic_port_props.state != PORT_ACTIVE)
	error("%s: port state is %s but should be ACTIVE; check subnet manager",
	      __func__, vapi_port_state_string(nic_port_props.state));

    /* build a protection domain */
    ret = VAPI_alloc_pd(vd->nic_handle, &vd->nic_pd);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_create_pd", __func__);
    /* ulong in 2.6, uint32 in 2.4 */
    debug(2, "%s: built pd %lx", __func__, (unsigned long) vd->nic_pd);

    /* see how many cq entries we are allowed to have */
    memset(&hca_cap, 0, sizeof(hca_cap));
    ret = VAPI_query_hca_cap(vd->nic_handle, &vendor_cap, &hca_cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_query_hca_cap", __func__);

    debug(0, "%s: max %d completion queue entries", __func__,
      hca_cap.max_num_cq);
    cqe_num = VAPI_NUM_CQ_ENTRIES;
    if (hca_cap.max_num_cq < cqe_num) {
	cqe_num = hca_cap.max_num_cq;
	warning("%s: hardly enough completion queue entries %d, hoping for %d",
	  __func__, hca_cap.max_num_cq, cqe_num);
    }

    /* build a CQ (ignore actual number returned) */
    debug(0, "%s: asking for %d completion queue entries", __func__, cqe_num);
    ret = VAPI_create_cq(vd->nic_handle, cqe_num, &vd->nic_cq, &cqe_num_out);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_create_cq ret %d", __func__, ret);

    /* create completion "channel" */
    ret = pipe(vd->cq_event_pipe);
    if (ret < 0)
	error_errno("%s: pipe", __func__);
    flags = fcntl(vd->cq_event_pipe[0], F_GETFL);
    if (flags < 0)
	error_errno("%s: get cq pipe flags", __func__);
    if (fcntl(vd->cq_event_pipe[0], F_SETFL, flags | O_NONBLOCK) < 0)
	error_errno("%s: set cq pipe nonblocking", __func__);

    /* register handler for the cq */
    ret = EVAPI_set_comp_eventh(vd->nic_handle, vd->nic_cq, cq_event_handler,
                                NULL, &vd->nic_cq_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_set_comp_eventh", __func__);

    /* build a pipe to queue up async events, and set it to non-blocking
     * on the receive side */
    ret = pipe(vd->async_event_pipe);
    if (ret < 0)
	error_errno("%s: pipe", __func__);
    flags = fcntl(vd->async_event_pipe[0], F_GETFL);
    if (flags < 0)
	error_errno("%s: get async pipe flags", __func__);
    if (fcntl(vd->async_event_pipe[0], F_SETFL, flags | O_NONBLOCK) < 0)
	error_errno("%s: set async pipe nonblocking", __func__);

    /* will be set on first connection */
    vd->sg_tmp_array = NULL;
    vd->sg_max_len = 0;
    vd->max_outstanding_wr = 0;

    return 0;
}

/*
 * VAPI shutdown.
 */
static void vapi_ib_finalize(void)
{
    int ret;
    struct vapi_device_priv *vd = ib_device->priv;

    if (vd->sg_tmp_array)
	free(vd->sg_tmp_array);
    ret = EVAPI_clear_comp_eventh(vd->nic_handle, vd->nic_cq_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_clear_comp_eventh", __func__);

    close(vd->cq_event_pipe[0]);
    close(vd->cq_event_pipe[1]);

    ret = VAPI_destroy_cq(vd->nic_handle, vd->nic_cq);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_destroy_cq", __func__);

    ret = VAPI_dealloc_pd(vd->nic_handle, vd->nic_pd);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_dealloc_pd", __func__);
    ret = EVAPI_clear_async_event_handler(vd->nic_handle,
                                          vd->nic_async_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_clear_async_event_handler", __func__);
    ret = EVAPI_release_hca_hndl(vd->nic_handle);

    close(vd->async_event_pipe[0]);
    close(vd->async_event_pipe[1]);

    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_release_hca_hndl", __func__);
    ret = VAPI_close_hca(vd->nic_handle);
    /*
     * Buggy vapi always returns EBUSY, just like for the open
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_close_hca", __func__);
     */

    free(vd);
    ib_device->priv = NULL;
}


/*
 * InfiniBand BMI method initialization and other out-of-line
 * boring stuff.
 *
 * Copyright (C) 2003-5 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * $Id: setup.c,v 1.19 2005-12-23 20:47:53 pw Exp $
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <malloc.h>
#include <sys/poll.h>
#include <netinet/in.h>  /* ntohs et al */
#include <arpa/inet.h>   /* inet_ntoa */
#include <netdb.h>       /* gethostbyname */
#include <src/io/bmi/bmi-method-callback.h>
#include <vapi_common.h>  /* VAPI_event_(record|syndrome)_sym */
#ifdef HAVE_IB_WRAP_COMMON_H
#include <wrap_common.h>  /* reinit_mosal externs */
#endif
#include <dlfcn.h>        /* look in mosal for syms */
/* bmi ib private header */
#include "ib.h"

/* constants used to initialize infiniband device */
static const char *VAPI_DEVICE = "InfiniHost0";
static const int VAPI_PORT = 1;
static const unsigned int VAPI_NUM_CQ_ENTRIES = 1024;
static const int VAPI_MTU = MTU1024;  /* default mtu, 1k best here */

/*
 * BMI_ib_initialize is not called before BMI_ib_method_addr_lookup.
 * This keeps the lookup function from playing with uninitialized
 * variables.
 */
static int bmi_ib_initialized = 0;

/*
 * Handle given by upper layer, which must be handed back to create
 * method_addrs.
 */
static int bmi_ib_method_id;

static IB_lid_t nic_lid;  /* my local subnet identifier */
static VAPI_pd_hndl_t nic_pd;  /* single protection domain for all memory/QP */
static EVAPI_async_handler_hndl_t nic_async_event_handler;
static int async_event_handler_waiting_drain = 0;

static void verify_prop_caps(VAPI_qp_cap_t *cap);
static int exchange_connection_data(ib_connection_t *c, int s, int is_server);
static void init_connection_modify_qp(VAPI_qp_hndl_t qp,
  VAPI_qp_num_t remote_qp_num, int remote_lid);

/*
 * Build new conneciton.
 */
static ib_connection_t *
ib_new_connection(int s, const char *peername, int is_server)
{
    ib_connection_t *c;
    int i, ret;
    VAPI_mr_t mr, mr_out;
    /* for create qp */
    VAPI_qp_init_attr_t qp_init_attr;
    VAPI_qp_prop_t prop;

    /* build new connection */
    c = Malloc(sizeof(*c));
    c->peername = strdup(peername);

    /* fill send and recv free lists and buf heads */
    c->eager_send_buf_contig = Malloc(EAGER_BUF_NUM * EAGER_BUF_SIZE);
    c->eager_recv_buf_contig = Malloc(EAGER_BUF_NUM * EAGER_BUF_SIZE);
    INIT_QLIST_HEAD(&c->eager_send_buf_free);
    INIT_QLIST_HEAD(&c->eager_recv_buf_free);
    c->eager_send_buf_head_contig = Malloc(EAGER_BUF_NUM
      * sizeof(*c->eager_send_buf_head_contig));
    c->eager_recv_buf_head_contig = Malloc(EAGER_BUF_NUM
      * sizeof(*c->eager_recv_buf_head_contig));
    for (i=0; i<EAGER_BUF_NUM; i++) {
	buf_head_t *ebs = &c->eager_send_buf_head_contig[i];
	buf_head_t *ebr = &c->eager_recv_buf_head_contig[i];
	INIT_QLIST_HEAD(&ebs->list);
	INIT_QLIST_HEAD(&ebr->list);
	ebs->c = ebr->c = c;
	ebs->num = ebr->num = i;
	ebs->buf = (char *) c->eager_send_buf_contig + i * EAGER_BUF_SIZE;
	ebr->buf = (char *) c->eager_recv_buf_contig + i * EAGER_BUF_SIZE;
	qlist_add_tail(&ebs->list, &c->eager_send_buf_free);
	qlist_add_tail(&ebr->list, &c->eager_recv_buf_free);
    }

    /* register memory region, recv */
    mr.type = VAPI_MR;
    mr.start = int64_from_ptr(c->eager_recv_buf_contig);
    mr.size = EAGER_BUF_NUM * EAGER_BUF_SIZE;
    mr.pd_hndl = nic_pd;
    mr.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
    ret = VAPI_register_mr(nic_handle, &mr, &c->eager_recv_mr, &mr_out);
    if (ret < 0)
	error_verrno(ret, "%s: register_mr eager recv", __func__);
    c->eager_recv_lkey = mr_out.l_key;

    /* register memory region, send */
    mr.type = VAPI_MR;
    mr.start = int64_from_ptr(c->eager_send_buf_contig);
    mr.size = EAGER_BUF_NUM * EAGER_BUF_SIZE;
    mr.pd_hndl = nic_pd;
    mr.acl = VAPI_EN_LOCAL_WRITE;
    ret = VAPI_register_mr(nic_handle, &mr, &c->eager_send_mr, &mr_out);
    if (ret < 0)
	error_verrno(ret, "%s: register_mr bounce", __func__);
    c->eager_send_lkey = mr_out.l_key;

    /* common qp properites */
    qp_init_attr.cap.max_oust_wr_sq = 5000;  /* outstanding WQEs */
    qp_init_attr.cap.max_oust_wr_rq = 5000;
    qp_init_attr.cap.max_sg_size_sq = 40;  /* scatter/gather entries */
    qp_init_attr.cap.max_sg_size_rq = 40;
    qp_init_attr.pd_hndl            = nic_pd;
    qp_init_attr.rdd_hndl           = 0;
    /* wire both send and recv to the same CQ */
    qp_init_attr.sq_cq_hndl         = nic_cq;
    qp_init_attr.rq_cq_hndl         = nic_cq;
    /* only generate completion queue entries if requested */
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    /* build main qp */
    ret = VAPI_create_qp(nic_handle, &qp_init_attr, &c->qp, &prop);
    if (ret < 0)
	error_verrno(ret, "%s: create QP", __func__);
    c->qp_num = prop.qp_num;
    verify_prop_caps(&prop.cap);

    /* and qp ack */
    ret = VAPI_create_qp(nic_handle, &qp_init_attr, &c->qp_ack, &prop);
    if (ret < 0)
	error_verrno(ret, "%s: create QP ack", __func__);
    c->qp_ack_num = prop.qp_num;
    verify_prop_caps(&prop.cap);

    /* initialize for post_sr and post_sr_ack */
    c->num_unsignaled_wr = 0;
    c->num_unsignaled_wr_ack = 0;

    /* put it on the list */
    qlist_add(&c->list, &connection);

    /* other vars */
    c->remote_map = 0;
    c->cancelled = 0;

    /* talk with the peer to get his lid and QP nums */
    if (exchange_connection_data(c, s, is_server) != 0) {
	ret = 1;
	goto out;
    }

    /* bring the two QPs up to RTR */
    init_connection_modify_qp(c->qp, c->remote_qp_num, c->remote_lid);
    init_connection_modify_qp(c->qp_ack, c->remote_qp_ack_num, c->remote_lid);

    /* post initial RRs */
    for (i=0; i<EAGER_BUF_NUM; i++)
	post_rr(c, &c->eager_recv_buf_head_contig[i]);

    /* final sychronize to ensure nothing happens before RRs are posted */
    for (i=0; i<2; i++) {
	int x;
	if (i ^ is_server) {
	    ret = read_full(s, &x, sizeof(x));
	    if (ret < 0) {
		ret = 1;
		warning_errno("%s: read rr post synch", __func__);
		goto out;
	    }
	    if (ret != sizeof(x)) {
		ret = 1;
		warning("%s: partial read of rr post synch, %d / %d", __func__,
		  ret, sizeof(x));
		goto out;
	    }
	} else {
	    ret = write_full(s, &x, sizeof(x));
	    if (ret < 0) {
		ret = 1;
		warning_errno("%s: write rr post synch", __func__);
		goto out;
	    }
	}
    }

    ret = 0;

  out:
    if (ret != 0) {
	/* XXX: any way to unpost the RRs first? */
	ib_close_connection(c);
	c = 0;
    }

    return c;
}

/*
 * If not set, set them.  Otherwise verify that none of our assumed global
 * limits are different for this new connection.
 */
static void
verify_prop_caps(VAPI_qp_cap_t *cap)
{
    if (sg_max_len == 0) {
	sg_max_len = cap->max_sg_size_sq;
	if (cap->max_sg_size_rq < sg_max_len)
	    sg_max_len = cap->max_sg_size_rq;
	sg_tmp_array = Malloc(sg_max_len * sizeof(*sg_tmp_array));
    } else {
	if (cap->max_sg_size_sq < sg_max_len)
	    error(
	      "%s: new connection has smaller send scatter/gather array size,"
	      " %d vs %d", __func__, cap->max_sg_size_sq, sg_max_len);
	if (cap->max_sg_size_rq < sg_max_len)
	    error(
	      "%s: new connection has smaller recv scatter/gather array size,"
	      " %d vs %d", __func__, cap->max_sg_size_rq, sg_max_len);
    }

    if (max_outstanding_wr == 0) {
	max_outstanding_wr = cap->max_oust_wr_sq;
    } else {
	if (cap->max_oust_wr_sq < max_outstanding_wr)
	    error(
	      "%s: new connection has smaller max_oust_wr_sq size, %d vs %d",
	      __func__, cap->max_oust_wr_sq, max_outstanding_wr);
    }
}

/*
 * Over TCP, share information about the connection needed to transition
 * the IB link to active.
 */
static int
exchange_connection_data(ib_connection_t *c, int s, int is_server)
{
    /*
     * Values passed through TCP to permit IB connection.  These
     * are transformed to appear in network byte order (big endian)
     * on the network.
     */
    struct {
	IB_lid_t lid;
	VAPI_qp_num_t qp_num;
	VAPI_qp_num_t qp_ack_num;
    } connection_handshake;
    int i, ret;

    /* sanity check sizes of things (actually only 24 bits in qp_num) */
    assert(sizeof(connection_handshake.lid) == sizeof(u_int16_t),
      "%s: connection_handshake.lid size %d expecting %d", __func__,
      sizeof(connection_handshake.lid), sizeof(u_int16_t));
    assert(sizeof(connection_handshake.qp_num) == sizeof(u_int32_t),
      "%s: connection_handshake.qp_num size %d expecting %d", __func__,
      sizeof(connection_handshake.qp_num), sizeof(u_int32_t));

    /* exchange information: server reads first, then writes; client opposite */
    for (i=0; i<2; i++) {
	if (i ^ is_server) {
	    ret = read_full(s, &connection_handshake,
	      sizeof(connection_handshake));
	    if (ret < 0) {
		ret = 1;
		warning_errno("%s: read", __func__);
		goto out;
	    }
	    if (ret != sizeof(connection_handshake)) {
		ret = 1;
		warning("%s: partial read, %d / %d", __func__, ret,
		  sizeof(connection_handshake));
		goto out;
	    }
	    c->remote_lid = ntohs(connection_handshake.lid);
	    c->remote_qp_num = ntohl(connection_handshake.qp_num);
	    c->remote_qp_ack_num = ntohl(connection_handshake.qp_ack_num);
	} else {
	    connection_handshake.lid = htons(nic_lid);
	    connection_handshake.qp_num = htonl(c->qp_num);
	    connection_handshake.qp_ack_num = htonl(c->qp_ack_num);
	    ret = write_full(s, &connection_handshake,
	      sizeof(connection_handshake));
	    if (ret < 0) {
		ret = 1;
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
 */
static void
init_connection_modify_qp(VAPI_qp_hndl_t qp, VAPI_qp_num_t remote_qp_num,
  int remote_lid)
{
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
    ret = VAPI_modify_qp(nic_handle, qp, &attr, &mask, &cap);
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
    ret = VAPI_modify_qp(nic_handle, qp, &attr, &mask, &cap);
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
    ret = VAPI_modify_qp(nic_handle, qp, &attr, &mask, &cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_modify_qp RTR -> RTS", __func__);
}

void
close_connection_drain_qp(VAPI_qp_hndl_t qp)
{
    int ret;
    int trips;
    VAPI_qp_attr_t attr;
    VAPI_qp_attr_mask_t mask;
    VAPI_qp_cap_t cap;

    /* transition to drain */
    QP_ATTR_MASK_CLR_ALL(mask);
    QP_ATTR_MASK_SET(mask,
       QP_ATTR_QP_STATE
     | QP_ATTR_EN_SQD_ASYN_NOTIF);
    attr.qp_state = VAPI_SQD;
    attr.en_sqd_asyn_notif = 1;
    ret = VAPI_modify_qp(nic_handle, qp, &attr, &mask, &cap);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_modify_qp RTS -> SQD", __func__);

    /* wait for the asynch notification */
    async_event_handler_waiting_drain = 1;
    trips = 0;
    while (async_event_handler_waiting_drain != 0) {
	struct timeval tv = { 0, 100000 };
	(void) select(0,0,0,0,&tv);
	if (++trips == 20)  /* 20 seconds later, bored */
	    break;
    }
    if (async_event_handler_waiting_drain == 0)
	debug(2, "%s: drain async notification worked fine", __func__);
    else
	async_event_handler_waiting_drain = 0;
	/* oops, but ignore error and return anyway */
}

/*
 * When a client prepares to exit, it notifies its servers and transitions
 * the QP to drain state, then waits for all messages to finish.
 */
static void
ib_drain_connection(ib_connection_t *c)
{
    buf_head_t *bh;

    /* already drained */
    if (c->cancelled)
	return;

    bh = qlist_try_del_head(&c->eager_send_buf_free);
    if (bh) {
	/* if no messages available, let garbage collection on server deal */
	VAPI_sg_lst_entry_t sg;
	VAPI_sr_desc_t sr;
	msg_header_t *mh;
	int ret;

	mh = bh->buf;
	mh->type = MSG_BYE;

	debug(2, "%s: sending bye", __func__);
	sg.addr = int64_from_ptr(bh->buf);
	sg.len = sizeof(mh);
	sg.lkey = c->eager_send_lkey;

	memset(&sr, 0, sizeof(sr));
	sr.opcode = VAPI_SEND;
	sr.comp_type = VAPI_UNSIGNALED;  /* == 1 */
	sr.sg_lst_p = &sg;
	sr.sg_lst_len = 1;
	ret = VAPI_post_sr(nic_handle, c->qp, &sr);
	if (ret < 0)
	    error_verrno(ret, "%s: VAPI_post_sr", __func__);
    }

    close_connection_drain_qp(c->qp);
    /* do not bother draining qp_ack, nothing sending on it anyway */
}

/*
 * At an explicit BYE message, or at finalize time, shut down a connection.
 * If descriptors are posted, hopefully they will be unposted by this.
 */
void
ib_close_connection(ib_connection_t *c)
{
    int ret;
    ib_method_addr_t *ibmap;

    debug(2, "%s: closing connection to %s", __func__, c->peername);
    ret = VAPI_destroy_qp(nic_handle, c->qp_ack);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_destroy_qp ack", __func__);
    ret = VAPI_destroy_qp(nic_handle, c->qp);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_destroy_qp", __func__);
    ret = VAPI_deregister_mr(nic_handle, c->eager_send_mr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_deregister_mr eager send", __func__);
    ret = VAPI_deregister_mr(nic_handle, c->eager_recv_mr);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_deregister_mr eager recv", __func__);
    free(c->eager_send_buf_contig);
    free(c->eager_recv_buf_contig);
    free(c->eager_send_buf_head_contig);
    free(c->eager_recv_buf_head_contig);
    /* never free the remote map, for the life of the executable, just
     * mark it unconnected since BMI will always have this structure. */
    ibmap = c->remote_map->method_data;
    ibmap->c = 0;
    free(c->peername);
    qlist_del(&c->list);
    free(c);
}

/*
 * Build and fill an IB-specific method_addr structure.
 */
struct method_addr *
ib_alloc_method_addr(ib_connection_t *c, const char *hostname, int port)
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
 * type which is returned.
 * XXX: I'm assuming that these actually return a _const_ pointer
 * so that I can hand back an existing map.
 */
struct method_addr *
BMI_ib_method_addr_lookup(const char *id)
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
    if (bmi_ib_initialized) {
	list_t *l;
	qlist_for_each(l, &connection) {
	    ib_connection_t *c = qlist_upcast(l);
	    ib_method_addr_t *ibmap = c->remote_map->method_data;
	    if (ibmap->port == port && !strcmp(ibmap->hostname, hostname)) {
	       map = c->remote_map;
	       break;
	    }
	}
    }

    if (map)
	free(hostname);  /* found it */
    else
	map = ib_alloc_method_addr(0, hostname, port);  /* alloc new one */
	/* but don't call method_addr_reg_callback! */

    return map;
}

/*
 * Blocking connect initiated by a post_sendunexpected{,_list}, or
 * post_recv*
 */
int
ib_tcp_client_connect(ib_method_addr_t *ibmap, struct method_addr *remote_map)
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

/*
 * On a server, initialize a socket for listening for new connections.
 */
static void
ib_tcp_server_init_listen_socket(struct method_addr *addr)
{
    int flags;
    struct sockaddr_in skin;
    ib_method_addr_t *ibc = addr->method_data;

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0)
	error_errno("%s: create tcp socket", __func__);
    flags = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flags,
      sizeof(flags)) < 0)
	error_errno("%s: setsockopt REUSEADDR", __func__);
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = AF_INET;
    skin.sin_port = htons(ibc->port);
  retry:
    if (bind(listen_sock, (struct sockaddr *) &skin, sizeof(skin)) < 0) {
	if (errno == EINTR)
	    goto retry;
	else
	    error_errno("%s: bind tcp socket", __func__);
    }
    if (listen(listen_sock, 1024) < 0)
	error_errno("%s: listen tcp socket", __func__);
    flags = fcntl(listen_sock, F_GETFL);
    if (flags < 0)
	error_errno("%s: fcntl getfl listen sock", __func__);
    flags |= O_NONBLOCK;
    if (fcntl(listen_sock, F_SETFL, flags) < 0)
	error_errno("%s: fcntl setfl nonblock listen sock", __func__);
}

/*
 * Check for new connections.  The listening socket is left nonblocking
 * so this test can be quick.  Returns >0 if an accept worked.
 */
int
ib_tcp_server_check_new_connections(void)
{
    struct sockaddr_in ssin;
    int s, len, ret = 0;

    len = sizeof(ssin);
    s = accept(listen_sock, (struct sockaddr *) &ssin, &len);
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
 * Watch the listen_sock for activity, but do not actually respond to it.  A
 * later call to testunexpected will pick up the new connection.  Returns >0
 * if an accept would work on another call (later _unexpected will find it).
 */
int
ib_tcp_server_block_new_connections(int timeout_ms)
{
    struct pollfd pfd;
    int ret;

    pfd.fd = listen_sock;
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
	if (errno == EINTR)  /* these are okay, debugging or whatever */
	    ret = 0;
	else
	    error_errno("%s: poll listen sock", __func__);
    }
    return ret;
}

/*
 * Callers sometimes want to know odd pieces of information.  Satisfy
 * them.
 */
int
BMI_ib_get_info(int option, void *param)
{
    int ret = 0;

    switch (option) {
	case BMI_CHECK_MAXSIZE:
	    /* reality is 2^31, but shrink to avoid negative int */
	    *(int *)param = (1UL << 31) - 1;
	    break;
	case BMI_DROP_ADDR_QUERY:
	    /* weird TCP thing, ignore */
	    break;
	case BMI_GET_UNEXP_SIZE:
	    *(int *)param = EAGER_BUF_PAYLOAD;
	    break;
	default:
	    warning("%s: hint %d not implemented", __func__, option);
	    ret = -ENOSYS;
    }
    return 0;
}

/*
 * Used to set some optional parameters.  Just ignore.
 */
int
BMI_ib_set_info(int option __unused, void *param __unused)
{
    return 0;
}

/*
 * Catch errors from IB.
 */
static void
async_event_handler(VAPI_hca_hndl_t nic_handle_in __attribute__((unused)),
  VAPI_event_record_t *e, void *private_data __attribute__((unused)) )
{
    /* catch drain events, else error */
    if (e->type == VAPI_SEND_QUEUE_DRAINED) {
	debug(2, "%s: caught send queue drained", __func__);
	async_event_handler_waiting_drain = 0;
    } else
	/* qp handle is ulong in 2.4, uint32 in 2.6 */
	error("%s: event %s, syndrome %s, qp or cq or port 0x%lx",
	  __func__, VAPI_event_record_sym(e->type),
	  VAPI_event_syndrome_sym(e->syndrome),
	  (unsigned long) e->modifier.qp_hndl);
}

/*
 * Hack to work around fork in daemon mode which confuses kernel
 * state.  I wish they did not have an _init constructor function in
 * libmosal.so.  It calls into MOSAL_user_lib_init().
 * This just breaks its saved state and reinitializes.  (task->mm
 * changes due to fork after init, hash lookup on that fails.)
 *
 * Seems to work even in the case of a non-backgrounded server too,
 * fortunately.
 */
#ifndef VAPI_INVAL_SRQ_HNDL
/* already declared in 2.6, so look for a 2.6-only tag to avoid */
extern void MOSAL_user_lib_init(void);
#endif
extern int mosal_fd;

static void
reinit_mosal(void)
{
    void *dlh;
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
     * force it to reinitialize itself.  Icky, but effective.  Works only
     * with older thca distributions that install the needed header.
     */
    call_result_t (*_dev_mosal_init_lib)(t_lib_descriptor **pp_t_lib);
    _dev_mosal_init_lib = dlsym(dlh, "_dev_mosal_init_lib");
    if (!dlerror()) {
	t_lib_descriptor *desc;
	int ret = (*_dev_mosal_init_lib)(&desc);
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
    mosal_ioctl_close = dlsym(dlh, "mosal_ioctl_close");
    if (dlerror())
	error("%s: magic symbol not found in libmosal", __func__);
    (*mosal_ioctl_close)();
    mosal_fd = -1;
    MOSAL_user_lib_init();

    /*
     * Note that even with shared libraries you do not have protection
     * against wandering headers.  The thca distributions have in the
     * past been eager to change critical #defines like VAPI_CQ_EMPTY
     * so libpvfs2.so is more or less tied to the vapi.h against which
     * it was compiled.
     */
}

/*
 * Startup, once per application.
 */
int
BMI_ib_initialize(struct method_addr *listen_addr, int method_id,
  int init_flags)
{
    int ret;
    VAPI_hca_port_t nic_port_props;
    VAPI_hca_vendor_t vendor_cap;
    VAPI_hca_cap_t hca_cap;
    VAPI_cqe_num_t cqe_num, cqe_num_out;

    debug(0, "%s: init", __func__);

    /* check params */
    bmi_ib_method_id = method_id;
    if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER))
	error("%s: error: BMI_INIT_SERVER requires non-null listen_addr"
	  " and v.v", __func__);

    reinit_mosal();

   /*
     * Apparently VAPI_open_hca() is a once-per-machine sort of thing, and
     * users are not expected to call it.  It returns EBUSY every time.
     * This call initializes the per-process user resources and starts up
     * all the threads.  Discard const char* for silly mellanox prototype;
     * it really is treated as constant.
     */
    ret = EVAPI_get_hca_hndl((char *)(unsigned long) VAPI_DEVICE, &nic_handle);
    if (ret < 0)
	return -ENOSYS;

    /* connect an asynchronous event handler to look for weirdness */
    ret = EVAPI_set_async_event_handler(nic_handle, async_event_handler, 0,
      &nic_async_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_set_async_event_handler", __func__);

    /* get my lid */
    /* ignore different-width-prototype warning here, cannot pass u8 */
    ret = VAPI_query_hca_port_prop(nic_handle, VAPI_PORT, &nic_port_props);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_query_hca_port_prop", __func__);
    nic_lid = nic_port_props.lid;

    /* build a protection domain */
    ret = VAPI_alloc_pd(nic_handle, &nic_pd);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_create_pd", __func__);
    /* ulong in 2.6, uint32 in 2.4 */
    debug(2, "%s: built pd %lx", __func__, (unsigned long) nic_pd);

    /* see how many cq entries we are allowed to have */
    memset(&hca_cap, 0, sizeof(hca_cap));
    ret = VAPI_query_hca_cap(nic_handle, &vendor_cap, &hca_cap);
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
    ret = VAPI_create_cq(nic_handle, cqe_num, &nic_cq, &cqe_num_out);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_create_cq ret %d", __func__, ret);

    /*
     * Set up tcp socket to listen for connection requests.
     * The hostname is currently ignored; the port number is used to bind
     * the listening TCP socket which accepts new connections.
     */
    if (init_flags & BMI_INIT_SERVER)
	ib_tcp_server_init_listen_socket(listen_addr);
    else
	listen_sock = -1;

    /*
     * Initialize data structures.
     */
    INIT_QLIST_HEAD(&connection);
    INIT_QLIST_HEAD(&sendq);
    INIT_QLIST_HEAD(&recvq);
    INIT_QLIST_HEAD(&memcache);

    EAGER_BUF_PAYLOAD = EAGER_BUF_SIZE - sizeof(msg_header_t);

    /* will be set on first connection */
    sg_tmp_array = 0;
    sg_max_len = 0;
    max_outstanding_wr = 0;

    bmi_ib_initialized = 1;  /* okay to play with state variables now */

#if 0
    /*
     * XXX: temporary while using registration cache.  Perhaps switch to
     * malloc/free hooks, or better yet, use dreg kernel module.
     * Think about how this fights with mpich's malloc hooks.
     */
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    debug(0, "%s: done", __func__);
    return 0;
}

/*
 * Shutdown.
 */
int
BMI_ib_finalize(void)
{
    int ret;

    /* if not server, send BYE to each connection and bring
     * down the QP */
    if (listen_sock < 0) {
	list_t *l;
	qlist_for_each(l, &connection) {
	    ib_connection_t *c = qlist_upcast(l);
	    ib_drain_connection(c);
	}
    }

    /* destroy QPs and other connection structures */
    while (connection.next != &connection) {
	ib_connection_t *c = (ib_connection_t *) connection.next;
	ib_close_connection(c);
    }
    /* global */
    if (listen_sock >= 0)
	close(listen_sock);
    if (sg_tmp_array)
	free(sg_tmp_array);
    ret = VAPI_destroy_cq(nic_handle, nic_cq);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_destroy_cq", __func__);
    memcache_shutdown();
    ret = VAPI_dealloc_pd(nic_handle, nic_pd);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_dealloc_pd", __func__);
    ret = EVAPI_clear_async_event_handler(nic_handle, nic_async_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_clear_async_event_handler", __func__);
    ret = EVAPI_release_hca_hndl(nic_handle);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_release_hca_hndl", __func__);
    ret = VAPI_close_hca(nic_handle);
    /*
     * Buggy vapi always returns EBUSY, just like for the open
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_close_hca", __func__);
     */
    return 0;
}

/*
 * Memory registration and deregistration.  Used both by sender and
 * receiver, vary if lkey or rkey = 0.
 *
 * Pain because a s/g list requires lots of little allocations.  Needs
 * wuj's clever discontig allocation stuff.
 */
void
ib_mem_register(memcache_entry_t *c)
{
    VAPI_mrw_t mrw, mrw_out;
    int ret;

    /* always turn on local write and write even if just BMI_SEND */
    mrw.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
    mrw.type = VAPI_MR;
    mrw.pd_hndl = nic_pd;
    mrw.start = int64_from_ptr(c->buf);
    mrw.size = c->len;
    ret = VAPI_register_mr(nic_handle, &mrw, &c->memkeys.mrh, &mrw_out);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_register_mr", __func__);
    c->memkeys.lkey = mrw_out.l_key;
    c->memkeys.rkey = mrw_out.r_key;
    debug(4, "%s: buf %p len %lld", __func__, c->buf, c->len);
}

void
ib_mem_deregister(memcache_entry_t *c)
{
    int ret;
    
    ret = VAPI_deregister_mr(nic_handle, c->memkeys.mrh);
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_deregister_mr", __func__);
    debug(4, "%s: buf %p len %lld lkey %x rkey %x", __func__,
      c->buf, c->len, c->memkeys.lkey, c->memkeys.rkey);
}

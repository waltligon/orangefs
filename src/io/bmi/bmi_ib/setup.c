/*
 * InfiniBand BMI method initialization and other out-of-line
 * boring stuff.
 *
 * Copyright (C) 2003-4 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * $Id: setup.c,v 1.10 2004-05-17 19:05:13 pw Exp $
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>  /* ntohs et al */
#include <arpa/inet.h>   /* inet_ntoa */
#include <netdb.h>       /* gethostbyname */
#include <src/common/quicklist/quicklist.h>
#include <src/io/bmi/bmi-method-support.h>
#include <src/io/bmi/bmi-method-callback.h>
/* ib includes */
#include <vapi.h>
#include <vapi_common.h>  /* VAPI_event_(record|syndrome)_sym */
#include <evapi.h>
#include <wrap_common.h>  /* reinit_mosal externs */
#include <dlfcn.h>        /* look in mosal for syms */
/* bmi ib private header */
#include "ib.h"

/* constants used to initialize infiniband device */
static const char *VAPI_DEVICE = "InfiniHost0";
static const int VAPI_PORT = 1;
static const unsigned int VAPI_NUM_CQ_ENTRIES = 1024;
static const int VAPI_MTU = MTU2048;  /* default mtu */

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

static void exchange_connection_data(ib_connection_t *c, int s, int is_server);
static void init_connection_modify_qp(VAPI_qp_hndl_t qp,
  VAPI_qp_num_t remote_qp_num, IB_lid_t remote_lid, int s, int is_server);

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

    /* build qp */
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
    ret = VAPI_create_qp(nic_handle, &qp_init_attr, &c->qp, &prop);
    if (ret < 0)
	error_verrno(ret, "%s: create QP", __func__);
    c->qp_num = prop.qp_num;

    if (sg_max_len == 0) {
	sg_max_len = prop.cap.max_sg_size_sq;
	if ((int)prop.cap.max_sg_size_rq < sg_max_len)
	    sg_max_len = prop.cap.max_sg_size_rq;
	sg_tmp_array = Malloc(sg_max_len * sizeof(*sg_tmp_array));
    } else {
	if ((int)prop.cap.max_sg_size_sq < sg_max_len)
	    error(
	      "%s: new connection has smaller (send) scatter/gather array size,"
	      " %d vs %d", __func__, prop.cap.max_sg_size_rq, sg_max_len);
	if ((int)prop.cap.max_sg_size_rq < sg_max_len)
	    error(
	      "%s: new connection has smaller (recv) scatter/gather array size,"
	      " %d vs %d", __func__, prop.cap.max_sg_size_sq, sg_max_len);
    }

    /* and qp ack */
    ret = VAPI_create_qp(nic_handle, &qp_init_attr, &c->qp_ack, &prop);
    if (ret < 0)
	error_verrno(ret, "%s: create QP ack", __func__);
    c->qp_ack_num = prop.qp_num;
    if ((int)prop.cap.max_sg_size_sq < sg_max_len)
	error(
	  "%s: new ack connection has smaller (send) scatter/gather array size,"
	  " %d vs %d", __func__, prop.cap.max_sg_size_rq, sg_max_len);
    if ((int)prop.cap.max_sg_size_rq < sg_max_len)
	error(
	  "%s: new ack connection has smaller (recv) scatter/gather array size,"
	  " %d vs %d", __func__, prop.cap.max_sg_size_sq, sg_max_len);
    
    exchange_connection_data(c, s, is_server);

    init_connection_modify_qp(c->qp, c->remote_qp_num,
      c->remote_lid, s, is_server);
    init_connection_modify_qp(c->qp_ack, c->remote_qp_ack_num,
      c->remote_lid, s, is_server);

    for (i=0; i<EAGER_BUF_NUM; i++)
	post_rr(c, &c->eager_recv_buf_head_contig[i]);

    /* final sychronize to ensure nothing happens before RRs are posted */
    for (i=0; i<2; i++) {
	int x;
	if (i ^ is_server) {
	    ret = read_full(s, &x, sizeof(x));
	    if (ret < 0)
		error_errno("%s: read rr post synch", __func__);
	    if (ret != sizeof(x))
		error("%s: partial read of rr post synch, %d / %d", __func__,
		  ret, sizeof(x));
	} else {
	    ret = write_full(s, &x, sizeof(x));
	    if (ret < 0)
		error_errno("%s: write rr post synch", __func__);
	}
    }

    /* done, put it on the list */
    c->remote_map = 0;
    qlist_add(&c->list, &connection);
    return c;
}

/*
 * Over TCP, share information about the connection needed to transition
 * the IB link to active.
 */
static void
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
      "%s: connection_handshake lid size %d expecting %d", __func__,
      sizeof(connection_handshake.lid), sizeof(u_int16_t));
    assert(sizeof(connection_handshake.qp_num) == sizeof(u_int32_t),
      "%s: connection_handshake qp_num size %d expecting %d", __func__,
      sizeof(connection_handshake.qp_num), sizeof(u_int32_t));

    /* exchange information: server reads first, then writes; client opposite */
    for (i=0; i<2; i++) {
	if (i ^ is_server) {
	    ret = read_full(s, &connection_handshake,
	      sizeof(connection_handshake));
	    if (ret < 0)
		error_errno("%s: read new connection handshake info", __func__);
	    if (ret != sizeof(connection_handshake))
		error("%s: partial read of handshake info, %d / %d", __func__,
		  ret, sizeof(connection_handshake));
	    c->remote_lid = ntohs(connection_handshake.lid);
	    c->remote_qp_num = ntohl(connection_handshake.qp_num);
	    c->remote_qp_ack_num = ntohl(connection_handshake.qp_ack_num);
	} else {
	    connection_handshake.lid = htons(nic_lid);
	    connection_handshake.qp_num = htonl(c->qp_num);
	    connection_handshake.qp_ack_num = htonl(c->qp_ack_num);
	    ret = write_full(s, &connection_handshake,
	      sizeof(connection_handshake));
	    if (ret < 0)
		error_errno("%s: write new connection handshake info",
		  __func__);
	}
    }
}


/*
 * Perform the many steps required to bring up both sides of an IB connection.
 */
static void
init_connection_modify_qp(VAPI_qp_hndl_t qp, VAPI_qp_num_t remote_qp_num,
  IB_lid_t remote_lid, int s, int is_server)
{
    int i, ret;
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

    /* syncronize both in RTR before going RTS */
    for (i=0; i<2; i++) {
	int x;
	if (i ^ is_server) {
	    ret = read(s, &x, sizeof(x));
	    if (ret < 0)
		error_errno("%s: read rtr synch", __func__);
	    if (ret != sizeof(x))
		error("%s: partial read of rtr synch, %d / %d", __func__,
		  ret, sizeof(x));
	} else {
	    ret = write(s, &x, sizeof(x));
	    if (ret < 0)
		error_errno("%s: write rtr synch", __func__);
	    if (ret != sizeof(x))
		error("%s: partial write of rtr synch, %d / %d", __func__,
		  ret, sizeof(x));
	}
    }

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

static void
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
    free(c->remote_map);
    free(c->peername);
    qlist_del(&c->list);
}

/*
 * Build and fill an IB-specific method_addr structure.
 */
struct method_addr *
ib_alloc_method_addr(ib_connection_t *c, const char *hostname, int port)
{
    struct method_addr *map;
    ib_method_addr_t *ibmap;

    map = alloc_method_addr(bmi_ib_method_id, sizeof(*ibmap));
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
    hostname = Malloc(cp - s + 1);
    strncpy(hostname, s, cp-s);
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
void
ib_tcp_client_connect(ib_method_addr_t *ibmap, struct method_addr *remote_map)
{
    int s;
    char peername[2048];
    struct hostent *hp;
    struct sockaddr_in skin;
    
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
	error_errno("%s: create tcp socket", __func__);
    hp = gethostbyname(ibmap->hostname);
    if (!hp)
	error_errno("%s: cannot resolve server %s", __func__, ibmap->hostname);
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = hp->h_addrtype;
    memcpy(&skin.sin_addr, hp->h_addr_list[0], hp->h_length);
    skin.sin_port = htons(ibmap->port);
    sprintf(peername, "%s:%d", ibmap->hostname, ibmap->port);
  retry:
    if (connect(s, (struct sockaddr *) &skin, sizeof(skin)) < 0) {
	if (errno == EINTR)
	    goto retry;
	else
	    error_errno("%s: connect to server %s", __func__, peername);
    }
    ibmap->c = ib_new_connection(s, peername, 0);
    ibmap->c->remote_map = remote_map;

    if (close(s) < 0)
	error_errno("%s: close sock", __func__);
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
 * so this test can be quick.
 */
void
ib_tcp_server_check_new_connections(void)
{
    struct sockaddr_in ssin;
    int s, len;

    len = sizeof(ssin);
    s = accept(listen_sock, (struct sockaddr *) &ssin, &len);
    if (s < 0) {
	if (!(errno == EAGAIN))
	    error_errno("%s: accept listen sock", __func__);
    } else {
	int ret;
	char peername[2048];
	ib_connection_t *c;

	char *hostname = strdup(inet_ntoa(ssin.sin_addr));
	int port = ntohs(ssin.sin_port);
	sprintf(peername, "%s:%d", hostname, port);

	c = ib_new_connection(s, peername, 1);
	c->remote_map = ib_alloc_method_addr(c, hostname, port);
	/* register this address with the method control layer */
	ret = bmi_method_addr_reg_callback(c->remote_map);
	if (ret < 0)
	    error_xerrno(ret, "%s: bmi_method_addr_reg_callback", __func__);

	debug(2, "%s: accepted new connection %s at server", __func__,
	  c->peername);
	if (close(s) < 0)
	    error_errno("%s: close new sock", __func__);

    }
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
 * state.  I wish they did not have an _init function.  It calls
 * into MOSAL_user_lib_init(), but there is no finalize equivalent.
 * This just breaks its saved state and reinitializes.  (task->mm
 * changes due to fork after init, hash lookup on that fails.)
 *
 * Seems to work even in the case of a non-backgrounded server too,
 * fortunately.
 *
 * We have to do something different on the 2.6 mellanox distro,
 * but would prefer not to have two different PVFS2 versions, one
 * for 2.4 clients and one for 2.6, so try to guess which is which.
 */

#ifndef VAPI_INVAL_SRQ_HNDL
/* already declared in 2.6, so look for a 2.6-only tag to avoid */
extern void MOSAL_user_lib_init(void);
#endif
extern int mosal_fd;

static void
reinit_mosal(void)
{
    t_lib_descriptor *desc;
    int ret;
    void *dlh;
    call_result_t (*_dev_mosal_init_lib)(t_lib_descriptor **pp_t_lib);
    int (*mosal_ioctl_close)(void);

    dlh = dlopen("libmosal.so", RTLD_LAZY);
    if (!dlh)
	error("%s: cannot open libmosal shared library", __func__);
    _dev_mosal_init_lib = dlsym(dlh, "_dev_mosal_init_lib");
    if (!dlerror()) {
	ret = (*_dev_mosal_init_lib)(&desc);
	debug(2, "%s: mosal init ret %d, desc %p", __func__, ret, desc);
	debug(2, "%s: desc->fd %d", __func__, desc->os_lib_desc_st.fd);
	close(desc->os_lib_desc_st.fd);
	/* both these state items protect against a reinit */
	desc->state = 0;
	mosal_fd = -1;
	MOSAL_user_lib_init();
#if 0
	/* just for debugging, print out the same values again */
	ret = _dev_mosal_init_lib(&desc);
	debug(2, "%s: after close of state fd", __func__);
	debug(2, "%s: mosal init ret %d, desc %p", __func__, ret, desc);
	debug(2, "%s: desc->fd %d", __func__, desc->os_lib_desc_st.fd);
#endif
    } else {
	/* Unfortunately this trick does not work.  Need to change
	 * a static variable in the library to be allowed to reinit.
	 * Read /dev/mosal open code to see if there's some other way
	 * to get this process understood.  Can then close the new
	 * socket and use the original library one maybe.
	 *
	 * Nope.  Library is convinced it is already initialized,
	 * can't change its fd.  Must do the following things.
	 */
	mosal_ioctl_close = dlsym(dlh, "mosal_ioctl_close");
	if (dlerror())
	    error("%s: neither magic symbol found in libmosal", __func__);
	(*mosal_ioctl_close)();
	mosal_fd = -1;
	MOSAL_user_lib_init();
    }
    /* XXX: note that you still need a separate application since header
     * files are different.  VAPI_CQ_EMPTY is -213 on 2.6 version, which
     * is not what 2.4-compiled check_cq() expects to hear back from
     * VAPI_poll_cq.
     *
     * Or hack that in yet another wrapper that gets the value at runtime.
     * Hope not too much else changed...
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

    /* open device; discard const char* for silly mellanox prototype */
    ret = VAPI_open_hca((char *)(unsigned long) VAPI_DEVICE, &nic_handle);
    /*
     * Buggy vapi lib always returns EBUSY, ignore this return value.
     *
    if (ret < 0)
	error_verrno(ret, "%s: VAPI_open_hca", __func__);
    */

    /* starts all the ib threads */
    ret = EVAPI_get_hca_hndl((char *)(unsigned long) VAPI_DEVICE, &nic_handle);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_get_hca_hndl", __func__);

    /* connect an asynchronous event handler to look for weirdness */
    ret = EVAPI_set_async_event_handler(nic_handle, async_event_handler, 0,
      &nic_async_event_handler);
    if (ret < 0)
	error_verrno(ret, "%s: EVAPI_set_async_event_handler", __func__);

    /* get my lid */
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

    EAGER_BUF_PAYLOAD = EAGER_BUF_SIZE - sizeof(msg_header_t);

    /* will be set on first connection */
    sg_tmp_array = 0;
    sg_max_len = 0;

    bmi_ib_initialized = 1;  /* okay to play with state variables now */

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
ib_mem_register(ib_buflist_t *buflist, int send_or_recv_type)
{
    int i;
    VAPI_mrw_t mrw;

    if (send_or_recv_type == TYPE_SEND) {
	buflist->lkey = Malloc(buflist->num * sizeof(*buflist->lkey));
	buflist->rkey = 0;
	mrw.acl = 0;  /* just local read for sender */
    } else {
	buflist->lkey = 0;
	buflist->rkey = Malloc(buflist->num * sizeof(*buflist->rkey));
	/* must turn on local write if want remote write */
	mrw.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
    }
    buflist->mr_handle = Malloc(buflist->num * sizeof(*buflist->mr_handle));

    /* constant across loop */
    mrw.type = VAPI_MR;
    mrw.pd_hndl = nic_pd;

    for (i=0; i<buflist->num; i++) {
	VAPI_mrw_t mrw_out;
	int ret;
	mrw.start = int64_from_ptr(buflist->buf.send[i]);  /* union */
	mrw.size = buflist->len[i];
	ret = VAPI_register_mr(nic_handle, &mrw, &buflist->mr_handle[i],
	  &mrw_out);
	if (ret < 0)
	    error_verrno(ret, "%s: VAPI_register_mr %d", __func__, i);
	if (send_or_recv_type == TYPE_SEND)
	    buflist->lkey[i] = mrw_out.l_key;
	else
	    buflist->rkey[i] = mrw_out.r_key;
	debug(4, "%s: %d addr %Lx size %Ld %s %x", __func__, i, mrw.start,
	  mrw.size, send_or_recv_type == TYPE_SEND ? "lkey" : "rkey",
	  send_or_recv_type == TYPE_SEND ? mrw_out.l_key : mrw_out.r_key);
    }
}

void
ib_mem_deregister(ib_buflist_t *buflist)
{
    int i;

    for (i=0; i<buflist->num; i++) {
	int ret = VAPI_deregister_mr(nic_handle, buflist->mr_handle[i]);
	if (ret < 0)
	    error_verrno(ret, "%s: VAPI_deregister_mr %d", __func__, i);
	debug(4, "%s: %d addr %Lx size %Ld lkey %x rkey %x", __func__, i,
	  int64_from_ptr(buflist->buf.send[i]), buflist->len[i],
	  buflist->lkey ? buflist->lkey[i] : 0,
	  buflist->rkey ? buflist->rkey[i] : 0);
    }
    free(buflist->mr_handle);
    if (buflist->lkey)
	free(buflist->lkey);
    if (buflist->rkey)
	free(buflist->rkey);
}

/*
 * RDMA BMI method.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2006 Kyle Schochenmaier <kschoche@scl.ameslab.gov>
 * Copyright (C) 2016 David Reynolds <david@omnibond.com>
 *
 * TODO: If we still need to support ibv_get_devices(), which was replaced
 *       by ibv_get_device_list(), then the #ifdef HAVE_IBV_GET_DEVICES
 *       statements and corresponding code need to be added back in.
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
#include <src/io/bmi/bmi-method-support.h>  /* bmi_method_ops ... */
#include <src/io/bmi/bmi-method-callback.h> /* bmi_method_addr_reg_callback */
#include <src/io/bmi/bmi-byteswap.h>        /* bmitoh64 */
#include <src/common/gen-locks/gen-locks.h> /* gen_mutex_t ... */
#include <src/common/misc/pvfs2-internal.h> /* llu */
#include <infiniband/verbs.h>
#include <pthread.h>
#include "pint-hint.h"

#ifdef HAVE_VALGRIND_H
#   include <memcheck.h>
#else
#   define VALGRIND_MAKE_MEM_DEFINED(addr,len)
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

/* constants used to initialize infiniband device */
static const int IBV_PORT = 1;  /* TODO: what if it isn't Port 1? */
static const unsigned int IBV_NUM_CQ_ENTRIES = 1024;
/* TODO: can the MTU be bumped up? */
static const int IBV_MTU = IBV_MTU_1024;  /* dmtu, 1k good for mellanox */

/* function prototypes */
static int BMI_rdma_initialize(struct bmi_method_addr *listen_addr,
                               int method_id,
                               int init_flags);

static int BMI_rdma_finalize(void);

static int BMI_rdma_set_info(int option,
                             void *param __unused);

static int BMI_rdma_get_info(int option, void *param);

static void *BMI_rdma_memalloc(bmi_size_t len,
                               enum bmi_op_type send_recv __unused);

static int BMI_rdma_memfree(void *buf,
                            bmi_size_t len,
                            enum bmi_op_type send_recv __unused);

static int BMI_rdma_unexpected_free(void *buf);

static int BMI_rdma_post_send(bmi_op_id_t *id,
                              struct bmi_method_addr *remote_map,
                              const void *buffer,
                              bmi_size_t total_size,
                              enum bmi_buffer_type buffer_flag __unused,
                              bmi_msg_tag_t tag,
                              void *user_ptr,
                              bmi_context_id context_id,
                              PVFS_hint hints __unused);

static int BMI_rdma_post_sendunexpected(bmi_op_id_t *id,
                                    struct bmi_method_addr *remote_map,
                                    const void *buffer,
                                    bmi_size_t total_size,
                                    enum bmi_buffer_type buffer_flag __unused,
                                    bmi_msg_tag_t tag,
                                    void *user_ptr,
                                    bmi_context_id context_id,
                                    PVFS_hint hints __unused);

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
                                   PVFS_hint hints __unused);

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
                                    PVFS_hint hints __unused);

static int BMI_rdma_post_recv(bmi_op_id_t *id,
                              struct bmi_method_addr *remote_map,
                              void *buffer,
                              bmi_size_t expected_len,
                              bmi_size_t *actual_len __unused,
                              enum bmi_buffer_type buffer_flag __unused,
                              bmi_msg_tag_t tag,
                              void *user_ptr,
                              bmi_context_id context_id,
                              PVFS_hint hints __unused);

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
                                   PVFS_hint hints __unused);

static int BMI_rdma_testcontext(int incount,
                                bmi_op_id_t *outids,
                                int *outcount,
                                bmi_error_code_t *errs,
                                bmi_size_t *sizes,
                                void **user_ptrs,
                                int max_idle_time,
                                bmi_context_id context_id);

static int BMI_rdma_testunexpected(int incount __unused,
                                   int *outcount,
                                   struct bmi_method_unexpected_info *ui,
                                   int max_idle_time);

static struct bmi_method_addr *BMI_rdma_method_addr_lookup(const char *id);

static int BMI_rdma_open_context(bmi_context_id context_id __unused);

static void BMI_rdma_close_context(bmi_context_id context_id __unused);

static int BMI_rdma_cancel(bmi_op_id_t id,
                           bmi_context_id context_id __unused);

static const char *BMI_rdma_rev_lookup(struct bmi_method_addr *meth);

/* internal functions */
static const char *wc_opcode_string(int opcode);

static int wc_status_to_bmi(int status);

static const char *wc_status_string(int status);

static const char *async_event_type_string(enum ibv_event_type event_type);

static int check_cq(void);

static int get_one_completion(struct bmi_rdma_wc *wc);

static void msg_header_init(msg_header_common_t *mh_common,
                            rdma_connection_t *c,
                            msg_type_t type);

static struct buf_head *get_eager_buf(rdma_connection_t *c);

static void post_sr(const struct buf_head *bh,
                    u_int32_t len);

static void post_sr_rdmaw(struct rdma_work *sq,
                          msg_header_cts_t *mh_cts,
                          void *mh_cts_buf);

static void post_rr(const rdma_connection_t *c,
                    struct buf_head *bh);

static void repost_rr(rdma_connection_t *c,
                      struct buf_head *bh);

static void encourage_send_waiting_buffer(struct rdma_work *sq);

static void encourage_send_incoming_cts(struct buf_head *bh,
                                        u_int32_t byte_len);

static struct rdma_work *find_matching_recv(rq_state_t statemask,
                                            const rdma_connection_t *c,
                                            bmi_msg_tag_t bmi_tag);

static struct rdma_work *alloc_new_recv(rdma_connection_t *c,
                                        struct buf_head *bh);

static void encourage_recv_incoming(struct buf_head *bh,
                                    msg_type_t type,
                                    u_int32_t byte_len);

static void encourage_rts_done_waiting_buffer(struct rdma_work *sq);

static void send_bye(rdma_connection_t *c);

static int send_cts(struct rdma_work *rq);

static int ensure_connected(struct bmi_method_addr *remote_map);

static int post_send(bmi_op_id_t *id,
                     struct bmi_method_addr *remote_map,
                     int numbufs,
                     const void *const *buffers,
                     const bmi_size_t *sizes,
                     bmi_size_t total_size,
                     bmi_msg_tag_t tag,
                     void *user_ptr,
                     bmi_context_id context_id,
                     int is_unexpected);

static int post_recv(bmi_op_id_t *id,
                     struct bmi_method_addr *remote_map,
                     int numbufs,
                     void *const *buffers,
                     const bmi_size_t *sizes,
                     bmi_size_t tot_expected_len,
                     bmi_msg_tag_t tag,
                     void *user_ptr,
                     bmi_context_id context_id);

static int test_sq(struct rdma_work *sq,
                   bmi_op_id_t *outid,
                   bmi_error_code_t *err,
                   bmi_size_t *size,
                   void **user_ptr,
                   int complete);

static int test_rq(struct rdma_work *rq,
                   bmi_op_id_t *outid,
                   bmi_error_code_t *err,
                   bmi_size_t *size,
                   void **user_ptr,
                   int complete);

static struct bmi_method_addr *rdma_alloc_method_addr(rdma_connection_t *c,
                                                      char *hostname,
                                                      int port,
                                                      int reconnect_flag);

static rdma_connection_t *rdma_new_connection(struct rdma_cm_id *id,
                                              const char *peername,
                                              int is_server);

static rdma_connection_t *alloc_connection(const char *peername);

static void alloc_eager_bufs(rdma_connection_t *c);

static int register_memory(rdma_connection_t *c);

static void build_qp_init_attr(int *num_wr,
                               struct ibv_qp_init_attr *attr);

static int verify_qp_caps(struct ibv_qp_init_attr attr, int num_wr);

static void build_conn_params(struct rdma_conn_param *params);

//static void init_connection_modify_qp(struct ibv_qp *qp,
//                                      uint32_t remote_qp_num,
//                                      int remote_lid);

//static void rdma_drain_qp(rdma_connect_t *c);

static void rdma_close_connection(rdma_connection_t *c);

static int rdma_client_event_loop(struct rdma_event_channel *ec,
                                  rdma_method_addr_t *rdma_map,
                                  struct bmi_method_addr *remote_map,
                                  int timeout_ms);

static int rdma_client_connect(rdma_method_addr_t *rdma_map,
                               struct bmi_method_addr *remote_map);

static void rdma_server_init_listener(struct bmi_method_addr *addr);

void *rdma_server_accept_thread(void *arg);

void *rdma_server_process_client_thread(void *arg);

static int rdma_block_for_activity(int timeout_ms);

static void prepare_cq_block(int *cq_fd, int *async_fd);

static void ack_cq_completion_event(void);

static int check_async_events(void);

static int mem_register(memcache_entry_t *c);

static void mem_deregister(memcache_entry_t *c);

static int build_rdma_context(void);

/* TODO: quick hack - need to rearrange data structures and prototypes */
struct rdma_device_priv;
static int return_active_nic_handle(struct rdma_device_priv *rd,
                                    struct ibv_port_attr *hca_port);

static void cleanup_rdma_context(void);


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
 * RDMA-private device-wide state.
 */
struct rdma_device_priv
{
    struct ibv_context *ctx;   /* context used to reference everything */
    struct ibv_cq *nic_cq;     /* single completion queue for all QPs */
    struct ibv_pd *nic_pd;     /* single protection domain for all memory/QPs */
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
 * wc_status_to_bmi()
 *
 * Description:
 *  Convert an IBV work completion status into a BMI error code.
 *
 * Params:
 *  [in] status - integer value of work completion status (ibv_wc_status)
 *
 * Returns:
 *  Integer value representing appropriate BMI error code
 */
static int wc_status_to_bmi(int status)
{
    int result = 0;

    switch (status)
    {
        case IBV_WC_SUCCESS:
            result = 0;
            break;

        case IBV_WC_RETRY_EXC_ERR:
            debug(0, "%s: converting IBV_WC_RETRY_EXC_ERR to BMI_EHOSTUNREACH",
                  __func__);
            result = -BMI_EHOSTUNREACH;
            break;

        default:
            warning("%s: unhandled wc status %s, error code unchanged",
                    __func__, wc_status_string(status));
            result = status;
            break;
    }

    return result;
}

#define CASE(e)  case e: s = #e; break

/*
 * wc_status_string()
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
static const char *wc_status_string(int status)
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
 * check_cq()
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
static int check_cq(void)
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

        vret = get_one_completion(&wc);
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
                debug(0, "%s: entry id %llu SEND error %s to %s",
                      __func__,
                      llu(wc.id),
                      wc_status_string(wc.status),
                      bh->c->peername);

                if (bh)
                {
                    rdma_connection_t *c = bh->c;
                    if (c->cancelled)
                    {
                        debug(0,
                              "%s: ignoring send error on cancelled conn to %s",
                              __func__, bh->c->peername);
                    }
                    else
                    {
                        debug(0, "%s: send error on non-cancelled conn to %s",
                              __func__, c->peername);

                        sq = bh->sq;
                        if (sq)
                        {
                            sq->state.send = SQ_ERROR;
                            sq->mop->error_code = wc.status;
                        }
                        break;
                    }
                }
            }
            else
            {
                warning("%s: entry id %llu opcode %s error %s from %s",
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
            warning("%s: cq entry id %llu opcode %d unexpected",
                    __func__, llu(wc.id), wc.opcode);
        }
    }

    return ret;
}

/*
 * get_one_completion()
 *
 * Description:
 *  Poll the completion queue for a single completed work request
 *
 * Params:
 *  [out] wc - pointer to work completion, if one was found
 *
 * Returns:
 *  0 if cq empty or if unhandled opcode found, 1 if completion found,
 *  otherwise -errno returned
 */
static int get_one_completion(struct bmi_rdma_wc *wc)
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
 * post_sr()
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
static void post_sr(const struct buf_head *bh,
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
    struct ibv_send_wr sr=
    {
        .next = NULL,
        .wr_id = int64_from_ptr(bh),
        .sg_list = &sg,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,  /* XXX: try unsignaled if possible */
        /* TODO: I thought we were already supposed to be doing unsignaled
         *       sends? (see comment at the top of this file)
         */
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
 * post_sr_rdmaw()
 *
 * Description:
 *  Called only in response to receipt of a CTS on the sender.  Rdma write
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
static void post_sr_rdmaw(struct rdma_work *sq,
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
 * post_rr()
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
static void post_rr(const rdma_connection_t *c,
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
 * repost_rr()
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
static void repost_rr(rdma_connection_t *c,
                      struct buf_head *bh)
{
    post_rr(c, bh);
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
    repost_rr(sq->c, bh);

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
                error("%s: EAGER received %lld too large for buffer %lld",
                      __func__,
                      lld(len),
                      lld(rq->buflist.tot_len));
                return;
            }

            memcpy_to_buflist(&rq->buflist,
                              (msg_header_eager_t *) bh->buf + 1,
                              len);

            /* re-post */
            repost_rr(c, bh);

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
                error("%s: RTS received %llu too large for buffer %llu",
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

        repost_rr(c, bh);

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

        repost_rr(c, bh);

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
        repost_rr(c, bh);
        rdma_close_connection(c);
    }
    else if (type == MSG_CREDIT)
    {
        /* already added the credit in check_cq */
        debug(2, "%s: recv CREDIT", __func__);
        repost_rr(c, bh);
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
    check_cq();

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
        repost_rr(rq->c, rq->bh);

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
    int ret = 0;

    debug(7, "%s (in): sq %p complete %d",
          __func__, sq, complete);

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

            ret = 1;
            goto out;
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
        *err = -BMI_ETIMEDOUT;

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

        ret = 1;
        goto out;
    }
    else if (sq->state.send == SQ_ERROR)
    {
        debug(0, "%s: sq %p found, state %s",
              __func__, sq, sq_state_name(sq->state.send));

        *err = wc_status_to_bmi(sq->mop->error_code);
        if (*err != 0)
        {
            *outid = sq->mop->op_id;

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

            ret = 1;
            goto out;
        }
    }
    else
    {
        debug(7, "%s: sq %p found, not done, state %s",
              __func__,
              sq,
              sq_state_name(sq->state.send));
    }

    ret = 0;

out:

    debug(7, "%s (out): sq %p outid %lld err %s size %lld user_ptr %p",
          __func__,
          sq,
          lld(*outid),
          bmi_status_string(*err),
          lld(*size),
          *user_ptr);

    return ret;
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
    int ret = 0;

    debug(7, "%s: rq %p complete %d", __func__, rq, complete);

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

            ret = 1;
            goto out;
        }
        /* this state needs help, push it (ideally would be triggered
         * when the resource is freed...) XXX */
    }
    else if (rq->state.recv == RQ_RTS_WAITING_CTS_BUFFER)
    {
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
        *err = -BMI_ETIMEDOUT;

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

        ret = 1;
        goto out;
    }
    else if (rq->state.recv == RQ_ERROR)
    {
        debug(0, "%s: rq %p found, state %s",
              __func__, rq, rq_state_name(rq->state.recv));

        if (rq->mop)
        {
            *err = wc_status_to_bmi(rq->mop->error_code);
            if (*err != 0)
            {
                *outid = rq->mop->op_id;

                if (user_ptr)
                {
                    *user_ptr = rq->mop->user_ptr;
                }

                qlist_del(&rq->list);
                id_gen_fast_unregister(rq->mop->op_id);
                c = rq->c;
                free(rq->mop);
                free(rq);
                --c->refcnt;

                ret = 1;
                goto out;
            }
        }
    }
    else
    {
        debug(7, "%s: rq %p found, not done, state %s",
              __func__,
              rq,
              rq_state_name(rq->state.recv));
    }

    ret = 0;

out:

    debug(7, "%s (out): rq %p outid %lld err %s size %lld user_ptr %p",
          __func__,
          rq,
          lld(*outid),
          bmi_status_string(*err),
          lld(*size),
          *user_ptr);

    return ret;
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
    activity += check_cq();

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
    activity += check_cq();

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
            debug(2, "%s: calling repost_rr", __func__);
            repost_rr(c, rq->bh);
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
 * TODO: if these are unused can we just remove them? (see query_addr_range)
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
    check_cq();
    mop = id_gen_fast_lookup(id);
    tsq = mop->method_data;

    if (tsq)
    {
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

            debug(0, "%s: sq %p id %lld cancelling op", __func__, tsq, lld(id));
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

            debug(0, "%s: rq %p id %lld cancelling op", __func__, rq, lld(id));
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
        struct rdma_connection_priv *rc = c->priv;

        c->cancelled = 1;
        rdma_disconnect(rc->id);
        /*
         * TODO: does rdma_disconnect() accomplish everything that was
         * previously handled in rdma_drain_qp()?
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
 *  Build and fill an RDMA-specific method_addr structure.
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
 *  TODO: split into build connection and establish connection?
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
    rdma_connection_t *c = NULL;
    struct rdma_connection_priv *rc = NULL;
    struct rdma_device_priv *rd = rdma_device->priv;
    struct ibv_qp_init_attr attr;
    int i;
    int ret;
    int num_wr = 0;

    if (is_server)
    {
        debug(4, "%s: [SERVER] starting, peername=%s, channel=%d",
              __func__, peername, id->channel->fd);
    }
    else
    {
        debug(4, "%s: [CLIENT] starting, peername=%s, channel=%d",
              __func__, peername, id->channel->fd);
    }

    c = alloc_connection(peername);

    /* build connection priv */
    rc = bmi_rdma_malloc(sizeof(*rc));
    c->priv = rc;
    rc->id = id;

    ret = register_memory(c);
    if (ret)
    {
        goto error_out;
    }

    /* create the main queue pair */
    debug(0, "%s: creating main queue pair", __func__);
    build_qp_init_attr(&num_wr, &attr);

    /* NOTE: rdma_create_qp() automatically transitions the QP through its
     * states; after allocation it is ready to post receives
     */
    debug(0, "%s: calling rdma_create_qp", __func__);
    ret = rdma_create_qp(id, rd->nic_pd, &attr);
    if (ret)
    {
        error("%s: rdma_create_qp failed", __func__);
        goto error_out;
    }

    /* TODO: is there anything that was previously taken care of in
     * init_connection_modify_qp() that still needs to be donw?
     *    - the local ack timeout (previously attr.timeout = 14 in
     *      init_connection_modify_qp) is set from the subnet_timeout value
     *      from opensm when using rdma_cm. It is currently set to 18. You
     *      can use the command opensm -c <config-file> to dump current
     *      opensm configuration to a file.
     */

    rc->qp = id->qp;

    VALGRIND_MAKE_MEM_DEFINED(&attr, sizeof(&attr));
    VALGRIND_MAKE_MEM_DEFINED(&rc->qp->qp_num, sizeof(rc->qp->qp_num));

    ret = verify_qp_caps(attr, num_wr);
    if (ret)
    {
        goto error_out;
    }

    /* post initial RRs and RRs for acks */
    debug(0, "%s: entering for loop for post_rr", __func__);
    for (i = 0; i < rdma_device->eager_buf_num; i++)
    {
        post_rr(c, &c->eager_recv_buf_head_contig[i]);
    }

    return c;

error_out:

    rdma_close_connection(c);
    c = NULL;
    return NULL;
}

/*
 * alloc_connection()
 *
 * Description:
 *  Allocate and initialize a new connection structure.
 *
 * Params:
 *  [in] peername - remote peername for the connection
 *
 * Returns:
 *  Pointer to new connection.
 */
static rdma_connection_t *alloc_connection(const char *peername)
{
    rdma_connection_t *c = bmi_rdma_malloc(sizeof(*c));
    c->peername = strdup(peername);

    alloc_eager_bufs(c);

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

    return c;
}

/*
 * alloc_eager_bufs()
 *
 * Description:
 *  Fill send and recv free lists and buf heads
 *
 * Params:
 *  [in/out] c - connection new buffers will belong to
 *
 * Returns:
 *  none
 */
static void alloc_eager_bufs(rdma_connection_t *c)
{
    int i = 0;

    c->eager_send_buf_contig = bmi_rdma_malloc(rdma_device->eager_buf_num *
                                               rdma_device->eager_buf_size);
    c->eager_recv_buf_contig = bmi_rdma_malloc(rdma_device->eager_buf_num *
                                               rdma_device->eager_buf_size);
    INIT_QLIST_HEAD(&c->eager_send_buf_free);
    INIT_QLIST_HEAD(&c->eager_recv_buf_free);

    c->eager_send_buf_head_contig = bmi_rdma_malloc(rdma_device->eager_buf_num *
                                        sizeof(*c->eager_send_buf_head_contig));
    c->eager_recv_buf_head_contig = bmi_rdma_malloc(rdma_device->eager_buf_num *
                                        sizeof(*c->eager_recv_buf_head_contig));

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
}

/*
 * register_memory()
 *
 * Description:
 *  Register eager send and recv memory regions.
 *
 * Params:
 *  [in/out] c - connection the memory regions will belong to
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int register_memory(rdma_connection_t *c)
{
    struct rdma_connection_priv *rc = c->priv;
    struct rdma_device_priv *rd = rdma_device->priv;
    size_t len;
    int access_flags;

    len = rdma_device->eager_buf_num * rdma_device->eager_buf_size;
    access_flags = IBV_ACCESS_LOCAL_WRITE
                       | IBV_ACCESS_REMOTE_WRITE
                       | IBV_ACCESS_REMOTE_READ;

    /* register memory region, Recv side */
    debug(0, "%s: calling ibv_reg_mr recv side", __func__);
    rc->eager_recv_mr = ibv_reg_mr(rd->nic_pd,
                                   c->eager_recv_buf_contig,
                                   len,
                                   access_flags);
    if (!rc->eager_recv_mr)
    {
        error("%s: ibv_reg_mr eager recv failed", __func__);
        return -ENOMEM;
    }

    /* register memory region, Send side */
    debug(0, "%s: calling ibv_reg_mr send side", __func__);
    rc->eager_send_mr = ibv_reg_mr(rd->nic_pd,
                                   c->eager_send_buf_contig,
                                   len,
                                   access_flags);
    if (!rc->eager_send_mr)
    {
        error("%s: ibv_reg_mr eager send failed", __func__);
        return -ENOMEM;
    }

    return 0;
}

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
 * verify_qp_caps()
 *
 * Description:
 *  Checks the capabilities returned by rdma_create_qp() to make sure we got
 *  at least what we asked for. If the device capabilities haven't been set
 *  yet, they will be set according to the attributes of the new QP.
 *  Otherwise, the new QP's attributes will be checked against the device
 *  capabilities we already have.
 *
 * Params:
 *  [in] attr   - attributes of the newly created QP
 *  [in] num_wr - requested number of work requests
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int verify_qp_caps(struct ibv_qp_init_attr attr, int num_wr)
{
    struct rdma_device_priv *rd = rdma_device->priv;

    if (rd->sg_max_len == 0)
    {
        /* set device capabilities */
        rd->sg_max_len = attr.cap.max_send_sge;
        if (attr.cap.max_recv_sge < rd->sg_max_len)
        {
            rd->sg_max_len = attr.cap.max_recv_sge;
        }

        rd->sg_tmp_array = bmi_rdma_malloc(rd->sg_max_len *
                                           sizeof(*rd->sg_tmp_array));
    }
    else
    {
        /* compare the caps that came back against what we already have */
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
        /* set device capabilities */
        rd->max_unsignaled_sends = attr.cap.max_send_wr;
    }
    else
    {
        /* compare the caps that came back against what we already have */
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

    return 0;
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
//{

/*
 * rdma_close_connection()
 *
 * Description:
 *  Try to close and free a connection, but only do it if refcnt has
 *  gone to zero. Called in response to an explicit BYE message or
 *  at finalize time.
 *
 * Params:
 *  [in/out] c - pointer to connection to close
 *
 * Returns:
 *  none
 */
static void rdma_close_connection(rdma_connection_t *c)
{
    int ret = 0;
    struct rdma_connection_priv *rc = c->priv;
    struct rdma_cm_event *event = NULL;
    struct rdma_cm_event event_copy;
    struct rdma_event_channel *channel = NULL;

    debug(2, "%s: closing connection to %s", __func__, c->peername);
    c->closed = 1;
    if (c->refcnt != 0)
    {
        debug(1, "%s: refcnt non-zero %d, delaying free", __func__, c->refcnt);
        return;
    }

    /* disconnect (also transfers associated QP to error state */
    //rdma_disconnect(rc->id);

#if 0
    /* TODO: is this loop necessary? do we need to wait for the event? */
    /* wait for disconnected event */
    while (rdma_get_cm_event(rc->id->channel, &event) == 0)
    {
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        /* TODO: I think maybe I should be checking for this in the
         *       server_accept_thread and client_event_loop instead */
        if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED)
        {
            break;
        }

        /* TODO: should I compare event_copy.id and rc->id to make sure
         *       what we expected is actually what disconnected?
         */
    }
#endif

    /* destroy the queue pair */
    if (rc->qp)
    {
        rdma_destroy_qp(rc->id);
        if (rc->qp != NULL)
        {
            error("%s: rdma_destroy_qp failed", __func__);
        }
    }

    /* destroy the memory regions */
    if (rc->eager_send_mr)
    {
        ret = ibv_dereg_mr(rc->eager_send_mr);
        if (ret)
        {
            error_xerrno(ret, "%s: ibv_dereg_mr eager send failed", __func__);
        }
    }

    if (rc->eager_recv_mr)
    {
        ret = ibv_dereg_mr(rc->eager_recv_mr);
        if (ret)
        {
            error_xerrno(ret, "%s: ibv_dereg_mr eager recv failed", __func__);
        }
    }

    /* destroy the id and event channel */
    if (rc->id)
    {
        channel = rc->id->channel;
        
        gossip_err("%s: destroying id=%llu, fd=%d\n",
                   __func__,
                   llu(int64_from_ptr(rc->id)),
                   channel->fd);

        ret = rdma_destroy_id(rc->id);
        if (ret < 0)
        {
            error_errno("%s: rdma_destroy_id failed", __func__);
        }

        //rdma_destroy_event_channel(channel);
        //channel = NULL;
    }

    free(rc);

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
    struct rdma_cm_event event_copy;
    struct rdma_conn_param conn_param;
    char peername[2048];
    int ret = -1;
    static int already_printed = 0;
    rdma_connection_t *c = NULL;
    int num_retries = 0;    /* number of retries for rdma_resolve_route() */

    while (rdma_get_cm_event(ec, &event) == 0)
    {
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED)
        {
            /* TODO: does the rdma_resolve_route() call need to be after
             *       I have registered the memory regions, created the queue
             *       pair, and posted receives? */
retry_resolve:
            ret = rdma_resolve_route(event_copy.id, timeout_ms);
            if (ret)
            {
                warning("%s: cannot resolve RDMA route to dest: %s",
                        __func__, strerror(errno));
                return -errno;
            }
        }
        else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED)
        {
            sprintf(peername,
                    "%s:%d",
                    inet_ntoa(event_copy.id->route.addr.dst_sin.sin_addr),
                    rdma_map->port);

            debug(0,
                "%s: calling rdma_new_connection for peername=%s on channel=%d",
                __func__, peername, event_copy.id->channel->fd);
            
            rdma_map->c = rdma_new_connection(event_copy.id, peername, 0);
            if (!rdma_map->c)
            {
                error_xerrno(EINVAL,
                             "%s: rdma_new_connection failed", __func__);
                return -EINVAL; /* TODO: more appropriate error? */
            }
            rdma_map->c->remote_map = remote_map;

            /* associate the new connection with the id */
            event_copy.id->context = (void *) rdma_map->c;

            debug(0, "%s: returned from rdma_new_connection", __func__);

            /* try to connect to the server */
            build_conn_params(&conn_param);

retry_connect:
            ret = rdma_connect(event_copy.id, &conn_param);
            if (ret)
            {
                if (errno == EINTR)
                {
                    goto retry_connect;
                }
                else
                {
                    warning("%s: connect to server %s: %s",
                            __func__, peername, strerror(errno));
                    rdma_close_connection(rdma_map->c);
                    rdma_map->c = NULL;
                    return -EINVAL; /* TODO: more appropriate error? */
                }
            }
        }
        else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
        {
            /* this will happen after the server calls rdma_accept() */
            debug(4, "%s: received event: %s",
                  __func__, rdma_event_str(event_copy.event));

            c = (rdma_connection_t *) event_copy.id->context;
            /* TODO: make sure c == rdma_map->c? */

            debug(4, "%s: connected, peername=%s", __func__, c->peername);
            break;
        }
        else if (event_copy.event == RDMA_CM_EVENT_ADDR_ERROR)
        {
            error("%s: got event %s.",
                  __func__, rdma_event_str(event_copy.event));

            if (!already_printed)
            {
                already_printed = 1;
                gossip_err("NOTE: This event means there was a problem "
                           "resolving a server address. If your servers have "
                           "both ethernet and IB interfaces, make sure you "
                           "are using the IB device name in your config and "
                           "tab files.");
            }

            return -BMI_EHOSTNTFD;
        }
        else if (event_copy.event == RDMA_CM_EVENT_ROUTE_ERROR)
        {
            error("%s: got event %s: status=%d (%s)",
                  __func__,
                  rdma_event_str(event_copy.event),
                  event_copy.status,
                  strerror(-event_copy.status));

            while (num_retries < 10)
            {
                warning("%s: retrying rdma_resolve_route()", __func__);
                num_retries++;
                goto retry_resolve;
            }

            return event_copy.status;
        }
        else
        {
            error("%s: rdma_get_cm_event() found unhandled event %s",
                  __func__, rdma_event_str(event_copy.event));
        }

        /* TODO: retry to connect if RDMA_CM_EVENT_REJECTED? */
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
    int timeout_ms = 1000;   /* TODO: choose optimized timeout value */

    debug(4, "%s: starting", __func__);

    /*
     * Convert the port number to a string.
     * NOTE: The first snprintf() call returns the number of characters in
     * the string representation of the port number. The second call
     * actually writes the port number into the string.
     */
    port_str_len = snprintf(NULL, 0, "%d", rdma_map->port);
    port_str = bmi_rdma_malloc(port_str_len + 1);
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

    gossip_err("%s: new id=%llu, fd=%d\n",
               __func__, llu(int64_from_ptr(conn_id)), conn_id->channel->fd); 

    ret = rdma_resolve_addr(conn_id, NULL, addrinfo->ai_dst_addr, timeout_ms);
    if (ret)
    {
        warning("%s: cannot resolve dest IP to RDMA address: %m", __func__);
        ret = -errno;
        goto error_out;
    }

    rdma_freeaddrinfo(addrinfo);
    addrinfo = NULL;

    ret = rdma_client_event_loop(ec, rdma_map, remote_map, timeout_ms);
    if (ret)
    {
        goto error_out;
    }

    return 0;

error_out:

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

    if (addrinfo)
    {
        rdma_freeaddrinfo(addrinfo);
        addrinfo = NULL;
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

    gossip_err("%s: listen_id=%llu, fd=%d\n",
               __func__,
               llu(int64_from_ptr(rdma_device->listen_id)),
               rdma_device->listen_id->channel->fd);

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

/* TODO: why doesn't it work asynchronously? */
/* 
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
*/
    timeout_ms = (int *) bmi_rdma_malloc(sizeof(int));
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
    struct rdma_cm_event event_copy;
    rdma_connection_t *c = NULL;
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
        /* TODO: will this block? should it? should I use a while loop? */
        ret = rdma_get_cm_event(rdma_device->listen_id->channel, &event);
        if (ret)
        {
            error_errno("%s: rdma_get_cm_event failed", __func__);
            continue;
        }

        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST)
        {
            debug(4, "%s: received event: %s",
                  __func__, rdma_event_str(event_copy.event));

#if 1 /* TODO: can we get rid of rc? */
            rc = (struct rdma_conn *) bmi_rdma_malloc(sizeof(*rc));
            if (!rc)
            {
                warning("%s: unable to malloc rc, errno=%d",
                        __func__, errno);
                sleep(30);
                continue;
            }

            rc->id = event_copy.id;     /* id for new connection */
            rc->hostname = strdup(
                            inet_ntoa(rc->id->route.addr.dst_sin.sin_addr));
            rc->port = ntohs(rc->id->route.addr.dst_sin.sin_port);
            sprintf(rc->peername, "%s:%d", rc->hostname, rc->port);

            debug(0,
                "%s: starting rdma_server_process_client_thread for channel=%d",
                __func__, rc->id->channel->fd);

            /* start the client thread */
            if (pthread_create(&thread,
                               NULL,
                               &rdma_server_process_client_thread,
                               rc))
            {
                warning("%s: unable to create process_client thread, errno=%d",
                        __func__, errno);
                free(rc);
            }
#else
//            conn_id = (struct rdma_cm_id *) bmi_rdma_malloc(sizeof(*conn_id));
//            if (!conn_id)
//            {
//                warning("%s: unable to malloc conn_id, errno=%d",
//                        __func__, errno);
//                sleep(30);
//                continue;
//            }
//
//            conn_id = event_copy.id;
//
//            debug(0,
//                "%s: starting rdma_server_process_client_thread for channel=%d",
//                __func__, conn_id->channel->fd);
//
//            /* start the client thread */
//            if (pthread_create(&thread,
//                               NULL,
//                               &rdma_server_process_client_thread,
//                               conn_id))
//            {
//                warning("%s: unable to create accept_client thread, errno=%d",
//                        __func__, errno);
//                free(conn_id);
//            }
#endif
        }
        else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
        {
            /* this will happen after rdma_accept() has been called
             * from rdma_server_process_client_thread */

            debug(4, "%s: received event: %s",
                  __func__, rdma_event_str(event_copy.event));

            c = (rdma_connection_t *) event_copy.id->context;

            /* don't set reconnect flag on this addr; we are a server in this
             * case and the peer will be responsible for maintaining the
             * connection
             */
            //c->remote_map = rdma_alloc_method_addr(c, rc->hostname, rc->port, 0);
            /* TODO: THIS IS ONLY TEMPORARY! Need to move the info stored
             *       in struct rdma_conn into rdma_connection_t */
            c->remote_map = rdma_alloc_method_addr(c,
                        inet_ntoa(event_copy.id->route.addr.dst_sin.sin_addr),
                        ntohs(event_copy.id->route.addr.dst_sin.sin_port),
                        0);

            /* register this address with the method control layer */
            c->bmi_addr = bmi_method_addr_reg_callback(c->remote_map);
            if (c->bmi_addr == 0)
            {
                error_xerrno(ENOMEM, "%s: bmi_method_addr_reg_callback",
                             __func__);
                /* TODO: break, cleanup, return null? */
                break;
            }

            debug(0, "%s: accepted new connection from %s at server",
                  __func__, c->peername);

            /* TODO: cleanup rc? */
        }
        else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED)
        {
            debug(4, "%s: received event: %s",
                  __func__, rdma_event_str(event_copy.event));

            /* TODO: something... */
        }
        else
        {
            error("%s: unexpected event: %s",
                  __func__, rdma_event_str(event_copy.event));
            continue;
        }

        /* TODO: do I need to handle a disconnect event here, too? */
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
        error_xerrno(EINVAL, "%s: rdma_new_connection failed", __func__);
        goto out;
    }
    debug(0, "%s: returned from rdma_new_connection", __func__);

    /* associate the new connection with the id */
    rc->id->context = (void *) c;

    /* TODO: do I need to build the connection/context here? */
    //struct rdma_conn_param conn_param;
    //build_conn_params(&conn_param);

    /* TODO: do I need to do pre-connection stuff here? */

    /* TODO: do I need to pass any connection parameters? */
    //ret = rdma_accept(rc->id, &conn_param);
    ret = rdma_accept(rc->id, NULL);
    if (ret)
    {
        warning("%s: rdma_accept(): %s", __func__, strerror(errno));
        free(rc);       /* TODO: do this here? */
    }

out:
    gen_mutex_unlock(&interface_mutex);

    /* TODO: should I free rc and rc->hostname here? are they still needed?
     *       what about the id? how long is an rdma_cm_id needed? when do I
     *       destroy it?
     */

    return NULL;
}

/*
 * rdma_block_for_activity()
 *
 * Description:
 *  Ask the device to write to its FD if a CQ event happens, and poll on it
 *  as well as the listen_id for activity, but do not actually respond to
 *  anything.  A later check_cq will handle CQ events, and a later call to
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
 * prepare_cq_block()
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
static void prepare_cq_block(int *cq_fd,
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
 * ack_cq_completion_event()
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
static void ack_cq_completion_event(void)
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

/*
 * check_async_events()
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
static int check_async_events(void)
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
 * BMI_rdma_memalloc()
 *
 * Description:
 *  Wrapper for memcache_memalloc(), which retrieves a free memory buffer
 *  or allocates a new memcache entry if no buffers of the desired length
 *  are available. If this is the first reference to the memcache entry, then
 *  the memory region is pinned.
 *
 * Params:
 *  [in] len - length of buffer to retrieve/allocate
 *
 * Returns:
 *  Pointer to buffer
 */
static void *BMI_rdma_memalloc(bmi_size_t len,
                               enum bmi_op_type send_recv __unused)
{
    return memcache_memalloc(rdma_device->memcache,
                             len,
                             rdma_device->eager_buf_payload);
}

/*
 * BMI_rdma_memfree()
 *
 * Description:
 *  Wrapper for memcache_memfree(), which frees a memory buffer. If the buffer
 *  belongs to a memcache entry, the entry is removed from the memcache and
 *  returned to the free chunk list. If this was the last reference to the
 *  memcache entry, then the memory region is unpinned.
 *
 * Params:
 *  [in] buf - buffer to be freed/deregistered
 *  [in] len - length of the buffer
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int BMI_rdma_memfree(void *buf,
                            bmi_size_t len,
                            enum bmi_op_type send_recv __unused)
{
    return memcache_memfree(rdma_device->memcache, buf, len);
}

/*
 * BMI_rdma_unexpected_free()
 *
 * Description:
 *  Frees an unexpected buffer.
 *
 * Params:
 *  [in] buf - buffer to be freed
 *
 * Returns:
 *  0
 */
static int BMI_rdma_unexpected_free(void *buf)
{
    free(buf);
    return 0;
}

/*
 * mem_register(), mem_deregister()
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
static int mem_register(memcache_entry_t *c)
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

static void mem_deregister(memcache_entry_t *c)
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
 *  [-] param   - unused (TODO: why is this "__unused"?? Looks used to me.)
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
        error("%s: BMI_INIT_SERVER requires non-null listen_addr and v.v",
              __func__);
        exit(1);
    }

    bmi_rdma_method_id = method_id;

    rdma_device = bmi_rdma_malloc(sizeof(*rdma_device));
    if (!rdma_device)
    {
        return bmi_errno_to_pvfs(-ENOMEM);
    }

    ret = build_rdma_context();
    if (ret)
    {
        gen_mutex_unlock(&interface_mutex);
        return bmi_errno_to_pvfs(-BMI_ENODEV);
    }

    /* initialize memcache */
    rdma_device->memcache = memcache_init(mem_register,
                                          mem_deregister);

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
 * build_rdma_context()
 *
 * Description:
 *  Called as part of the initialization process for the BMI RDMA module.
 *  Builds and initializes the RDMA device context, as well as the  data
 *  structures that are associated with the device context and are required
 *  for RDMA communication to occur (i.e. protection domain, completion
 *  channel, completion queue).
 *
 * Params:
 *  none
 *
 * Returns:
 *  0 on success, -errno on failure
 */
static int build_rdma_context(void)
{
    int flags, ret = 0;
    //struct ibv_device *nic_handle;
    //struct ibv_context *ctx;
    int cqe_num;    /* local variables, mainly for debug */
    struct rdma_device_priv *rd;
    struct ibv_port_attr hca_port;  /* TODO: compatible? */
    struct ibv_device_attr hca_cap;

    rd = bmi_rdma_malloc(sizeof(*rd));
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

    /* TODO: ibv_req_notify_cq? */

    /* Use non-blocking IO on the async fd and completion fd */
    flags = fcntl(rd->ctx->async_fd, F_GETFL);
    if (flags < 0)
    {
        error_errno("%s: set async fd nonblocking", __func__);
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
 * return_active_nic_handle()
 *
 * Description:
 *  This function returns the first active HCA device which returns a
 *  valid IBV_PORT_ACTIVE.
 *
 * Params:
 *  [out] rd - preallocated from build_rdma_context(); rd->ctx is allocated
 *             by rdma_get_devices() inside this function
 *  [out] hca_port - hcas port attributes
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
        error("%s: NO RDMA DEVICES FOUND", __func__);
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
    struct rdma_connection_priv *rc = NULL;

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

            rc = c->priv;

            /* Send BYE message to servers, then disconnect */
            send_bye(c);
            rdma_disconnect(rc->id);
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
        gossip_err("%s: destroying id=%llu, fd=%d\n",
                   __func__,
                   llu(int64_from_ptr(rdma_device->listen_id)),
                   rdma_device->listen_id->channel->fd);
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

    cleanup_rdma_context();

    free(rdma_device);
    rdma_device = NULL;

    gen_mutex_unlock(&interface_mutex);
    debug(0, "BMI_rdma_finalize: RDMA module finalized.");
    return 0;
}

/*
 * cleanup_rdma_context()
 *
 * Description:
 *  Destroy/deallocate the data structures that are associated with the
 *  RDMA device context.
 *
 * Params:
 *  none
 *
 * Returns:
 *  none
 */
static void cleanup_rdma_context(void)
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

/* exported method interface */
const struct bmi_method_ops bmi_rdma_ops =
{
    .method_name = "bmi_rdma",
    .flags = 0,
    .initialize = BMI_rdma_initialize,
    .finalize = BMI_rdma_finalize,
    .set_info = BMI_rdma_set_info,
    .get_info = BMI_rdma_get_info,
    .memalloc = BMI_rdma_memalloc,
    .memfree = BMI_rdma_memfree,
    .unexpected_free = BMI_rdma_unexpected_free,
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

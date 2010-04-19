/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *   Copyright (C) 2007 Myricom, Inc.
 *   Author: Myricom, Inc. <help at myri.com>
 *
 *   See COPYING in top-level directory.
 */

#include "mx.h"
#include "pint-hint.h"
#include "pint-event.h"
#include "pvfs2-debug.h"


static int      tmp_id  = 0;    /* temporary id until bmi_mx is init'ed */
struct bmx_data *bmi_mx = NULL; /* global state for bmi_mx */

mx_status_t     BMX_NO_STATUS;

#if BMX_MEM_ACCT
uint64_t        mem_used = 0;   /* bytes used */
gen_mutex_t     mem_used_lock;  /* lock */
#endif

/* statics for event logging */
static PINT_event_type bmi_mx_send_event_id;
static PINT_event_type bmi_mx_recv_event_id;

static PINT_event_group bmi_mx_event_group;
static pid_t bmi_mx_pid;

mx_unexp_handler_action_t
bmx_unexpected_recv(void *context, mx_endpoint_addr_t source,
                      uint64_t match_value, uint32_t length, void *data_if_available);

static int
bmx_peer_connect(struct bmx_peer *peer);
static void
bmx_create_peername(void);

/**** Completion function token handling ****************************/
/* We should not hold any locks when calling mx_test[_any](),
 * mx_wait_any() or mx_cancel(). We want to avoid races between them,
 * however. So, before calling any completion function, the caller
 * must hold this token.  These functions implement a token system (i.e.
 * semaphore) that will wake up mx_wait_any() to reduce blocking times
 * for the calling function.
 */

static void
bmx_get_completion_token(void)
{
        int     done    = 0;

        do {
                gen_mutex_lock(&bmi_mx->bmx_completion_lock);
                if (bmi_mx->bmx_refcount == 1) {
                        bmi_mx->bmx_refcount--;
                        done = 1;
                        gen_mutex_unlock(&bmi_mx->bmx_completion_lock);
                } else {
                        assert(bmi_mx->bmx_refcount == 0);
                        /* someone has the lock, wake the MX endpoint in
                         * case they are blocking in mx_wait_any() */
                        gen_mutex_unlock(&bmi_mx->bmx_completion_lock);
                        mx_wakeup(bmi_mx->bmx_ep);
                }
        } while (!done);

        return;
}

static void
bmx_release_completion_token(void)
{
        gen_mutex_lock(&bmi_mx->bmx_completion_lock);
        bmi_mx->bmx_refcount++;
        assert(bmi_mx->bmx_refcount == 1);
        gen_mutex_unlock(&bmi_mx->bmx_completion_lock);
        return;
}

/**** TX/RX handling functions **************************************/

static void
bmx_ctx_free(struct bmx_ctx *ctx)
{
        if (ctx == NULL) return;

        if (!qlist_empty(&ctx->mxc_global_list))
                qlist_del_init(&ctx->mxc_global_list);

        if (!qlist_empty(&ctx->mxc_list))
                qlist_del_init(&ctx->mxc_list);

        if (ctx->mxc_buffer != NULL) {
                BMX_FREE(ctx->mxc_buffer, BMX_UNEXPECTED_SIZE);
        }
        if (ctx->mxc_seg_list != NULL) {
                if (ctx->mxc_nseg > 0) {
                        BMX_FREE(ctx->mxc_seg_list, ctx->mxc_nseg * sizeof(void *));
                }
        }
        BMX_FREE(ctx, sizeof(*ctx));
        return;
}

static int
bmx_ctx_alloc(struct bmx_ctx **ctxp, enum bmx_req_type type)
{
        struct bmx_ctx *ctx     = NULL;

        if (ctxp == NULL) return -BMI_EINVAL;

        BMX_MALLOC(ctx, sizeof(*ctx));
        if (ctx == NULL) return -BMI_ENOMEM;

        ctx->mxc_type = type;
        ctx->mxc_state = BMX_CTX_INIT;
        /* ctx->mxc_msg_type */

        INIT_QLIST_HEAD(&ctx->mxc_global_list);
        INIT_QLIST_HEAD(&ctx->mxc_list);

        /* ctx->mxc_mop */
        /* ctx->mxc_peer */
        /* ctx->mxc_tag */

        ctx->mxc_seg.segment_ptr = ctx->mxc_buffer;
        /* ctx->mxc_seg.segment_length */
        /* only server recv unexpected messages */
        if (bmi_mx->bmx_is_server == 1 && type == BMX_REQ_RX) {
#if BMX_MEM_TWEAK
                int i = 0;
                for (i = 0; i < BMX_UNEXPECTED_NUM; i++) {
                        struct bmx_buffer *buf  = NULL;
                        BMX_MALLOC(buf, sizeof(*buf));
                        if (buf) {
                                INIT_QLIST_HEAD(&buf->mxb_list);
                                BMX_MALLOC(buf->mxb_buffer, BMX_UNEXPECTED_SIZE);
                                if (buf->mxb_buffer) {
                                        if (i == 0) {
                                                ctx->mxc_buffer = buf->mxb_buffer;
                                                gen_mutex_lock(&bmi_mx->bmx_used_unex_buffers_lock);
                                                qlist_add(&buf->mxb_list,
                                                          &bmi_mx->bmx_used_unex_buffers);
                                                gen_mutex_unlock(&bmi_mx->bmx_used_unex_buffers_lock);
                                        } else {
                                                gen_mutex_lock(&bmi_mx->bmx_idle_unex_buffers_lock);
                                                qlist_add(&buf->mxb_list,
                                                          &bmi_mx->bmx_idle_unex_buffers);
                                                gen_mutex_unlock(&bmi_mx->bmx_idle_unex_buffers_lock);
                                        }
                                } else {
                                        BMX_FREE(buf, sizeof(*buf));
                                }
                        }
                }
#else
                BMX_MALLOC(ctx->mxc_buffer, BMX_UNEXPECTED_SIZE);
                if (ctx->mxc_buffer == NULL) {
                        bmx_ctx_free(ctx);
                        return -BMI_ENOMEM;
                }
#endif
        }
        /* ctx->mxc_seg_list */
        /* ctx->mxc_nseg */
        /* ctx->mxc_nob */
        /* ctx->mxc_mxreq */
        /* ctx->mxc_mxstat */

        ctx->mxc_get = 1; /* put_idle_ctx() will increment the put */
        /* ctx->mxc_put */

        if (type == BMX_REQ_TX) {
                gen_mutex_lock(&bmi_mx->bmx_lock);
                qlist_add_tail(&ctx->mxc_global_list, &bmi_mx->bmx_txs);
                gen_mutex_unlock(&bmi_mx->bmx_lock);
        } else {
                gen_mutex_lock(&bmi_mx->bmx_lock);
                qlist_add_tail(&ctx->mxc_global_list, &bmi_mx->bmx_rxs);
                gen_mutex_unlock(&bmi_mx->bmx_lock);
        }

        *ctxp = ctx;
        return 0;
}

static void
bmx_ctx_init(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer   = NULL;

        BMX_ENTER;

        if (ctx == NULL) return;

        peer = ctx->mxc_peer;

        /* ctx->mxc_type */
        ctx->mxc_state = BMX_CTX_IDLE;
        /* ctx->mxc_msg_type */
        
        /* ctx->mxc_global_list */
        if (!qlist_empty(&ctx->mxc_list)) {
                debug(BMX_DB_ERR, "%s %s still on a list", __func__,
                      ctx->mxc_type == BMX_REQ_TX ? "tx" : "rx");
                exit(1);
        }

        ctx->mxc_mop = NULL;
        ctx->mxc_peer = NULL;
        ctx->mxc_tag = 0;
        ctx->mxc_match = 0ULL;

        if (ctx->mxc_type == BMX_REQ_TX && ctx->mxc_buffer != NULL && ctx->mxc_nseg == 1) {
                BMX_FREE(ctx->mxc_buffer, ctx->mxc_seg.segment_length);
                ctx->mxc_buffer = NULL;
        }
        ctx->mxc_seg.segment_ptr = ctx->mxc_buffer;
        ctx->mxc_seg.segment_length = 0;
        if (ctx->mxc_seg_list != NULL) {
                if (ctx->mxc_nseg > 0) {
                        BMX_FREE(ctx->mxc_seg_list, ctx->mxc_nseg * sizeof(void *));
                }
                ctx->mxc_seg_list = NULL;
        }
        ctx->mxc_nseg = 0;
        ctx->mxc_nob = 0LL;
        ctx->mxc_mxreq = NULL;
        memset(&ctx->mxc_mxstat, 0, sizeof(mx_status_t));

        /* ctx->mxc_get */
        /* ctx->mxc_put */

        BMX_EXIT;
        return;
}

/* add to peer's queued txs/rxs list */
static void
bmx_q_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;
        list_t          *queue = ctx->mxc_type == BMX_REQ_TX ? &peer->mxp_queued_txs :
                                                              &peer->mxp_queued_rxs;

        BMX_ENTER;
        ctx->mxc_state = BMX_CTX_QUEUED;
        gen_mutex_lock(&peer->mxp_lock);
        qlist_add_tail(&ctx->mxc_list, queue);
        gen_mutex_unlock(&peer->mxp_lock);
        BMX_EXIT;
        return;
}

/* add to peer's pending rxs list */
static void
bmx_q_pending_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;

        BMX_ENTER;
        ctx->mxc_state = BMX_CTX_PENDING;
        if (ctx->mxc_type == BMX_REQ_RX) {
                if (peer) {
                        gen_mutex_lock(&peer->mxp_lock);
                        qlist_add_tail(&ctx->mxc_list, &peer->mxp_pending_rxs);
                        gen_mutex_unlock(&peer->mxp_lock);
                }
        }
        BMX_EXIT;
        return;
}

/* remove from peer's pending rxs list */
static void
bmx_deq_pending_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;

        BMX_ENTER;
        if (ctx->mxc_state == BMX_CTX_PENDING) {
                ctx->mxc_state = BMX_CTX_COMPLETED;
        }
        if (ctx->mxc_type == BMX_REQ_RX) {
                if (peer && !qlist_empty(&ctx->mxc_list)) {
                        gen_mutex_lock(&peer->mxp_lock);
                        qlist_del_init(&ctx->mxc_list);
                        gen_mutex_unlock(&peer->mxp_lock);
                }
        }
        BMX_EXIT;
        return;
}

/* dequeue from unexpected rx list */
static void
bmx_deq_unex_rx(struct bmx_ctx **rxp)
{
        struct bmx_ctx  *rx     = NULL;
        list_t          *list   = &bmi_mx->bmx_unex_rxs;

        BMX_ENTER;
        gen_mutex_lock(&bmi_mx->bmx_unex_rxs_lock);
        if (!qlist_empty(list)) {
                rx = qlist_entry(list->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&rx->mxc_list);
        }
        gen_mutex_unlock(&bmi_mx->bmx_unex_rxs_lock);
        *rxp = rx;

        BMX_EXIT;
        return;
}

/* add to the completion queue for the appropriate context */
static void
bmx_q_completed(struct bmx_ctx *ctx, enum bmx_ctx_state state,
                mx_status_t status, bmi_error_code_t error)
{
        int             id      = 0;
        gen_mutex_t     *lock   = NULL;
        list_t          *list   = NULL;

        BMX_ENTER;

        ctx->mxc_state = state;
        ctx->mxc_mxstat = status;
        ctx->mxc_error = error < 0 ? error : -error;

        if (ctx->mxc_type == BMX_REQ_RX &&
            ctx->mxc_msg_type == BMX_MSG_UNEXPECTED) {
                list = &bmi_mx->bmx_unex_rxs;
                lock = &bmi_mx->bmx_unex_rxs_lock;
        } else {
                id = (int) ctx->mxc_mop->context_id;
                lock = &bmi_mx->bmx_done_q_lock[id];
                list = &bmi_mx->bmx_done_q[id];
        }


        gen_mutex_lock(lock);
        qlist_add_tail(&ctx->mxc_list, list);
        gen_mutex_unlock(lock);
        BMX_EXIT;
        return;
}

static void
bmx_deq_completed(struct bmx_ctx **ctxp, bmi_context_id context_id)
{
        int             id      = (int) context_id;
        list_t          *list   = &bmi_mx->bmx_done_q[id];
        gen_mutex_t     *lock   = &bmi_mx->bmx_done_q_lock[id];
        struct bmx_ctx  *ctx    = NULL;

        BMX_ENTER;

        gen_mutex_lock(lock);
        if (!qlist_empty(list)) {
                ctx = qlist_entry(list->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&ctx->mxc_list);
        }
        gen_mutex_unlock(lock);
        *ctxp = ctx;

        BMX_EXIT;
        return;
}

static struct bmx_ctx *
bmx_get_idle_rx(void)
{
        struct bmx_ctx  *rx     = NULL;
        list_t          *l      = &bmi_mx->bmx_idle_rxs;

        gen_mutex_lock(&bmi_mx->bmx_idle_rxs_lock);
        if (!qlist_empty(l)) {
                rx = qlist_entry(l->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&rx->mxc_list);

                if (rx->mxc_get != rx->mxc_put) {
                        debug(BMX_DB_ERR, "get_idle_rx() get (%llu) != put (%llu)",
                                (unsigned long long) rx->mxc_get, 
                                (unsigned long long) rx->mxc_put);
                        exit(1);
                }
                rx->mxc_get++;

                if (!(rx->mxc_state == BMX_CTX_IDLE || rx->mxc_state == BMX_CTX_INIT)) {
                        debug(BMX_DB_ERR, "get_idle_rx() rx state is %d",
                                (int) rx->mxc_state);
                        exit(1);
                }
                rx->mxc_state = BMX_CTX_PREP;
        }
        gen_mutex_unlock(&bmi_mx->bmx_idle_rxs_lock);

        return rx;
}

static void
bmx_put_idle_ctx(struct bmx_ctx *ctx)
{
        list_t          *list   = &bmi_mx->bmx_idle_txs;
        gen_mutex_t     *lock   = &bmi_mx->bmx_idle_txs_lock;

        if (ctx == NULL) {
                debug(BMX_DB_WARN, "put_idle_ctx() called with NULL");
                return;
        }
        ctx->mxc_put++;
        if (ctx->mxc_get != ctx->mxc_put) {
                debug(BMX_DB_ERR, "put_idle_ctx() get (%llu) != put (%llu)",
                         (unsigned long long) ctx->mxc_get,
                         (unsigned long long) ctx->mxc_put);
                exit(1);
        }
        bmx_ctx_init(ctx);

        if (ctx->mxc_type == BMX_REQ_RX) {
                list   = &bmi_mx->bmx_idle_rxs;
                lock   = &bmi_mx->bmx_idle_rxs_lock;
        }

        gen_mutex_lock(lock);
        qlist_add(&ctx->mxc_list, list);
        gen_mutex_unlock(lock);
        return;
}

static void
bmx_reduce_idle_rxs(int count)
{
        int              i      = 0;
        struct bmx_ctx  *rx     = NULL;

        for (i = 0; i < count; i++) {
                rx = bmx_get_idle_rx();
                if (rx != NULL) {
                        bmx_ctx_free(rx);
                }
        }

        return;
}

static struct bmx_ctx *
bmx_get_idle_tx(void)
{
        struct bmx_ctx  *tx     = NULL;
        list_t          *l      = &bmi_mx->bmx_idle_txs;

        gen_mutex_lock(&bmi_mx->bmx_idle_txs_lock);
        if (!qlist_empty(l)) {
                tx = qlist_entry(l->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&tx->mxc_list);

                if (tx->mxc_get != tx->mxc_put) {
                        debug(BMX_DB_ERR, "get_idle_tx() get (%llu) != put (%llu)",
                                (unsigned long long) tx->mxc_get,
                                (unsigned long long) tx->mxc_put);
                        exit(1);
                }
                tx->mxc_get++;

                if (!(tx->mxc_state == BMX_CTX_IDLE || tx->mxc_state == BMX_CTX_INIT)) {
                        debug(BMX_DB_ERR, "get_idle_tx() tx state is %d",
                                (int) tx->mxc_state);
                        exit(1);
                }
                tx->mxc_state = BMX_CTX_PREP;
        }
        gen_mutex_unlock(&bmi_mx->bmx_idle_txs_lock);

        return tx;
}

/**** peername parsing functions **************************************/

static int
bmx_verify_hostname(char *host)
{
        int             ret     = 0;
        int             len     = 0;
        const char     *legal   = NULL;

        legal = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";

        len = strlen(host);
        if (len > 64) {
                debug(BMX_DB_INFO, "%s is not a legal hostname", host);
        }

        ret = strspn(host, legal);

        if (len != ret || len > 63 || len == 0) {
                return -1;
        }

        return 0;
}

static int
bmx_verify_num_str(char *num_str)
{
        int             ret     = 0;
        int             len     = 0;
        const char     *legal   = "0123456789";

        len = strlen(num_str);

        ret = strspn(num_str, legal);

        if (len != ret) {
                return -1;
        }

        return 0;
}


/* parse mx://hostname:board:ep_id/filesystem/
 * or    mx://hostname:ep_id/filesystem/
 * this is pretty robust but if strtol() fails for board or ep_id, it
 * returns 0 and we do not know that it failed. 
 * This handles legal hostnames (1-63 chars) include a-zA-Z0-9 as well as . and -
 * It will accept IPv4 addresses but not IPv6 (too many semicolons) */
static int
bmx_parse_peername(const char *peername, char **hostname, uint32_t *board, uint32_t *ep_id)
{
        int             ret             = 0;
        int             colon1_found    = 0;
        int             colon2_found    = 0;
        char           *s               = NULL;
        char           *colon1          = NULL;
        char           *colon2          = NULL;
        char           *fs              = NULL;
        char           *host            = NULL;
        uint32_t        bd              = -1;
        uint32_t        ep              = 0;

        if (peername == NULL || hostname == NULL || board == NULL || ep_id == NULL) {
                debug(BMX_DB_INFO, "parse_peername() called with invalid parameter");
                return -BMI_EINVAL;
        }

        if (peername[0] != 'm' ||
            peername[1] != 'x' ||
            peername[2] != ':' ||
            peername[3] != '/' ||
            peername[4] != '/') {
                debug(BMX_DB_INFO, "parse_peername() peername does not start with mx://");
                return -1;
        }

        s = strdup(&peername[5]);
        fs = strchr(s, '/');
        if (fs) {
                *fs = '\0';
        }
        colon1 = strchr(s, ':');
        if (!colon1) {
                debug(BMX_DB_INFO, "parse_peername() strchr() failed");
        } else {
                colon2 = strrchr(s, ':');
                if (colon1 == colon2) {
                        /* colon2_found == 0 */
                        debug(BMX_DB_INFO, "parse_peername() MX hostname does not "
                                           "include a board number");
                } else {
                        colon2_found = 1;
                        *colon2 = '\0';
                }
                colon1_found = 1;
                *colon1 = '\0';
        }
        /* if MX hostname includes board number...
         * s      = hostname\0board\0ep_id\0filesystem
         * colon1 =         \0board\0ep_id\0filesystem
         * colon2 =                \0ep_id\0filesystem
         * fs     =                       \0filesystem
         *
         * else if MX hostname does _not_ include a board number...
         * s      = hostname\0ep_id\0filesystem
         * colon1 =         \0ep_id\0filesystem
         * colon2 =         \0ep_id\0filesystem
         * fs     =                \0filesystem
         */

        colon1++;
        colon2++;

        /* make sure there are no more ':' in the strings */
        if (colon1_found && colon2_found) {
                if (NULL != strchr(colon1, ':') ||
                    NULL != strchr(colon2, ':')) {
                        debug(BMX_DB_INFO, "parse_peername() too many ':' (%s %s)", 
                                           colon1, colon2);
                        free(s);
                        return -1;
                }
        }

        host = strdup(s);
        if (!host) {
                debug(BMX_DB_MEM, "parse_peername() malloc() failed");
                free(s);
                return -1;
        }

        if (colon1_found && colon2_found) {
                bd = (uint32_t) strtol(colon1, NULL, 0);
                ep = (uint32_t) strtol(colon2, NULL, 0);
        } else if (colon1_found && !colon2_found) {
                ep = (uint32_t) strtol(colon2, NULL, 0);
        } else {
                debug(BMX_DB_WARN, "%s is not a valid hostname", host);
                free(host);
                free(s);
                return -1;
        }

        ret = bmx_verify_hostname(host);
        if (ret != 0) {
                debug(BMX_DB_INFO, "%s is not a valid hostname", host);
                free(host);
                free(s);
                return -1;
        }
        ret = bmx_verify_num_str(colon1);
        if (ret != 0) {
                debug(BMX_DB_INFO, "%s is not a valid board ID", host);
                free(host);
                free(s);
                return -1;
        }
        ret = bmx_verify_num_str(colon2);
        if (ret != 0) {
                debug(BMX_DB_INFO, "%s is not a valid endpoint ID", host);
                free(host);
                free(s);
                return -1;
        }

        *hostname = host;
        *board = bd;
        *ep_id = ep;

        free(s);

        return 0;
}

/**** peer handling functions **************************************/

static void
bmx_peer_free(struct bmx_peer *peer)
{
        struct bmx_method_addr *mxmap = peer->mxp_mxmap;

        if (mxmap != NULL) {
                mxmap->mxm_peer = NULL;
        }

        if (!qlist_empty(&peer->mxp_queued_txs) ||
            !qlist_empty(&peer->mxp_queued_rxs) ||
            !qlist_empty(&peer->mxp_pending_rxs)) {
                debug(BMX_DB_ERR, "freeing peer with non-empty lists");
                exit(1);
        }
        gen_mutex_lock(&bmi_mx->bmx_peers_lock);
        if (!qlist_empty(&peer->mxp_list)) qlist_del_init(&peer->mxp_list);
        gen_mutex_unlock(&bmi_mx->bmx_peers_lock);
        BMX_FREE(peer, sizeof(*peer));
        return;
}

static void
bmx_peer_addref(struct bmx_peer *peer)
{
        gen_mutex_lock(&peer->mxp_lock);
        debug(BMX_DB_PEER, "%s refcount was %d", __func__, peer->mxp_refcount);
        peer->mxp_refcount++;
        gen_mutex_unlock(&peer->mxp_lock);
        return;
}

static void
bmx_peer_decref(struct bmx_peer *peer)
{
        BMX_ENTER;
        gen_mutex_lock(&peer->mxp_lock);
        if (peer->mxp_refcount == 0) {
                debug(BMX_DB_WARN, "peer_decref() called for %s when refcount == 0",
                                peer->mxp_mxmap->mxm_peername);
        }
        peer->mxp_refcount--;
        if (peer->mxp_refcount == 1 && peer->mxp_state == BMX_PEER_DISCONNECT) {
                /* all txs and rxs are completed or canceled, reset state */
                debug(BMX_DB_PEER, "Setting peer %s to BMX_PEER_INIT",
                                   peer->mxp_mxmap->mxm_peername);
                peer->mxp_state = BMX_PEER_INIT;
        }
        gen_mutex_unlock(&peer->mxp_lock);

        if (peer->mxp_refcount == 0) {
                debug(BMX_DB_PEER, "%s freeing peer %s", __func__,
                                peer->mxp_mxmap->mxm_peername);
                struct bmx_method_addr *mxmap = peer->mxp_mxmap;

                mx_set_endpoint_addr_context(peer->mxp_epa, NULL);
                gen_mutex_lock(&bmi_mx->bmx_lock);
                mxmap->mxm_peer = NULL;
                gen_mutex_unlock(&bmi_mx->bmx_lock);
                bmx_peer_free(peer);
        }
        BMX_EXIT;
        return;
}

static int
bmx_peer_alloc(struct bmx_peer **peerp, struct bmx_method_addr *mxmap)
{
        int              i              = 0;
        int              ret            = 0;
        char             name[MX_MAX_HOSTNAME_LEN + 1];
        uint64_t         nic_id         = 0ULL;
        mx_return_t      mxret          = MX_SUCCESS;
        struct bmx_peer *peer           = NULL;

        if (peerp == NULL) {
                debug(BMX_DB_PEER, "peer_alloc() peerp = NULL");
                return -1;
        }
        BMX_MALLOC(peer, sizeof(*peer));
        if (!peer) {
                debug(BMX_DB_MEM, "peer_alloc() unable to malloc peer");
                return -BMI_ENOMEM;
        }
        peer->mxp_map   = mxmap->mxm_map;
        peer->mxp_mxmap = mxmap;

        /* init lists before calling in case we call peer_free() */
        INIT_QLIST_HEAD(&peer->mxp_queued_txs);
        INIT_QLIST_HEAD(&peer->mxp_queued_rxs);
        INIT_QLIST_HEAD(&peer->mxp_pending_rxs);
        INIT_QLIST_HEAD(&peer->mxp_list);
        
        memset(name, 0, sizeof(*name));
        if (mxmap->mxm_board != -1) {
                sprintf(name, "%s:%d", mxmap->mxm_hostname, mxmap->mxm_board);
        } else {
                sprintf(name, "%s", mxmap->mxm_hostname);
        }
        mxret = mx_hostname_to_nic_id(name, &nic_id);
        if (mxret == MX_SUCCESS) {
                peer->mxp_nic_id = nic_id;
        } else {
                debug((BMX_DB_MX|BMX_DB_WARN), "peer_alloc() unable to lookup nic_id "
                      "for %s (mx_hostname_to_nic_id() returned %s)", name,
                      mx_strerror(mxret));
                bmx_peer_free(peer);
                return -BMI_EHOSTUNREACH;
        }
        /* peer->mxp_epa will come from mx_iconnect() */

        peer->mxp_state = BMX_PEER_INIT;
        /* peer->mxp_tx_id assigned to me by peer */
        gen_mutex_lock(&bmi_mx->bmx_lock);
        peer->mxp_rx_id = bmi_mx->bmx_next_id++;
        gen_mutex_unlock(&bmi_mx->bmx_lock);
        if (bmi_mx->bmx_next_id > BMX_MAX_PEER_ID) {
                /* FIXME we should reset to 1
                 *      check if a new ID is already used
                 *      but we have no idea when one is no longer used
                 */
                debug(BMX_DB_ERR, "peer id is rolling over. FATAL ERROR");
                exit(1);
        }

        /* peer->mxp_refcount */

        gen_mutex_init(&peer->mxp_lock);

        for (i = 0; i < BMX_PEER_RX_NUM; i++) {
                struct bmx_ctx  *rx     = NULL;

                ret = bmx_ctx_alloc(&rx, BMX_REQ_RX);
                if (ret != 0) {
                        bmx_reduce_idle_rxs(i);
                        bmx_peer_free(peer);
                        return ret;
                }
                bmx_put_idle_ctx(rx);
        }

        /* on servers with server-to-server comms, we are racing 
         * between method_addr_lookup() and handle_conn_req() */

        bmx_peer_addref(peer); /* for the peers list */
        gen_mutex_lock(&bmi_mx->bmx_peers_lock);
        qlist_add_tail(&peer->mxp_list, &bmi_mx->bmx_peers);
        gen_mutex_unlock(&bmi_mx->bmx_peers_lock);

        mxmap->mxm_peer = peer;
        *peerp = peer;

        return 0;
}

static int
bmx_peer_init_state(struct bmx_peer *peer)
{
        int             ret     = 0;

        BMX_ENTER;

        gen_mutex_lock(&peer->mxp_lock);

        /* we have a ref for each pending tx and rx, don't init
         * if the refcount > 0 or pending_rxs is not empty */
        if (!qlist_empty(&peer->mxp_pending_rxs) ||
            peer->mxp_refcount != 0) {
                ret = -1;
        } else {
                /* ok to init */
                debug(BMX_DB_PEER, "Setting peer %s to BMX_PEER_INIT",
                                   peer->mxp_mxmap->mxm_peername);
                peer->mxp_state = BMX_PEER_INIT;
        }

        gen_mutex_unlock(&peer->mxp_lock);

        BMX_EXIT;

        return 0;
}

/**** startup/shutdown functions **************************************/

/* init bmi_mx */
static int
bmx_globals_init(int method_id)
{
        int     i       = 0;

#if BMX_MEM_ACCT
        mem_used = 0;
        gen_mutex_init(&mem_used_lock);
#endif
        BMX_MALLOC(bmi_mx, sizeof(*bmi_mx));
        if (bmi_mx == NULL) {
                return -1;
        }

        bmi_mx->bmx_method_id = method_id;

        /* bmi_mx->bmx_peername */
        /* bmi_mx->bmx_hostname */
        /* bmi_mx->bmx_board */
        /* bmi_mx->bmx_ep_id */
        /* bmi_mx->bmx_ep */
        /* bmi_mx->bmx_sid */
        /* bmi_mx->bmx_is_server */

        INIT_QLIST_HEAD(&bmi_mx->bmx_peers);
        gen_mutex_init(&bmi_mx->bmx_peers_lock);

        INIT_QLIST_HEAD(&bmi_mx->bmx_txs);
        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_txs);
        gen_mutex_init(&bmi_mx->bmx_idle_txs_lock);

        INIT_QLIST_HEAD(&bmi_mx->bmx_rxs);
        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_rxs);
        gen_mutex_init(&bmi_mx->bmx_idle_rxs_lock);

        gen_mutex_init(&bmi_mx->bmx_completion_lock);
        /* set to 1 to allow testing to start */
        bmi_mx->bmx_refcount = 1;

        for (i = 0; i < BMI_MAX_CONTEXTS; i++) {
                INIT_QLIST_HEAD(&bmi_mx->bmx_done_q[i]);
                gen_mutex_init(&bmi_mx->bmx_done_q_lock[i]);
        }

        INIT_QLIST_HEAD(&bmi_mx->bmx_unex_rxs);
        gen_mutex_init(&bmi_mx->bmx_unex_rxs_lock);

        bmi_mx->bmx_next_id = 1;
        gen_mutex_init(&bmi_mx->bmx_lock);      /* global lock, use for global txs,
                                                   global rxs, next_id, etc. */

#if BMX_MEM_TWEAK
        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_buffers);
        gen_mutex_init(&bmi_mx->bmx_idle_buffers_lock);
        INIT_QLIST_HEAD(&bmi_mx->bmx_used_buffers);
        gen_mutex_init(&bmi_mx->bmx_used_buffers_lock);

        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_unex_buffers);
        gen_mutex_init(&bmi_mx->bmx_idle_unex_buffers_lock);
        INIT_QLIST_HEAD(&bmi_mx->bmx_used_unex_buffers);
        gen_mutex_init(&bmi_mx->bmx_used_unex_buffers_lock);
#endif
        return 0;
}


static int
bmx_open_endpoint(mx_endpoint_t *ep, uint32_t board, uint32_t ep_id)
{
        mx_return_t     mxret   = MX_SUCCESS;
        mx_param_t      param;

        /* This will tell MX to use context IDs. Normally, MX has one
         * set of queues for posted recvs, unexpected, etc. This will
         * create seaparate sets of queues for each msg type. 
         * The benefit is that we can call mx_test_any() for each 
         * message type and not have to scan a long list of non-
         * matching recvs. */
        param.key = MX_PARAM_CONTEXT_ID;
        param.val.context_id.bits = 4;
        param.val.context_id.shift = BMX_MSG_SHIFT;

        mxret = mx_open_endpoint(board, ep_id, BMX_MAGIC,
                                 &param, 1, ep);
        if (mxret != MX_SUCCESS) {
                return -1;
        }

        mxret = mx_register_unexp_handler(*ep, (mx_unexp_handler_t)
                                          bmx_unexpected_recv, NULL);
        if (mxret != MX_SUCCESS) {
                debug(BMX_DB_WARN, "mx_register_unexp_callback() failed "
                                "with %s", mx_strerror(mxret));
                mx_close_endpoint(*ep);
                mx_finalize();  
                return -1;
        }

        mxret = mx_set_request_timeout(*ep, NULL, BMX_TIMEOUT);
        if (mxret != MX_SUCCESS) {
                debug(BMX_DB_WARN, "mx_set_request_timeout() failed with %s",
                                 mx_strerror(mxret));
                mx_close_endpoint(*ep);
                mx_finalize();
                return -1;
        }

        return 0;
}

/* The listen_addr is our method if we are a server. It is NULL for a
 * client. The other params are NULL/0 for the client as well. */
static int
BMI_mx_initialize(bmi_method_addr_p listen_addr, int method_id, int init_flags)
{
        int             i       = 0;
        int             ret     = 0;
        mx_return_t     mxret   = MX_SUCCESS;

        BMX_ENTER;

         /* check params */
        if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER)) {
                debug(BMX_DB_ERR, "mx_initialize() with illegal parameters. "
                        "BMI_INIT_SERVER requires non-null listen_addr");
                exit(1);
        }

        ret = bmx_globals_init(method_id);
        if (ret != 0) {
                debug(BMX_DB_WARN, "bmx_globals_init() failed with no memory");
                return -BMI_ENOMEM;
        }

        /* disable shmem to allow clients and servers on the same machine */
        setenv("MX_DISABLE_SHMEM", "1", 1);

        /* return errors, do not abort */
        mx_set_error_handler(MX_ERRORS_RETURN);

        /* only complete sends after they are delivered */
        setenv("MX_ZOMBIE", "0", 1);

        mxret = mx_init();
        if (!(mxret == MX_SUCCESS || mxret == MX_ALREADY_INITIALIZED)) {
                debug(BMX_DB_WARN, "mx_init() failed with %s", mx_strerror(mxret));
                BMX_FREE(bmi_mx, sizeof(*bmi_mx));
                return -BMI_ENODEV;
        }

        /* if we are a server, open an endpoint now. If a client, wait until first
         * sendunexpected or first recv. */
        if (init_flags & BMI_INIT_SERVER) {
                struct bmx_ctx         *rx     = NULL;
                struct bmx_method_addr *mxmap  = listen_addr->method_data;
                mx_endpoint_addr_t      epa;
                uint32_t                ep_id   = 0;
                uint32_t                sid     = 0;
                uint64_t                nic_id  = 0ULL;
                struct bmx_peer         *peer   = NULL;

                bmi_mx->bmx_hostname = strdup(mxmap->mxm_hostname);
                bmi_mx->bmx_board = mxmap->mxm_board;
                bmi_mx->bmx_ep_id = mxmap->mxm_ep_id;
                bmi_mx->bmx_is_server = 1;
                bmx_create_peername();

                ret = bmx_open_endpoint(&bmi_mx->bmx_ep, mxmap->mxm_board, mxmap->mxm_ep_id);
                if (ret != 0) {
                        debug(BMX_DB_ERR, "open_endpoint() failed");
                        BMX_FREE(bmi_mx, sizeof(*bmi_mx));
                        exit(1);
                }

                /* get our MX session id */
                mx_get_endpoint_addr(bmi_mx->bmx_ep, &epa);
                mx_decompose_endpoint_addr2(epa, &nic_id, &ep_id, &sid);
                bmi_mx->bmx_sid = sid;

                bmx_peer_alloc(&peer, mxmap);

                /* We allocate BMX_PEER_RX_NUM when we peer_alloc()
                 * Allocate some here to catch the peer CONN_REQ */
                for (i = 0; i < BMX_SERVER_RXS; i++) {
                        ret = bmx_ctx_alloc(&rx, BMX_REQ_RX);
                        if (ret == 0) {
                                bmx_put_idle_ctx(rx);
                        }
                }
        }

#if BMX_MEM_TWEAK
        for (i = 0; i < BMX_BUFF_NUM; i++) {
                struct bmx_buffer *buf  = NULL;
                BMX_MALLOC(buf, sizeof(*buf));
                if (buf) {
                        INIT_QLIST_HEAD(&buf->mxb_list);
                        BMX_MALLOC(buf->mxb_buffer, BMX_BUFF_SIZE);
                        if (buf->mxb_buffer) {
                                qlist_add(&buf->mxb_list, &bmi_mx->bmx_idle_buffers);
                        } else {
                                BMX_FREE(buf, sizeof(*buf));
                        }
                }
        }
#endif

#if BMX_MEM_ACCT
        debug(BMX_DB_MEM, "memory used at end of initialization %lld", llu(mem_used));
#endif
        BMX_EXIT;

        return 0;
}

static int
BMI_mx_finalize(void)
{
        struct bmx_data *tmp = bmi_mx;

        BMX_ENTER;

        gen_mutex_lock(&tmp->bmx_lock);

        /* shutdown MX */
        mx_wakeup(bmi_mx->bmx_ep);
        mx_close_endpoint(bmi_mx->bmx_ep);
        mx_finalize();

        /* free rxs */
        {
                struct bmx_ctx *rx   = NULL;
                struct bmx_ctx *next = NULL;
                qlist_for_each_entry_safe(rx, next, &bmi_mx->bmx_rxs, mxc_global_list) {
                        bmx_ctx_free(rx);
                }
        }

        /* free txs */
        {
                struct bmx_ctx *tx   = NULL;
                struct bmx_ctx *next = NULL;
                qlist_for_each_entry_safe(tx, next, &bmi_mx->bmx_txs, mxc_global_list) {
                        bmx_ctx_free(tx);
                }
        }

        /* free peers */
        {
                struct bmx_peer *peer   = NULL;
                struct bmx_peer *next   = NULL;
                qlist_for_each_entry_safe(peer, next, &bmi_mx->bmx_peers, mxp_list) {
                        bmx_peer_free(peer);
                }
        }

#if BMX_MEM_TWEAK
        {
                list_t *idle = &bmi_mx->bmx_idle_buffers;
                list_t *used = &bmi_mx->bmx_used_buffers;
                struct bmx_buffer *mem  = NULL;
                struct bmx_buffer *next = NULL;

                qlist_for_each_entry_safe(mem, next, idle, mxb_list) {
                        if (mem->mxb_used != 0)
                                debug(BMX_DB_MEM, "idle buffer used %d times", 
                                                  mem->mxb_used);
                        BMX_FREE(mem->mxb_buffer, BMX_BUFF_SIZE);
                        BMX_FREE(mem, sizeof(*mem));
                }
                qlist_for_each_entry_safe(mem, next, used, mxb_list) {
                        if (mem->mxb_used != 0)
                                debug(BMX_DB_MEM, "used buffer used %d times", 
                                                  mem->mxb_used);
                        BMX_FREE(mem->mxb_buffer, BMX_BUFF_SIZE);
                        BMX_FREE(mem, sizeof(*mem));
                }
                debug(BMX_DB_MEM, "%d misses", bmi_mx->bmx_misses);
        }
#endif

        if (bmi_mx->bmx_hostname) {
                free(bmi_mx->bmx_hostname);
                bmi_mx->bmx_hostname = NULL;
        }
        if (bmi_mx->bmx_peername) {
                free(bmi_mx->bmx_peername);
                bmi_mx->bmx_peername = NULL;
        }

        bmi_mx = NULL;

        gen_mutex_unlock(&tmp->bmx_lock);

        BMX_FREE(tmp, sizeof(*tmp));

#if BMX_MEM_ACCT
        debug(BMX_DB_MEM, "memory leaked at shutdown %lld", llu(mem_used));
#endif
        BMX_EXIT;
        return 0;
}


/**** BMI_mx_* and support functions **************************************/

/* bmx_peer_disconnect - close the connection to this peer
 * @peer - a bmx_peer pointer
 * @mx_dis - an integer, 0 or 1, where 1 means call mx_disconnect()
 *
 * This function sets peer state to DISCONNECT, sets all queued rxs and txs to
 * CANCELED and places them on the canceled list, cancels pending rxs and 
 * optionally calls mx_disconnect() to cancel pending txs and matched rxs.
 */
static void
bmx_peer_disconnect(struct bmx_peer *peer, int mx_dis, bmi_error_code_t err)
{
        struct bmx_ctx  *tx     = NULL;
        struct bmx_ctx  *rx     = NULL;
        struct bmx_ctx  *next   = NULL;

        debug(BMX_DB_CONN, "%s for %s in state %d (%d)", __func__,
                        peer->mxp_mxmap->mxm_peername, peer->mxp_state, mx_dis);
        gen_mutex_lock(&peer->mxp_lock);
        if (peer->mxp_state == BMX_PEER_DISCONNECT) {
                gen_mutex_unlock(&peer->mxp_lock);
                return;
        }
        debug(BMX_DB_PEER, "Setting peer %s to BMX_PEER_DISCONNECT",
                           peer->mxp_mxmap->mxm_peername);
        peer->mxp_state = BMX_PEER_DISCONNECT;

        /* cancel queued txs */
        while (!qlist_empty(&peer->mxp_queued_txs)) {
                list_t          *queued_txs     = &peer->mxp_queued_txs;
                tx = qlist_entry(queued_txs->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&tx->mxc_list);
                bmx_q_completed(tx, BMX_CTX_CANCELED, BMX_NO_STATUS, err);
        }
        /* cancel queued rxs */
        while (!qlist_empty(&peer->mxp_queued_rxs)) {
                list_t          *queued_rxs     = &peer->mxp_queued_rxs;
                rx = qlist_entry(queued_rxs->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&rx->mxc_list);
                bmx_q_completed(rx, BMX_CTX_CANCELED, BMX_NO_STATUS, err);
        }
        /* try to cancel pending rxs */
        qlist_for_each_entry_safe(rx, next, &peer->mxp_pending_rxs, mxc_list) {
                uint32_t        result = 0;
                mx_cancel(bmi_mx->bmx_ep, &rx->mxc_mxreq, &result);
                if (result) {
                        qlist_del_init(&rx->mxc_list);
                        bmx_q_completed(rx, BMX_CTX_CANCELED, BMX_NO_STATUS, err);
                }
        }
        gen_mutex_unlock(&peer->mxp_lock);
        if (mx_dis) {
                /* cancel all posted txs and matched rxs */
                mx_disconnect(bmi_mx->bmx_ep, peer->mxp_epa);
        }
        return;
}

static int
BMI_mx_set_info(int option, void *inout_parameter)
{
        struct bmi_method_addr      *map    = NULL;
        struct bmx_method_addr  *mxmap  = NULL;
        struct bmx_peer         *peer   = NULL;

        BMX_ENTER;

        switch(option) {
                case BMI_DROP_ADDR:
                        if (inout_parameter != NULL) {
                                map = (struct bmi_method_addr *) inout_parameter;
                                mxmap = map->method_data;
                                debug(BMX_DB_PEER, "%s drop %s map 0x%p mxmap 0x%p)",
                                                __func__, mxmap->mxm_peername != NULL ?
                                                mxmap->mxm_peername : "NULL", map,
                                                mxmap);
                                if (bmi_mx != NULL) {
                                        peer = mxmap->mxm_peer;
                                        bmx_peer_disconnect(peer, 1, BMI_ENETRESET);
                                }
                                if (mxmap->mxm_peername) {
                                        debug(BMX_DB_MEM, "freeing mxm_peername");
                                        free((void *) mxmap->mxm_peername);
                                        mxmap->mxm_peername = NULL;
                                }
                                if (mxmap->mxm_hostname) {
                                        debug(BMX_DB_MEM, "freeing mxm_hostname");
                                        free((void *) mxmap->mxm_hostname);
                                        mxmap->mxm_hostname = NULL;
                                }
                                debug(BMX_DB_PEER, "freeing map 0x%p", map);
                                free(map);
                        }
                break;
                default:
                        /* XXX: should return -ENOSYS, but 0 now until callers 
                         * handle that correctly. */
                break;
        }
        BMX_EXIT;

        return 0;
}

static int
BMI_mx_get_info(int option, void *inout_parameter)
{
        int     ret     = 0;

        BMX_ENTER;

        switch(option) {
                case BMI_CHECK_MAXSIZE:
                        /* reality is 2^31, but shrink to avoid negative int */
                        *(int *)inout_parameter = (1U << 31) - 1;
                        break;
                case BMI_GET_UNEXP_SIZE:
                        *(int *)inout_parameter = BMX_UNEXPECTED_SIZE;
                        break;
                default:
                        ret = -BMI_ENOSYS;
        }
        BMX_EXIT;

        return ret;
}

#define BMX_IO_BUF      1
#define BMX_UNEX_BUF    2

static void *
bmx_memalloc(bmi_size_t size, int type)
{
        void                    *buf    = NULL;
#if BMX_MEM_TWEAK
        int                     *misses = NULL;
        struct bmx_buffer       *mem    = NULL;
        list_t                  *idle   = NULL;
        list_t                  *used   = NULL;
        gen_mutex_t             *idle_lock  = NULL;
        gen_mutex_t             *used_lock  = NULL;

        if (type == BMX_IO_BUF) {
                idle = &bmi_mx->bmx_idle_buffers;
                used = &bmi_mx->bmx_used_buffers;
                idle_lock = &bmi_mx->bmx_idle_buffers_lock;
                used_lock = &bmi_mx->bmx_used_buffers_lock;
                misses = &bmi_mx->bmx_misses;
        } else if (type == BMX_UNEX_BUF) {
                idle = &bmi_mx->bmx_idle_unex_buffers;
                used = &bmi_mx->bmx_used_unex_buffers;
                idle_lock = &bmi_mx->bmx_idle_unex_buffers_lock;
                used_lock = &bmi_mx->bmx_used_unex_buffers_lock;
                misses = &bmi_mx->bmx_unex_misses;
        } else {
                return NULL;
        }

        gen_mutex_lock(idle_lock);
        if (size <= (BMX_BUFF_SIZE) && !qlist_empty(idle)) {
                mem = qlist_entry(idle->next, struct bmx_buffer, mxb_list);
                qlist_del_init(&mem->mxb_list);
                gen_mutex_unlock(idle_lock);
                buf = mem->mxb_buffer;
                mem->mxb_used++;
                gen_mutex_lock(used_lock);
                qlist_add(&mem->mxb_list, used);
                gen_mutex_unlock(used_lock);
                gen_mutex_lock(idle_lock);
        } else {
                (*misses)++;
                gen_mutex_unlock(idle_lock);
                buf = malloc((size_t) size);
                gen_mutex_lock(idle_lock);
        }
        gen_mutex_unlock(idle_lock);
#else
        buf = malloc((size_t) size);
#endif
        return buf;
}

void *
BMI_mx_memalloc(bmi_size_t size, enum bmi_op_type send_recv)
{
        void                    *buf    = NULL;
        buf = bmx_memalloc(size, BMX_IO_BUF);
        return buf;
}

static int
bmx_memfree(void *buffer, bmi_size_t size, int type)
{
#if BMX_MEM_TWEAK
        int                     found   = 0;
        struct bmx_buffer       *mem    = NULL;
        list_t                  *idle   = NULL;
        list_t                  *used   = NULL;
        gen_mutex_t             *idle_lock  = NULL;
        gen_mutex_t             *used_lock  = NULL;

        if (type == BMX_IO_BUF) {
                idle = &bmi_mx->bmx_idle_buffers;
                used = &bmi_mx->bmx_used_buffers;
                idle_lock = &bmi_mx->bmx_idle_buffers_lock;
                used_lock = &bmi_mx->bmx_used_buffers_lock;
        } else if (type == BMX_UNEX_BUF) {
                idle = &bmi_mx->bmx_idle_unex_buffers;
                used = &bmi_mx->bmx_used_unex_buffers;
                idle_lock = &bmi_mx->bmx_idle_unex_buffers_lock;
                used_lock = &bmi_mx->bmx_used_unex_buffers_lock;
        } else {
                return -1;
        }

        gen_mutex_lock(used_lock);
        qlist_for_each_entry(mem, used, mxb_list) {
                if (mem->mxb_buffer == buffer) {
                        found = 1;
                        qlist_del_init(&mem->mxb_list);
                        gen_mutex_unlock(used_lock);
                        gen_mutex_lock(idle_lock);
                        qlist_add(&mem->mxb_list, idle);
                        gen_mutex_unlock(idle_lock);
                        gen_mutex_lock(used_lock);
                        break;
                }
        }
        gen_mutex_unlock(used_lock);

        if (found == 0) {
                free(buffer);
        }
#else
        free(buffer);
#endif
        return 0;
}

static int
BMI_mx_memfree(void *buffer, bmi_size_t size, enum bmi_op_type send_recv)
{
        int     ret     = 0;
        ret = bmx_memfree(buffer, size, BMX_IO_BUF);
        return ret;
}

static int
BMI_mx_unexpected_free(void *buf)
{
        int     ret     = 0;

        BMX_ENTER;

        ret = bmx_memfree(buf, BMX_UNEXPECTED_SIZE, BMX_UNEX_BUF);

        BMX_EXIT;

        return 0;
}

static void
bmx_parse_match(uint64_t match, uint8_t *type, uint32_t *id, uint32_t *tag,
    uint8_t* class)
{
        *type   = (uint8_t)  (match >> BMX_MSG_SHIFT);
        *class  = (uint8_t)  (match >> BMX_CLASS_SHIFT);
        *id     = (uint32_t) ((match >> BMX_ID_SHIFT) & BMX_MAX_PEER_ID); /* 20 bits */
        *tag    = (uint32_t) (match & BMX_MAX_TAG); /* 32 bits */
        return;
}

static void
bmx_create_match(struct bmx_ctx *ctx)
{
        int             connect = 0;
        uint64_t        type    = (uint64_t) ctx->mxc_msg_type;
        uint64_t        id      = 0ULL;
        uint64_t        class   = 0ULL;
        uint64_t        tag     = (uint64_t) ((uint32_t) ctx->mxc_tag);

        if (ctx->mxc_msg_type == BMX_MSG_CONN_REQ || 
            ctx->mxc_msg_type == BMX_MSG_CONN_ACK) {
                connect = 1;
        }

        if ((ctx->mxc_type == BMX_REQ_TX && connect == 0) ||
            (ctx->mxc_type == BMX_REQ_RX && connect == 1)) {
                id = (uint64_t) ctx->mxc_peer->mxp_tx_id;
        } else if ((ctx->mxc_type == BMX_REQ_TX && connect == 1) ||
                   (ctx->mxc_type == BMX_REQ_RX && connect == 0)) {
                id = (uint64_t) ctx->mxc_peer->mxp_rx_id;
        } else {
                debug(BMX_DB_INFO, "create_match() for %s called with "
                                "connect = %d", ctx->mxc_type == BMX_REQ_TX ?
                                "TX" : "RX", connect);
        }
        
        if ((id >> 20) != 0) {
                debug(BMX_DB_ERR, "invalid %s of %llu\n", ctx->mxc_type == BMX_REQ_TX ?
                        "mxp_tx_id" : "mxp_rx_id", (unsigned long long) id);
                exit(1);
        }

        class += ctx->mxc_class;
        ctx->mxc_match = (type << BMX_MSG_SHIFT) | (id << BMX_ID_SHIFT) |
            tag | (class << BMX_CLASS_SHIFT);

        return;
}

static bmi_error_code_t
bmx_mx_to_bmi_errno(enum mx_status_code code)
{
        int     err     = 0;

        switch (code) {
        case MX_STATUS_SUCCESS:
                err = 0;
                break;
        case MX_STATUS_TIMEOUT:
                err = BMI_ETIMEDOUT;
                break;
        case MX_STATUS_ENDPOINT_CLOSED:
        case MX_STATUS_BAD_SESSION:
        case MX_STATUS_BAD_KEY:
        case MX_STATUS_BAD_ENDPOINT:
                err = BMI_ECONNREFUSED;
                break;
        case MX_STATUS_ENDPOINT_UNREACHABLE:
                err = BMI_ENETRESET;
                break;
        case MX_STATUS_NO_RESOURCES:
        case MX_STATUS_EVENTQ_FULL:
                err = BMI_ENOMEM;
                break;
        default:
                debug(BMX_DB_WARN, "request status is %s", mx_strstatus(code));
                err = BMI_EIO;
                break;
        }
        return -err;
}

/* if (peer->mxp_state == BMX_PEER_READY)
 *     add to pending list
 *     add refcount on peer
 *     mx_isend()
 * else
 *     add to peer's queued txs
 */
static int
bmx_post_tx(struct bmx_ctx *tx)
{
        int             ret     = 0;
        struct bmx_peer *peer   = tx->mxc_peer;
        mx_return_t     mxret   = MX_SUCCESS;
        mx_segment_t    *segs   = NULL;

        debug((BMX_DB_FUNC|BMX_DB_CTX), "entering %s match= 0x%llx length= %lld "
                        "peer state= %d op_id= %llu", __func__, llu(tx->mxc_match), 
                        lld(tx->mxc_nob), peer->mxp_state, llu(tx->mxc_mop->op_id));
        if (peer->mxp_state == BMX_PEER_READY) {
                bmx_q_pending_ctx(tx);  /* uses peer lock */
                if (tx->mxc_nseg == 1) {
                        segs = &tx->mxc_seg;
                } else {
                        segs = tx->mxc_seg_list;
                }
                mxret = mx_isend(bmi_mx->bmx_ep, segs, tx->mxc_nseg, peer->mxp_epa,
                                 tx->mxc_match, (void *) tx, &tx->mxc_mxreq);
                if (mxret != MX_SUCCESS) {
                        ret = -BMI_ENOMEM;
                        bmx_deq_pending_ctx(tx);        /* uses peer lock */
                        bmx_q_completed(tx, BMX_CTX_CANCELED, BMX_NO_STATUS, BMI_ENOMEM);
                }
        } else { /* peer is not ready */
                debug(BMX_DB_PEER, "%s peer is not ready (%d) q_ctx(tx) "
                                "match= 0x%llx length=%lld", __func__, peer->mxp_state,
                                llu(tx->mxc_match), lld(tx->mxc_nob));
                bmx_q_ctx(tx);  /* uses peer lock */
        }
        BMX_EXIT;
        return ret;
}

static int
bmx_ensure_connected(struct bmx_method_addr *mxmap)
{
        int             ret     = 0;
        struct bmx_peer *peer   = mxmap->mxm_peer;

        /* NOTE: can this happen? we call peer_alloc() when using 
         * method_addr_lookup() */
        if (peer == NULL) {
                ret = bmx_peer_alloc(&peer, mxmap);
                if (ret != 0) {
                        debug((BMX_DB_CONN|BMX_DB_MEM), "%s could not allocate peer for %s",
                                        __func__, mxmap->mxm_peername);
                        goto out;
                }
        }
        if (peer->mxp_state == BMX_PEER_INIT) {
                debug(BMX_DB_CONN, "%s calling bmx_peer_connect() for %s",
                                        __func__, mxmap->mxm_peername);
                ret = bmx_peer_connect(peer);
        } else if (peer->mxp_state == BMX_PEER_DISCONNECT) {
                debug(BMX_DB_CONN, "%s %s is not connected", __func__, mxmap->mxm_peername);
                ret = -BMI_EHOSTDOWN;
        }
out:
        return ret;
}

static int
bmx_post_send_common(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
                     int numbufs, const void *const *buffers, 
                     const bmi_size_t *sizes, bmi_size_t total_size, 
                     bmi_msg_tag_t tag, void *user_ptr,
                     bmi_context_id context_id, uint8_t class, 
                     int is_unexpected, PVFS_hint hints)
{
        struct bmx_ctx          *tx     = NULL;
        struct method_op        *mop    = NULL;
        struct bmx_method_addr  *mxmap  = NULL;
        struct bmx_peer         *peer   = NULL;
        int                      ret    = 0;
        PINT_event_id            eid    = 0;

        PINT_EVENT_START(
            bmi_mx_send_event_id, bmi_mx_pid, NULL, &eid,
            PINT_HINT_GET_CLIENT_ID(hints),
            PINT_HINT_GET_REQUEST_ID(hints),
            PINT_HINT_GET_RANK(hints),
            PINT_HINT_GET_HANDLE(hints),
            PINT_HINT_GET_OP_ID(hints),
            total_size);

        mxmap = remote_map->method_data;

        ret = bmx_ensure_connected(mxmap);
        if (ret != 0) {
                goto out;
        }
        peer = mxmap->mxm_peer;
        bmx_peer_addref(peer); /* add ref and hold until test or testcontext */

        /* get idle tx, if available, otherwise alloc one */
        tx = bmx_get_idle_tx();
        if (tx == NULL) {
                ret = bmx_ctx_alloc(&tx, BMX_REQ_TX);
                if (ret != 0) {
                        ret = -BMI_ENOMEM;
                        bmx_peer_decref(peer);
                        goto out;
                }
                tx->mxc_state = BMX_CTX_PREP;
        }

        /* map buffer(s) */
        if (numbufs == 1) {
                tx->mxc_seg.segment_ptr = (void *) *buffers;
                tx->mxc_seg.segment_length = *sizes;
                tx->mxc_nob = *sizes;
        } else {
                int             i       = 0;
                mx_segment_t    *segs   = NULL;

                BMX_MALLOC(segs, (numbufs * sizeof(*segs)));
                if (segs == NULL) {
                        bmx_put_idle_ctx(tx);
                        bmx_peer_decref(peer);
                        ret = -BMI_ENOMEM;
                        goto out;
                }
                tx->mxc_seg_list = segs;
                for (i = 0; i < numbufs; i++) {
                        segs[i].segment_ptr = (void *) buffers[i];
                        segs[i].segment_length = sizes[i];
                        tx->mxc_nob += sizes[i];
                }
        }
        tx->mxc_nseg = numbufs;

        if (tx->mxc_nob != total_size) {
                debug(BMX_DB_INFO, "user provided total length %lld does not match "
                                "the buffer list total length %lld", lld(total_size), 
                                lld(tx->mxc_nob));
        }

        if (is_unexpected && tx->mxc_nob > (long long) BMX_UNEXPECTED_SIZE) {
                bmx_put_idle_ctx(tx);
                bmx_peer_decref(peer);
                ret = -BMI_EINVAL;
                goto out;
        }

        tx->mxc_tag = tag;
        tx->mxc_peer = peer;
        tx->mxc_class = class;
        if (!is_unexpected) {
                tx->mxc_msg_type = BMX_MSG_EXPECTED;
        } else {
                tx->mxc_msg_type = BMX_MSG_UNEXPECTED;
        }

        BMX_MALLOC(mop, sizeof(*mop));
        if (mop == NULL) {
                bmx_put_idle_ctx(tx);
                bmx_peer_decref(peer);
                ret = -BMI_ENOMEM;
                goto out;
        }
        id_gen_fast_register(&mop->op_id, mop);
        debug(BMX_DB_CTX, "TX id_gen_fast_register(%llu)", llu(mop->op_id));
        mop->addr = remote_map;  /* set of function pointers, essentially */
        mop->method_data = tx;
        mop->user_ptr = user_ptr;
        mop->context_id = context_id;
        mop->event_id = eid;
        *id = mop->op_id;
        tx->mxc_mop = mop;

        assert(context_id == mop->context_id);
        assert(context_id == tx->mxc_mop->context_id);

        bmx_create_match(tx);

        debug(BMX_DB_CTX, "%s tag= %d length= %d %s op_id= %llu context_id= %lld",
                        __func__, tag, (int) total_size,
                        is_unexpected ? "UNEXPECTED" : "EXPECTED",
                        llu(mop->op_id), lld(context_id));

        ret = bmx_post_tx(tx);

out:
        return ret;
}

static int
BMI_mx_post_send_list(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
                      const void *const *buffers, const bmi_size_t *sizes, int list_count,
                      bmi_size_t total_size, enum bmi_buffer_type buffer_flag __unused,
                      bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id,
                      PVFS_hint hints)
{
        int ret = 0;

        BMX_ENTER;

        ret = bmx_post_send_common(id, remote_map, list_count, buffers, sizes, 
                                    total_size, tag, user_ptr, context_id,
                                    0, 0, hints);

        BMX_EXIT;

        return ret;
}

static int
BMI_mx_post_sendunexpected_list(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
                  const void *const *buffers, const bmi_size_t *sizes, int list_count,
                  bmi_size_t total_size, enum bmi_buffer_type buffer_flag __unused,
                  bmi_msg_tag_t tag, uint8_t class, void *user_ptr, bmi_context_id context_id,
                  PVFS_hint hints)
{
        int ret = 0;

        BMX_ENTER;

        return bmx_post_send_common(id, remote_map, list_count, buffers, sizes, 
                                    total_size, tag, user_ptr, context_id,
                                    class, 1, hints);

        BMX_EXIT;

        return ret;
}

/* if (peer->mxp_state == BMX_PEER_READY)
 *     add to pending list
 *     add refcount on peer
 *     mx_irecv()
 * else
 *     add to peer's queued rxs
 */
static int
bmx_post_rx(struct bmx_ctx *rx)
{
        int             ret     = 0;
        struct bmx_peer *peer   = rx->mxc_peer;
        mx_return_t     mxret   = MX_SUCCESS;
        mx_segment_t    *segs   = NULL;

        debug((BMX_DB_FUNC|BMX_DB_CTX), "entering %s match= 0x%llx length= %lld "
                        "peer state= %d op_id= %llu", __func__, llu(rx->mxc_match), 
                        lld(rx->mxc_nob), peer->mxp_state, llu(rx->mxc_mop->op_id));
        if (peer->mxp_state == BMX_PEER_READY) {
                bmx_q_pending_ctx(rx);  /* uses peer lock */
                if (rx->mxc_nseg == 1) {
                        segs = &rx->mxc_seg;
                } else {
                        segs = rx->mxc_seg_list;
                }
                mxret = mx_irecv(bmi_mx->bmx_ep, segs, rx->mxc_nseg,
                                 rx->mxc_match, BMX_MASK_ALL, (void *) rx, &rx->mxc_mxreq);
                if (mxret != MX_SUCCESS) {
                        ret = -BMI_ENOMEM;
                        bmx_deq_pending_ctx(rx);        /* uses peer lock */
                        bmx_q_completed(rx, BMX_CTX_CANCELED, BMX_NO_STATUS, BMI_ENOMEM);
                }
        } else { /* peer is not ready */
                debug(BMX_DB_PEER, "%s peer is not ready (%d) q_ctx(rx) match= 0x%llx "
                                "length=%lld", __func__, peer->mxp_state,
                                llu(rx->mxc_match), (long long) rx->mxc_nob);
                bmx_q_ctx(rx);  /* uses peer lock */
        }
        BMX_EXIT;
        return ret;
}

static int
bmx_post_recv_common(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
                     int numbufs, void *const *buffers, const bmi_size_t *sizes,
                     bmi_size_t tot_expected_len, bmi_msg_tag_t tag,
                     void *user_ptr, bmi_context_id context_id,
                     PVFS_hint hints)
{
        int                      ret    = 0;
        struct bmx_ctx          *rx     = NULL;
        struct method_op        *mop    = NULL;
        struct bmx_method_addr  *mxmap  = NULL;
        struct bmx_peer         *peer   = NULL;
        PINT_event_id            eid    = 0;

        PINT_EVENT_START(
            bmi_mx_recv_event_id, bmi_mx_pid, NULL, &eid,
            PINT_HINT_GET_CLIENT_ID(hints),
            PINT_HINT_GET_REQUEST_ID(hints),
            PINT_HINT_GET_RANK(hints),
            PINT_HINT_GET_HANDLE(hints),
            PINT_HINT_GET_OP_ID(hints),
            tot_expected_len);

        mxmap = remote_map->method_data;

        ret = bmx_ensure_connected(mxmap);
        if (ret != 0) {
                goto out;
        }
        peer = mxmap->mxm_peer;
        bmx_peer_addref(peer); /* add ref and hold until test or testcontext */

        /* get idle tx, if available, otherwise alloc one */
        rx = bmx_get_idle_rx();
        if (rx == NULL) {
                ret = bmx_ctx_alloc(&rx, BMX_REQ_RX);
                if (ret != 0) {
                        bmx_peer_decref(peer);
                        goto out;
                }
                rx->mxc_state = BMX_CTX_PREP;
        }
        rx->mxc_tag = tag;
        rx->mxc_msg_type = BMX_MSG_EXPECTED;
        rx->mxc_peer = peer;

        /* map buffer(s) */
        if (numbufs == 1) {
                rx->mxc_seg.segment_ptr = (char *) *buffers;
                rx->mxc_seg.segment_length = *sizes;
                rx->mxc_nob = *sizes;
        } else {
                int             i       = 0;
                mx_segment_t    *segs   = NULL;

                BMX_MALLOC(segs, (numbufs * sizeof(*segs)));
                if (segs == NULL) {
                        bmx_put_idle_ctx(rx);
                        bmx_peer_decref(peer);
                        ret = -BMI_ENOMEM;
                        goto out;
                }
                rx->mxc_seg_list = segs;
                for (i = 0; i < numbufs; i++) {
                        segs[i].segment_ptr = (void *) buffers[i];
                        segs[i].segment_length = sizes[i];
                        rx->mxc_nob += sizes[i];
                }
        }
        rx->mxc_nseg = numbufs;

        if (rx->mxc_nob != tot_expected_len) {
                debug(BMX_DB_INFO, "user provided total length %d does not match "
                                "the buffer list total length %lld", 
                                (uint32_t) tot_expected_len, (long long) rx->mxc_nob);
        }

        BMX_MALLOC(mop, sizeof(*mop));
        if (mop == NULL) {
                bmx_put_idle_ctx(rx);
                bmx_peer_decref(peer);
                ret = -BMI_ENOMEM;
                goto out;
        }
        id_gen_fast_register(&mop->op_id, mop);
        debug(BMX_DB_CTX, "RX id_gen_fast_register(%llu)", llu(mop->op_id));
        mop->addr = remote_map;  /* set of function pointers, essentially */
        mop->method_data = rx;
        mop->user_ptr = user_ptr;
        mop->context_id = context_id;
        mop->event_id = eid;
        *id = mop->op_id;
        rx->mxc_mop = mop;

        bmx_create_match(rx);

        debug(BMX_DB_CTX, "%s tag= %d length= %d op_id= %llu", __func__, 
                          tag, (int) tot_expected_len, llu(mop->op_id));

        ret = bmx_post_rx(rx);
out:
        return ret;
}

static int
BMI_mx_post_recv_list(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
               void *const *buffers, const bmi_size_t *sizes, int list_count,
               bmi_size_t tot_expected_len, bmi_size_t *tot_actual_len __unused,
               enum bmi_buffer_type buffer_flag __unused, bmi_msg_tag_t tag, void *user_ptr,
               bmi_context_id context_id,
               PVFS_hint hints)
{
        int ret = 0;

        BMX_ENTER;

        ret = bmx_post_recv_common(id, remote_map, list_count, buffers, sizes,
                                    tot_expected_len, tag, user_ptr, context_id,
                                    hints);

        BMX_EXIT;

        return ret;
}

static void
bmx_peer_post_queued_rxs(struct bmx_peer *peer)
{
        struct bmx_ctx  *rx             = NULL;
        list_t          *queued_rxs     = &peer->mxp_queued_rxs;

        BMX_ENTER;

        gen_mutex_lock(&peer->mxp_lock);
        while (!qlist_empty(queued_rxs)) {
                if (peer->mxp_state != BMX_PEER_READY) {
                        gen_mutex_unlock(&peer->mxp_lock);
                        return;
                }
                rx = qlist_entry(queued_rxs->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&rx->mxc_list);
                gen_mutex_unlock(&peer->mxp_lock);
                bmx_post_rx(rx);
                gen_mutex_lock(&peer->mxp_lock);
        }
        gen_mutex_unlock(&peer->mxp_lock);

        BMX_EXIT;

        return;
}

static void
bmx_peer_post_queued_txs(struct bmx_peer *peer)
{
        struct bmx_ctx  *tx             = NULL;
        list_t          *queued_txs     = &peer->mxp_queued_txs;

        BMX_ENTER;

        gen_mutex_lock(&peer->mxp_lock);
        while (!qlist_empty(queued_txs)) {
                if (peer->mxp_state != BMX_PEER_READY) {
                        gen_mutex_unlock(&peer->mxp_lock);
                        return;
                }
                tx = qlist_entry(queued_txs->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&tx->mxc_list);
                gen_mutex_unlock(&peer->mxp_lock);
                /* we may have posted this before we got the peer's id */
                bmx_create_match(tx);
                bmx_post_tx(tx);
                gen_mutex_lock(&peer->mxp_lock);
        }
        gen_mutex_unlock(&peer->mxp_lock);

        BMX_EXIT;

        return;
}


static int
bmx_post_unexpected_recv(mx_endpoint_addr_t source, uint8_t type, uint32_t id, 
                         uint32_t tag, uint64_t match, uint32_t length)
{
        int             ret     = 0;
        struct bmx_ctx  *rx     = NULL;
        struct bmx_peer *peer   = NULL;
        void            *peerp  = (void *) &peer;
        mx_return_t     mxret   = MX_SUCCESS;
        uint8_t         class = 0;

        BMX_ENTER;

        if (id == 0 && tag == 0 && type == 0) {
                bmx_parse_match(match, &type, &id, &tag, &class);
        }

        rx = bmx_get_idle_rx();
        if (rx != NULL) {
                mx_get_endpoint_addr_context(source, &peerp);
                peer = (struct bmx_peer *) peerp;
                if (peer == NULL) {
                        debug(BMX_DB_PEER, "unknown peer sent message 0x%llx "
                                        "length %u", llu(match), length);
                }
                bmx_peer_addref(peer); /* can peer be NULL? */
                rx->mxc_peer = peer;
                rx->mxc_msg_type = type;
                rx->mxc_tag = tag;
                rx->mxc_match = match;
                rx->mxc_seg.segment_ptr = rx->mxc_buffer;
                rx->mxc_seg.segment_length = length;
                rx->mxc_nseg = 1;
                rx->mxc_nob = (long long) length;

                if (length > BMX_UNEXPECTED_SIZE) {
                        debug(BMX_DB_WARN, "receiving unexpected msg with "
                                        "%d bytes. Will receive with length 0.",
                                        length);
                        rx->mxc_seg.segment_length = 0;
                }
                debug(BMX_DB_CTX, "%s rx match= 0x%llx length= %lld", __func__,
                                llu(rx->mxc_match), lld(rx->mxc_nob));
                mxret = mx_irecv(bmi_mx->bmx_ep, &rx->mxc_seg, rx->mxc_nseg,
                                 rx->mxc_match, BMX_MASK_ALL, (void *) rx, &rx->mxc_mxreq);
                if (mxret != MX_SUCCESS) {
                        debug((BMX_DB_MX|BMX_DB_CTX), "mx_irecv() failed with %s for an "
                                        "unexpected recv with tag %d length %d",
                                        mx_strerror(mxret), tag, length);
                        bmx_put_idle_ctx(rx);
                        ret = -1;
                }
        } else {
                ret = -1;
        }

        BMX_EXIT;

        return ret;
}

/* MX calls this function if an incoming msg does not match a posted recv.
 * MX blocks while in this function. Make it as fast as possible - 
 * do not allocate memory, etc.
 *
 * This function is also a nice way of finding early expected receives
 * before they are posted by PVFS/BMI.
 */
mx_unexp_handler_action_t
bmx_unexpected_recv(void *context, mx_endpoint_addr_t source,
                      uint64_t match_value, uint32_t length, void *data_if_available)
{
        int                     ret     = MX_RECV_CONTINUE;
        struct bmx_ctx          *rx     = NULL;
        uint8_t                 type    = 0;
        uint8_t                 class   = 0;
        uint32_t                id      = 0;
        uint32_t                tag     = 0;
        struct bmx_peer         *peer   = NULL;
        void                    *peerp  = &peer;
        mx_return_t             mxret   = MX_SUCCESS;

        bmx_parse_match(match_value, &type, &id, &tag, &class);

        switch (type) {
        case BMX_MSG_CONN_REQ:
                debug(BMX_DB_CONN, "CONN_REQ from %s", (char *) data_if_available);
                if (!bmi_mx->bmx_is_server) {
                        debug(BMX_DB_ERR, "client receiving CONN_REQ");
                        exit(1);
                }
                /* a client is trying to contact us */
                /* do not alloc peer which can block, post rx only */
                rx = bmx_get_idle_rx();
                if (rx != NULL) {
                        rx->mxc_msg_type = type;
                        rx->mxc_tag = tag; /* this is the bmi_mx version number */
                        rx->mxc_match = match_value;
                        rx->mxc_seg.segment_length = length;
                        rx->mxc_nseg = 1;
                        rx->mxc_nob = (long long) length;
                        debug(BMX_DB_CONN, "%s rx match= 0x%llx length= %lld", 
                                        __func__, llu(rx->mxc_match), lld(rx->mxc_nob));
                        mxret = mx_irecv(bmi_mx->bmx_ep, &rx->mxc_seg, rx->mxc_nseg,
                                         rx->mxc_match, BMX_MASK_ALL, (void *) rx, &rx->mxc_mxreq);
                        if (mxret != MX_SUCCESS) {
                                debug(BMX_DB_CONN, "mx_irecv() failed for an "
                                                "unexpected recv with %s", 
                                                mx_strerror(mxret));
                                bmx_put_idle_ctx(rx);
                                ret = MX_RECV_FINISHED;
                        }
                } else {
                        ret = MX_RECV_FINISHED;
                }
                break;
        case BMX_MSG_CONN_ACK:
                /* the server is replying to our CONN_REQ */
                mx_get_endpoint_addr_context(source, &peerp);
                peer = (struct bmx_peer *) peerp;
                if (peer == NULL) {
                        debug((BMX_DB_CONN|BMX_DB_PEER), "receiving CONN_ACK but "
                                        "the endpoint context does not have a peer");
                } else {
                        debug(BMX_DB_CONN, "CONN_ACK from %s id= %d", 
                                        peer->mxp_mxmap->mxm_peername, id);
                        if (tag == BMX_VERSION) {
                                debug(BMX_DB_CONN, "setting %s's state to READY", 
                                                 peer->mxp_mxmap->mxm_peername);
                                gen_mutex_lock(&peer->mxp_lock);
                                peer->mxp_tx_id = id;
                                peer->mxp_state = BMX_PEER_READY;
                                gen_mutex_unlock(&peer->mxp_lock);
                                bmx_peer_post_queued_rxs(peer);
                                bmx_peer_post_queued_txs(peer);
                        } else {
                                bmx_peer_disconnect(peer, 1, BMI_EPROTO);
                        }
                }
                /* we are done with the recv, drop it */
                ret = MX_RECV_FINISHED;
                break;
        case BMX_MSG_UNEXPECTED:
                if (!bmi_mx->bmx_is_server) {
                        void *peerp = &peer;
                        mx_get_endpoint_addr_context(source, &peerp);
                        peer = (struct bmx_peer *) peerp;
                        debug(BMX_DB_ERR, "client receiving unexpected message "
                                "from %s with mask 0x%llx length %u",
                                peer == NULL ? "unknown" : peer->mxp_mxmap->mxm_peername,
                                llu(match_value), length);
                        exit(1);
                }
                ret = bmx_post_unexpected_recv(source, type, id, tag, match_value, length);
                if (ret != 0) {
                        /* we will catch this later in testunexpected() */
                        debug(BMX_DB_CTX, "Missed unexpected receive");
                }
                ret = MX_RECV_CONTINUE;
                break;
        case BMX_MSG_EXPECTED:
                /* do nothing, BMI will post a recv for it */
                debug(BMX_DB_CTX, "Early expected message  length %u  tag %u  match "
                                "0x%llx", length, tag, llu(match_value));
                break;
        default:
                debug(BMX_DB_ERR, "received unexpected msg with type %d", type);
                exit(1);
                break;
        }

        return ret;
}

/* This is called before BMI_mx_initialize() on servers, do not use anything from bmx_data */
static struct bmi_method_addr *
bmx_alloc_method_addr(const char *peername, const char *hostname, uint32_t board, uint32_t ep_id)
{
        struct bmi_method_addr      *map            = NULL;
        struct bmx_method_addr  *mxmap          = NULL;

        BMX_ENTER;

        if (bmi_mx == NULL) {
                map = bmi_alloc_method_addr(
                    tmp_id, (bmi_size_t) sizeof(*mxmap));
        } else {
                map = bmi_alloc_method_addr(bmi_mx->bmx_method_id, (bmi_size_t) sizeof(*mxmap));
        }
        if (map == NULL) return NULL;

        mxmap = map->method_data;
        mxmap->mxm_map = map;
        mxmap->mxm_peername = strdup(peername);
        mxmap->mxm_hostname = strdup(hostname);
        mxmap->mxm_board = board;
        mxmap->mxm_ep_id = ep_id;
        /* mxmap->mxm_peer */

        BMX_EXIT;

        return map;
}


/* test for ICON_REQ messages (on the client)
 * if found
 *    get idle tx
 *    marshall CONN_REQ
 *    set peer state to WAIT
 *    send CONN_REQ
 */
static void
bmx_handle_icon_req(void)
{
        uint32_t        result  = 0;

        do {
                uint64_t        match   = (uint64_t) BMX_MSG_ICON_REQ << BMX_MSG_SHIFT;
                uint64_t        mask    = BMX_MASK_MSG;
                mx_status_t     status;

                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {
                        int                     length  = 0;
                        struct bmx_ctx          *tx     = NULL;
                        struct bmx_peer         *peer   = NULL;
                        struct bmx_method_addr  *mxmap  = NULL;

                        peer = (struct bmx_peer *) status.context;
                        mxmap = peer->mxp_mxmap;
                        debug(BMX_DB_CONN, "%s returned for %s with %s", __func__, 
                                        mxmap->mxm_peername, mx_strstatus(status.code));

                        if (status.code != MX_STATUS_SUCCESS) {
                                debug((BMX_DB_CONN|BMX_DB_PEER),
                                      "%s: connect to %s failed with %s", __func__,
                                      mxmap->mxm_peername, mx_strstatus(status.code));
                                bmx_peer_disconnect(peer, 0, bmx_mx_to_bmi_errno(status.code));
                                /* drop ref taken before calling mx_iconnect */
                                bmx_peer_decref(peer);
                                continue;
                        }

                        gen_mutex_lock(&peer->mxp_lock);
                        peer->mxp_epa = status.source;
                        gen_mutex_unlock(&peer->mxp_lock);
                        mx_set_endpoint_addr_context(peer->mxp_epa, (void *) peer);

                        tx = bmx_get_idle_tx();
                        if (tx == NULL) {
                                int     ret     = 0;
                                ret = bmx_ctx_alloc(&tx, BMX_REQ_TX);
                                if (ret != 0) {
                                        bmx_peer_disconnect(peer, 1, BMI_ENOMEM);
                                        /* drop ref taken before calling mx_iconnect */
                                        bmx_peer_decref(peer);
                                        continue;
                                }
                                tx->mxc_state = BMX_CTX_PREP;
                        }
                        tx->mxc_msg_type = BMX_MSG_CONN_REQ;
                        /* tx->mxc_mop unused */
                        tx->mxc_peer = peer;
                        tx->mxc_tag = BMX_VERSION;
                        bmx_create_match(tx);
                        length = strlen(bmi_mx->bmx_peername) + 1; /* pad for '\0' */
                        BMX_MALLOC(tx->mxc_buffer, length);
                        if (tx->mxc_buffer == NULL) {
                                bmx_peer_disconnect(peer, 1, BMI_ENOMEM);
                                /* drop ref taken before calling mx_iconnect */
                                bmx_peer_decref(peer);
                                continue;
                        }
                        snprintf(tx->mxc_buffer, length, "%s", bmi_mx->bmx_peername);
                        tx->mxc_seg.segment_ptr = tx->mxc_buffer;
                        tx->mxc_seg.segment_length = length;
                        tx->mxc_nseg = 1;
                        tx->mxc_state = BMX_CTX_PENDING;
                        debug(BMX_DB_CONN, "%s tx match= 0x%llx length= %lld", __func__,
                                        llu(tx->mxc_match), lld(tx->mxc_nob));
                        mx_isend(bmi_mx->bmx_ep, &tx->mxc_seg, tx->mxc_nseg, peer->mxp_epa,
                                 tx->mxc_match, (void *) tx, &tx->mxc_mxreq);
                }
        } while (result);

        return;
}

/* test for received CONN_REQ messages (on the server)
 * if found
 *    create peer
 *    create mxmap
 *    mx_iconnect() w/BMX_MSG_ICON_ACK
 */
static void
bmx_handle_conn_req(void)
{
        uint32_t        result  = 0;
        uint64_t        match   = (uint64_t) BMX_MSG_CONN_REQ << BMX_MSG_SHIFT;
        uint64_t        mask    = BMX_MASK_MSG;
        uint64_t        ack     = (uint64_t) BMX_MSG_ICON_ACK << BMX_MSG_SHIFT;
        mx_status_t     status;

        do {
                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {
                        uint8_t                 type    = 0;
                        uint8_t                 class   = 0;
                        uint32_t                id      = 0;
                        uint32_t                sid     = 0;
                        uint32_t                version = 0;
                        uint64_t                nic_id  = 0ULL;
                        uint32_t                ep_id   = 0;
                        mx_request_t            request;
                        struct bmx_ctx          *rx     = NULL;
                        struct bmx_peer         *peer   = NULL;
                        struct bmx_method_addr  *mxmap  = NULL;

                        rx = (struct bmx_ctx *) status.context;
                        debug(BMX_DB_CONN, "%s returned %s match 0x%llx with %s", __func__, 
                                        rx->mxc_type == BMX_REQ_TX ? "TX" : "RX",
                                        llu(rx->mxc_match), mx_strstatus(status.code));
                        if (rx->mxc_type == BMX_REQ_TX) {
                                /* ignore the client's completion of the CONN_REQ send */
                                struct bmx_ctx *tx = rx;
                                debug(BMX_DB_CONN, "CONN_REQ sent to %s",
                                                tx->mxc_peer->mxp_mxmap->mxm_peername);
                                /* drop ref taken before mx_iconnect() */
                                bmx_peer_decref(tx->mxc_peer);
                                bmx_put_idle_ctx(tx);
                                continue;
                        } else if (status.code != MX_STATUS_SUCCESS) {
                                bmx_peer_decref(rx->mxc_peer);
                                bmx_put_idle_ctx(rx);
                                continue;
                        }
                        bmx_parse_match(rx->mxc_match, &type, &id, &version,
                            &class);
                        if (version != BMX_VERSION) {
                                /* TODO send error conn_ack */
                                debug(BMX_DB_WARN, "version mismatch with peer "
                                                "%s (our version 0x%x, peer's version "
                                                "0x%x)", (char *) rx->mxc_buffer, 
                                                BMX_VERSION, version);
                                bmx_peer_decref(rx->mxc_peer);
                                bmx_put_idle_ctx(rx);
                                continue;
                        }
                        if (bmi_mx->bmx_is_server == 0) {
                                debug(BMX_DB_WARN, "received CONN_REQ on a client.");
                                bmx_peer_decref(rx->mxc_peer);
                                bmx_put_idle_ctx(rx);
                                continue;
                        }
                        mx_decompose_endpoint_addr2(status.source, &nic_id,
                                                    &ep_id, &sid);
                        {
                                void *peerp = &peer;
                                mx_get_endpoint_addr_context(status.source, &peerp);
                                peer = (struct bmx_peer *) peerp;
                        }
                        if (peer == NULL) { /* new peer */
                                int             ret             = 0;
                                char           *host            = NULL;
                                uint32_t        board           = 0;
                                uint32_t        ep_id           = 0;
                                const char     *peername        = rx->mxc_buffer;
                                struct bmi_method_addr *map         = NULL;

                                debug((BMX_DB_CONN|BMX_DB_PEER), "%s peer %s connecting",
                                                __func__, peername);

                                ret = bmx_parse_peername(peername, &host,
                                                         &board, &ep_id);
                                if (ret != 0) {
                                        debug(BMX_DB_CONN, "parse_peername() "
                                                        "failed on %s",
                                                        (char *) rx->mxc_buffer);
                                        bmx_peer_decref(rx->mxc_peer);
                                        bmx_put_idle_ctx(rx);
                                        continue;
                                }
                                map = bmx_alloc_method_addr(peername, host,
                                                            board, ep_id);
                                if (map == NULL) {
                                        debug((BMX_DB_CONN|BMX_DB_MEM), "unable to alloc a "
                                                        "method addr for %s", peername);
                                        bmx_peer_decref(rx->mxc_peer);
                                        bmx_put_idle_ctx(rx);
                                        continue;
                                }
                                free(host);
                                mxmap = map->method_data;
                                ret = bmx_peer_alloc(&peer, mxmap);
                                if (ret != 0) {
                                        debug((BMX_DB_CONN|BMX_DB_MEM), "unable to alloc a "
                                                        "peer for %s", peername);
                                        bmx_peer_decref(rx->mxc_peer);
                                        bmx_put_idle_ctx(rx);
                                        continue;
                                }
                        } else if (sid != peer->mxp_sid) { /* reconnecting peer */
                                /* cancel queued txs and rxs, pending rxs */
                                debug((BMX_DB_CONN|BMX_DB_PEER), "%s peer "
                                      "%s reconnecting", __func__, 
                                      peer->mxp_mxmap->mxm_peername);
                                if (peer->mxp_state == BMX_PEER_READY)
                                        bmx_peer_disconnect(peer, 0, BMI_ENETRESET);
                                mxmap = peer->mxp_mxmap;
                        } else {
                                debug((BMX_DB_CONN|BMX_DB_PEER), "%s peer "
                                      "%s reconnecting with same sid", __func__, 
                                      peer->mxp_mxmap->mxm_peername);
                                mxmap = peer->mxp_mxmap;
                        }
                        gen_mutex_lock(&peer->mxp_lock);
                        debug(BMX_DB_PEER, "Setting peer %s to BMX_PEER_WAIT",
                                           peer->mxp_mxmap->mxm_peername);
                        peer->mxp_state = BMX_PEER_WAIT;
                        peer->mxp_tx_id = id;
                        peer->mxp_sid = sid;
                        gen_mutex_unlock(&peer->mxp_lock);
                        bmx_peer_addref(peer); /* add ref until completion of CONN_ACK */
                        mx_iconnect(bmi_mx->bmx_ep, peer->mxp_nic_id, mxmap->mxm_ep_id,
                                    BMX_MAGIC, ack, peer, &request);
                        bmx_put_idle_ctx(rx);
                }
        } while (result);

        return;
}

/* test for ICON_ACK messages (on the server)
 * if found
 *    register mxmap
 *    get idle tx
 *    marshall CONN_ACK
 *    set peer state to READY
 *    send CONN_ACK
 */
static void
bmx_handle_icon_ack(void)
{
        uint32_t        result  = 0;
        struct bmx_ctx  *tx     = NULL;
        struct bmx_peer *peer   = NULL;

        if (!bmi_mx->bmx_is_server) return;
        do {
                uint64_t        match   = (uint64_t) BMX_MSG_ICON_ACK << BMX_MSG_SHIFT;
                uint64_t        mask    = BMX_MASK_MSG;
                mx_status_t     status;

                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {

                        peer = (struct bmx_peer *) status.context;
                        debug(BMX_DB_CONN, "%s returned for %s with %s", __func__, 
                                        peer->mxp_mxmap->mxm_peername, 
                                        mx_strstatus(status.code));
                        if (status.code != MX_STATUS_SUCCESS) {
                                debug((BMX_DB_CONN|BMX_DB_PEER|BMX_DB_WARN),
                                      "%s: connect to %s failed with %s", __func__,
                                      peer->mxp_mxmap->mxm_peername, 
                                      mx_strstatus(status.code));
                                bmx_peer_disconnect(peer, 1, bmx_mx_to_bmi_errno(status.code));
                                /* drop ref taken before calling mx_iconnect */
                                bmx_peer_decref(peer);
                                continue;
                        }
                        gen_mutex_lock(&peer->mxp_lock);
                        peer->mxp_epa = status.source;
                        debug(BMX_DB_PEER, "Setting peer %s to BMX_PEER_READY",
                                           peer->mxp_mxmap->mxm_peername);
                        peer->mxp_state = BMX_PEER_READY;
                        /* NOTE no need to call bmx_peer_post_queued_[rxs|txs]()
                         * since the server should not have any queued msgs */
                        gen_mutex_unlock(&peer->mxp_lock);
                        mx_set_endpoint_addr_context(peer->mxp_epa, (void *) peer);

                        tx = bmx_get_idle_tx();
                        if (tx == NULL) {
                                int     ret     = 0;
                                ret = bmx_ctx_alloc(&tx, BMX_REQ_TX);
                                if (ret != 0) {
                                        debug((BMX_DB_CONN|BMX_DB_MEM), "unable to alloc a "
                                                        "ctx to send a CONN_ACK to %s", 
                                                        peer->mxp_mxmap->mxm_peername);
                                        bmx_peer_disconnect(peer, 1, BMI_ENOMEM);
                                        /* drop ref taken before calling mx_iconnect */
                                        bmx_peer_decref(peer);
                                        continue;
                                }
                                tx->mxc_state = BMX_CTX_PREP;
                        }
                        tx->mxc_msg_type = BMX_MSG_CONN_ACK;
                        /* tx->mxc_mop unused */
                        tx->mxc_peer = peer;
                        tx->mxc_tag = BMX_VERSION;
                        bmx_create_match(tx);
                        tx->mxc_seg.segment_length = 0;
                        tx->mxc_nseg = 1;
                        debug(BMX_DB_CONN, "%s tx match= 0x%llx length= %lld", __func__,
                                        llu(tx->mxc_match), lld(tx->mxc_nob));
                        mx_isend(bmi_mx->bmx_ep, &tx->mxc_seg, tx->mxc_nseg, peer->mxp_epa,
                                 tx->mxc_match, (void *) tx, &tx->mxc_mxreq);
                        if (!peer->mxp_exist) {
                                debug(BMX_DB_PEER, "calling bmi_method_addr_reg_callback"
                                      "on %s", peer->mxp_mxmap->mxm_peername);
                                bmi_method_addr_reg_callback(peer->mxp_map);
                                peer->mxp_exist = 1;
                        }
                }
        } while (result);

        return;
}

/* test for CONN_ACK messages (on the server)
 * Since the unexpected_recv() function drops the CONN_ACK on the client
 * side, we only need this on the server to get the completion and 
 * put the tx on the idle list. */
static void
bmx_handle_conn_ack(void)
{
        uint32_t        result  = 0;
        struct bmx_ctx  *tx     = NULL;

        if (!bmi_mx->bmx_is_server) goto out;
        do {
                uint64_t        match   = (uint64_t) BMX_MSG_CONN_ACK << BMX_MSG_SHIFT;
                uint64_t        mask    = BMX_MASK_MSG;
                mx_status_t     status;

                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {
                        tx = (struct bmx_ctx *) status.context;
                        debug(BMX_DB_CONN, "%s returned tx match 0x%llx with %s", __func__, 
                                        llu(tx->mxc_match), mx_strstatus(status.code));
                        bmx_peer_decref(tx->mxc_peer);
                        bmx_put_idle_ctx(tx);
                }
        } while (result);

out:
        return;
}

static void
bmx_connection_handlers(void)
{
        static int      count   = 0;
        int             print   = (count++ % 1000 == 0);

        if (print)
                BMX_ENTER;

        /* push connection messages along */
        bmx_handle_icon_req();
        bmx_handle_conn_req();
        bmx_handle_icon_ack();
        bmx_handle_conn_ack();
        if (print)
                BMX_EXIT;
        return;
}

static void
bmx_complete_ctx(struct bmx_ctx *ctx, bmi_op_id_t *outid, bmi_error_code_t *err,
                 bmi_size_t *size, void **user_ptr)
{
        struct bmx_peer *peer   = ctx->mxc_peer;

        *outid = ctx->mxc_mop->op_id;
        *err = ctx->mxc_error;
        *size = ctx->mxc_mxstat.xfer_length;
        if (user_ptr)
                *user_ptr = ctx->mxc_mop->user_ptr;
        PINT_EVENT_END(
            (ctx->mxc_type == BMX_REQ_TX ?
             bmi_mx_send_event_id : bmi_mx_recv_event_id),
            bmi_mx_pid, NULL, ctx->mxc_mop->event_id,
            *outid, *size);

        id_gen_fast_unregister(ctx->mxc_mop->op_id);
        BMX_FREE(ctx->mxc_mop, sizeof(*ctx->mxc_mop));
        bmx_put_idle_ctx(ctx);
        bmx_peer_decref(peer); /* drop the ref taken in [send|recv]_common */

        return;
}

static int
BMI_mx_test(bmi_op_id_t id, int *outcount, bmi_error_code_t *err,
            bmi_size_t *size, void **user_ptr, int max_idle_time __unused,
            bmi_context_id context_id)
{
        uint32_t         result = 0;
        struct method_op *mop   = NULL;
        struct bmx_ctx   *ctx   = NULL;
        struct bmx_peer  *peer  = NULL;

        BMX_ENTER;

        bmx_connection_handlers();

        bmx_get_completion_token();

        mop = id_gen_fast_lookup(id);
        ctx = mop->method_data;
        peer = ctx->mxc_peer;

        assert(context_id == mop->context_id);
        if (ctx->mxc_type == BMX_REQ_RX)
                assert(ctx->mxc_msg_type != BMX_MSG_UNEXPECTED);
        assert(context_id == ctx->mxc_mop->context_id);

        switch (ctx->mxc_state) {
        case BMX_CTX_COMPLETED:
        case BMX_CTX_CANCELED:
                gen_mutex_lock(&bmi_mx->bmx_done_q_lock[(int) context_id]);
                qlist_del_init(&ctx->mxc_list);
                gen_mutex_unlock(&bmi_mx->bmx_done_q_lock[(int) context_id]);
                bmx_complete_ctx(ctx, &id, err, size, user_ptr);
                *outcount = 1;
                break;
        case BMX_CTX_PENDING:
                mx_test(bmi_mx->bmx_ep, &ctx->mxc_mxreq, &ctx->mxc_mxstat, &result);
                if (result) {
                        bmx_deq_pending_ctx(ctx);
                        bmx_complete_ctx(ctx, &id, err, size, user_ptr);
                        *outcount = 1;
                }
                break;
        default:
                debug(BMX_DB_CTX, "%s called on %s with state %d", __func__,
                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX", ctx->mxc_state);
        }
        bmx_release_completion_token();
        BMX_EXIT;

        return 0;
}


static int
BMI_mx_testcontext(int incount, bmi_op_id_t *outids, int *outcount,
            bmi_error_code_t *errs, bmi_size_t *sizes, void **user_ptrs,
            int max_idle_time, bmi_context_id context_id)
{
        int             i               = 0;
        int             completed       = 0;
        int             old             = 0;
        uint64_t        match           = 0ULL;
        uint64_t        mask            = BMX_MASK_MSG;
        struct bmx_ctx  *ctx            = NULL;
        struct bmx_peer *peer           = NULL;
        int             wait            = 0;
        static int      count           = 0;
        int             print           = 0;

        if (count++ % 1000 == 0) {
                BMX_ENTER;
                print = 1;
        }

        bmx_connection_handlers();

        bmx_get_completion_token();

        /* always return queued, completed messages first */
        do {
                bmx_deq_completed(&ctx, context_id);
                if (ctx) {
                        bmx_complete_ctx(ctx, &outids[completed], &errs[completed],
                                         &sizes[completed], &user_ptrs[completed]);
                        completed++;
                }
        } while (completed < incount && ctx != NULL);

        if (completed > 0)
                debug(BMX_DB_CTX, "%s found %d completed messages", __func__, completed);

        /* try to complete expected messages
         * we will always try (incount - completed) times even
         *     if some iterations have no result */

        match = (uint64_t) BMX_MSG_EXPECTED << BMX_MSG_SHIFT;
        for (i = completed; i < incount; i++) {
                uint32_t        result  = 0;
                mx_status_t     status;

                old = completed;

                if (wait == 0 || wait == 2) {
                        mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                        if (!result && wait == 0 && max_idle_time > 0) wait = 1;
                } else { /* wait == 1 */
                        mx_wait_any(bmi_mx->bmx_ep, max_idle_time, match, mask, 
                                    &status, &result);
                        wait = 2;
                }

                if (result) {
                        ctx = (struct bmx_ctx *) status.context;
                        bmx_deq_pending_ctx(ctx);
                        if (ctx->mxc_mop->context_id != context_id) {
                                bmx_q_completed(ctx, BMX_CTX_COMPLETED, status,
                                                bmx_mx_to_bmi_errno(status.code));
                                continue;
                        }
                        ctx->mxc_mxstat = status;
                        peer = ctx->mxc_peer;
                        debug(BMX_DB_CTX, "%s completing expected %s with match 0x%llx "
                                        "for %s with op_id %llu length %d %s "
                                        "context_id= %d mop->context_id= %d",
                                        __func__, ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX",
                                        llu(ctx->mxc_match), peer->mxp_mxmap->mxm_peername,
                                        llu(ctx->mxc_mop->op_id), status.xfer_length,
                                        mx_strstatus(status.code), (int) context_id,
                                        (int) ctx->mxc_mop->context_id);

                        bmx_complete_ctx(ctx, &outids[completed], &errs[completed],
                                         &sizes[completed], &user_ptrs[completed]);
                        completed++;
                }
        }

        if (completed - old > 0)
                debug(BMX_DB_CTX, "%s found %d expected messages", __func__, completed - old);

        /* try to complete unexpected sends */

        match = (uint64_t) BMX_MSG_UNEXPECTED << BMX_MSG_SHIFT;

        old = completed;

        for (i = completed; i < incount; i++) {
                uint32_t        result          = 0;
                mx_status_t     status;
                int             again           = 1;

                ctx = NULL;

                while (!ctx && again) {
                        again = 0;
                        mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                        if (result) {
                                ctx = (struct bmx_ctx *) status.context;
                                bmx_deq_pending_ctx(ctx);
                                peer = ctx->mxc_peer;
                                if (ctx->mxc_type == BMX_REQ_RX ||
                                    ctx->mxc_mop->context_id != context_id) {
                                        /* queue until testunexpected or queue
                                         * until testcontext for the correct context */
                                        bmx_q_completed(ctx, BMX_CTX_COMPLETED, status,
                                                        bmx_mx_to_bmi_errno(status.code));
                                        result = 0;
                                        again = 1;
                                        ctx = NULL;
                                }
                        }
                }
                if (result) {
                        debug(BMX_DB_CTX, "%s completing unexpected %s with "
                                        "match 0x%llx for %s with op_id %llu", 
                                        __func__, 
                                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX",
                                        llu(ctx->mxc_match), 
                                        peer->mxp_mxmap->mxm_peername,
                                        llu(ctx->mxc_mop->op_id));

                        ctx->mxc_mxstat = status;
                        bmx_complete_ctx(ctx, &outids[completed], &errs[completed],
                                         &sizes[completed], &user_ptrs[completed]);

                        if (status.code != MX_SUCCESS) {
                                debug(BMX_DB_CTX, "%s unexpected send completed with "
                                      "error %s", __func__, mx_strstatus(status.code));
                                bmx_peer_disconnect(peer, 0, BMI_ENETRESET);
                        }
                        completed++;
                }
        }
        bmx_release_completion_token();

        if (completed - old > 0) {
                debug(BMX_DB_CTX, "%s found %d unexpected tx messages", 
              __func__, completed - old);
        }

        if (print)
                BMX_EXIT;

        *outcount = completed;
        return completed;
}

/* test for unexpected receives only, not unex sends */
static int
BMI_mx_testunexpected(int incount __unused, int *outcount,
            struct bmi_method_unexpected_info *ui, uint8_t class, int max_idle_time __unused)
{
        uint32_t        result          = 0;
        uint64_t        match           = ((uint64_t) BMX_MSG_UNEXPECTED << BMX_MSG_SHIFT);
        uint64_t        mask            = BMX_MASK_MSG;
        mx_status_t     status;
        static int      count           = 0;
        int             print           = 0;
        struct bmx_ctx  *rx             = NULL;
        struct bmx_peer *peer           = NULL;
        int             again           = 1;
        uint64_t        class_match     = 0;

        if (count++ % 1000 == 0) {
                BMX_ENTER;
                print = 1;
        }

        bmx_connection_handlers();

        bmx_get_completion_token();

        /* must match the correct class as well */
        class_match += class;
        match |= (class_match << BMX_CLASS_SHIFT);

        /* if the unexpected handler cannot get a rx, it does not post a receive.
         * probe for unexpected and post a rx. */
        mx_iprobe(bmi_mx->bmx_ep, match, mask, &status, &result);
        if (result) {
                int     ret     = 0;
                ret = bmx_post_unexpected_recv(status.source, 0, 0, 0,
                                               status.match_info,
                                               status.xfer_length);
                if (ret != 0) {
                        debug(BMX_DB_CTX, "%s mx_iprobe() found rx with match 0x%llx "
                                        "length %d but could not receive it", __func__,
                                        llu(status.match_info), status.xfer_length);
                }
        }

        /* check for unexpected receives */
        *outcount = 0;

        bmx_deq_unex_rx(&rx);
        if (rx) {
                result = 1;
                status = rx->mxc_mxstat;
                peer = rx->mxc_peer;
        }

        while (!rx && again) {
                again = 0;
                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {
                        rx   = (struct bmx_ctx *) status.context;
                        bmx_deq_pending_ctx(rx);
                        peer = rx->mxc_peer;
                        if (rx->mxc_type == BMX_REQ_TX) {
                                bmx_q_completed(rx, BMX_CTX_COMPLETED, status,
                                                bmx_mx_to_bmi_errno(status.code));
                                result = 0;
                                again = 1;
                                rx = NULL;
                        }
                }
        }

        if (result) {
                debug(BMX_DB_CTX, "%s completing RX with match 0x%llx for %s",
                      __func__, llu(rx->mxc_match), peer->mxp_mxmap->mxm_peername);

                ui->error_code = 0;
                ui->addr = peer->mxp_map;
                ui->size = rx->mxc_nob;
                /* avoid a memcpy by giving the rx buffer to BMI
                 * and malloc a new one for the rx */
                ui->buffer = rx->mxc_buffer;
                rx->mxc_buffer = bmx_memalloc(BMX_UNEXPECTED_SIZE, BMX_UNEX_BUF);
                rx->mxc_seg.segment_ptr = rx->mxc_buffer;
                ui->tag = rx->mxc_tag;

                bmx_put_idle_ctx(rx);
                bmx_peer_decref(peer); /* drop the ref taken in unexpected_recv() */
                *outcount = 1;
        }
        bmx_release_completion_token();

        if (print)
                BMX_EXIT;

        return 0;
}

static void
bmx_create_peername(void)
{
        char    peername[MX_MAX_HOSTNAME_LEN + 28]; /* mx://host:board:ep_id\0 */

        if (bmi_mx->bmx_board != -1) {
                /* mx://host:board:ep_id\0 */
                sprintf(peername, "mx://%s:%u:%u", bmi_mx->bmx_hostname,
                        bmi_mx->bmx_board, bmi_mx->bmx_ep_id);
        } else {
                /* mx://host:ep_id\0 */
                sprintf(peername, "mx://%s:%u", bmi_mx->bmx_hostname, 
                        bmi_mx->bmx_ep_id);
        }
        bmi_mx->bmx_peername = strdup(peername);
        return;
}

static int
bmx_peer_connect(struct bmx_peer *peer)
{
        int                     ret    = 0;
        uint64_t                nic_id = 0ULL;
        mx_request_t            request;
        uint64_t                match  = (uint64_t) BMX_MSG_ICON_REQ << BMX_MSG_SHIFT;
        struct bmx_method_addr *mxmap  = peer->mxp_mxmap;

        BMX_ENTER;

        gen_mutex_lock(&peer->mxp_lock);
        if (peer->mxp_state == BMX_PEER_INIT) {
                debug(BMX_DB_PEER, "Setting peer %s to BMX_PEER_WAIT",
                                   peer->mxp_mxmap->mxm_peername);
                peer->mxp_state = BMX_PEER_WAIT;
        } else {
                gen_mutex_unlock(&peer->mxp_lock);
                return 0;
        }
        gen_mutex_unlock(&peer->mxp_lock);
        bmx_peer_addref(peer); /* add ref until completion of CONN_REQ */

        /* if we have not opened our endpoint, do so now */
        if (bmi_mx->bmx_ep == NULL) {
                char                    host[MX_MAX_HOSTNAME_LEN + 1];
                char                    *colon  = NULL;
                mx_endpoint_addr_t      epa;

                ret = bmx_open_endpoint(&bmi_mx->bmx_ep,
                                        MX_ANY_NIC,
                                        MX_ANY_ENDPOINT);
                if (ret != 0) {
                        debug((BMX_DB_MX|BMX_DB_CONN), "failed to open endpoint when "
                                        "trying to conenct to %s", mxmap->mxm_peername);
                        bmx_peer_decref(peer);
                        return ret;
                }
                mx_get_endpoint_addr(bmi_mx->bmx_ep, &epa);
                /* get our nic_id and ep_id */
                mx_decompose_endpoint_addr2(epa, &nic_id, &bmi_mx->bmx_ep_id,
                                            &bmi_mx->bmx_sid);
                /* get our board number */
                mx_nic_id_to_board_number(nic_id, &bmi_mx->bmx_board);
                /* get our hostname */
                mx_nic_id_to_hostname(nic_id, host);
                bmi_mx->bmx_hostname = strdup(host);
                if (bmi_mx->bmx_hostname == NULL) {
                        debug((BMX_DB_MX|BMX_DB_CONN), "%s mx_nic_id_to_hostname() did "
                                        "not find hostname %s", __func__, host);
                        return -1;
                }
                /* if hostname has :board, remove it */
                colon = strchr(bmi_mx->bmx_hostname, ':');
                if (colon != NULL) {
                        *colon = '\0';
                } else {
                        /* no board number in our name */
                        bmi_mx->bmx_board = -1;
                }
                /* create our peername */
                bmx_create_peername();
        }
        /* this is a new peer, start the connect handshake
         * by calling mx_iconnect() w/BMX_MSG_ICON_REQ */
        mx_iconnect(bmi_mx->bmx_ep, peer->mxp_nic_id, mxmap->mxm_ep_id,
                    BMX_MAGIC, match, (void *) peer, &request);

        BMX_EXIT;

        return ret;
}

/* if it is a new peer:
 *    alloc method_addr
 *    alloc peer
 *    if client
 *       open endpoint
 *       mx_iconnect()
 */
/* This is called on the server before BMI_mx_initialize(). */
static struct bmi_method_addr *
BMI_mx_method_addr_lookup(const char *id)
{
        int                     ret     = 0;
        int                     len     = 0;
        char                   *host    = NULL;
        uint32_t                board   = 0;
        uint32_t                ep_id   = 0;
        struct bmi_method_addr *map     = NULL;
        struct bmx_method_addr *mxmap   = NULL;

        BMX_ENTER;

        debug(BMX_DB_INFO, "%s with id %s", __func__, id);
        ret = bmx_parse_peername(id, &host, &board, &ep_id);
        if (ret != 0) {
                debug(BMX_DB_PEER, "%s method_parse_peername() failed on %s", __func__, id);
                return NULL;
        }

        if (bmi_mx != NULL) {
                struct bmx_peer         *peer   = NULL;

                gen_mutex_lock(&bmi_mx->bmx_peers_lock);
                qlist_for_each_entry(peer, &bmi_mx->bmx_peers, mxp_list) {
                        mxmap = peer->mxp_mxmap;
                        if (!strcmp(mxmap->mxm_hostname, host) &&
                            mxmap->mxm_board == board &&
                            mxmap->mxm_ep_id == ep_id) {
                                map = peer->mxp_map;
                                len = strlen(host);
                                BMX_FREE(host, len);
                                break;
                        }
                }
                gen_mutex_unlock(&bmi_mx->bmx_peers_lock);
        }
        
        if (map == NULL) {
                map = bmx_alloc_method_addr(id, host, board, ep_id);
                if (bmi_mx != NULL) {
                        struct bmx_peer *peer   = NULL;

                        mxmap = map->method_data;
                        ret = bmx_peer_alloc(&peer, mxmap);
                        if (ret != 0) {
                                debug((BMX_DB_MEM|BMX_DB_PEER), "%s unable to alloc peer "
                                                "for %s", __func__, mxmap->mxm_peername);
                                goto out;
                        }
                        ret = bmx_peer_connect(peer);
                        if (ret != 0) {
                                debug((BMX_DB_CONN|BMX_DB_MX|BMX_DB_PEER), "%s peer_connect()"
                                       " failed with %d", __func__, ret);
                        }
                }
                if (map != NULL) free(host);
        }
out:
        BMX_EXIT;

        return map;
}

static int
BMI_mx_open_context(bmi_context_id context_id __unused)
{
        return 0;
}

static void
BMI_mx_close_context(bmi_context_id context_id __unused)
{
        return;
}

/* NOTE There may be a race between this and BMI_mx_testcontext(). */
static int
BMI_mx_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
        struct method_op        *mop;
        struct bmx_ctx          *ctx     = NULL;
        struct bmx_peer         *peer   = NULL;
        uint32_t                result  = 0;

        BMX_ENTER;

        bmx_get_completion_token();

        mop = id_gen_fast_lookup(id);
        ctx = mop->method_data;
        peer = ctx->mxc_peer;

        assert(context_id == ctx->mxc_mop->context_id);

        debug(BMX_DB_CTX, "%s %s op_id %llu mxc_state %d peer state %d", __func__, 
                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX", 
                        llu(ctx->mxc_mop->op_id), ctx->mxc_state, peer->mxp_state);

        /* avoid race with connection setup */
        gen_mutex_lock(&peer->mxp_lock);

        switch (ctx->mxc_state) {
        case BMX_CTX_QUEUED:
                qlist_del_init(&ctx->mxc_list);
                gen_mutex_unlock(&peer->mxp_lock);
                bmx_q_completed(ctx, BMX_CTX_CANCELED, BMX_NO_STATUS, BMI_ECANCEL);
                break;
        case BMX_CTX_PENDING:
                gen_mutex_unlock(&peer->mxp_lock);
                if (ctx->mxc_type == BMX_REQ_TX) {
                        /* see if it completed first */
                        mx_test(bmi_mx->bmx_ep, &ctx->mxc_mxreq, &ctx->mxc_mxstat, &result);
                        if (result == 1) {
                                debug(BMX_DB_CTX, "%s completed TX op_id %llu "
                                      "mxc_state %d peer state %d status.code %s",
                                      __func__, llu(ctx->mxc_mop->op_id), ctx->mxc_state,
                                      peer->mxp_state, mx_strstatus(ctx->mxc_mxstat.code));
                                bmx_deq_pending_ctx(ctx);
                                bmx_q_completed(ctx, BMX_CTX_CANCELED,
                                                ctx->mxc_mxstat, BMI_ECANCEL);
                        } else {
                                /* and if not, then disconnect() */
                                bmx_peer_disconnect(peer, 1, BMI_ENETRESET);
                        }
                } else { /* BMX_REQ_RX */
                        mx_cancel(bmi_mx->bmx_ep, &ctx->mxc_mxreq, &result);
                        if (result == 1) {
                                bmx_deq_pending_ctx(ctx);
                                bmx_q_completed(ctx, BMX_CTX_CANCELED,
                                                BMX_NO_STATUS, BMI_ECANCEL);
                        }
                }
                break;
        default:
                debug(BMX_DB_CTX, "%s called on %s with state %d", __func__,
                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX", ctx->mxc_state);
        }
        bmx_release_completion_token();

        BMX_EXIT;

        return 0;
}

/* Unlike bmi_ib, we always know our peername, check if the peer exists */
static const char *
BMI_mx_rev_lookup(struct bmi_method_addr *meth)
{
        struct bmx_method_addr  *mxmap = meth->method_data;

        BMX_ENTER;

        if (mxmap->mxm_peer && mxmap->mxm_peer->mxp_state != BMX_PEER_DISCONNECT)
                return mxmap->mxm_peername;
        else
                return "(unconnected)";
}


const struct bmi_method_ops bmi_mx_ops = 
{
    .method_name               = "bmi_mx",
    .flags = 0,
    .initialize                = BMI_mx_initialize,
    .finalize                  = BMI_mx_finalize,
    .set_info                  = BMI_mx_set_info,
    .get_info                  = BMI_mx_get_info,
    .memalloc                  = BMI_mx_memalloc,
    .memfree                   = BMI_mx_memfree,
    .unexpected_free           = BMI_mx_unexpected_free,
    .test                      = BMI_mx_test,
    .testsome                  = 0,
    .testcontext               = BMI_mx_testcontext,
    .testunexpected            = BMI_mx_testunexpected,
    .method_addr_lookup        = BMI_mx_method_addr_lookup,
    .post_send_list            = BMI_mx_post_send_list,
    .post_recv_list            = BMI_mx_post_recv_list,
    .post_sendunexpected_list  = BMI_mx_post_sendunexpected_list,
    .open_context              = BMI_mx_open_context,
    .close_context             = BMI_mx_close_context,
    .cancel                    = BMI_mx_cancel,
    .rev_lookup_unexpected     = BMI_mx_rev_lookup,
};

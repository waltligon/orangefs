/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *   Copyright (C) 2006 Myricom, Inc.
 *   Author: Myricom, Inc. <help at myri.com>
 *
 *   See COPYING in top-level directory.
 */

#include "mx.h"

static int      tmp_id  = 0;    /* temporary id until bmi_mx is init'ed */
struct bmx_data *bmi_mx = NULL; /* global state for bmi_mx */

#if BMX_MEM_ACCT
uint64_t        mem_used = 0;   /* bytes used */
gen_mutex_t     mem_used_lock;  /* lock */
#endif

#if BMX_LOGGING
int     send_start;
int     send_finish;
int     recv_start;
int     recv_finish;
int     sendunex_start;
int     sendunex_finish;
int     recvunex_start;
int     recvunex_finish;
#endif

mx_unexp_handler_action_t
bmx_unexpected_recv(void *context, mx_endpoint_addr_t source,
                      uint64_t match_value, uint32_t length, void *data_if_available);

static int
bmx_peer_connect(struct bmx_peer *peer);

/**** TX/RX handling functions **************************************/

void
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

int
bmx_ctx_alloc(struct bmx_ctx **ctxp, enum bmx_req_type type)
{
        struct bmx_ctx *ctx     = NULL;

        if (ctxp == NULL) return -EINVAL;

        BMX_MALLOC(ctx, sizeof(*ctx));
        if (ctx == NULL) return -ENOMEM;

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
                BMX_MALLOC(ctx->mxc_buffer, BMX_UNEXPECTED_SIZE);
                if (ctx->mxc_buffer == NULL) {
                        bmx_ctx_free(ctx);
                        return -ENOMEM;
                }
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

void
bmx_ctx_init(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer   = NULL;

        if (ctx == NULL) return;

        peer = ctx->mxc_peer;

        /* ctx->mxc_type */
        ctx->mxc_state = BMX_CTX_IDLE;
        /* ctx->mxc_msg_type */
        
        /* ctx->mxc_global_list */
        if (!qlist_empty(&ctx->mxc_list)) {
                if (peer != NULL) gen_mutex_lock(&peer->mxp_lock);
                qlist_del_init(&ctx->mxc_list);
                if (peer != NULL) gen_mutex_unlock(&peer->mxp_lock);
        }

        ctx->mxc_mop = NULL;
        ctx->mxc_peer = NULL;
        ctx->mxc_tag = 0;
        ctx->mxc_match = 0LL;

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

        return;
}

/* add to peer's queued txs/rxs list */
void
bmx_q_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;
        list_t          *queue = ctx->mxc_type == BMX_REQ_TX ? &peer->mxp_queued_txs :
                                                              &peer->mxp_queued_rxs;

        ctx->mxc_state = BMX_CTX_QUEUED;
        gen_mutex_lock(&peer->mxp_lock);
        qlist_add_tail(&ctx->mxc_list, queue);
        gen_mutex_unlock(&peer->mxp_lock);
        return;
}

/* remove from peer's queued txs/rxs list */
void
bmx_deq_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;

        if (!qlist_empty(&ctx->mxc_list)) {
                gen_mutex_lock(&peer->mxp_lock);
                qlist_del_init(&ctx->mxc_list);
                gen_mutex_unlock(&peer->mxp_lock);
        }
        return;
}

/* add to peer's pending rxs list */
void
bmx_q_pending_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;

        ctx->mxc_state = BMX_CTX_PENDING;
        if (ctx->mxc_type == BMX_REQ_RX) {
                if (peer) {
                        gen_mutex_lock(&peer->mxp_lock);
                        qlist_add_tail(&ctx->mxc_list, &peer->mxp_pending_rxs);
                        gen_mutex_unlock(&peer->mxp_lock);
                }
        }
        return;
}

/* remove from peer's pending rxs list */
void
bmx_deq_pending_ctx(struct bmx_ctx *ctx)
{
        struct bmx_peer *peer = ctx->mxc_peer;

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
        return;
}

/* add to the global canceled list */
void
bmx_q_canceled_ctx(struct bmx_ctx *ctx)
{
        ctx->mxc_state = BMX_CTX_CANCELED;
        gen_mutex_lock(&bmi_mx->bmx_canceled_lock);
        qlist_add_tail(&ctx->mxc_list, &bmi_mx->bmx_canceled);
        gen_mutex_unlock(&bmi_mx->bmx_canceled_lock);
        return;
}

struct bmx_ctx *
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

void
bmx_put_idle_rx(struct bmx_ctx *rx)
{
        if (rx == NULL) {
                debug(BMX_DB_WARN, "put_idle_rx() called with NULL");
                return;
        }
        if (rx->mxc_type != BMX_REQ_RX) {
                debug(BMX_DB_WARN, "put_idle_rx() called with a TX");
                return;
        }
        if (rx->mxc_get != rx->mxc_put + 1) {
                debug(BMX_DB_ERR, "put_idle_rx() get (%llu) != put (%llu) + 1",
                         (unsigned long long) rx->mxc_get,
                         (unsigned long long) rx->mxc_put);
                exit(1);
        }
        bmx_ctx_init(rx);
        rx->mxc_put++;
        gen_mutex_lock(&bmi_mx->bmx_idle_rxs_lock);
        qlist_add(&rx->mxc_list, &bmi_mx->bmx_idle_rxs);
        gen_mutex_unlock(&bmi_mx->bmx_idle_rxs_lock);
        return;
}

void
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

struct bmx_ctx *
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

void
bmx_put_idle_tx(struct bmx_ctx *tx)
{
        if (tx == NULL) {
                debug(BMX_DB_WARN, "put_idle_tx() called with NULL");
                return;
        }
        if (tx->mxc_type != BMX_REQ_TX) {
                debug(BMX_DB_WARN, "put_idle_tx() called with a TX");
                return;
        }
        if (tx->mxc_get != tx->mxc_put + 1) {
                debug(BMX_DB_ERR, "put_idle_tx() get (%llu) != put (%llu) + 1",
                         (unsigned long long) tx->mxc_get,
                         (unsigned long long) tx->mxc_put);
                exit(1);
        }
        bmx_ctx_init(tx);
        tx->mxc_put++;
        gen_mutex_lock(&bmi_mx->bmx_idle_txs_lock);
        qlist_add(&tx->mxc_list, &bmi_mx->bmx_idle_txs);
        gen_mutex_unlock(&bmi_mx->bmx_idle_txs_lock);
        return;
}

/**** peername parsing functions **************************************/

int
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

int
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
 * this is pretty robust but if strtol() fails for board or ep_id, it
 * returns 0 and we do not know that it failed. 
 * This handles legal hostnames (1-63 chars) include a-zA-Z0-9 as well as . and -
 * It will accept IPv4 addresses but not IPv6 (too many semicolons) */
int
bmx_parse_peername(const char *peername, char **hostname, uint32_t *board, uint32_t *ep_id)
{
        int             ret             = 0;
        int             len             = 0;
        int             colon1_found    = 0;
        int             colon2_found    = 0;
        char           *s               = NULL;
        char           *colon1          = NULL;
        char           *colon2          = NULL;
        char           *fs              = NULL;
        char           *host            = NULL;
        uint32_t        bd              = 0;
        uint32_t        ep              = 0;

        if (peername == NULL || hostname == NULL || board == NULL || ep_id == NULL) {
                debug(BMX_DB_INFO, "parse_peername() called with invalid parameter");
                return -EINVAL;
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
                        debug(BMX_DB_INFO, "parse_peername() strrchr() returned the same ':'");
                } else {
                        colon2_found = 1;
                        *colon2 = '\0';
                }
                colon1_found = 1;
                *colon1 = '\0';
        }
        /* s      = hostname\0board\0ep_id\0filesystem
         * colon1 =         \0board\0ep_id\0filesystem
         * colon2 =                \0ep_id\0filesystem
         * fs     =                       \0filesystem
         */

        colon1++;
        colon2++;

        /* make sure there are no more ':' in the strings */
        if (colon1_found && colon2_found) {
                if (NULL != strchr(colon1, ':') ||
                    NULL != strchr(colon2, ':')) {
                        debug(BMX_DB_INFO, "parse_peername() too many ':' (%s %s)", 
                                           colon1, colon2);
                        len = sizeof(*s);
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
        strcpy(host, s);

        if (colon1_found) {
                bd = (uint32_t) strtol(colon1, NULL, 0);
                if (colon2_found) {
                        ep = (uint32_t) strtol(colon2, NULL, 0);
                }
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

void
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

void
bmx_peer_addref(struct bmx_peer *peer)
{
        gen_mutex_lock(&peer->mxp_lock);
        debug(BMX_DB_PEER, "%s refcount was %d", __func__, peer->mxp_refcount);
        peer->mxp_refcount++;
        gen_mutex_unlock(&peer->mxp_lock);
        return;
}

void
bmx_peer_decref(struct bmx_peer *peer)
{
        gen_mutex_lock(&peer->mxp_lock);
        if (peer->mxp_refcount == 0) {
                debug(BMX_DB_WARN, "peer_decref() called for %s when refcount == 0",
                                peer->mxp_mxmap->mxm_peername);
        }
        peer->mxp_refcount--;
        if (peer->mxp_refcount == 1 && peer->mxp_state == BMX_PEER_DISCONNECT) {
                /* all txs and rxs are completed or canceled, reset state */
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
        return;
}

int
bmx_peer_alloc(struct bmx_peer **peerp, struct bmx_method_addr *mxmap)
{
        int              i              = 0;
        int              ret            = 0;
        char             name[MX_MAX_HOSTNAME_LEN + 1];
        uint64_t         nic_id         = 0LL;
        mx_return_t      mxret          = MX_SUCCESS;
        struct bmx_peer *peer           = NULL;

        if (peerp == NULL) {
                debug(BMX_DB_PEER, "peer_alloc() peerp = NULL");
                return -1;
        }
        BMX_MALLOC(peer, sizeof(*peer));
        if (!peer) {
                return -ENOMEM;
        }
        peer->mxp_map   = mxmap->mxm_map;
        peer->mxp_mxmap = mxmap;

        memset(name, 0, sizeof(*name));
        sprintf(name, "%s:%d", mxmap->mxm_hostname, mxmap->mxm_board);
        mxret = mx_hostname_to_nic_id(name, &nic_id);
        if (mxret == MX_SUCCESS) {
                peer->mxp_nic_id = nic_id;
        } else {
                bmx_peer_free(peer);
                return -1;
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

        INIT_QLIST_HEAD(&peer->mxp_queued_txs);
        INIT_QLIST_HEAD(&peer->mxp_queued_rxs);
        INIT_QLIST_HEAD(&peer->mxp_pending_rxs);
        /* peer->mxp_refcount */

        INIT_QLIST_HEAD(&peer->mxp_list);
        gen_mutex_init(&peer->mxp_lock);

        for (i = 0; i < BMX_PEER_RX_NUM; i++) {
                struct bmx_ctx  *rx     = NULL;

                ret = bmx_ctx_alloc(&rx, BMX_REQ_RX);
                if (ret != 0) {
                        bmx_reduce_idle_rxs(i);
                        bmx_peer_free(peer);
                        return ret;
                }
                bmx_put_idle_rx(rx);
        }

        bmx_peer_addref(peer); /* for the peers list */
        gen_mutex_lock(&bmi_mx->bmx_peers_lock);
        qlist_add_tail(&peer->mxp_list, &bmi_mx->bmx_peers);
        gen_mutex_unlock(&bmi_mx->bmx_peers_lock);

        mxmap->mxm_peer = peer;
        *peerp = peer;

        return 0;
}

int
bmx_peer_init_state(struct bmx_peer *peer)
{
        int             ret     = 0;

        gen_mutex_lock(&peer->mxp_lock);

        /* we have a ref for each pending tx and rx, don't init
         * if the refcount > 0 or pending_rxs is not empty */
        if (!qlist_empty(&peer->mxp_pending_rxs) ||
            peer->mxp_refcount != 0) {
                ret = -1;
        } else {
                /* ok to init */
                peer->mxp_state = BMX_PEER_INIT;
        }

        gen_mutex_unlock(&peer->mxp_lock);

        return 0;
}

/**** startup/shutdown functions **************************************/

/* init bmi_mx */
int
bmx_globals_init(int method_id)
{
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
        /* bmi_mx->bmx_is_server */

        INIT_QLIST_HEAD(&bmi_mx->bmx_peers);
        gen_mutex_init(&bmi_mx->bmx_peers_lock);

        INIT_QLIST_HEAD(&bmi_mx->bmx_txs);
        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_txs);
        gen_mutex_init(&bmi_mx->bmx_idle_txs_lock);

        INIT_QLIST_HEAD(&bmi_mx->bmx_rxs);
        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_rxs);
        gen_mutex_init(&bmi_mx->bmx_idle_rxs_lock);

        INIT_QLIST_HEAD(&bmi_mx->bmx_canceled);
        gen_mutex_init(&bmi_mx->bmx_canceled_lock);

        bmi_mx->bmx_next_id = 1;
        gen_mutex_init(&bmi_mx->bmx_lock);      /* global lock, use for global txs,
                                                   global rxs, next_id, etc. */

#if BMX_MEM_TWEAK
        INIT_QLIST_HEAD(&bmi_mx->bmx_idle_buffers);
        gen_mutex_init(&bmi_mx->bmx_idle_buffers_lock);
        INIT_QLIST_HEAD(&bmi_mx->bmx_used_buffers);
        gen_mutex_init(&bmi_mx->bmx_used_buffers_lock);
#endif
        return 0;
}


int
bmx_open_endpoint(mx_endpoint_t *ep, uint32_t board, uint32_t ep_id)
{
        mx_return_t     mxret   = MX_SUCCESS;
        mx_param_t      param;

        /* This will tell MX use context IDs. Normally, MX has one
         * set of queues for posted recvs, unexpected, etc. This will
         * create seaparate sets of queues for each msg type. 
         * The benefit is that we can call mx_test_any() for each 
         * message type and not have to scan a long list of non-
         * matching recvs. */
        param.key = MX_PARAM_CONTEXT_ID;
        param.val.context_id.bits = 4;
        param.val.context_id.shift = 60;

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
BMI_mx_initialize(method_addr_p listen_addr, int method_id, int init_flags)
{
        int             i       = 0;
        int             ret     = 0;
        mx_return_t     mxret   = MX_SUCCESS;

#if BMX_LOGGING
        MPE_Init_log();
#define BMX_LOG_STATE 1
#if BMX_LOG_STATE
        send_start              = MPE_Log_get_event_number();
        send_finish             = MPE_Log_get_event_number();
        recv_start              = MPE_Log_get_event_number();
        recv_finish             = MPE_Log_get_event_number();
        sendunex_start          = MPE_Log_get_event_number();
        sendunex_finish         = MPE_Log_get_event_number();
        recvunex_start          = MPE_Log_get_event_number();
        recvunex_finish         = MPE_Log_get_event_number();
        MPE_Describe_state(send_start, send_finish, "Send", "red");
        MPE_Describe_state(recv_start, recv_finish, "Recv", "blue");
        MPE_Describe_state(sendunex_start, sendunex_finish, "SendUnex", "orange");
        MPE_Describe_state(recvunex_start, recvunex_finish, "RecvUnex", "green");
#else
        MPE_Log_get_solo_eventID(&send_start);
        MPE_Log_get_solo_eventID(&send_finish);
        MPE_Log_get_solo_eventID(&recv_start);
        MPE_Log_get_solo_eventID(&recv_finish);
        MPE_Log_get_solo_eventID(&sendunex_start);
        MPE_Log_get_solo_eventID(&sendunex_finish);
        MPE_Log_get_solo_eventID(&recvunex_start);
        MPE_Log_get_solo_eventID(&recvunex_finish);
        MPE_Describe_info_event(send_start, "Send_start", "red1", "tag:%d");
        MPE_Describe_info_event(send_finish, "Send_finish", "red3", "tag:%d");
        MPE_Describe_info_event(recv_start, "Recv_start", "blue1", "tag:%d");
        MPE_Describe_info_event(recv_finish, "Recv_finish", "blue3", "tag:%d");
        MPE_Describe_info_event(sendunex_start, "SendUnex_start", "orange1", "tag:%d");
        MPE_Describe_info_event(sendunex_finish, "SendUnex_finish", "orange3", "tag:%d");
        MPE_Describe_info_event(recvunex_start, "RecvUnex_start", "green1", "tag:%d");
        MPE_Describe_info_event(recvunex_finish, "RecvUnex_finish", "green3", "tag:%d");
#endif /* state or event */
#endif /* BMX_LOGGING */

         /* check params */
        if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER)) {
                debug(BMX_DB_ERR, "mx_initialize() with illegal parameters. "
                        "BMI_INIT_SERVER requires non-null listen_addr");
                exit(1);
        }

        ret = bmx_globals_init(method_id);
        if (ret != 0) {
                debug(BMX_DB_WARN, "bmx_globals_init() failed with no memory");
                return -ENOMEM;
        }

        mxret = mx_init();
        if (mxret != MX_SUCCESS) {
                debug(BMX_DB_WARN, "mx_init() failed with %s", mx_strerror(mxret));
                BMX_FREE(bmi_mx, sizeof(*bmi_mx));
                return -ENODEV;
        }

        /* return errors, do not abort */
        mx_set_error_handler(MX_ERRORS_RETURN);

        /* if we are a server, open an endpoint now. If a client, wait until first
         * sendunexpected or first recv. */
        if (init_flags & BMI_INIT_SERVER) {
                struct bmx_ctx         *rx     = NULL;
                struct bmx_method_addr *mxmap  = listen_addr->method_data;

                bmi_mx->bmx_hostname = (char *) mxmap->mxm_hostname;
                bmi_mx->bmx_board = mxmap->mxm_board;
                bmi_mx->bmx_ep_id = mxmap->mxm_ep_id;
                bmi_mx->bmx_is_server = 1;

                ret = bmx_open_endpoint(&bmi_mx->bmx_ep, mxmap->mxm_board, mxmap->mxm_ep_id);
                if (ret != 0) {
                        debug(BMX_DB_ERR, "open_endpoint() failed");
                        BMX_FREE(bmi_mx, sizeof(*bmi_mx));
                        exit(1);
                }
                /* We allocate BMX_PEER_RX_NUM when we peer_alloc()
                 * Allocate some here to catch the peer CONN_REQ */
                for (i = 0; i < BMX_SERVER_RXS; i++) {
                        ret = bmx_ctx_alloc(&rx, BMX_REQ_RX);
                        if (ret == 0) {
                                bmx_put_idle_rx(rx);
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
        return 0;
}

static int
BMI_mx_finalize(void)
{
        struct bmx_data *tmp = bmi_mx;

        debug(BMX_DB_FUNC, "entering %s", __func__);

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

        bmi_mx = NULL;

        gen_mutex_unlock(&tmp->bmx_lock);

        BMX_FREE(tmp, sizeof(*tmp));

#if BMX_MEM_ACCT
        debug(BMX_DB_MEM, "memory leaked at shutdown %lld", llu(mem_used));
#endif

#if BMX_LOGGING
        MPE_Finish_log("/tmp/bmi_mx.log");
#endif
        debug(BMX_DB_FUNC, "leaving %s", __func__);

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
bmx_peer_disconnect(struct bmx_peer *peer, int mx_dis)
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
        peer->mxp_state = BMX_PEER_DISCONNECT;

        /* cancel queued txs */
        while (!qlist_empty(&peer->mxp_queued_txs)) {
                list_t          *queued_txs     = &peer->mxp_queued_txs;
                tx = qlist_entry(queued_txs->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&tx->mxc_list);
                bmx_q_canceled_ctx(tx);
        }
        /* cancel queued rxs */
        while (!qlist_empty(&peer->mxp_queued_rxs)) {
                list_t          *queued_rxs     = &peer->mxp_queued_rxs;
                rx = qlist_entry(queued_rxs->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&rx->mxc_list);
                bmx_q_canceled_ctx(rx);
        }
        /* try to cancel pending rxs */
        qlist_for_each_entry_safe(rx, next, &peer->mxp_pending_rxs, mxc_list) {
                uint32_t        result = 0;
                mx_cancel(bmi_mx->bmx_ep, &rx->mxc_mxreq, &result);
                if (result) {
                        qlist_del_init(&rx->mxc_list);
                        bmx_q_canceled_ctx(rx);
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
        struct method_addr      *map    = NULL;
        struct bmx_method_addr  *mxmap  = NULL;
        struct bmx_peer         *peer   = NULL;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        switch(option) {
                case BMI_DROP_ADDR:
                        if (inout_parameter != NULL) {
                                map = (struct method_addr *) inout_parameter;
                                mxmap = map->method_data;
                                debug(BMX_DB_PEER, "%s drop %s map 0x%p mxmap 0x%p)",
                                                __func__, mxmap->mxm_peername != NULL ?
                                                mxmap->mxm_peername : "NULL", map,
                                                mxmap);
                                if (bmi_mx != NULL) {
                                        peer = mxmap->mxm_peer;
                                        bmx_peer_disconnect(peer, 1);
                                }
                                if (!mxmap->mxm_peername) free((void *) mxmap->mxm_peername);
                                mxmap->mxm_peername = NULL;
                                if (!mxmap->mxm_hostname) free((void *) mxmap->mxm_hostname);
                                mxmap->mxm_hostname = NULL;
                                debug(BMX_DB_PEER, "freeing map 0x%p", map);
                                free(map);
                        }
                break;
                default:
                        /* XXX: should return -ENOSYS, but 0 now until callers 
                         * handle that correctly. */
                break;
        }
        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return 0;
}

static int
BMI_mx_get_info(int option, void *inout_parameter)
{
        int     ret     = 0;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        switch(option) {
                case BMI_CHECK_MAXSIZE:
                        /* reality is 2^31, but shrink to avoid negative int */
                        *(int *)inout_parameter = (1U << 31) - 1;
                        break;
                case BMI_GET_UNEXP_SIZE:
                        *(int *)inout_parameter = BMX_UNEXPECTED_SIZE;
                        break;
                default:
                        ret = -ENOSYS;
        }
        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return ret;
}

void *
BMI_mx_memalloc(bmi_size_t size, enum bmi_op_type send_recv)
{
        void                    *buf    = NULL;
#if BMX_MEM_TWEAK
        list_t                  *idle  = &bmi_mx->bmx_idle_buffers;
        list_t                  *used  = &bmi_mx->bmx_used_buffers;
        struct bmx_buffer       *mem    = NULL;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        gen_mutex_lock(&bmi_mx->bmx_idle_buffers_lock);
        if (size <= (BMX_BUFF_SIZE) && !qlist_empty(idle)) {
                mem = qlist_entry(idle->next, struct bmx_buffer, mxb_list);
                qlist_del_init(&mem->mxb_list);
                gen_mutex_unlock(&bmi_mx->bmx_idle_buffers_lock);
                buf = mem->mxb_buffer;
                mem->mxb_used++;
                gen_mutex_lock(&bmi_mx->bmx_used_buffers_lock);
                qlist_add(&mem->mxb_list, used);
                gen_mutex_unlock(&bmi_mx->bmx_used_buffers_lock);
                gen_mutex_lock(&bmi_mx->bmx_idle_buffers_lock);
        } else {
                bmi_mx->bmx_misses++;
                gen_mutex_unlock(&bmi_mx->bmx_idle_buffers_lock);
                buf = malloc((size_t) size);
                gen_mutex_lock(&bmi_mx->bmx_idle_buffers_lock);
        }
        gen_mutex_unlock(&bmi_mx->bmx_idle_buffers_lock);
#else
        debug(BMX_DB_FUNC, "entering %s", __func__);

        buf = malloc((size_t) size);
#endif

        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return buf;
}

static int
BMI_mx_memfree(void *buffer, bmi_size_t size, enum bmi_op_type send_recv)
{
#if BMX_MEM_TWEAK
        int                     found   = 0;
        list_t                  *used   = &bmi_mx->bmx_used_buffers;
        list_t                  *idle   = &bmi_mx->bmx_idle_buffers;
        struct bmx_buffer       *mem    = NULL;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        gen_mutex_lock(&bmi_mx->bmx_used_buffers_lock);
        qlist_for_each_entry(mem, used, mxb_list) {
                if (mem->mxb_buffer == buffer) {
                        found = 1;
                        qlist_del_init(&mem->mxb_list);
                        gen_mutex_unlock(&bmi_mx->bmx_used_buffers_lock);
                        gen_mutex_lock(&bmi_mx->bmx_idle_buffers_lock);
                        qlist_add(&mem->mxb_list, idle);
                        gen_mutex_unlock(&bmi_mx->bmx_idle_buffers_lock);
                        gen_mutex_lock(&bmi_mx->bmx_used_buffers_lock);
                        break;
                }
        }
        gen_mutex_unlock(&bmi_mx->bmx_used_buffers_lock);

        if (found == 0) {
                free(buffer);
        }
#else
        debug(BMX_DB_FUNC, "entering %s", __func__);

        free(buffer);
#endif

        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return 0;
}

static int
BMI_mx_unexpected_free(void *buf)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        BMX_FREE(buf, BMX_UNEXPECTED_SIZE);

        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return 0;
}

void
bmx_parse_match(uint64_t match, uint8_t *type, uint32_t *id, uint32_t *tag)
{
        *type   = (uint8_t)  (match >> 60);
        *id     = (uint32_t) ((match >> 32) & 0xFFFFF); /* 20 bits */
        *tag    = (uint32_t) (match & 0xFFFFFFFF);
        return;
}

void
bmx_create_match(struct bmx_ctx *ctx)
{
        int             connect = 0;
        uint64_t        type    = (uint64_t) ctx->mxc_msg_type;
        uint64_t        id      = 0LL;
        uint64_t        tag     = (uint64_t) ctx->mxc_tag;

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

        ctx->mxc_match = (type << 60) | (id << 32) | tag;

        return;
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
                        ret = -ENOMEM;
                        bmx_deq_pending_ctx(tx);        /* uses peer lock */
                        bmx_q_canceled_ctx(tx);
                }
        } else { /* peer is not ready */
                debug(BMX_DB_PEER, "%s peer is not ready (%d) q_ctx(tx) "
                                "match= 0x%llx length=%lld", __func__, peer->mxp_state,
                                llu(tx->mxc_match), lld(tx->mxc_nob));
                bmx_q_ctx(tx);  /* uses peer lock */
        }
        return ret;
}

static int
bmx_ensure_connected(struct bmx_method_addr *mxmap)
{
        int             ret     = 0;
        struct bmx_peer *peer   = mxmap->mxm_peer;

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
                ret = -ENOTCONN;
        }
out:
        return ret;
}

static int
bmx_post_send_common(bmi_op_id_t *id, struct method_addr *remote_map,
                     int numbufs, const void *const *buffers, 
                     const bmi_size_t *sizes, bmi_size_t total_size, 
                     bmi_msg_tag_t tag, void *user_ptr,
                     bmi_context_id context_id, int is_unexpected)
{
        struct bmx_ctx          *tx     = NULL;
        struct method_op        *mop    = NULL;
        struct bmx_method_addr  *mxmap  = NULL;
        struct bmx_peer         *peer   = NULL;
        int                      ret    = 0;

#if BMX_LOGGING
        if (!is_unexpected) {
                MPE_Log_event(send_start, (int) tag, NULL);
        } else {
                MPE_Log_event(sendunex_start, (int) tag, NULL);
        }
#endif

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
                        ret = -ENOMEM;
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
                        bmx_put_idle_tx(tx);
                        bmx_peer_decref(peer);
                        ret = -ENOMEM;
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
                bmx_put_idle_tx(tx);
                bmx_peer_decref(peer);
                ret = -EINVAL;
                goto out;
        }

        tx->mxc_tag = tag;
        tx->mxc_peer = peer;
        if (!is_unexpected) {
                tx->mxc_msg_type = BMX_MSG_EXPECTED;
        } else {
                tx->mxc_msg_type = BMX_MSG_UNEXPECTED;
        }

        BMX_MALLOC(mop, sizeof(*mop));
        if (mop == NULL) {
                bmx_put_idle_tx(tx);
                bmx_peer_decref(peer);
                ret = -ENOMEM;
                goto out;
        }
        debug(BMX_DB_CTX, "TX id_gen_safe_register(%llu)", llu(mop->op_id));
        id_gen_safe_register(&mop->op_id, mop);
        mop->addr = remote_map;  /* set of function pointers, essentially */
        mop->method_data = tx;
        mop->user_ptr = user_ptr;
        mop->context_id = context_id;
        *id = mop->op_id;
        tx->mxc_mop = mop;

        bmx_create_match(tx);

        debug(BMX_DB_CTX, "%s tag= %d length= %d %s op_id= %llu", __func__, tag,
                        (int) total_size, is_unexpected ? "UNEXPECTED" : "EXPECTED",
                        llu(mop->op_id));

        ret = bmx_post_tx(tx);

out:
        return ret;
}

static int
BMI_mx_post_send(bmi_op_id_t *id, struct method_addr *remote_map,
                 const void *buffer, bmi_size_t size,
                 enum bmi_buffer_type buffer_flag __unused,
                 bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        return bmx_post_send_common(id, remote_map, 1, &buffer, &size, size,
                                    tag, user_ptr, context_id, 0);
}

static int
BMI_mx_post_send_list(bmi_op_id_t *id, struct method_addr *remote_map,
                      const void *const *buffers, const bmi_size_t *sizes, int list_count,
                      bmi_size_t total_size, enum bmi_buffer_type buffer_flag __unused,
                      bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        return bmx_post_send_common(id, remote_map, list_count, buffers, sizes, 
                                    total_size, tag, user_ptr, context_id, 0);
}

static int
BMI_mx_post_sendunexpected(bmi_op_id_t *id, struct method_addr *remote_map,
                 const void *buffer, bmi_size_t size,
                 enum bmi_buffer_type buffer_flag __unused,
                 bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        return bmx_post_send_common(id, remote_map, 1, &buffer, &size, size,
                                    tag, user_ptr, context_id, 1);
}

static int
BMI_mx_post_sendunexpected_list(bmi_op_id_t *id, struct method_addr *remote_map,
                  const void *const *buffers, const bmi_size_t *sizes, int list_count,
                  bmi_size_t total_size, enum bmi_buffer_type buffer_flag __unused,
                  bmi_msg_tag_t tag, void *user_ptr, bmi_context_id context_id)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        return bmx_post_send_common(id, remote_map, list_count, buffers, sizes, 
                                    total_size, tag, user_ptr, context_id, 1);
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
                                 rx->mxc_match, -1ULL, (void *) rx, &rx->mxc_mxreq);
                if (mxret != MX_SUCCESS) {
                        ret = -ENOMEM;
                        bmx_deq_pending_ctx(rx);        /* uses peer lock */
                        bmx_q_canceled_ctx(rx);
                }
        } else { /* peer is not ready */
                debug(BMX_DB_PEER, "%s peer is not ready (%d) q_ctx(rx) match= 0x%llx "
                                "length=%lld", __func__, peer->mxp_state,
                                llu(rx->mxc_match), (long long) rx->mxc_nob);
                bmx_q_ctx(rx);  /* uses peer lock */
        }
        return ret;
}

static int
bmx_post_recv_common(bmi_op_id_t *id, struct method_addr *remote_map,
                     int numbufs, void *const *buffers, const bmi_size_t *sizes,
                     bmi_size_t tot_expected_len, bmi_msg_tag_t tag,
                     void *user_ptr, bmi_context_id context_id)
{
        int                      ret    = 0;
        struct bmx_ctx          *rx     = NULL;
        struct method_op        *mop    = NULL;
        struct bmx_method_addr  *mxmap  = NULL;
        struct bmx_peer         *peer   = NULL;

#if BMX_LOGGING
        MPE_Log_event(recv_start, (int) tag, NULL);
#endif

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
                        bmx_put_idle_rx(rx);
                        bmx_peer_decref(peer);
                        ret = -ENOMEM;
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
                bmx_put_idle_rx(rx);
                bmx_peer_decref(peer);
                ret = -ENOMEM;
                goto out;
        }
        debug(BMX_DB_CTX, "RX id_gen_safe_register(%llu)", llu(mop->op_id));
        id_gen_safe_register(&mop->op_id, mop);
        mop->addr = remote_map;  /* set of function pointers, essentially */
        mop->method_data = rx;
        mop->user_ptr = user_ptr;
        mop->context_id = context_id;
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
BMI_mx_post_recv(bmi_op_id_t *id, struct method_addr *remote_map,
                 void *buffer, bmi_size_t expected_len, bmi_size_t *actual_len __unused,
                 enum bmi_buffer_type buffer_flag __unused, bmi_msg_tag_t tag, void *user_ptr,
                 bmi_context_id context_id)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        return bmx_post_recv_common(id, remote_map, 1, &buffer, &expected_len,
                                    expected_len, tag, user_ptr, context_id);
}

static int
BMI_mx_post_recv_list(bmi_op_id_t *id, struct method_addr *remote_map,
               void *const *buffers, const bmi_size_t *sizes, int list_count,
               bmi_size_t tot_expected_len, bmi_size_t *tot_actual_len __unused,
               enum bmi_buffer_type buffer_flag __unused, bmi_msg_tag_t tag, void *user_ptr,
               bmi_context_id context_id)
{
        debug(BMX_DB_FUNC, "entering %s", __func__);

        return bmx_post_recv_common(id, remote_map, list_count, buffers, sizes,
                                    tot_expected_len, tag, user_ptr, context_id);
}

static void
bmx_peer_post_queued_rxs(struct bmx_peer *peer)
{
        struct bmx_ctx  *rx             = NULL;
        list_t          *queued_rxs     = &peer->mxp_queued_rxs;

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

        return;
}

static void
bmx_peer_post_queued_txs(struct bmx_peer *peer)
{
        struct bmx_ctx  *tx             = NULL;
        list_t          *queued_txs     = &peer->mxp_queued_txs;

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

        return;
}


static int
bmx_post_unexpected_recv(mx_endpoint_addr_t source, uint8_t type, uint32_t id, 
                         uint32_t tag, uint64_t match, uint32_t length)
{
        int             ret     = 0;
        struct bmx_ctx  *rx     = NULL;
        struct bmx_peer *peer   = NULL;
        mx_return_t     mxret   = MX_SUCCESS;

        if (id == 0 && tag == 0 && type == 0) {
                bmx_parse_match(match, &type, &id, &tag);
        }

#if BMX_LOGGING
        MPE_Log_event(recvunex_start, (int) tag, NULL);
#endif

        rx = bmx_get_idle_rx();
        if (rx != NULL) {
                mx_get_endpoint_addr_context(source, (void **) &peer);
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
                                 rx->mxc_match, -1ULL, (void *) rx, &rx->mxc_mxreq);
                if (mxret != MX_SUCCESS) {
                        debug((BMX_DB_MX|BMX_DB_CTX), "mx_irecv() failed with %s for an "
                                        "unexpected recv with tag %d length %d",
                                        mx_strerror(mxret), tag, length);
                        bmx_put_idle_rx(rx);
                        ret = -1;
                }
        } else {
                ret = -1;
        }

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
        uint32_t                id      = 0;
        uint32_t                tag     = 0;
        struct bmx_peer         *peer   = NULL;
        mx_return_t             mxret   = MX_SUCCESS;

        bmx_parse_match(match_value, &type, &id, &tag);

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
                                         rx->mxc_match, -1ULL, (void *) rx, &rx->mxc_mxreq);
                        if (mxret != MX_SUCCESS) {
                                debug(BMX_DB_CONN, "mx_irecv() failed for an "
                                                "unexpected recv with %s", 
                                                mx_strerror(mxret));
                                bmx_put_idle_rx(rx);
                                ret = MX_RECV_FINISHED;
                        }
                } else {
                        ret = MX_RECV_FINISHED;
                }
                break;
        case BMX_MSG_CONN_ACK:
                /* the server is replying to our CONN_REQ */
                if (bmi_mx->bmx_is_server) {
                        debug(BMX_DB_ERR, "server receiving CONN_ACK");
                        exit(1);
                }
                mx_get_endpoint_addr_context(source, (void **) &peer);
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
                                bmx_peer_disconnect(peer, 1);
                        }
                }
                /* we are done with the recv, drop it */
                ret = MX_RECV_FINISHED;
                break;
        case BMX_MSG_UNEXPECTED:
                if (!bmi_mx->bmx_is_server) {
                        mx_get_endpoint_addr_context(source, (void **) &peer);
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
static struct method_addr *
bmx_alloc_method_addr(const char *peername, const char *hostname, uint32_t board, uint32_t ep_id)
{
        struct method_addr      *map            = NULL;
        struct bmx_method_addr  *mxmap          = NULL;

        if (bmi_mx == NULL) {
                map = alloc_method_addr(tmp_id, (bmi_size_t) sizeof(*mxmap));
        } else {
                map = alloc_method_addr(bmi_mx->bmx_method_id, (bmi_size_t) sizeof(*mxmap));
        }
        if (map == NULL) return NULL;

        mxmap = map->method_data;
        mxmap->mxm_map = map;
        mxmap->mxm_peername = strdup(peername);
        mxmap->mxm_hostname = hostname;
        mxmap->mxm_board = board;
        mxmap->mxm_ep_id = ep_id;
        /* mxmap->mxm_peer */

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

        if (bmi_mx->bmx_is_server) return;
        do {
                uint64_t        match   = (uint64_t) BMX_MSG_ICON_REQ << 60;
                uint64_t        mask    = (uint64_t) 0xF << 60;
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
                                bmx_peer_disconnect(peer, 0);
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
                                        bmx_peer_disconnect(peer, 1);
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
                                bmx_peer_disconnect(peer, 1);
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

/* test for CONN_REQ messages (on the server)
 * if found
 *    create peer
 *    create mxmap
 *    mx_iconnect() w/BMX_MSG_ICON_ACK
 */
static void
bmx_handle_conn_req(void)
{
        uint32_t        result  = 0;
        uint64_t        match   = (uint64_t) BMX_MSG_CONN_REQ << 60;
        uint64_t        mask    = (uint64_t) 0xF << 60;
        uint64_t        ack     = (uint64_t) BMX_MSG_ICON_ACK << 60;
        mx_status_t     status;

        do {
                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {
                        uint8_t                 type    = 0;
                        uint32_t                id      = 0;
                        uint32_t                version = 0;
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
                                bmx_put_idle_tx(tx);
                                continue;
                        } else if (status.code != MX_STATUS_SUCCESS) {
                                bmx_peer_decref(rx->mxc_peer);
                                bmx_put_idle_rx(rx);
                                continue;
                        }
                        bmx_parse_match(rx->mxc_match, &type, &id, &version);

                        if (version != BMX_VERSION) {
                                /* TODO send error conn_ack */
                                debug(BMX_DB_WARN, "version mismatch with peer "
                                                "%s (our version 0x%x, peer's version "
                                                "0x%x)", (char *) rx->mxc_buffer, 
                                                BMX_VERSION, version);
                                bmx_peer_decref(rx->mxc_peer);
                                bmx_put_idle_rx(rx);
                                continue;
                        }
                        if (bmi_mx->bmx_is_server == 0) {
                                debug(BMX_DB_WARN, "received CONN_REQ on a client.");
                                bmx_peer_decref(rx->mxc_peer);
                                bmx_put_idle_rx(rx);
                                continue;
                        }
                        mx_get_endpoint_addr_context(status.source, (void **) &peer);
                        if (peer == NULL) { /* new peer */
                                int             ret             = 0;
                                char           *host            = NULL;
                                uint32_t        board           = 0;
                                uint32_t        ep_id           = 0;
                                const char     *peername        = rx->mxc_buffer;
                                struct method_addr *map         = NULL;

                                debug((BMX_DB_CONN|BMX_DB_PEER), "%s peer %s connecting",
                                                __func__, peername);

                                ret = bmx_parse_peername(peername, &host,
                                                         &board, &ep_id);
                                if (ret != 0) {
                                        debug(BMX_DB_CONN, "parse_peername() "
                                                        "failed on %s",
                                                        (char *) rx->mxc_buffer);
                                        bmx_peer_decref(rx->mxc_peer);
                                        bmx_put_idle_rx(rx);
                                        continue;
                                }
                                map = bmx_alloc_method_addr(peername, host,
                                                            board, ep_id);
                                if (map == NULL) {
                                        debug((BMX_DB_CONN|BMX_DB_MEM), "unable to alloc a "
                                                        "method addr for %s", peername);
                                        bmx_peer_decref(rx->mxc_peer);
                                        bmx_put_idle_rx(rx);
                                        continue;
                                }
                                mxmap = map->method_data;
                                ret = bmx_peer_alloc(&peer, mxmap);
                                if (ret != 0) {
                                        debug((BMX_DB_CONN|BMX_DB_MEM), "unable to alloc a "
                                                        "peer for %s", peername);
                                        bmx_peer_decref(rx->mxc_peer);
                                        bmx_put_idle_rx(rx);
                                        continue;
                                }
                        } else { /* reconnecting peer */
                                /* cancel queued txs and rxs, pending rxs */
                                debug((BMX_DB_CONN|BMX_DB_PEER), "%s peer %s reconnecting",
                                                __func__, peer->mxp_mxmap->mxm_peername);
                                bmx_peer_disconnect(peer, 0);
                                mxmap = peer->mxp_mxmap;
                        }
                        gen_mutex_lock(&peer->mxp_lock);
                        peer->mxp_state = BMX_PEER_WAIT;
                        peer->mxp_tx_id = id;
                        gen_mutex_unlock(&peer->mxp_lock);
                        bmx_peer_addref(peer); /* add ref until completion of CONN_ACK */
                        mx_iconnect(bmi_mx->bmx_ep, peer->mxp_nic_id, mxmap->mxm_ep_id,
                                    BMX_MAGIC, ack, peer, &request);
                        bmx_put_idle_rx(rx);
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
                uint64_t        match   = (uint64_t) BMX_MSG_ICON_ACK << 60;
                uint64_t        mask    = (uint64_t) 0xF << 60;
                mx_status_t     status;

                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {

                        peer = (struct bmx_peer *) status.context;
                        debug(BMX_DB_CONN, "%s returned for %s with %s", __func__, 
                                        peer->mxp_mxmap->mxm_peername, 
                                        mx_strstatus(status.code));
                        if (status.code != MX_STATUS_SUCCESS) {
                                bmx_peer_disconnect(peer, 1);
                                /* drop ref taken before calling mx_iconnect */
                                bmx_peer_decref(peer);
                                continue;
                        }
                        gen_mutex_lock(&peer->mxp_lock);
                        peer->mxp_epa = status.source;
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
                                        bmx_peer_disconnect(peer, 1);
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

        if (!bmi_mx->bmx_is_server) return;
        do {
                uint64_t        match   = (uint64_t) BMX_MSG_CONN_ACK << 60;
                uint64_t        mask    = (uint64_t) 0xF << 60;
                mx_status_t     status;

                mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
                if (result) {
                        tx = (struct bmx_ctx *) status.context;
                        debug(BMX_DB_CONN, "%s returned tx match 0x%llx with %s", __func__, 
                                        llu(tx->mxc_match), mx_strstatus(status.code));
                        bmx_peer_decref(tx->mxc_peer);
                        bmx_put_idle_tx(tx);
                }
        } while (result);

        return;
}

static void
bmx_connection_handlers(void)
{
        /* push connection messages along */
        bmx_handle_icon_req();
        bmx_handle_conn_req();
        bmx_handle_icon_ack();
        bmx_handle_conn_ack();
        return;
}

static int
BMI_mx_test(bmi_op_id_t id, int *outcount, bmi_error_code_t *err,
            bmi_size_t *size, void **user_ptr, int max_idle_time __unused,
            bmi_context_id context_id __unused)
{
        uint32_t         result = 0;
        struct method_op *mop   = NULL;
        struct bmx_ctx   *ctx   = NULL;
        struct bmx_peer  *peer  = NULL;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        bmx_connection_handlers();

        mop = id_gen_safe_lookup(id);
        ctx = mop->method_data;
        peer = ctx->mxc_peer;

        switch (ctx->mxc_state) {
        case BMX_CTX_CANCELED:
                /* we are racing with testcontext */
                gen_mutex_lock(&bmi_mx->bmx_canceled_lock);
                if (ctx->mxc_state != BMX_CTX_CANCELED) {
                        gen_mutex_unlock(&bmi_mx->bmx_canceled_lock);
                        return 0;
                }
                qlist_del_init(&ctx->mxc_list);
                gen_mutex_unlock(&bmi_mx->bmx_canceled_lock);
                *outcount = 1;
                *err = -PVFS_ETIMEDOUT;
                if (ctx->mxc_mop) {
                        if (user_ptr) {
                                *user_ptr = ctx->mxc_mop->user_ptr;
                        }
                        id_gen_safe_unregister(ctx->mxc_mop->op_id);
                        BMX_FREE(ctx->mxc_mop, sizeof(*ctx->mxc_mop));
                }
                bmx_peer_decref(peer);
                if (ctx->mxc_type == BMX_REQ_TX) {
                        bmx_put_idle_tx(ctx);
                } else {
                        bmx_put_idle_rx(ctx);
                }
                break;
        case BMX_CTX_PENDING:
                mx_test(bmi_mx->bmx_ep, &ctx->mxc_mxreq, &ctx->mxc_mxstat, &result);
                if (result) {
                        *outcount = 1;
                        if (ctx->mxc_mxstat.code == MX_STATUS_SUCCESS) {
                                *err = 0;
                                *size = ctx->mxc_mxstat.xfer_length;
                        } else {
                                *err = -PVFS_ETIMEDOUT;
                        }
                        if (ctx->mxc_mop) {
                                if (user_ptr) {
                                        *user_ptr = ctx->mxc_mop->user_ptr;
                                }
                                id_gen_safe_unregister(ctx->mxc_mop->op_id);
                                BMX_FREE(ctx->mxc_mop, sizeof(*ctx->mxc_mop));
                        }
                        bmx_deq_pending_ctx(ctx);
                        if (ctx->mxc_type == BMX_REQ_TX) {
                                bmx_put_idle_tx(ctx);
                        } else {
                                bmx_put_idle_rx(ctx);
                        }
                        bmx_peer_decref(peer);
                }
                break;
        default:
                debug(BMX_DB_CTX, "%s called on %s with state %d", __func__,
                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX", ctx->mxc_state);
        }
        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return 0;
}


static int
BMI_mx_testcontext(int incount, bmi_op_id_t *outids, int *outcount,
            bmi_error_code_t *errs, bmi_size_t *sizes, void **user_ptrs,
            int max_idle_time, bmi_context_id context_id)
{
        int             i               = 0;
        int             completed       = 0;
        uint64_t        match           = 0LL;
        uint64_t        mask            = (uint64_t) 0xF << 60;
        struct bmx_ctx  *ctx            = NULL;
        struct bmx_peer *peer           = NULL;
        list_t          *canceled       = &bmi_mx->bmx_canceled;
	int 		wait		= 0;

        bmx_connection_handlers();

        /* always return canceled messages first */
        while (completed < incount && !qlist_empty(canceled)) {
                gen_mutex_lock(&bmi_mx->bmx_canceled_lock);
                ctx = qlist_entry(canceled->next, struct bmx_ctx, mxc_list);
                qlist_del_init(&ctx->mxc_list);
                /* change state in case test is trying to reap it as well */
                ctx->mxc_state = BMX_CTX_COMPLETED;
                gen_mutex_unlock(&bmi_mx->bmx_canceled_lock);
                peer = ctx->mxc_peer;
                outids[completed] = ctx->mxc_mop->op_id;
                errs[completed] = -PVFS_ETIMEDOUT;
                if (user_ptrs)
                        user_ptrs[completed] = ctx->mxc_mop->user_ptr;
                id_gen_safe_unregister(ctx->mxc_mop->op_id);
                BMX_FREE(ctx->mxc_mop, sizeof(*ctx->mxc_mop));
                completed++;
                if (ctx->mxc_type == BMX_REQ_TX) {
                        bmx_put_idle_tx(ctx);
                } else {
                        bmx_put_idle_rx(ctx);
                }
                bmx_peer_decref(peer); /* drop the ref taken in [send|recv]_common */
        }
        if (completed > 0) {
                debug(BMX_DB_CTX, "%s found %d canceled messages", __func__, completed);
        }
        if (!bmi_mx->bmx_is_server) { /* client */
                /* check for completed unexpected sends */
                match = (uint64_t) BMX_MSG_UNEXPECTED << 60;
                for (i = completed; i < incount; i++) {
                        uint32_t        result  = 0;
                        mx_status_t     status;
        
                        mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
			
                        if (result) {
                                ctx = (struct bmx_ctx *) status.context;
                                peer = ctx->mxc_peer;
                                debug(BMX_DB_CTX, "%s completing unexpected %s with "
                                                "match 0x%llx for %s with op_id %llu", 
                                                __func__, 
                                                ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX",
                                                llu(ctx->mxc_match), 
                                                peer->mxp_mxmap->mxm_peername,
                                                llu(ctx->mxc_mop->op_id));
        
                                if (!qlist_empty(&ctx->mxc_list)) {
                                        gen_mutex_lock(&peer->mxp_lock);
                                        qlist_del_init(&ctx->mxc_list);
                                        gen_mutex_unlock(&peer->mxp_lock);
                                }
                                outids[completed] = ctx->mxc_mop->op_id;
                                if (status.code == MX_SUCCESS) {
                                        errs[completed] = 0;
                                        sizes[completed] = status.xfer_length;
                                } else {
                                        errs[completed] = -PVFS_ECANCEL;
                                }
                                if (user_ptrs)
                                        user_ptrs[completed] = ctx->mxc_mop->user_ptr;
                                id_gen_safe_unregister(ctx->mxc_mop->op_id);
                                BMX_FREE(ctx->mxc_mop, sizeof(*ctx->mxc_mop));
                                completed++;
#if BMX_LOGGING
                                MPE_Log_event(sendunex_finish, (int) ctx->mxc_tag, NULL);
#endif

                                if (ctx->mxc_type == BMX_REQ_TX) {
                                        bmx_put_idle_tx(ctx);
                                } else {
                                        bmx_put_idle_rx(ctx);
                                }
                                bmx_peer_decref(peer); /* drop the ref taken in [send|recv]_common */
                        }
                }
        }
        /* return completed messages
         * we will always try (incount - completed) times even
         *     if some iterations have no result */
        match = (uint64_t) BMX_MSG_EXPECTED << 60;
        for (i = completed; i < incount; i++) {
                uint32_t        result  = 0;
                mx_status_t     status;

		if(wait == 0 || wait == 2)
		{
		    mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
		    if(!result && wait == 0 && max_idle_time > 0) wait = 1;
		}
		else if(wait == 1 && max_idle_time > 0)
		{
		    mx_wait_any(bmi_mx->bmx_ep, max_idle_time, match, mask, &status, &result);
		    wait = 2;
		}

                if (result) {
                        ctx = (struct bmx_ctx *) status.context;
                        peer = ctx->mxc_peer;
                        debug(BMX_DB_CTX, "%s completing expected %s with match 0x%llx "
                                        "for %s with op_id %llu length %d %s", __func__, 
                                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX",
                                        llu(ctx->mxc_match), peer->mxp_mxmap->mxm_peername,
                                        llu(ctx->mxc_mop->op_id), status.xfer_length,
                                        mx_strstatus(status.code));

                        if (!qlist_empty(&ctx->mxc_list)) {
                                gen_mutex_lock(&peer->mxp_lock);
                                qlist_del_init(&ctx->mxc_list);
                                gen_mutex_unlock(&peer->mxp_lock);
                        }
                        outids[completed] = ctx->mxc_mop->op_id;
                        if (status.code == MX_SUCCESS) {
                                errs[completed] = 0;
                                sizes[completed] = status.xfer_length;
                        } else {
                                errs[completed] = -PVFS_ECANCEL;
                        }
                        if (user_ptrs)
                                user_ptrs[completed] = ctx->mxc_mop->user_ptr;
                        id_gen_safe_unregister(ctx->mxc_mop->op_id);
                        BMX_FREE(ctx->mxc_mop, sizeof(*ctx->mxc_mop));
                        completed++;
#if BMX_LOGGING
                        if (ctx->mxc_type == BMX_REQ_TX) {
                                MPE_Log_event(send_finish, (int) ctx->mxc_tag, NULL);
                        } else {
                                MPE_Log_event(recv_finish, (int) ctx->mxc_tag, NULL);
                        }
#endif
                        if (ctx->mxc_type == BMX_REQ_TX) {
                                bmx_put_idle_tx(ctx);
                        } else {
                                bmx_put_idle_rx(ctx);
                        }
                        bmx_peer_decref(peer); /* drop the ref taken in [send|recv]_common */
                }
        }
        *outcount = completed;
        return completed;
}

static int
BMI_mx_testunexpected(int incount __unused, int *outcount,
            struct method_unexpected_info *ui, int max_idle_time __unused)
{
        uint32_t        result  = 0;
        uint64_t        match   = (uint64_t) BMX_MSG_UNEXPECTED << 60;
        uint64_t        mask    = (uint64_t) 0xF << 60;
        mx_status_t     status;

        bmx_connection_handlers();

        /* if the unexpected handler cannot get a rx, it does not post a receive.
         * probe for unexpected and post a rx */
        mx_iprobe(bmi_mx->bmx_ep, match, mask, &status, &result);
        if (result) {
                int     ret     = 0;
                ret = bmx_post_unexpected_recv(status.source, 0, 0, 0, status.match_info, status.xfer_length);
                if (ret != 0) {
                        debug(BMX_DB_CTX, "%s mx_iprobe() found rx with match 0x%llx "
                                        "length %d but could not receive it", __func__,
                                        llu(status.match_info), status.xfer_length);
                }
        }

        /* check for unexpected messages */
        *outcount = 0;
        mx_test_any(bmi_mx->bmx_ep, match, mask, &status, &result);
        if (result) {
                struct bmx_ctx  *rx   = (struct bmx_ctx *) status.context;
                struct bmx_peer *peer = rx->mxc_peer;
                debug(BMX_DB_CTX, "*** %s completing RX with match 0x%llx for %s",
                                __func__, llu(rx->mxc_match), peer->mxp_mxmap->mxm_peername);

                ui->error_code = 0;
                ui->addr = peer->mxp_map;
                ui->size = rx->mxc_nob;
                /* avoid a memcpy by giving the rx buffer to BMI
                 * and malloc a new one for the rx */
                ui->buffer = rx->mxc_buffer;
                BMX_MALLOC(rx->mxc_buffer, BMX_UNEXPECTED_SIZE);
                rx->mxc_seg.segment_ptr = rx->mxc_buffer;
                ui->tag = rx->mxc_tag;

                if (!qlist_empty(&rx->mxc_list)) {
                        gen_mutex_lock(&peer->mxp_lock);
                        qlist_del_init(&rx->mxc_list);
                        gen_mutex_unlock(&peer->mxp_lock);
                }
#if BMX_LOGGING
                MPE_Log_event(recvunex_finish, (int) rx->mxc_tag, NULL);
#endif
                bmx_put_idle_rx(rx);
                bmx_peer_decref(peer); /* drop the ref taken in unexpected_recv() */
                *outcount = 1;
        }
        return 0;
}

static void
bmx_create_peername(void)
{
        char    peername[MX_MAX_HOSTNAME_LEN + 28]; /* mx://host:board:ep_id\0 */

        sprintf(peername, "mx://%s:%u:%u", bmi_mx->bmx_hostname, bmi_mx->bmx_board,
                bmi_mx->bmx_ep_id);
        bmi_mx->bmx_peername = strdup(peername);
        return;
}

static int
bmx_peer_connect(struct bmx_peer *peer)
{
        int                     ret    = 0;
        uint64_t                nic_id = 0LL;
        mx_request_t            request;
        uint64_t                match  = (uint64_t) BMX_MSG_ICON_REQ << 60;
        struct bmx_method_addr *mxmap  = peer->mxp_mxmap;

        if (bmi_mx->bmx_is_server) {
                return 1;
        }
        gen_mutex_lock(&peer->mxp_lock);
        if (peer->mxp_state == BMX_PEER_INIT) {
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
                                        bmi_mx->bmx_board,
                                        MX_ANY_ENDPOINT);
                if (ret != 0) {
                        debug((BMX_DB_MX|BMX_DB_CONN), "failed to open endpoint when "
                                        "trying to conenct to %s", mxmap->mxm_peername);
                        bmx_peer_decref(peer);
                        return ret;
                }
                mx_get_endpoint_addr(bmi_mx->bmx_ep, &epa);
                /* get our nic_id and ep_id */
                mx_decompose_endpoint_addr(epa, &nic_id, &bmi_mx->bmx_ep_id);
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
                }
                /* create our peername */
                bmx_create_peername();
        }
        /* this is a new peer, start the connect handshake
         * by calling mx_iconnect() w/BMX_MSG_ICON_REQ */
        mx_iconnect(bmi_mx->bmx_ep, peer->mxp_nic_id, mxmap->mxm_ep_id,
                    BMX_MAGIC, match, (void *) peer, &request);
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
static struct method_addr *
BMI_mx_method_addr_lookup(const char *id)
{
        int                     ret     = 0;
        char                   *host    = NULL;
        uint32_t                board   = 0;
        uint32_t                ep_id   = 0;
        struct method_addr     *map     = NULL;
        struct bmx_method_addr *mxmap   = NULL;

        debug(BMX_DB_FUNC, "entering %s", __func__);

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
                                BMX_FREE(host, sizeof(*host));
                                break;
                        }
                }
                gen_mutex_unlock(&bmi_mx->bmx_peers_lock);
        }
        
        if (map == NULL) {
                map = bmx_alloc_method_addr(id, host, board, ep_id);
                if (bmi_mx != NULL && ! bmi_mx->bmx_is_server) { /* we are a client */
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
        }
out:
        debug(BMX_DB_FUNC, "leaving %s", __func__);

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
BMI_mx_cancel(bmi_op_id_t id, bmi_context_id context_id __unused)
{
        struct method_op        *mop;
        struct bmx_ctx          *ctx     = NULL;
        struct bmx_peer         *peer   = NULL;
        uint32_t                result  = 0;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        mop = id_gen_safe_lookup(id);
        ctx = mop->method_data;
        peer = ctx->mxc_peer;

        debug(BMX_DB_CTX, "%s %s op_id %llu mxc_state %d peer state %d", __func__, 
                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX", 
                        llu(ctx->mxc_mop->op_id), ctx->mxc_state, peer->mxp_state);
        switch (ctx->mxc_state) {
        case BMX_CTX_QUEUED:
                /* we are racing with the connection setup */
                bmx_deq_ctx(ctx);
                bmx_q_canceled_ctx(ctx);
                break;
        case BMX_CTX_PENDING:
                if (ctx->mxc_type == BMX_REQ_TX) {
                        bmx_peer_disconnect(peer, 1);
                } else { /* BMX_REQ_RX */
                        mx_cancel(bmi_mx->bmx_ep, &ctx->mxc_mxreq, &result);
                        if (result == 1) {
                                bmx_deq_pending_ctx(ctx);
                                bmx_q_canceled_ctx(ctx);
                        }
                }
                break;
        default:
                debug(BMX_DB_WARN, "%s called on %s with state %d", __func__,
                        ctx->mxc_type == BMX_REQ_TX ? "TX" : "RX", ctx->mxc_state);
        }
        debug(BMX_DB_FUNC, "leaving %s", __func__);

        return 0;
}

/* Unlike bmi_ib, we always know our peername, check if the peer exists */
static const char *
BMI_mx_rev_lookup(struct method_addr *meth)
{
        struct bmx_method_addr  *mxmap = meth->method_data;

        debug(BMX_DB_FUNC, "entering %s", __func__);

        if (mxmap->mxm_peer && mxmap->mxm_peer->mxp_state != BMX_PEER_DISCONNECT)
                return mxmap->mxm_peername;
        else
                return "(unconnected)";
}


struct bmi_method_ops bmi_mx_ops = 
{
    .method_name                        = "bmi_mx",
    .BMI_meth_initialize                = BMI_mx_initialize,
    .BMI_meth_finalize                  = BMI_mx_finalize,
    .BMI_meth_set_info                  = BMI_mx_set_info,
    .BMI_meth_get_info                  = BMI_mx_get_info,
    .BMI_meth_memalloc                  = BMI_mx_memalloc,
    .BMI_meth_memfree                   = BMI_mx_memfree,
    .BMI_meth_unexpected_free           = BMI_mx_unexpected_free,
    .BMI_meth_post_send                 = BMI_mx_post_send,
    .BMI_meth_post_sendunexpected       = BMI_mx_post_sendunexpected,
    .BMI_meth_post_recv                 = BMI_mx_post_recv,
    .BMI_meth_test                      = BMI_mx_test,
    .BMI_meth_testsome                  = 0,
    .BMI_meth_testcontext               = BMI_mx_testcontext,
    .BMI_meth_testunexpected            = BMI_mx_testunexpected,
    .BMI_meth_method_addr_lookup        = BMI_mx_method_addr_lookup,
    .BMI_meth_post_send_list            = BMI_mx_post_send_list,
    .BMI_meth_post_recv_list            = BMI_mx_post_recv_list,
    .BMI_meth_post_sendunexpected_list  = BMI_mx_post_sendunexpected_list,
    .BMI_meth_open_context              = BMI_mx_open_context,
    .BMI_meth_close_context             = BMI_mx_close_context,
    .BMI_meth_cancel                    = BMI_mx_cancel,
    .BMI_meth_rev_lookup_unexpected     = BMI_mx_rev_lookup,
};

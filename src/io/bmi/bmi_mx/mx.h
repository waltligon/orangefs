/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *   Copyright (C) 2007 Myricom, Inc.
 *   Author: Myricom, Inc. <help at myri.com>
 *
 *   See COPYING in top-level directory.
 */

#ifndef __mx_h
#define __mx_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <mx_extensions.h>      /* needed for callback handler, etc. */
#include <myriexpress.h>

#include <src/common/id-generator/id-generator.h>
#include <src/io/bmi/bmi-method-support.h>   /* bmi_method_ops ... */
#include <src/io/bmi/bmi-method-callback.h>  /* bmi_method_addr_reg_callback */
#include <src/io/bmi/bmi-types.h>
#include <src/common/quicklist/quicklist.h>
#include <src/common/gen-locks/gen-locks.h>  /* gen_mutex_t ... */
#include <src/common/gossip/gossip.h>
#include <src/common/misc/pvfs2-internal.h>  /* lld(), llu() */

#ifdef __GNUC__
/* confuses debugger */
/* #  define __hidden __attribute__((visibility("hidden"))) */
#  define __hidden
#  define __unused __attribute__((unused))
#else
#  define __hidden
#  define __unused
#endif

typedef struct qlist_head list_t;       /* easier to type */

#define BMX_MAGIC            0x70766673 /* "pvfs" - for MX filter */
#define BMX_VERSION          0x100      /* version number */

/* Allocate 20 RX structs per peer */
#define BMX_PEER_RX_NUM      (20)
/* On servers only, these RX structs will also have a 8 KB buffer to
 * receive unexpected messages */
#define BMX_UNEXPECTED_SIZE  (8 << 10)
#define BMX_UNEXPECTED_NUM   2  /* buffers allocated per rx */

#define BMX_MEM_TWEAK 1         /* use buffer list for mem[alloc|free] */
#define BMX_DEBUG     1         /* enable debug (gossip) statements */
#define BMX_MEM_ACCT  0         /* track number of bytes alloc's and freed */

#if BMX_MEM_TWEAK
/* Allocate 16 4MB buffers for use with BMI_mx_mem[alloc|free] */
#define BMX_BUFF_SIZE      (4 << 20)
#define BMX_BUFF_NUM       16
#endif

#if BMX_MEM_ACCT
#define BMX_MALLOC(ptr, size)                                   \
        do {                                                    \
                gen_mutex_lock(&mem_used_lock);                 \
                mem_used += (size);                             \
                gen_mutex_unlock(&mem_used_lock);               \
                (ptr) = calloc(1, (size));                      \
                if ((ptr) == NULL) {                            \
                        gen_mutex_lock(&mem_used_lock);         \
                        mem_used -= (size);                     \
                        gen_mutex_unlock(&mem_used_lock);       \
                }                                               \
        } while (0)
#define BMX_FREE(ptr, size)                             \
        do {                                            \
                gen_mutex_lock(&mem_used_lock);         \
                mem_used -= (size);                     \
                gen_mutex_unlock(&mem_used_lock);       \
                if ((ptr) != NULL)                      \
                        free((ptr));                    \
        } while (0)
#else
#define BMX_MALLOC(ptr, size)                   \
        do {                                    \
                (ptr) = calloc(1, (size));      \
        } while (0)
#define BMX_FREE(ptr, size)                     \
        do {                                    \
                free((void *) (ptr));           \
        } while (0)
#endif /* BMX_MEM_ACCT */


/* The server will get unexpected messages from new clients.
 * Pre-allocate some rxs to catch these. The worst case is when
 * every client tries to connect at once. In this case, set this
 * value to the number of clients in PVFS. */
#define BMX_SERVER_RXS       (100)

/* BMX [UN]EXPECTED msgs use the 64-bits of the match info as follows:
 *    Bits      Description
 *    60-63     Msg type
 *    52-59     Msg class
 *    32-51     Peer id (of the sender, assigned by receiver)
 *     0-31     bmi_msg_tag_t
 */

/* BMX CONN_[REQ|ACK] msgs use the 64-bits of the match info as follows:
 *    Bits      Description
 *    60-63     Msg type
 *    52-59     Reserved
 *    32-51     Peer id (to use when contacting the sender)
 *     0-31     Version
 */

#define BMX_MSG_SHIFT   60
#define BMX_CLASS_SHIFT 52
#define BMX_ID_SHIFT    32
#define BMX_MASK_ALL    (~0ULL)
#define BMX_MASK_MSG    (0xFULL << BMX_MSG_SHIFT)
#define BMX_MASK_MSG_AND_CLASS    (0xFFFULL << BMX_CLASS_SHIFT)

#define BMX_MAX_PEER_ID ((1<<20) - 1)   /* 20 bits - actually 1,048,574 peers
                                           1 to 1,048,575 */
#define BMX_MAX_TAG     (~0U)            /* 32 bits */


#define BMX_TIMEOUT     (20 * 1000)       /* msg timeout in milliseconds */

#if BMX_MEM_TWEAK
struct bmx_buffer
{
        list_t              mxb_list;     /* hang on used/idle lists */
        int                 mxb_used;     /* count how many times used */
        void               *mxb_buffer;   /* the actual buffer */
};
#endif

/* Global interface state */
struct bmx_data
{
        int                 bmx_method_id;      /* BMI id assigned to us */

        char               *bmx_peername;       /* my mx://hostname/board/ep_id  */
        char               *bmx_hostname;       /* my hostname */
        uint32_t            bmx_board;          /* my MX board index */
        uint32_t            bmx_ep_id;          /* my MX endpoint ID */
        mx_endpoint_t       bmx_ep;             /* my MX endpoint */
        uint32_t            bmx_sid;            /* my MX session id */
        int                 bmx_is_server;      /* am I a server? */

        list_t              bmx_peers;          /* list of all peers */
        gen_mutex_t         bmx_peers_lock;     /* peers list lock */

        list_t              bmx_txs;            /* list of all txs for cleanup */
        list_t              bmx_idle_txs;       /* available for sending */
        gen_mutex_t         bmx_idle_txs_lock;  /* idle_txs lock */

        list_t              bmx_rxs;            /* list of all rxs for cleanup */
        list_t              bmx_idle_rxs;       /* available for receiving */
        gen_mutex_t         bmx_idle_rxs_lock;  /* idle_rxs lock */

        gen_mutex_t         bmx_completion_lock; /* lock for test* functions */
        int                 bmx_refcount;       /* try to avoid races between test*
                                                   and cancel functions */

                                                /* completed expected msgs
                                                 * including unexpected sends */
        list_t              bmx_done_q[BMI_MAX_CONTEXTS];
        gen_mutex_t         bmx_done_q_lock[BMI_MAX_CONTEXTS];

        list_t              bmx_unex_rxs;       /* completed unexpected recvs */
        gen_mutex_t         bmx_unex_rxs_lock;  /* completed unexpected recvs lock */

        uint32_t            bmx_next_id;        /* for the next peer_id */
        gen_mutex_t         bmx_lock;           /* global lock - use for global rxs,
                                                   global txs, next_id, etc. */

#if BMX_MEM_TWEAK
        list_t              bmx_idle_buffers;
        gen_mutex_t         bmx_idle_buffers_lock;
        list_t              bmx_used_buffers;
        gen_mutex_t         bmx_used_buffers_lock;
        int                 bmx_misses;

        list_t              bmx_idle_unex_buffers;
        gen_mutex_t         bmx_idle_unex_buffers_lock;
        list_t              bmx_used_unex_buffers;
        gen_mutex_t         bmx_used_unex_buffers_lock;
        int                 bmx_unex_misses;
#endif
};

enum bmx_peer_state {
        BMX_PEER_DISCONNECT     = -1,       /* disconnecting, failed, etc. */
        BMX_PEER_INIT           =  0,       /* ready for connect */
        BMX_PEER_WAIT           =  1,       /* trying to connect */
        BMX_PEER_READY          =  2,       /* connected */
};

struct bmx_method_addr
{
        struct bmi_method_addr  *mxm_map;        /* peer's bmi_method_addrt */
        const char              *mxm_peername;   /* mx://hostname/board/ep_id  */
        const char              *mxm_hostname;   /* peer's hostname */
        uint32_t                 mxm_board;      /* peer's MX board index */
        uint32_t                 mxm_ep_id;      /* peer's MX endpoint ID */
        struct bmx_peer         *mxm_peer;       /* peer pointer */
};

struct bmx_peer
{
        struct bmi_method_addr *mxp_map;        /* his bmi_method_addr * */
        struct bmx_method_addr *mxp_mxmap;      /* his bmx_method_addr */
        uint64_t                mxp_nic_id;     /* his NIC id */
        mx_endpoint_addr_t      mxp_epa;        /* his MX endpoint address */
        uint32_t                mxp_sid;        /* his MX session id */
        int                     mxp_exist;      /* have we connected before? */

        enum bmx_peer_state     mxp_state;      /* INIT, WAIT, READY, DISCONNECT */
        uint32_t                mxp_tx_id;      /* id assigned to me by peer */
        uint32_t                mxp_rx_id;      /* id I assigned to peer */

        list_t                  mxp_queued_txs; /* waiting on READY */
        list_t                  mxp_queued_rxs; /* waiting on INIT (if in DISCONNECT) */
        list_t                  mxp_pending_rxs; /* in-flight rxs (in case of cancel) */
        int                     mxp_refcount;   /* queued and pending txs and pending rxs */

        list_t                  mxp_list;       /* hang this on bmx_peers */
        gen_mutex_t             mxp_lock;       /* peer lock */
};

enum bmx_req_type {
        BMX_REQ_TX      = 1,
        BMX_REQ_RX      = 2,
};

enum bmx_ctx_state {
        BMX_CTX_INIT            = 0,
        BMX_CTX_IDLE            = 1,    /* state when on tx/rx idle list */
        BMX_CTX_PREP            = 2,    /* not really useful */
        BMX_CTX_QUEUED          = 3,    /* queued_[txs|rxs] */
        BMX_CTX_PENDING         = 4,    /* posted */
        BMX_CTX_COMPLETED       = 5,    /* waiting for user test */
        BMX_CTX_CANCELED        = 6,    /* canceled and waiting user test */
};

enum bmx_msg_type {
        BMX_MSG_ICON_REQ        = 0xb,      /* iconnect() before CONN_REQ */
        BMX_MSG_CONN_REQ        = 0xc,      /* bmx connect request message */
        BMX_MSG_ICON_ACK        = 0x9,      /* iconnect() before CONN_ACK */
        BMX_MSG_CONN_ACK        = 0xa,      /* bmx connect ack msg */
        BMX_MSG_UNEXPECTED      = 0xd,      /* unexpected msg */
        BMX_MSG_EXPECTED        = 0xe,      /* expected message */
};

struct bmx_ctx
{
        enum bmx_req_type   mxc_type;       /* TX or RX */
        enum bmx_ctx_state  mxc_state;      /* INIT, IDLE, PREP, PENDING, ... */
        enum bmx_msg_type   mxc_msg_type;   /* UNEXPECTED, EXPECTED, ... */

        list_t              mxc_global_list; /* hang on global list for cleanup */
        list_t              mxc_list;       /* hang on idle, queued and pending lists */

        struct method_op   *mxc_mop;        /* method op */
        struct bmx_peer    *mxc_peer;       /* owning peer */
        bmi_msg_tag_t       mxc_tag;        /* BMI tag (int32_t) */
        uint8_t             mxc_class;      /* BMI unexpected msg class */
        uint64_t            mxc_match;      /* match info */

        mx_segment_t        mxc_seg;        /* local MX segment */
        void               *mxc_buffer;     /* local buffer for server unexpected rxs
                                               and client CONN_REQ only */
        mx_segment_t       *mxc_seg_list;   /* MX segment array for _list funcs */
        int                 mxc_nseg;       /* number of segments */
        bmi_size_t          mxc_nob;        /* number of bytes (int64_t) */
        mx_request_t        mxc_mxreq;      /* MX request */
        mx_status_t         mxc_mxstat;     /* MX status */
        bmi_error_code_t    mxc_error;      /* BMI error code */

        uint64_t            mxc_get;        /* # of times returned from idle list */
        uint64_t            mxc_put;        /* # of times returned to idle list */
};

struct bmx_connreq
{
        char                mxm_peername[256]; /* my peername mx://hostname/... */
} __attribute__ ((__packed__)) ;

/*
 * Debugging macros.
 */
#define BMX_DB_MEM      0x1     /* memory alloc/free and accounting */
#define BMX_DB_CTX      0x2     /* handling tx/rx structures, sending/receiving */
#define BMX_DB_PEER     0x4     /* modifying peers */
#define BMX_DB_CONN     0x8     /* connection handling */
#define BMX_DB_ERR      0x10    /* fatal errors, should always be followed by exit() */
#define BMX_DB_FUNC     0x20    /* enterling/leaving functions */
#define BMX_DB_INFO     0x40    /* just informational */
#define BMX_DB_WARN     0x80    /* non-fatal error */
#define BMX_DB_MX       0x100   /* MX functions */

#define BMX_DB_ALL      0xFFFF  /* print everything */

#if BMX_DEBUG
/* set the mask to the BMX_DB_* errors that you want gossip to report */
#define BMX_DB_MASK (BMX_DB_ERR|BMX_DB_WARN)
#define debug(lvl,fmt,args...)                                                 \
  do {                                                                         \
      if (lvl & BMX_DB_MASK) {                                                 \
          if (lvl & (BMX_DB_ERR | BMX_DB_WARN)) { /* always send to log */     \
              gossip_err("bmi_mx: " fmt ".\n", ##args);                        \
              gossip_backtrace();                                              \
          } else {                                                             \
              gossip_debug(GOSSIP_BMI_DEBUG_MX, "bmi_mx: " fmt ".\n", ##args); \
          }                                                                    \
      }                                                                        \
  } while (0)
#else  /* ! BMX_DEBUG */
#define debug(lvl,fmt,...) do { } while (0)
#endif /* BMX_DEBUG */

#define BMX_ENTER                                                               \
  do {                                                                          \
        debug(BMX_DB_FUNC, "entering %s", __func__);                            \
  } while (0);

#define BMX_EXIT                                                                \
  do {                                                                          \
        debug(BMX_DB_FUNC, "exiting  %s", __func__);                            \
  } while (0);

#endif  /* __mx_h */

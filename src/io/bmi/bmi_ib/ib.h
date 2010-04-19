/*
 * Private header shared by BMI InfiniBand implementation files.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 */
#ifndef __ib_h
#define __ib_h

#include <src/io/bmi/bmi-types.h>
#include <src/common/quicklist/quicklist.h>
#include <src/common/gossip/gossip.h>
#include <pvfs2-debug.h>
#include <pvfs2-encode-stubs.h>
#include <pvfs2-types.h>

#ifdef __GNUC__
/* confuses debugger */
/* #  define __hidden __attribute__((visibility("hidden"))) */
#  define __hidden
#  define __unused __attribute__((unused))
#else
#  define __hidden
#  define __unused
#endif

/* 20 8kB buffers allocated to each connection for unexpected messages */
#define DEFAULT_EAGER_BUF_NUM  (20)
#define DEFAULT_EAGER_BUF_SIZE (8 << 10)

struct buf_head;

/*
 * Connection record.  Each machine gets its own set of buffers and
 * an entry in this table.
 */
typedef struct {
    struct qlist_head list;

    /* connection management */
    struct bmi_method_addr *remote_map;
    char *peername;  /* string representation of remote_map */

    /* per-connection buffers */
    void *eager_send_buf_contig;    /* bounce bufs, for short sends */
    void *eager_recv_buf_contig;    /* eager bufs, for short recvs */

    /* lists of free bufs */
    struct qlist_head eager_send_buf_free;
    struct qlist_head eager_recv_buf_free;
    struct buf_head *eager_send_buf_head_contig;
    struct buf_head *eager_recv_buf_head_contig;

    int cancelled;  /* was any operation cancelled by BMI */
    int refcnt;  /* sq or rq that need the connection to hang around */
    int closed;  /* closed, but hanging around waiting for zero refcnt */

    int send_credit;    /* free slots on receiver */
    int return_credit;  /* receive buffers he filled but that we've emptied */

    void *priv;

    BMI_addr_t bmi_addr;
} ib_connection_t;

/*
 * List structure of buffer areas, represents one at each local
 * and remote sides.
 */
struct buf_head {
    struct qlist_head list;
    int num;             /* ordinal index in the alloced buf heads */
    ib_connection_t *c;  /* owning connection */
    struct ib_work *sq;  /* owning sq (usually) or rq */
    void *buf;           /* actual memory */
};

/* "private data" part of method_addr */
typedef struct {
    char *hostname;
    int port;
    ib_connection_t *c;
    int reconnect_flag;
} ib_method_addr_t;

/*
 * Names of all the sendq and recvq states and message types, with string
 * arrays for debugging.  These must start at 1, not 0, for name printing.
 */
typedef enum {
    SQ_WAITING_BUFFER = 1,
    SQ_WAITING_EAGER_SEND_COMPLETION,
    SQ_WAITING_RTS_SEND_COMPLETION,
    SQ_WAITING_RTS_SEND_COMPLETION_GOT_CTS,
    SQ_WAITING_CTS,
    SQ_WAITING_DATA_SEND_COMPLETION,
    SQ_WAITING_RTS_DONE_BUFFER,
    SQ_WAITING_RTS_DONE_SEND_COMPLETION,
    SQ_WAITING_USER_TEST,
    SQ_CANCELLED,
} sq_state_t;
typedef enum {  /* bits *_USER_POST will be ORed */
    RQ_EAGER_WAITING_USER_POST = 0x1,
    RQ_EAGER_WAITING_USER_TESTUNEXPECTED = 0x2,
    RQ_EAGER_WAITING_USER_TEST = 0x4,
    RQ_RTS_WAITING_USER_POST = 0x8,
    RQ_RTS_WAITING_CTS_BUFFER = 0x10,
    RQ_RTS_WAITING_CTS_SEND_COMPLETION = 0x20,
    RQ_RTS_WAITING_RTS_DONE = 0x40,
    RQ_RTS_WAITING_USER_TEST = 0x80,
    RQ_WAITING_INCOMING = 0x100,
    RQ_CANCELLED = 0x200,
} rq_state_t;
typedef enum {
    MSG_EAGER_SEND = 1,
    MSG_EAGER_SENDUNEXPECTED,
    MSG_RTS,
    MSG_CTS,
    MSG_RTS_DONE,
    MSG_CREDIT,
    MSG_BYE,
} msg_type_t;

#ifdef __util_c
#define entry(x) { x, #x }
typedef struct {
    int num;
    const char *name;
} name_t;
static name_t sq_state_names[] = {
    entry(SQ_WAITING_BUFFER),
    entry(SQ_WAITING_EAGER_SEND_COMPLETION),
    entry(SQ_WAITING_RTS_SEND_COMPLETION),
    entry(SQ_WAITING_RTS_SEND_COMPLETION_GOT_CTS),
    entry(SQ_WAITING_CTS),
    entry(SQ_WAITING_DATA_SEND_COMPLETION),
    entry(SQ_WAITING_RTS_DONE_BUFFER),
    entry(SQ_WAITING_RTS_DONE_SEND_COMPLETION),
    entry(SQ_WAITING_USER_TEST),
    entry(SQ_CANCELLED),
    { 0, 0 }
};
static name_t rq_state_names[] = {
    entry(RQ_EAGER_WAITING_USER_POST),
    entry(RQ_EAGER_WAITING_USER_TESTUNEXPECTED),
    entry(RQ_EAGER_WAITING_USER_TEST),
    entry(RQ_RTS_WAITING_USER_POST),
    entry(RQ_RTS_WAITING_CTS_BUFFER),
    entry(RQ_RTS_WAITING_CTS_SEND_COMPLETION),
    entry(RQ_RTS_WAITING_RTS_DONE),
    entry(RQ_RTS_WAITING_USER_TEST),
    entry(RQ_WAITING_INCOMING),
    entry(RQ_CANCELLED),
    { 0, 0 }
};
static name_t msg_type_names[] = {
    entry(MSG_EAGER_SEND),
    entry(MSG_EAGER_SENDUNEXPECTED),
    entry(MSG_RTS),
    entry(MSG_CTS),
    entry(MSG_RTS_DONE),
    entry(MSG_CREDIT),
    entry(MSG_BYE),
    { 0, 0 }
};
#undef entry
#endif  /* __ib_c */

/*
 * Pin and cache explicitly allocated things to avoid registration
 * overheads.  Two sources of entries here:  first, when BMI_memalloc
 * is used to allocate big enough chunks, the malloced regions are
 * entered into this list.  Second, when a bmi/ib routine needs to pin
 * memory, it is cached here too.  Note that the second case really
 * needs a dreg-style consistency check against userspace freeing, though.
 */
typedef struct {
    struct qlist_head list;
    void *buf;
    bmi_size_t len;
    int count;  /* refcount, usage of this entry */

    /* IB-specific fields */
    struct {
	uint64_t mrh;   /* pointer on openib, 32-bit int on vapi */
	uint32_t lkey;  /* 32-bit mandated by IB spec */
	uint32_t rkey;
    } memkeys;
} memcache_entry_t;

/*
 * This struct describes multiple memory ranges, used in the list send and
 * recv routines.  The memkeys array here will be filled from the individual
 * memcache_entry memkeys above.
 */
typedef struct {
    int num;
    union {
	const void *const *send;
	void *const *recv;
    } buf;
    const bmi_size_t *len;  /* this type chosen to match BMI API */
    bmi_size_t tot_len;     /* sum_{i=0..num-1} len[i] */
    memcache_entry_t **memcache; /* storage managed by memcache_register etc. */
} ib_buflist_t;

/*
 * Common structure for both ib_send and ib_recv outstanding work items.
 */
struct ib_work {
    struct qlist_head list;
    int type;  /* BMI_SEND or BMI_RECV */
    struct method_op *mop;   /* pointer back to owning method_op */

    ib_connection_t *c;

    /* gather (send) or scatter (recv) list of buffers */
    ib_buflist_t buflist;

    /* places to hang just one buf when not using _list funcs, avoids
     * small mallocs in that case but permits use of generic code */
    union {
	const void *send;
	void *recv;
    } buflist_one_buf;
    bmi_size_t  buflist_one_len;

    /* bh represents our local buffer for sending, maybe CTS messages
     * as sent for receive items */
    struct buf_head *bh;

    /* tag as posted by user, or return value on recvs */
    bmi_msg_tag_t bmi_tag;

    /* send or receive state */
    union {
	sq_state_t send;
	rq_state_t recv;
    } state;

    int is_unexpected;      /* send: if user posted an unexpected message */
    u_int64_t rts_mop_id;    /* recv: return tag to give to rts sender */
    bmi_size_t actual_len;   /* recv: could be shorter than posted */
    uint8_t class;          /* only for unexpected messages */
};

/*
 * Header structure used for various sends.  Make sure these stay fully 64-bit
 * aligned.  All of eager, rts, and cts messages must start with ->type so
 * we can switch on that first.  All elements in these arrays will be encoded
 * by the usual le-bytefield encoder used for all wire transfers.
 */
typedef struct {
    msg_type_t type;
    u_int32_t credit;  /* return credits */
} msg_header_common_t;
endecode_fields_2(msg_header_common_t,
    enum, type,
    uint32_t, credit);

/*
 * Eager message header, with data following this struct.
 */
typedef struct {
    msg_header_common_t c;
    bmi_msg_tag_t bmi_tag;
    uint32_t class;
} msg_header_eager_t;
endecode_fields_4(msg_header_eager_t,
    enum, c.type,
    uint32_t, c.credit,
    int32_t, bmi_tag,
    uint32_t, class);

/*
 * MSG_RTS instead of MSG_EAGER from sender to receiver for big messages.
 */
typedef struct {
    msg_header_common_t c;
    bmi_msg_tag_t bmi_tag;
    u_int32_t __pad;
    u_int64_t mop_id;  /* handle to ease lookup when CTS is delivered */
    u_int64_t tot_len;
} msg_header_rts_t;
endecode_fields_6(msg_header_rts_t,
    enum, c.type,
    uint32_t, c.credit,
    int32_t, bmi_tag,
    int32_t, __pad,
    uint64_t, mop_id,
    uint64_t, tot_len);

/*
 * MSG_CTS from receiver to sender with buffer information.
 */
typedef struct {
    msg_header_common_t c;
    u_int64_t rts_mop_id;  /* return id from the RTS */
    u_int64_t buflist_tot_len;
    u_int32_t buflist_num;  /* number of buffers, then lengths to follow */
    u_int32_t __pad;
    /* format:
     *   u_int64_t buf[1..buflist_num]
     *   u_int32_t len[1..buflist_num]
     *   u_int32_t key[1..buflist_num]
     */
} msg_header_cts_t;
#define MSG_HEADER_CTS_BUFLIST_ENTRY_SIZE (8 + 4 + 4)
endecode_fields_5(msg_header_cts_t,
    enum, c.type,
    uint32_t, c.credit,
    uint64_t, rts_mop_id,
    uint64_t, buflist_tot_len,
    uint32_t, buflist_num);

/*
 * After RDMA data has been sent, this RTS_DONE message tells the
 * receiver that the sender has finished.
 */
typedef struct {
    msg_header_common_t c;
    u_int64_t mop_id;
} msg_header_rts_done_t;
endecode_fields_3(msg_header_rts_done_t,
    enum, c.type,
    uint32_t, c.credit,
    uint64_t, mop_id);


/*
 * Generic work completion from poll_cq() for both vapi and openib.
 */
struct bmi_ib_wc {
    uint64_t id;
    int status;  /* opaque, but zero means success */
    uint32_t byte_len;
    enum { BMI_IB_OP_SEND, BMI_IB_OP_RECV, BMI_IB_OP_RDMA_WRITE } opcode;
};

/*
 * VAPI or OpenIB functions
 */
struct ib_device_func {
    int (*new_connection)(ib_connection_t *c, int sock, int is_server);
    void (*close_connection)(ib_connection_t *c);
    void (*drain_qp)(ib_connection_t *c);
    int (*ib_initialize)(void);
    void (*ib_finalize)(void);
    void (*post_sr)(const struct buf_head *bh, u_int32_t len);
    void (*post_rr)(const ib_connection_t *c, struct buf_head *bh);
    void (*post_sr_rdmaw)(struct ib_work *sq, msg_header_cts_t *mh_cts,
                          void *mh_cts_buf);
    void (*prepare_cq_block)(int *cq_fd, int *async_fd);
    void (*ack_cq_completion_event)(void);
    int (*check_cq)(struct bmi_ib_wc *wc);
    const char *(*wc_status_string)(int status);
    int (*mem_register)(memcache_entry_t *c);
    void (*mem_deregister)(memcache_entry_t *c);
    int (*check_async_events)(void);
};

/*
 * State that applies across all users of the device, built at initialization.
 */
typedef struct {
    int listen_sock;  /* TCP sock on which to listen for new connections */
    struct bmi_method_addr *listen_addr;  /* and BMI listen address */

    struct qlist_head connection; /* list of current connections */
    struct qlist_head sendq;  /* outstanding sent items */
    struct qlist_head recvq;  /* outstanding posted receives (or unexpecteds) */
    void *memcache;  /* opaque structure that holds memory cache state */

    /*
     * Both eager and bounce buffers are the same size, and same number, since
     * there is a symmetry of how many can be in use at the same time.
     * This number limits the number of outstanding entries in the unexpected
     * receive queue, and expected but non-preposted queue, since the data
     * sits in the buffer where it was received until the user posts or tests
     * for it.
     */
    int eager_buf_num;
    unsigned long eager_buf_size;
    bmi_size_t eager_buf_payload;

    void *priv;
    struct ib_device_func func;

} ib_device_t;
extern ib_device_t *ib_device;

/*
 * Internal functions in util.c.
 */
void error(const char *fmt, ...) __attribute__((noreturn,format(printf,1,2)));
void error_errno(const char *fmt, ...)
  __attribute__((noreturn,format(printf,1,2)));
void error_xerrno(int errnum, const char *fmt, ...)
  __attribute__((noreturn,format(printf,2,3)));
void warning(const char *fmt, ...) __attribute__((format(printf,1,2)));
void warning_errno(const char *fmt, ...) __attribute__((format(printf,1,2)));
void warning_xerrno(int errnum, const char *fmt, ...)
  __attribute__((format(printf,2,3)));
void info(const char *fmt, ...) __attribute__((format(printf,1,2)));
void *bmi_ib_malloc(unsigned long n) __attribute__((malloc));
void *qlist_del_head(struct qlist_head *list);
void *qlist_try_del_head(struct qlist_head *list);
const char *sq_state_name(sq_state_t num);
const char *rq_state_name(rq_state_t num);
const char *msg_type_name(msg_type_t num);
void memcpy_to_buflist(ib_buflist_t *buflist, const void *buf, bmi_size_t len);
void memcpy_from_buflist(ib_buflist_t *buflist, void *buf);
int read_full(int fd, void *buf, size_t num);
int write_full(int fd, const void *buf, size_t count);

/*
 * Memory allocation and caching internal functions, in mem.c.
 */
void *memcache_memalloc(void *md, bmi_size_t len, int eager_limit);
int memcache_memfree(void *md, void *buf, bmi_size_t len);
void memcache_register(void *md, ib_buflist_t *buflist);
void memcache_preregister(void *md, const void *buf, bmi_size_t len,
                          enum PVFS_io_type rw);
void memcache_deregister(void *md, ib_buflist_t *buflist);
void *memcache_init(int (*mem_register)(memcache_entry_t *),
                    void (*mem_deregister)(memcache_entry_t *));
void memcache_shutdown(void *md);
void memcache_cache_flush(void *md);

/*
 * Handle pointer to 64-bit integer conversions.  On 32-bit architectures
 * the extra (unsigned long) cast truncates the 64-bit int before trying to
 * stuff it into a 32-bit pointer.
 */
#define ptr_from_int64(p) (void *)(unsigned long)(p)
#define int64_from_ptr(p) (u_int64_t)(unsigned long)(p)

/* make cast from list entry to actual item explicit */
#define qlist_upcast(l) ((void *)(l))

/*
 * Tell the compiler about situations that we truly expect to happen
 * or not.  Funny name to avoid collision with some IB libraries.
 */
#if defined(__GNUC_MINOR__) && (__GNUC_MINOR__ < 96)
# define __builtin_expect(x, v) (x)
#endif
#define bmi_ib_likely(x)       __builtin_expect(!!(x), 1)
#define bmi_ib_unlikely(x)     __builtin_expect(!!(x), 0)

/*
 * Memory caching settings, needed both by ib.c and vapi.c or openib.c.
 */
#define MEMCACHE_BOUNCEBUF 0
#define MEMCACHE_EARLY_REG 1

/*
 * Debugging macros.
 */
#if 1
#define DEBUG_LEVEL 2
#define debug(lvl,fmt,args...) \
    do { \
	if (lvl <= DEBUG_LEVEL) \
	    gossip_debug(GOSSIP_BMI_DEBUG_IB, fmt ".\n", ##args); \
    } while (0)
#else
#  define debug(lvl,fmt,...) do { } while (0)
#endif

/*
 * Varargs form of assert().
 */
#if !defined(NDEBUG)
#define bmi_ib_assert(cond, fmt, args...) \
    do { \
	if (bmi_ib_unlikely(!(cond))) \
	    error(fmt, ##args); \
    } while (0)
#else
#  define bmi_ib_assert(cond, fmt, ...) do { } while (0)
#endif

#endif  /* __ib_h */

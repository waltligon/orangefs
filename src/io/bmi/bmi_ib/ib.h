/*
 * Private header shared by BMI InfiniBand implementation files.
 *
 * Copyright (C) 2003-5 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * $Id: ib.h,v 1.10 2005-12-23 20:47:53 pw Exp $
 */
#ifndef __ib_h
#define __ib_h

#include <src/io/bmi/bmi-types.h>
#include <src/common/quicklist/quicklist.h>
#include <vapi.h>

#ifdef __GNUC__
/* #  define __hidden __attribute__((visibility("hidden"))) */
#  define __hidden  /* confuses debugger */
#  define __unused __attribute__((unused))
#else
#  define __hidden
#  define __unused
#endif

typedef struct qlist_head list_t;  /* easier to type */

struct S_buf_head;
/*
 * Connection record.  Each machine gets its own set of buffers and
 * an entry in this table.
 */
typedef struct {
    list_t list;

    /* connection management */
    struct method_addr *remote_map;
    char *peername;  /* string representation of remote_map */

    /* ib local params */
    VAPI_qp_hndl_t qp;
    VAPI_qp_hndl_t qp_ack;
    VAPI_qp_num_t qp_num;
    VAPI_qp_num_t qp_ack_num;
    VAPI_mr_hndl_t eager_send_mr;
    VAPI_mr_hndl_t eager_recv_mr;
    VAPI_lkey_t eager_send_lkey;  /* for post_sr */
    VAPI_lkey_t eager_recv_lkey;  /* for post_rr */
    unsigned int num_unsignaled_wr;  /* keep track of outstanding WRs */
    unsigned int num_unsignaled_wr_ack;
    /* ib remote params */
    IB_lid_t remote_lid;
    VAPI_qp_num_t remote_qp_num;
    VAPI_qp_num_t remote_qp_ack_num;

    /* per-connection buffers */
    void *eager_send_buf_contig;    /* bounce bufs, for short sends */
    void *eager_recv_buf_contig;    /* eager bufs, for short recvs */

    /* lists of free bufs */
    list_t eager_send_buf_free;
    list_t eager_recv_buf_free;
    struct S_buf_head *eager_send_buf_head_contig;
    struct S_buf_head *eager_recv_buf_head_contig;

    int cancelled;  /* was any operation cancelled by BMI */

} ib_connection_t;

/*
 * List structure of buffer areas, represents one at each local
 * and remote sides.
 */
typedef struct S_buf_head {
    list_t list;
    int num;               /* ordinal index in the alloced buf heads */
    ib_connection_t *c;    /* owning connection */
    struct S_ib_send *sq;  /* owning sq or rq */
    void *buf;             /* actual memory */
} buf_head_t;

/* "private data" part of method_addr */
typedef struct {
    const char *hostname;
    int port;
    ib_connection_t *c;
} ib_method_addr_t;

/*
 * Names of all the sendq and recvq states and message types, with string
 * arrays for debugging.
 */
typedef enum {
    SQ_WAITING_BUFFER=1,
    SQ_WAITING_EAGER_ACK,
    SQ_WAITING_CTS,
    SQ_WAITING_DATA_LOCAL_SEND_COMPLETE,
    SQ_WAITING_USER_TEST,
    SQ_CANCELLED,
} sq_state_t;
typedef enum {
    RQ_EAGER_WAITING_USER_POST = 0x1,
    RQ_EAGER_WAITING_USER_TESTUNEXPECTED = 0x2,
    RQ_EAGER_WAITING_USER_TEST = 0x4,
    RQ_RTS_WAITING_USER_POST = 0x8,
    RQ_RTS_WAITING_CTS_BUFFER = 0x10,
    RQ_RTS_WAITING_DATA = 0x20,
    RQ_RTS_WAITING_USER_TEST = 0x40,
    RQ_WAITING_INCOMING = 0x80,
    RQ_CANCELLED = 0x100,
} rq_state_t;
typedef enum {
    MSG_EAGER_SEND=1,
    MSG_EAGER_SENDUNEXPECTED,
    MSG_RTS,
    MSG_CTS,
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
    entry(SQ_WAITING_EAGER_ACK),
    entry(SQ_WAITING_CTS),
    entry(SQ_WAITING_DATA_LOCAL_SEND_COMPLETE),
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
    entry(RQ_RTS_WAITING_DATA),
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
    entry(MSG_BYE),
    { 0, 0 }
};
#undef entry
#endif  /* __ib_c */

/*
 * Fields for memory registration.  Both lkey and rkey are always valid,
 * even if only being used for a BMI_SEND.  Permissions managed at app level
 * not at mem reg level.
 */
typedef struct {
    VAPI_mr_hndl_t mrh;
    VAPI_lkey_t lkey;
    VAPI_rkey_t rkey;
} memkeys_t;

/*
 * Pin and cache explicitly allocated things to avoid registration
 * overheads.  Two sources of entries here:  first, when BMI_memalloc
 * is used to allocate big enough chunks, the malloced regions are
 * entered into this list.  Second, when a bmi/ib routine needs to pin
 * memory, it is cached here too.  Note that the second case really
 * needs a dreg-style consistency check against userspace freeing, though.
 */
typedef struct {
    list_t list;
    void *buf;
    bmi_size_t len;
    int count;  /* refcount, usage of this entry */
    memkeys_t memkeys;
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
 * Send message record.  There is no EAGER_SENT since we use RD which
 * ensures reliability, so the message is marked complete immediately
 * and removed from the queue.
 */
typedef struct S_ib_send {
    list_t list;
    int type;  /* BMI_SEND */
    /* pointer back to owning method_op (BMI interface) */
    struct method_op *mop;
    sq_state_t state;
    ib_connection_t *c;

    /* gather list of buffers */
    ib_buflist_t buflist;

    /* places to hang just one buf when not using _list funcs, avoids
     * small mallocs in that case but permits use of generic code */
    const void *buflist_one_buf;
    bmi_size_t  buflist_one_len;

    int is_unexpected;  /* if user posted this that way */
    /* bh represents one local and one remote buffer tied up */
    buf_head_t *bh;
    bmi_msg_tag_t bmi_tag;
} ib_send_t;

/*
 * Receive message record.
 */
typedef struct {
    list_t list;
    int type;  /* BMI_RECV */
    /* pointer back to owning method_op (BMI interface) */
    struct method_op *mop;
    rq_state_t state;
    ib_connection_t *c;

    /* scatter list of buffers */
    ib_buflist_t buflist;

    /* places to hang just one buf when not using _list funcs, avoids
     * small mallocs in that case but permits use of generic code */
    void *      buflist_one_buf;
    bmi_size_t  buflist_one_len;

    /* local and remote buf heads for sending associated cts */
    buf_head_t *bh, *bhr;
    u_int64_t rts_mop_id;  /* return tag to give to rts sender */
    /* return value for test and wait, necessary when not pre-posted */
    bmi_msg_tag_t bmi_tag;
    bmi_size_t actual_len;
} ib_recv_t;

/*
 * Header structure used on top of eager sends, and also used to request
 * rendez-vous mode sends, or to indicate eager ack.
 * Make sure these stay fully 64-bit aligned.
 */
typedef struct {
    msg_type_t type;
    bmi_msg_tag_t bmi_tag;
} msg_header_t;

/*
 * Follows msg_header_t only on MSG_RTS messages.
 */
typedef struct {
    u_int64_t mop_id;  /* handle to ease lookup when CTS is delivered */
    u_int64_t tot_len;
} msg_header_rts_t;

/*
 * Same for MSG_CTS
 */
typedef struct {
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

/*
 * Internal functions in setup.c used by ib.c.
 */
extern void ib_close_connection(ib_connection_t *c);
extern void close_connection_drain_qp(VAPI_qp_hndl_t qp);
extern int ib_tcp_client_connect(ib_method_addr_t *ibmap,
  struct method_addr *remote_map);
extern int ib_tcp_server_check_new_connections(void);
extern int ib_tcp_server_block_new_connections(int timeout_ms);
extern void ib_mem_register(memcache_entry_t *c);
extern void ib_mem_deregister(memcache_entry_t *c);

/*
 * Method functions in setup.c.
 */
extern int BMI_ib_initialize(struct method_addr *listen_addr, int method_id,
  int init_flags);
extern int BMI_ib_finalize(void);
extern struct method_addr *ib_alloc_method_addr(ib_connection_t *c,
  const char *hostname, int port);
extern struct method_addr *BMI_ib_method_addr_lookup(const char *id);
extern int BMI_ib_get_info(int option, void *param);
extern int BMI_ib_set_info(int option, void *param);

/*
 * Internal functions in ib.c used by setup.c.
 */
extern void post_rr(const ib_connection_t *c, buf_head_t *bh);

/*
 * Internal functions in util.c.
 */
void error(const char *fmt, ...) __attribute__((noreturn,format(printf,1,2)));
void error_errno(const char *fmt, ...)
  __attribute__((noreturn,format(printf,1,2)));
void error_xerrno(int errnum, const char *fmt, ...)
  __attribute__((noreturn,format(printf,2,3)));
void error_verrno(int ecode, const char *fmt, ...)
  __attribute__((noreturn,format(printf,2,3)));
void warning(const char *fmt, ...) __attribute__((format(printf,1,2)));
void warning_errno(const char *fmt, ...) __attribute__((format(printf,1,2)));
void info(const char *fmt, ...) __attribute__((format(printf,1,2)));
extern void *Malloc(unsigned long n) __attribute__((malloc));
extern u_int64_t swab64(u_int64_t x);
extern void *qlist_del_head(struct qlist_head *list);
extern void *qlist_try_del_head(struct qlist_head *list);
/* convenient to assign this to the owning type */
#define qlist_upcast(l) ((void *)(l))
extern const char *sq_state_name(sq_state_t num);
extern const char *rq_state_name(rq_state_t num);
extern const char *msg_type_name(msg_type_t num);
extern void memcpy_to_buflist(ib_buflist_t *buflist, const void *buf,
  bmi_size_t len);
extern void memcpy_from_buflist(ib_buflist_t *buflist, void *buf);
extern int read_full(int fd, void *buf, size_t num);
extern int write_full(int fd, const void *buf, size_t count);

/*
 * Memory allocation and caching internal functions, in mem.c.
 */
extern void *BMI_ib_memalloc(bmi_size_t size, enum bmi_op_type send_recv);
extern int BMI_ib_memfree(void *buf, bmi_size_t size,
  enum bmi_op_type send_recv);
extern void memcache_register(ib_buflist_t *buflist);
extern void memcache_deregister(ib_buflist_t *buflist);
extern void memcache_shutdown(void);

/*
 * Shared variables, space allocated in ib.c.
 */
extern VAPI_hca_hndl_t nic_handle;  /* NIC reference */
extern VAPI_cq_hndl_t nic_cq;  /* single completion queue for all QPs */
extern int listen_sock;  /* TCP sock on whih to listen for new connections */
extern list_t connection; /* list of current connections */
extern list_t sendq;
extern list_t recvq;
extern list_t memcache;
/*
 * Temp array for filling scatter/gather lists to pass to IB functions,
 * allocated once at start to max size defined as reported by the qp.
 */
extern VAPI_sg_lst_entry_t *sg_tmp_array;
extern unsigned int sg_max_len;
/*
 * Maximum number of outstanding work requests in the NIC, same for both
 * SQ and RQ, though we only care about SQ on the QP and ack QP.  Used to
 * decide when to use a SIGNALED completion on a send to avoid WQE buildup.
 */
extern unsigned int max_outstanding_wr;
/*
 * Both eager and bounce buffers are the same size, and same number, since
 * there is a symmetry of how many can be in use at the same time.
 * This number limits the number of outstanding entries in the unexpected
 * receive queue, and expected but non-preposted queue, since the data
 * sits in the buffer where it was received until the user posts or tests
 * for it.
 */
static const int EAGER_BUF_NUM = 20;
static const unsigned long EAGER_BUF_SIZE = 8 << 10;  /* 8 kB */
extern bmi_size_t EAGER_BUF_PAYLOAD;

/*
 * Handle pointer to 64-bit integer conversions.  On 32-bit architectures
 * the extra (unsigned long) cast truncates the 64-bit int before trying to
 * stuff it into a 32-bit pointer.
 */
#define ptr_from_int64(p) (void *)(unsigned long)(p)
#define int64_from_ptr(p) (u_int64_t)(unsigned long)(p)

/*
 * Tell the compiler we really do not expect this to happen.
 */
#if defined(__GNUC_MINOR__) && (__GNUC_MINOR__ < 96)
# define __builtin_expect(x, v) (x)
#endif
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

/*
 * Debugging macros.
 */
#if 0
#define DEBUG_LEVEL 4
#define debug(lvl,fmt,args...) \
    do { \
	if (lvl <= DEBUG_LEVEL) \
	    info(fmt,##args); \
    } while (0)
#else
#  define debug(lvl,fmt,...) do { } while (0)
#endif

#if 0
#define assert(cond,fmt,args...) \
    do { \
	if (unlikely(!(cond))) \
	    error(fmt,##args); \
    } while (0)
#else
#  define assert(cond,fmt,...) do { } while (0)
#endif

#endif  /* __ib_h */

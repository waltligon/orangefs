/*
 * Portals BMI method.
 *
 * Copyright (C) 2007-8 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 */
#include <string.h>
#include <errno.h>

#if defined(__LIBCATAMOUNT__) || defined(__CRAYXT_COMPUTE_LINUX_TARGET) || defined(__CRAYXT_SERVICE)
/* Cray XT3 and XT4 version, both catamount and compute-node-linux */
#define PTL_IFACE_DEFAULT PTL_IFACE_SS
#include <portals/portals3.h>
#include <sys/utsname.h>
#else
/* TCP version */
#include <portals/portals3.h>
#include <portals/p3nal_utcp.h>  /* sets PTL_IFACE_DEFAULT to UTCP */
#include <portals/p3api/debug.h>
#include <netdb.h>  /* gethostbyname */
#include <arpa/inet.h>  /* inet_ntop */
#endif

#include <assert.h>
#include <sys/signal.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* include definitions */
#include <src/io/bmi/bmi-method-support.h>   /* bmi_method_ops */
#include <src/io/bmi/bmi-method-callback.h>  /* bmi_method_addr_reg_callback */
#include <src/common/gossip/gossip.h>
#include <src/common/gen-locks/gen-locks.h>  /* gen_mutex_t */
#include <src/common/misc/pvfs2-internal.h>  /* lld */
#include <src/common/id-generator/id-generator.h>
#include "pint-hint.h"

#ifdef HAVE_VALGRIND_H
#include <memcheck.h>
#else
#define VALGRIND_MAKE_MEM_DEFINED(addr,len)
#endif

#ifdef __GNUC__
#  define __unused __attribute__((unused))
#else
#  define __unused
#endif

/*
 * Debugging macros.
 */
#if 1
#define DEBUG_LEVEL 2
#define debug(lvl,fmt,args...) \
    do { \
	if (lvl <= DEBUG_LEVEL) \
	    gossip_debug(GOSSIP_BMI_DEBUG_PORTALS, fmt, ##args); \
    } while (0)
#else
#  define debug(lvl,fmt,...) do { } while (0)
#endif

/*
 * No global locking.  Portals has its own library-level locking, but
 * we need to keep the BMI polling thread away from the main thread that
 * is doing send/recv.
 */
static gen_mutex_t ni_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t list_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t pma_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t eq_mutex = GEN_MUTEX_INITIALIZER;

/*
 * Handle given by upper layer, which must be handed back to create
 * method_addrs.
 */
static int bmi_portals_method_id;

/*
 * Various static ptls objects.  One per instance of the code.
 */
static ptl_handle_ni_t ni = PTL_INVALID_HANDLE;
static ptl_handle_me_t mark_me = PTL_INVALID_HANDLE;
static ptl_handle_me_t zero_me = PTL_INVALID_HANDLE;
static ptl_handle_md_t zero_md;
static ptl_handle_eq_t eq = PTL_INVALID_HANDLE;
static int ni_init_dup;  /* did runtime open the nic for us? */

/*
 * Server listens at this well-known portal for unexpected messages.  Clients
 * will let portals pick a portal for them.  There tend not to be too many of
 * these:  utcp has 8, so pick a small number.
 *
 * This is also used for clients.  If they used MEAttachAny, then
 * we'd have to remember the ptl_index from a sendunexpected over to the
 * answering send call in the server.  No need for connection state just
 * for that.
 *
 * Cray has a bunch reserved.  SNLers suggest using ARMCI's.
 */
static const ptl_pt_index_t ptl_index = 37;

/*
 * Handy const.  Cray version needs padding, though, so initialize this
 * elsewhere.
 */
static ptl_process_id_t any_pid;

/*
 * Cray does not have these, but TCP does.
 */
#ifndef HAVE_PTLERRORSTR
static const char *PtlErrorStr(unsigned int ptl_errno)
{
    return ptl_err_str[ptl_errno];
}
#endif

#ifndef HAVE_PTLEVENTKINDSTR
static const char *PtlEventKindStr(ptl_event_kind_t ev_kind)
{
    extern const char *ptl_event_str[];

    return ptl_event_str[ev_kind];
}
#endif

/*
 * Match bits.  The lower 32 bits always carry the bmi_tag.  If this bit
 * in the top is set, it is an unexpected message.  The secondmost top bit is
 * used when posting a _send_, strangely enough.  If the send is too long,
 * and the receiver has not preposted, later the receiver will issue a Get
 * to us for the data.  That get will use the second set of match bits.
 *
 * The the next 22 top bits are used to encode a sequence number per
 * peer.  As BMI can post multiple sends with the same tag, we have to
 * be careful that if send #2 for a given tag goes to the zero_md, that
 * when he does the get, he grabs from buffer #2, not buffer #1 because
 * the sender was too slow in unlinking it.
 *
 * The next 8 bits are used for the class value on unexpected messages.
 */
static const uint64_t match_bits_unexpected = 1ULL << 63;  /* 8... */
static const uint64_t match_bits_long_send = 1ULL << 62;   /* 4... */
static const uint32_t match_bits_seqno_max = 1UL << 22;
static const int      match_bits_seqno_shift = 32;
static const int      match_bits_class_shift = 54;

static uint64_t mb_from_tag_and_seqno(uint32_t tag, uint32_t seqno)
{
    uint64_t mb;

    mb = seqno;
    mb <<= match_bits_seqno_shift;
    mb |= tag;
    /* caller may set the long send bit too */
    return mb;
}

/*
 * Buffer for incoming unexpected send messages.  Only the server needs
 * a list of these.  Each message can be no more than 8k.  The total memory
 * devoted to these on the server is 256k.  Not each unexpected message
 * will take the full 8k, depending on the size of the request.
 *
 * Arriving data is copied out of these as quickly as possible.  No refcnt
 * as in the nonprepost case below, where data sits around in those buffers.
 */
#define UNEXPECTED_MESSAGE_SIZE (8 << 10)
#define UNEXPECTED_QUEUE_SIZE   (256 << 10)
#define UNEXPECTED_NUM_MD 2
#define UNEXPECTED_SIZE_PER_MD  (UNEXPECTED_QUEUE_SIZE/UNEXPECTED_NUM_MD)

#define UNEXPECTED_MD_INDEX_OFFSET (1)
#define NONPREPOST_MD_INDEX_OFFSET (UNEXPECTED_NUM_MD + 1)

static char *unexpected_buf = NULL;
/* poor-man's circular buffer */
static ptl_handle_me_t unexpected_me[UNEXPECTED_NUM_MD];
static ptl_handle_md_t unexpected_md[UNEXPECTED_NUM_MD];
static int unexpected_need_repost[UNEXPECTED_NUM_MD];
static int unexpected_need_repost_sum;
static int unexpected_is_posted[UNEXPECTED_NUM_MD];

/*
 * This scheme relies on the zero page being unused, i.e. addrsesses
 * from 0 up to 4k or so.
 */
static int unexpected_md_index(void *user_ptr)
{
    int i;
    uintptr_t d = (uintptr_t) user_ptr;

    if (d >= UNEXPECTED_MD_INDEX_OFFSET &&
        d < UNEXPECTED_MD_INDEX_OFFSET + UNEXPECTED_NUM_MD)
	return d - UNEXPECTED_MD_INDEX_OFFSET;
    else
	return -1;
}

/*
 * More buffers for non-preposted receive handling.  While the unexpected
 * ones above are for explicit API needs of BMI, these are just to work
 * around the lack of a requirement to pre-post buffers in BMI.  Portals
 * would otherwise drop messages when there is no matching receive.  Instead
 * we grab them (or parts of them) with these buffers.
 *
 * Only the first 8k of each non-preposted message is saved, but we leave
 * room for up to 8M of these.  If the total message size is larger, the
 * receiver will do a Get to fetch the rest.  Note that each non-preposted
 * message causes a struct bmip_work to be malloced too.
 *
 * The nonprepost_refcnt[] variable handles what happens when the MD is
 * unlinked.  As nonprepost messages arrive, each new rq adds 1 to the
 * refcnt.  As receives are posted and consume rqs, the refcnt drops.
 * If an UNLINK event happens, is_posted goes zero.  If an rq consumption
 * drops the refcnt to zero, and it is not posted, it is reinitialized
 * and reposted.  Whew.
 */
#define NONPREPOST_MESSAGE_SIZE (8 << 10)
#define NONPREPOST_QUEUE_SIZE (8 << 20)
#define NONPREPOST_NUM_MD 2

static char *nonprepost_buf = NULL;
/* poor-man's circular buffer */
static ptl_handle_me_t nonprepost_me[NONPREPOST_NUM_MD];
static ptl_handle_md_t nonprepost_md[NONPREPOST_NUM_MD];
static int nonprepost_is_posted[NONPREPOST_NUM_MD];
static int nonprepost_refcnt[NONPREPOST_NUM_MD];

static int nonprepost_md_index(void *user_ptr)
{
    int i;
    uintptr_t d = (uintptr_t) user_ptr;

    if (d >= NONPREPOST_MD_INDEX_OFFSET &&
        d < NONPREPOST_MD_INDEX_OFFSET + NONPREPOST_NUM_MD)
	return d - NONPREPOST_MD_INDEX_OFFSET;
    else
	return -1;
}

/*
 * "private data" part of method_addr.  These describe peers, so
 * have the portal and pid of the remote side.  We need to keep a list
 * to be able to find a pma from a given pid, even though these are
 * already kept in a list up in BMI.  Some day fix that interface.
 */
struct bmip_method_addr {
    struct qlist_head list;
    char *hostname;  /* given by user, converted to a nid by us */
    char *peername;  /* for rev_lookup */
    ptl_process_id_t pid;  /* this is a struct with u32 nid + u32 pid */
    uint32_t seqno_out;  /* each send has a separate sequence number */
    uint32_t seqno_in;
};
static QLIST_HEAD(pma_list);

/*
 * Work item queue states document the lifetime of a send or receive.
 */
enum work_state {
    SQ_WAITING_ACK,
    SQ_WAITING_GET,
    SQ_WAITING_USER_TEST,
    SQ_CANCELLED,
    RQ_WAITING_INCOMING,
    RQ_WAITING_GET,
    RQ_WAITING_USER_TEST,
    RQ_WAITING_USER_POST,
    RQ_LEN_ERROR,
    RQ_CANCELLED,
};

static const char *state_name(enum work_state state)
{
    switch (state) {
    case SQ_WAITING_ACK:
	return "SQ_WAITING_ACK";
    case SQ_WAITING_GET:
	return "SQ_WAITING_GET";
    case SQ_WAITING_USER_TEST:
	return "SQ_WAITING_USER_TEST";
    case SQ_CANCELLED:
	return "SQ_CANCELLED";
    case RQ_WAITING_INCOMING:
	return "RQ_WAITING_INCOMING";
    case RQ_WAITING_GET:
	return "RQ_WAITING_GET";
    case RQ_WAITING_USER_TEST:
	return "RQ_WAITING_USER_TEST";
    case RQ_WAITING_USER_POST:
	return "RQ_WAITING_USER_POST";
    case RQ_LEN_ERROR:
	return "RQ_LEN_ERROR";
    case RQ_CANCELLED:
	return "RQ_CANCELLED";
    }
    return "(unknown state)";
}

/*
 * Common structure for both send and recv outstanding work items.  There
 * is a queue of these for each send and recv that are frequently searched.
 */
struct bmip_work {
    struct qlist_head list;
    int type;               /* BMI_SEND or BMI_RECV */
    enum work_state state;  /* send or receive state */
    struct method_op mop;   /* so BMI can use ids to find these */

    bmi_size_t tot_len;     /* size posted for send or recv */
    bmi_size_t actual_len;  /* recv: possibly shorter than posted */

    bmi_msg_tag_t bmi_tag;  /* recv: unexpected or nonpp tag that arrived */
    uint64_t match_bits;    /* recv: full match bits, including seqno */
    uint8_t bmi_class;      /* recv (unexp): class of unexpected message */

    int is_unexpected;      /* send: if user posted this as unexpected */

    ptl_handle_me_t me;     /* recv: prepost match list entry */
			    /* send: send me for possible get */
    ptl_handle_md_t md;     /* recv: prepost or get destination, to cancel */
			    /* send: send md for possible get */
    ptl_handle_me_t tme;
    ptl_handle_md_t tmd;
    int saw_send_end_and_ack; /* send: make sure both states before unlink */

    /* non-preposted receive, keep ref to a nonpp static buffer */
    const void *nonpp_buf;   /* pointer to nonpp buffer in MD */
    int nonpp_md;            /* index into nonprepost_md[] */

    /* unexpected receive, unex_buf is malloced to hold the data */
    void *unex_buf;
};

/*
 * All operations are on exactly one list.  Even queue items that cannot
 * be reached except from BMI lookup are on a list.  The lists are pretty
 * specific.
 *
 * q_send_waiting_ack - sent the message, waiting for ack
 * q_send_waiting_get - sent the message, ack says truncated, wait his get
 * q_recv_waiting_incoming - posted recv, waiting for data to arrive
 * q_recv_waiting_get - he sent before we recvd, now we sent get
 * q_recv_nonprepost - data arrived before recv was posted
 * q_unexpected_done - unexpected message arrived, waiting testunexpected
 * q_done - send or recv completed, waiting caller to test
 */
/* lists that are never shared, or even looked at, but keep code prettier so
 * we don't have to see if we need to qdel before qadd */
static QLIST_HEAD(q_send_waiting_ack);
static QLIST_HEAD(q_send_waiting_get);
static QLIST_HEAD(q_recv_waiting_incoming);
static QLIST_HEAD(q_recv_waiting_get);

/* real lists that need locking between event handler and test/post */
static QLIST_HEAD(q_recv_nonprepost);
static QLIST_HEAD(q_unexpected_done);
static QLIST_HEAD(q_done);

static struct bmi_method_addr *addr_from_nidpid(ptl_process_id_t pid);
static void unexpected_repost(int which);
static int nonprepost_init(void);
static int nonprepost_fini(void);
static int unexpected_fini(void);
static void nonprepost_repost(int which);
static const char *bmip_rev_lookup(struct bmi_method_addr *addr);


/*----------------------------------------------------------------------------
 * Test
 */

/*
 * Deal with an event, advancing the relevant state machines.
 */
static int handle_event(ptl_event_t *ev)
{
    struct bmip_work *sq, *rq;
    int which, ret;

    if (ev->ni_fail_type != 0) {
	gossip_err("%s: ni err %d\n", __func__, ev->ni_fail_type);
	return -EIO;
    }

    debug(6, "%s: event type %s\n", __func__, PtlEventKindStr(ev->type));

    switch (ev->type) {
    case PTL_EVENT_SEND_END:
	/*
	 * Sometimes this state happens _after_ the ACK.  Boggle.  Cannot
	 * unlink the sq until this state.  Doing it in the ack state may be
	 * too early.  But we don't know if it is safe to unlink until the
	 * ack comes back and says if he received it, or if he will do a
	 * Get on the MD.  So just mark a flag.  It goes to two only if
	 * the ack indicated the other side will not need to do a get.
	 *
	 * Note that an outgoing get request also triggers this.  Sigh.
	 */
	sq = ev->md.user_ptr;
	if (sq->type == BMI_RECV) {
		rq = ev->md.user_ptr;
		debug(2, "%s, rq %p stat %s get went out\n", __func__, rq,
		      state_name(rq->state));
		break;
	}
	debug(2, "%s: sq %p went out len %llu/%llu mb %llx\n", __func__, sq,
	      ev->mlength, ev->rlength, ev->match_bits);
	if (!sq->is_unexpected && ++sq->saw_send_end_and_ack == 2) {
		debug(2, "%s: saw end last, unlinking %p me %d (md %d)\n",
		      __func__, sq, sq->me, sq->md);
		ret = PtlMEUnlink(sq->me);
		if (ret)
		    gossip_err("%s: PtlMEUnlink sq %p: %s\n", __func__,
			       sq, PtlErrorStr(ret));
	}
	break;

    case PTL_EVENT_ACK:
	/* recv an ack from him, advance the state and unlink */
	sq = ev->md.user_ptr;
	debug(2, "%s: sq %p ack rcvd len %llu/%llu\n",
	      __func__, sq, ev->mlength, ev->rlength);

	/*
	 * the rlength always comes back as 0 on catamount, even if we
	 * sent 51200 bytes
	 */
	if (ev->mlength != ev->rlength) {
	    gossip_err("%s: mlen %llu and rlen %llu do not agree\n", __func__,
	    	       ev->mlength, ev->rlength);
	    exit(1);
	}

	if (ev->mlength > 0) {
	    /* make sure both SEND_END and ACK happened for these */
	    if (!sq->is_unexpected && ++sq->saw_send_end_and_ack == 2) {
		    debug(2, "%s: saw ack last, unlinking %p\n", __func__, sq);
		    ret = PtlMEUnlink(sq->me);
		    if (ret)
			gossip_err("%s: PtlMEUnlink sq %p: %s\n", __func__,
				   sq, PtlErrorStr(ret));
	    }
	    sq->state = SQ_WAITING_USER_TEST;
	    gen_mutex_lock(&list_mutex);
	    qlist_del(&sq->list);
	    qlist_add_tail(&sq->list, &q_done);
	    gen_mutex_unlock(&list_mutex);
	} else {
	    /* otherside will Get, then automatic unlink on threshold=3 */
	    sq->state = SQ_WAITING_GET;
	    gen_mutex_lock(&list_mutex);
	    qlist_del(&sq->list);
	    qlist_add_tail(&sq->list, &q_send_waiting_get);
	    gen_mutex_unlock(&list_mutex);
	}
	break;

    case PTL_EVENT_PUT_END:
	/*
	 * Peer did a send to us.  Four cases:
	 *   1.  unexpected message, user_ptr is &unexpected_md[i];
	 *   2a. non-preposted message, user_ptr is &preposted_md[i];
	 *   2b. zero md, non-preposted that was too big and truncated
	 *   3.  expected pre-posted receive, our rq in user_ptr.
	 */
	which = unexpected_md_index(ev->md.user_ptr);
	if (which >= 0) {
	    /* build new unexpected rq and copy in the data */
	    debug(2, "%s: unexpected len %lld put to us, mb %llx\n", __func__,
		  lld(ev->mlength), llu(ev->match_bits));
	    rq = malloc(sizeof(*rq));
	    if (!rq) {
		    gossip_err("%s: alloc unexpected rq\n", __func__);
		    break;
	    }
	    if (ev->mlength > UNEXPECTED_MESSAGE_SIZE)
		exit(1);

	    /*
	     * malloc this separately to hand to testunexpected caller; that
	     * is the semantics of the call, and makes managing the MDs
	     * easier.
	     */
	    rq->type = BMI_RECV;
	    rq->unex_buf = malloc(ev->mlength);
	    if (!rq->unex_buf) {
		    gossip_err("%s: alloc unexpected rq data\n", __func__);
		    free(rq);
		    break;
	    }
	    rq->actual_len = ev->mlength;
	    rq->bmi_tag = ev->match_bits & 0xffffffffULL;  /* just 32 bits */
            rq->bmi_class = (uint8_t) (ev->match_bits >>
                match_bits_class_shift);
	    rq->mop.addr = addr_from_nidpid(ev->initiator);
	    memcpy(rq->unex_buf, (char *) ev->md.start + ev->offset,
		   ev->mlength);
	    gen_mutex_lock(&list_mutex);
	    qlist_add_tail(&rq->list, &q_unexpected_done);
	    gen_mutex_unlock(&list_mutex);
	    debug(1, "%s: unexpected %d offset %llu\n", __func__, which,
	    	  llu(ev->offset));
	    if (UNEXPECTED_SIZE_PER_MD - ev->offset < UNEXPECTED_MESSAGE_SIZE) {
		debug(1, "%s: reposting unexpected %d\n", __func__, which);
		if (unexpected_need_repost[which] == 0) {
		    unexpected_need_repost[which] = 1;
		    ++unexpected_need_repost_sum;
		}
	    }
	    /* try to unpost some, if they are free now */
	    if (unexpected_need_repost_sum) {
		for (which = 0; which < UNEXPECTED_NUM_MD; which++) {
		    if (unexpected_need_repost[which])
			unexpected_repost(which);
		}
	    }
	    break;
	}

	which = nonprepost_md_index(ev->md.user_ptr);
	if (which >= 0 || ev->md_handle == zero_md) {
	    /* build new nonprepost rq, but just keep pointer to the data, or
	     * if truncated, build the req but no data to hang onto */
	    debug(1, "%s: nonprepost len %llu/%llu mb %llx%s\n",
		  __func__, llu(ev->mlength), llu(ev->rlength),
		  ev->match_bits,
		  ev->md_handle == zero_md ? ", truncated" : "");

	    if (which >= 0 && ev->md_handle == zero_md) {
		gossip_err("%s: which %d but zero md\n", __func__, which);
		exit(1);
	    }

	    rq = malloc(sizeof(*rq));
	    if (!rq) {
		    gossip_err("%s: alloc nonprepost rq\n", __func__);
		    break;
	    }

	    rq->type = BMI_RECV;
	    rq->state = RQ_WAITING_USER_POST;
	    rq->actual_len = ev->rlength;
	    rq->bmi_tag = ev->match_bits & 0xffffffffULL;  /* just 32 bits */
	    rq->match_bits = ev->match_bits;
	    rq->mop.addr = addr_from_nidpid(ev->initiator);
	    if (ev->md_handle == zero_md) {
		rq->nonpp_buf = NULL;
	    } else {
		rq->nonpp_buf = (char *) ev->md.start + ev->offset;
		rq->nonpp_md = which;
		/* keep a ref to this md until the recv finishes */
		++nonprepost_refcnt[rq->nonpp_md];
	    }
	    debug(2, "%s: rq %p NEW NONPREPOST mb 0x%llx%s\n", __func__,
	          rq, llu(rq->match_bits),
		  ev->md_handle == zero_md ? ", truncated" : "");
	    gen_mutex_lock(&list_mutex);
	    qlist_add_tail(&rq->list, &q_recv_nonprepost);
	    gen_mutex_unlock(&list_mutex);
	    break;
	}

	/* must be something we preposted, with user_ptr is rq */
	rq = ev->md.user_ptr;
#ifdef DEBUG_CNL_ODDITIES
	if ((uintptr_t) rq & 1) {
	    debug(1, "%s: OFF BY 1 rq %p\n", __func__, rq);
	    rq = (void *) ((uintptr_t) rq - 1);
	}
#endif
	rq->actual_len = ev->rlength;  /* attempted length sent */
	rq->state = RQ_WAITING_USER_TEST;
	if (rq->actual_len > rq->tot_len)
	    rq->state = RQ_LEN_ERROR;
	debug(1, "%s: rq %p len %lld tag 0x%llx mb 0x%llx thresh %d put to us\n",
	      __func__, rq, lld(rq->actual_len), llu(rq->bmi_tag),
	      llu(rq->match_bits), ev->md.threshold);
	gen_mutex_lock(&list_mutex);
	qlist_del(&rq->list);
	qlist_add_tail(&rq->list, &q_done);
	gen_mutex_unlock(&list_mutex);

#ifdef DEBUG_CNL_ODDITIES
	/*
	 * At least on linux compute nodes, the me does not auto-unlink
	 * properly, even though the md did get unlinked.  It is necessary
	 * to undo the ME too.  Note that the MD threshold is not updated
	 * to zero; it still sits at one (or whatever it was originally
	 * set up to be).
	 */
	/* ret = PtlMDUnlink(rq->md); debug(2, "md unlink %d gives %s\n", rq->md, PtlErrorStr(ret)); */
	/* ret = PtlMDUnlink(rq->tmd); debug(2, "tmd unlink %d gives %s\n", rq->tmd, PtlErrorStr(ret)); */
	ret = PtlMEUnlink(rq->me); debug(2, "me unlink %d gives %s\n", rq->me, PtlErrorStr(ret));
	ret = PtlMEUnlink(rq->tme); debug(2, "tme unlink %d gives %s\n", rq->tme, PtlErrorStr(ret));
#endif
	break;

    case PTL_EVENT_GET_END:
	/* our send, turned into a get from the receiver, is now done, as
	 * far as we are conerned, as he has gotten it from us */
	sq = ev->md.user_ptr;
	debug(1, "%s: peer got sq %p len %llu/%llu mb %llx\n", __func__, sq,
	      llu(ev->mlength), llu(ev->rlength), ev->match_bits);
	sq->state = SQ_WAITING_USER_TEST;
	gen_mutex_lock(&list_mutex);
	qlist_del(&sq->list);
	qlist_add_tail(&sq->list, &q_done);
	gen_mutex_unlock(&list_mutex);
	break;

    case PTL_EVENT_REPLY_END:
	rq = ev->md.user_ptr;
	debug(2, "%s: get completed, rq %p\n", __func__, rq);
	rq->state = RQ_WAITING_USER_TEST;
	gen_mutex_lock(&list_mutex);
	qlist_del(&rq->list);
	qlist_add_tail(&rq->list, &q_done);
	gen_mutex_unlock(&list_mutex);
	break;

    case PTL_EVENT_UNLINK:
	/* XXX: does this ever get called on CNL?  Apparently not. */
	debug(2, "%s: unlink event! user_ptr %p\n", __func__, ev->md.user_ptr);
	which = nonprepost_md_index(ev->md.user_ptr);
	if (which >= 0) {
	    debug(1, "%s: unlinked nonprepost md %d, is_posted %d refcnt %d\n",
	    	  __func__, which, nonprepost_is_posted[which],
		  nonprepost_refcnt[which]);
	    nonprepost_is_posted[which] = 0;
	    if (nonprepost_refcnt[which] == 0)
		/* already satisfied all the recvs, can this happen so fast? */
		nonprepost_repost(which);
	    break;
	}

	debug(1, "%s: unlinked a send or recv, nothing to do\n", __func__);

	/*
	 * Expected recv, unlink just cleans it up.  Already got the send
	 * event.
	 */
	break;

    case PTL_EVENT_SEND_START:
    	debug(0, "%s: send start, a debugging message thresh %d\n", __func__,
	      ev->md.threshold);
	break;
    case PTL_EVENT_PUT_START:
    	debug(0, "%s: put start, a debugging message, thresh %d\n", __func__,
	      ev->md.threshold);
	break;

    default:
	gossip_err("%s: unknown event %s\n", __func__,
		   PtlEventKindStr(ev->type));
	return -EIO;
    }

    return 0;
}

/*
 * Try to drain everything off the EQ.  No idling can be done in here
 * because we have to hold the eq lock for the duration, but if a recv
 * post comes in, it wants to do the post now, not wait another 10 ms.
 *
 * If idle_ms == PTL_TIME_FOREVER, block until an event happens.  This is only
 * used for PtlMDUpdate where we cannot progress until the event is delivered.
 * Portals will increase the eq pending count when the first part of a message
 * comes in, and not generate an event until the end.  This could be lots of
 * packets.  Sorry this could introduce very long delays.
 *
 * Under the hood, utcp cannot implement the timeout anyway.
 */
static int __check_eq(int idle_ms)
{
    ptl_event_t ev;
    int ret, i, ms = idle_ms;

    for (;;) {
	ret = PtlEQPoll(&eq, 1, ms, &ev, &i);
	if (ret == PTL_OK || ret == PTL_EQ_DROPPED) {
	    VALGRIND_MAKE_MEM_DEFINED(&ev, sizeof(ev));
	    handle_event(&ev);
	    ms = 0;  /* just quickly pull events off */
	    if (ret == PTL_EQ_DROPPED) {
		/* oh well, hope things retry, just point this out */
		gossip_err("%s: PtlEQPoll: dropped some completions\n",
			   __func__);
	    }
	} else if (ret == PTL_EQ_EMPTY) {
	    ret = 0;
	    break;
	} else {
	    gossip_err("%s: PtlEQPoll: %s\n", __func__, PtlErrorStr(ret));
	    ret = -EIO;
	    break;
	}
    }

    return ret;
}

/*
 * While doing a post_recv(), we have to make sure no events get processed
 * on the EQ.  But other pvfs threads might call testunexpected() etc.  There
 * is a mutex for the eq that wraps check_eq().  A separate lock/release eq
 * is used to block out other senders or eq checkers during a send.
 */
static int check_eq(int idle_ms __unused)
{
    int ret;

    gen_mutex_lock(&eq_mutex);
    ret = __check_eq(0);  /* never idle */
    gen_mutex_unlock(&eq_mutex);
    return ret;
}

/*
 * Used by testcontext and test.  Called with the list lock held.
 */
static void fill_done(struct bmip_work *w, bmi_op_id_t *id, bmi_size_t *size,
		      void **user_ptr, bmi_error_code_t *err)
{
    *id = w->mop.op_id;
    *size = (w->type == BMI_SEND) ? w->tot_len : w->actual_len;
    if (user_ptr)
	*user_ptr = w->mop.user_ptr;
    *err = 0;
    if (w->state == SQ_CANCELLED || w->state == RQ_CANCELLED)
	*err = -PVFS_ETIMEDOUT;
    if (w->state == RQ_LEN_ERROR)
	*err = -PVFS_EOVERFLOW;

    debug(2, "%s: %s %p size %llu peer %s\n", __func__,
	  w->type == BMI_SEND ? "sq" : "rq", w, llu(*size),
	  bmip_rev_lookup(w->mop.addr));

    /* free resources too */
    id_gen_fast_unregister(w->mop.op_id);
    qlist_del(&w->list);
    free(w);
}

static int bmip_testcontext(int incount, bmi_op_id_t *outids, int *outcount,
			    bmi_error_code_t *errs, bmi_size_t *sizes,
			    void **user_ptrs, int max_idle_time,
			    bmi_context_id context_id __unused)
{
    struct bmip_work *w, *wn;
    int ret, n = 0;
    int timeout = 0;

    for (;;) {
	/*
	 * Poll once quickly to grab some completions.  Then if nothing
	 * has finished, come back and wait for the timeout.
	 */
	ret = check_eq(timeout);
	if (ret)
	    goto out;

	gen_mutex_lock(&list_mutex);
	qlist_for_each_entry_safe(w, wn, &q_done, list) {
	    if (n == incount)
		break;
	    fill_done(w, &outids[n], &sizes[n],
		      user_ptrs ? &user_ptrs[n] : NULL, &errs[n]);
	    ++n;
	}
	gen_mutex_unlock(&list_mutex);

	if (n > 0 || timeout == max_idle_time)
	    break;

	timeout = max_idle_time;
    }

out:
    *outcount = n;
    return ret;
}

/*
 * Used by lots of BMI test codes, but never in core PVFS.  Easy enough
 * to implement though.
 */
static int bmip_test(bmi_op_id_t id, int *outcount, bmi_error_code_t *err,
		     bmi_size_t *size, void **user_ptr, int max_idle_time,
		     bmi_context_id context_id __unused)
{
    struct bmip_work *w, *wn;
    int ret, n = 0;
    int timeout = 0;

    for (;;) {
	ret = check_eq(timeout);
	if (ret)
	    goto out;

	gen_mutex_lock(&list_mutex);
	qlist_for_each_entry_safe(w, wn, &q_done, list) {
	    if (w->mop.op_id == id) {
		fill_done(w, &id, size, user_ptr, err);
		++n;
		break;
	    }
	}
	gen_mutex_unlock(&list_mutex);

	if (n > 0 || timeout == max_idle_time)
	    break;

	timeout = max_idle_time;
    }

out:
    *outcount = n;
    return ret;
}

/*
 * Only used by one BMI test program, not worth the code space to implement.
 */
static int bmip_testsome(int num __unused, bmi_op_id_t *ids __unused,
			 int *outcount __unused, int *other_thing __unused,
			 bmi_error_code_t *err __unused,
			 bmi_size_t *size __unused, void **user_ptr __unused,
			 int max_idle_time __unused,
			 bmi_context_id context_id __unused)
{
    gossip_err("%s: unimplemented\n", __func__);
    return bmi_errno_to_pvfs(-ENOSYS);
}

/*
 * Check the EQ, briefly, then return any unexpecteds, then wait for up
 * to the idle time.
 */
static int bmip_testunexpected(int incount, int *outcount,
			       struct bmi_method_unexpected_info *ui,
                               uint8_t class,
			       int max_idle_time)
{
    struct bmip_work *w, *wn;
    int ret, n = 0;
    int timeout = 0;

    for (;;) {
	ret = check_eq(0);
	if (ret)
	    goto out;

	qlist_for_each_entry_safe(w, wn, &q_unexpected_done, list) {
	    if (n == incount)
		break;
            if(w->bmi_class != class)
                continue;
	    ui[n].error_code = 0;
	    ui[n].addr = w->mop.addr;
	    ui[n].size = w->actual_len;
	    ui[n].buffer = w->unex_buf;  /* preallocated above */
	    ui[n].tag = w->bmi_tag;
	    qlist_del(&w->list);
	    free(w);
	    ++n;
	}

	if (n > 0 || timeout == max_idle_time)
	    break;

	timeout = max_idle_time;
    }

out:
    *outcount = n;
    return ret;
}


/*----------------------------------------------------------------------------
 * Send
 */

/*
 * Clients do not open the NIC until they go to connect to a server.
 * In theory, which server could dictate which NIC.
 *
 * Server also calls this to initialize, but with a non-ANY pid.
 */
static int ensure_ni_initialized(struct bmip_method_addr *peer __unused,
				 ptl_process_id_t my_pid)
{
    int ret = 0;
    ptl_process_id_t no_pid;
    int nic_type;
    ptl_md_t zero_mdesc = {
	.threshold = PTL_MD_THRESH_INF,
	.max_size = 0,
	.options = PTL_MD_OP_PUT | PTL_MD_TRUNCATE | PTL_MD_MAX_SIZE |
		   PTL_MD_EVENT_START_DISABLE,
	.user_ptr = 0,
    };

    /* already initialized */
    if (ni != PTL_INVALID_HANDLE)
	return ret;

    gen_mutex_lock(&ni_mutex);

    /* check again now that we have the mutex */
    if (ni != PTL_INVALID_HANDLE)
	goto out;

    /*
     * XXX: should do this properly.  If server, we could look
     * up our listen address and get an interface from that.  If client,
     * lookup server, figure out how route would go to it, choose
     * that interface.  Yeah.
     */

#if defined(__CRAYXT_SERVICE) || defined(__CRAYXT_COMPUTE_LINUX_TARGET)
    /*
     * Magic for Cray XT service nodes and compute node linux.
     * Catamount uses default, TCP uses default.
     */
    nic_type = CRAY_USER_NAL;
#else
    nic_type = PTL_IFACE_DEFAULT;
#endif

    /* needed for TCP */
    /* setenv("PTL_IFACE", "eth0", 0); */

    ret = PtlNIInit(nic_type, my_pid.pid, NULL, NULL, &ni);
#if defined(__LIBCATAMOUNT__) || defined(__CRAYXT_COMPUTE_LINUX_TARGET)
    if (ret == PTL_IFACE_DUP && ni != PTL_INVALID_HANDLE) {
	ret = 0;  /* already set up by pre-main on catamount nodes */
	ni_init_dup = 1;
    }
#endif
    if (ret) {
	/* error number is bogus here, do not try to decode it */
	gossip_err("%s: PtlNIInit failed: %s\n", __func__, PtlErrorStr(ret));
	ni = PTL_INVALID_HANDLE;  /* init call nulls it out */
	ret = -EIO;
	goto out;
    }

    /*
     * May not be able to assign PID to whatever we want.  Let's see
     * what runtime has assigned.
     */
    {
    ptl_process_id_t id;
    ret = PtlGetId(ni, &id);
    if (ret != 0) {
	gossip_err("%s: PtlGetId failed: %d\n", __func__, ret);
	ni = PTL_INVALID_HANDLE;
	ret = -EIO;
	goto out;
    }
    debug(0, "%s: runtime thinks my id is %d.%d\n", __func__, id.nid, id.pid);
    }

#if !(defined(__LIBCATAMOUNT__) || defined(__CRAYXT_SERVICE) || defined(__CRAYXT_COMPUTE_LINUX_TARGET))
    /*
     * Need an access control entry to allow everybody to talk, else root
     * cannot talk to random user, e.g.  Not implemented on Cray.
     */
#ifdef HAVE_PTLACENTRY_JID
    ret = PtlACEntry(ni, 0, any_pid, (ptl_uid_t) -1, (ptl_jid_t) -1, ptl_index);
#else
    ret = PtlACEntry(ni, 0, any_pid, (ptl_uid_t) -1, ptl_index);
#endif
    if (ret) {
	gossip_err("%s: PtlACEntry: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }
#endif

    /* a single EQ for all messages, with some arbitrary depth */
    ret = PtlEQAlloc(ni, 100, NULL, &eq);
    if (ret) {
	gossip_err("%s: PtlEQAlloc: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }

    /* "mark" match entry that denotes the bottom of the prepost entries */
    no_pid.nid = 0;
    no_pid.pid = 0;
    ret = PtlMEAttach(ni, ptl_index, no_pid, 0, 0, PTL_RETAIN, PTL_INS_BEFORE,
		      &mark_me);
    if (ret) {
	gossip_err("%s: PtlMEAttach mark: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }

    /* "zero" grabs just the header (of nonprepost, not unexpected), drops the
     * contents */
    ret = PtlMEAttach(ni, ptl_index, any_pid, 0,
		      (0x3fffffffULL << 32) | 0xffffffffULL, PTL_RETAIN,
		      PTL_INS_AFTER, &zero_me);
    if (ret) {
	gossip_err("%s: PtlMEAttach zero: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }

    zero_mdesc.eq_handle = eq;
    ret = PtlMDAttach(zero_me, zero_mdesc, PTL_RETAIN, &zero_md);
    if (ret) {
	gossip_err("%s: PtlMDAttach zero: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }

    /* now it is time to build this queue, once per NI */
    nonprepost_init();

out:
    if (ret) {
	if (mark_me != PTL_INVALID_HANDLE)
	    PtlMEUnlink(zero_me);
	if (mark_me != PTL_INVALID_HANDLE)
	    PtlMEUnlink(mark_me);
	if (eq != PTL_INVALID_HANDLE)
	    PtlEQFree(eq);
	if (ni != PTL_INVALID_HANDLE)
	    PtlNIFini(ni);
    }
    gen_mutex_unlock(&ni_mutex);
    return ret;
}

/*
 * Fill in bits for BMI, used by caller for later test or cancel.
 */
static void fill_mop(struct bmip_work *w, bmi_op_id_t *id,
		     struct bmi_method_addr *addr, void *user_ptr,
		     bmi_context_id context_id)
{
    id_gen_fast_register(&w->mop.op_id, &w->mop);
    w->mop.addr = addr;
    w->mop.method_data = w;
    w->mop.user_ptr = user_ptr;
    w->mop.context_id = context_id;
    *id = w->mop.op_id;
}

/*
 * Initialize an mdesc for a get or put, either sq or rq side.
 */
static void build_mdesc(struct bmip_work *w, ptl_md_t *mdesc, int numbufs,
			void *const *buffers, const bmi_size_t *sizes)
{
    mdesc->threshold = 1;
    mdesc->options = 0;  /* PTL_MD_EVENT_START_DISABLE; */
    mdesc->eq_handle = eq;
    mdesc->user_ptr = w;

    if (numbufs > 1) {
	int i;
	ptl_md_iovec_t *iov = (void *) &w[1];
	for (i=0; i<numbufs; i++) {
	    iov[i].iov_base = buffers[i];
	    iov[i].iov_len = sizes[i];
	}
	mdesc->options |= PTL_MD_IOVEC;
	mdesc->start = (void *) &w[1];
	mdesc->length = numbufs;
    } else {
	mdesc->start = *buffers;
	mdesc->length = *sizes;
    }
}

/*
 * Generic interface for both send and sendunexpected, list and non-list send.
 */
static int
post_send(bmi_op_id_t *id, struct bmi_method_addr *addr,
	  int numbufs, const void *const *buffers, const bmi_size_t *sizes,
	  bmi_size_t total_size, bmi_msg_tag_t bmi_tag, void *user_ptr,
	  bmi_context_id context_id, int is_unexpected, uint8_t class)
{
    struct bmip_method_addr *pma = addr->method_data;
    struct bmip_work *sq;
    uint64_t mb;
    int ret;
    ptl_md_t mdesc;

    /* unexpected messages must fit inside the agreed limit */
    if (is_unexpected && total_size > UNEXPECTED_MESSAGE_SIZE)
	return -EINVAL;

    ret = ensure_ni_initialized(pma, any_pid);
    if (ret)
	goto out;

    sq = malloc(sizeof(*sq) + numbufs * sizeof(ptl_md_iovec_t));
    if (!sq) {
	ret = -ENOMEM;
	goto out;
    }
    sq->type = BMI_SEND;
    sq->saw_send_end_and_ack = 0;
    sq->tot_len = total_size;
    sq->is_unexpected = is_unexpected;
    fill_mop(sq, id, addr, user_ptr, context_id);
    /* lose cast to fit in non-const portals iovec */
    build_mdesc(sq, &mdesc, numbufs, (void *const *)(uintptr_t) buffers, sizes);
    mdesc.threshold = 2;  /* put, ack */

    sq->state = SQ_WAITING_ACK;
    gen_mutex_lock(&list_mutex);
    qlist_add_tail(&sq->list, &q_send_waiting_ack);
    gen_mutex_unlock(&list_mutex);

    /* if not unexpected, use an ME in case he has to come get it */
    if (sq->is_unexpected) {

	debug(2, "%s: sq %p len %lld peer %s tag %d unexpected\n", __func__, sq,
	      lld(total_size), pma->peername, bmi_tag);
	/* md without any match entry, for sending */
        mb = 0;
        mb += class;
        mb << match_bits_class_shift;
	mb |= match_bits_unexpected; 
        mb |= bmi_tag;
	ret = PtlMDBind(ni, mdesc, PTL_UNLINK, &sq->md);
	if (ret) {
	    gossip_err("%s: PtlMDBind: %s\n", __func__, PtlErrorStr(ret));
	    return -EIO;
	}
	debug(2, "%s: bound md %d\n", __func__, sq->md);
    } else {
	/* seqno increments on every expected send (only) */
	if (++pma->seqno_out >= match_bits_seqno_max)
	    pma->seqno_out = 0;
	mb = mb_from_tag_and_seqno(bmi_tag, pma->seqno_out);

	debug(2, "%s: sq %p len %lld peer %s tag %d seqno %u mb 0x%llx\n",
	      __func__, sq, lld(total_size), pma->peername, bmi_tag,
	      pma->seqno_out, llu(mb));

	/* long-send bit only on the ME, not as the outgoing mb in PtlPut */
	ret = PtlMEInsert(mark_me, pma->pid, match_bits_long_send | mb,
			  0, PTL_UNLINK, PTL_INS_BEFORE, &sq->me);
	if (ret) {
	    gossip_err("%s: PtlMEInsert: %s\n", __func__, PtlErrorStr(ret));
	    return -EIO;
	}

	/*
	 * Truncate is used to send just what we have, which may be less
	 * than he wants to receive.  Otherwise would have to chop down the
	 * iovec list to fit on the Get-ter, or use GetRegion.
	 */
	mdesc.options |= PTL_MD_OP_GET | PTL_MD_TRUNCATE;
	mdesc.threshold = 3;  /* put, ack, maybe get */
	ret = PtlMDAttach(sq->me, mdesc, PTL_UNLINK, &sq->md);
	if (ret) {
	    gossip_err("%s: PtlMDBind: %s\n", __func__, PtlErrorStr(ret));
	    return -EIO;
	}
    }

    sq->bmi_tag = bmi_tag;  /* both for debugging dumps */
    sq->match_bits = mb;

    ret = PtlPut(sq->md, PTL_ACK_REQ, pma->pid, ptl_index, 0, mb, 0, 0);
    if (ret) {
	gossip_err("%s: PtlPut: %s\n", __func__, PtlErrorStr(ret));
	return -EIO;
    }

out:
    return ret;
}

static int bmip_post_send_list(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
			       const void *const *buffers,
			       const bmi_size_t *sizes, int list_count,
			       bmi_size_t total_size,
			       enum bmi_buffer_type buffer_flag __unused,
			       bmi_msg_tag_t tag, void *user_ptr,
			       bmi_context_id context_id,
                               PVFS_hint hints __unused)
{
    return post_send(id, remote_map, list_count, buffers, sizes,
		     total_size, tag, user_ptr, context_id, 0, 0);
}

static int bmip_post_sendunexpected_list(bmi_op_id_t *id,
					 struct bmi_method_addr *remote_map,
					 const void *const *buffers,
					 const bmi_size_t *sizes,
					 int list_count, bmi_size_t total_size,
					 enum bmi_buffer_type bflag __unused,
					 bmi_msg_tag_t tag, 
                                         uint8_t class,
                                         void *user_ptr,
					 bmi_context_id context_id, 
                                         PVFS_hint hints __unused)
{
    return post_send(id, remote_map, list_count, buffers, sizes,
		     total_size, tag, user_ptr, context_id, 1, class);
}


/*----------------------------------------------------------------------------
 * Receive
 */

/*
 * Assumes that buf/len will fit in numbufs/buffers/sizes.  For use
 * in copying out of non-preposted buffer area to user-supplied iovec.
 */
static void memcpy_buflist(int numbufs __unused, void *const *buffers,
			   const bmi_size_t *sizes, const void *vbuf,
			   bmi_size_t len)
{
    bmi_size_t thislen;
    const char *buf = vbuf;
    int i = 0;

    while (len) {
	thislen = len;
	if (thislen > sizes[i])
	    thislen = sizes[i];
	memcpy(buffers[i], buf, thislen);
	buf += thislen;
	len -= thislen;
	++i;
    }
}

/*
 * Part of post_recv.  Search the queue of non-preposted receives that the
 * other side has sent to us.  If one matches, satisfy this receive now,
 * else return and let the receive be preposted.
 */
static int match_nonprepost_recv(bmi_op_id_t *id, struct bmi_method_addr *addr,
				 int numbufs, void *const *buffers,
				 const bmi_size_t *sizes,
				 bmi_size_t total_size, bmi_msg_tag_t tag,
				 void *user_ptr, bmi_context_id context_id)
{
    struct bmip_method_addr *pma = addr->method_data;
    int found = 0;
    int ret = 0;
    ptl_md_t mdesc;
    struct bmip_work *rq;
    uint64_t mb;

    /* expected match bits */
    mb = mb_from_tag_and_seqno(tag, pma->seqno_in);

    /* XXX: remove bmi_tag comparison if match_bits works */
    gen_mutex_lock(&list_mutex);
    qlist_for_each_entry(rq, &q_recv_nonprepost, list) {
	debug(2, "%s: compare rq %p addr %p =? %p tag %u =? %u mb 0x%llx =? 0x%llx\n", __func__,
	      rq, rq->mop.addr, addr, rq->bmi_tag, tag, llu(rq->match_bits),
	      llu(mb));
	if (rq->mop.addr == addr && rq->bmi_tag == tag && rq->match_bits == mb) {
	    found = 1;
	    qlist_del(&rq->list);
	    break;
	}
    }
    gen_mutex_unlock(&list_mutex);
    if (!found)
	goto out;   /* not found, proceed with prepost */

    /* rq matched, use the addr found at event time */
    fill_mop(rq, id, rq->mop.addr, user_ptr, context_id);

    /* verify length fits */
    if (rq->actual_len > total_size) {
	gossip_err("%s: send len %llu longer than posted %lld\n", __func__,
		   llu(rq->actual_len), lld(total_size));
	rq->state = RQ_LEN_ERROR;
	goto foundout;
    }

    /* short message, copy and release MD buf */
    if (rq->nonpp_buf != NULL) {
	memcpy_buflist(numbufs, buffers, sizes, rq->nonpp_buf,
		       rq->actual_len);
	--nonprepost_refcnt[rq->nonpp_md];
	if (nonprepost_refcnt[rq->nonpp_md] == 0 &&
	   !nonprepost_is_posted[rq->nonpp_md]) {
	    nonprepost_repost(rq->nonpp_md);
	}
	rq->state = RQ_WAITING_USER_TEST;
	debug(2, "%s: found short message rq %p, copied\n", __func__, rq);
	goto foundout;
    }

    /* initiate a get for the truncated message */
    if (numbufs > 1) {
	/* need room for the iovec somewhere, might as well be here */
	struct bmip_work *rq2 = malloc(sizeof(*rq) +
				       numbufs * sizeof(ptl_md_iovec_t));
	if (!rq2) {
	    ret = -ENOMEM;
	    goto out;
	}
	memcpy(rq2, rq, sizeof(*rq));
	free(rq);
	rq = rq2;
    }

    rq->tot_len = total_size;
    build_mdesc(rq, &mdesc, numbufs, buffers, sizes);
    mdesc.threshold = 2;  /* XXX: on Cray only, this must be 2, not 1 */

    ret = PtlMDBind(ni, mdesc, PTL_UNLINK, &rq->md);
    if (ret) {
	gossip_err("%s: PtlMDBind: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	free(rq);
	goto out;
    }

    mb |= match_bits_long_send;
    debug(2, "%s: rq %p doing get mb 0x%llx\n", __func__, rq, llu(mb));
    ret = PtlGet(rq->md, pma->pid, ptl_index, 0, mb, 0);
    if (ret) {
	gossip_err("%s: PtlGet: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	free(rq);
	goto out;
    }

    rq->state = RQ_WAITING_GET;
    gen_mutex_lock(&list_mutex);
    qlist_add_tail(&rq->list, &q_recv_waiting_get);
    gen_mutex_unlock(&list_mutex);
    ret = 1;
    goto out;

foundout:
    gen_mutex_lock(&list_mutex);
    qlist_add_tail(&rq->list, &q_done);
    gen_mutex_unlock(&list_mutex);
    ret = 1;  /* we handled it */

out:
    return ret;
}

static int post_recv(bmi_op_id_t *id, struct bmi_method_addr *addr,
		     int numbufs, void *const *buffers, const bmi_size_t *sizes,
		     bmi_size_t total_size, bmi_msg_tag_t tag,
		     void *user_ptr, bmi_context_id context_id)
{
    struct bmip_method_addr *pma = addr->method_data;
    struct bmip_work *rq = NULL;
    ptl_md_t mdesc;
    int ret, ms = 0;
    uint64_t mb = 0;

    ret = ensure_ni_initialized(pma, any_pid);
    if (ret)
	goto out;

    /* increment the expected seqno of the message he will send us */
    if (++pma->seqno_in >= match_bits_seqno_max)
	pma->seqno_in = 0;

    debug(2, "%s: len %lld peer %s tag %d seqno %u\n", __func__,
          lld(total_size), pma->peername, tag, pma->seqno_in);

    rq = NULL;
    gen_mutex_lock(&eq_mutex);  /* do not let test threads manipulate eq */
restart:
    /* drain the EQ */
    debug(2, "%s: check eq\n", __func__);
    __check_eq(ms);

    /* first check the nonpreposted receive queue */
    debug(2, "%s: match nonprepost?\n", __func__);
    ret = match_nonprepost_recv(id, addr, numbufs, buffers, sizes,
				total_size, tag, user_ptr, context_id);

    if (ret != 0) {
	if (ret > 0)  /* handled it via the nonprepost queue */
	    ret = 0;
	goto out;  /* or error */
    }

    /* multiple trips through this loop caused by many MD_NO_UPDATEs, just
     * save the built-up rq and me/md from the first time through */
    if (!rq) {
	rq = malloc(sizeof(*rq) + numbufs * sizeof(ptl_md_iovec_t));
	if (!rq) {
	    ret = -ENOMEM;
	    goto out;
	}
	rq->type = BMI_RECV;
	rq->tot_len = total_size;
	rq->actual_len = 0;
	rq->bmi_tag = tag;
	fill_mop(rq, id, addr, user_ptr, context_id);
	memset(&mdesc, 0, sizeof(mdesc));
	build_mdesc(rq, &mdesc, numbufs, buffers, sizes);
	mdesc.threshold = 0;  /* initially inactive */
	mdesc.options |= PTL_MD_OP_PUT;

	/* put at the end of the preposted list, just before the first
	 * nonprepost or unex ME. */
	rq->me = PTL_INVALID_HANDLE;
	debug(2, "%s: me insert\n", __func__);
	mb = mb_from_tag_and_seqno(tag, pma->seqno_in);
	rq->match_bits = mb;
	ret = PtlMEInsert(mark_me, pma->pid, mb, 0, PTL_UNLINK,
			  PTL_INS_BEFORE, &rq->me);
	if (ret) {
	    gossip_err("%s: PtlMEInsert: %s\n", __func__, PtlErrorStr(ret));
	    ret = -EIO;
	    goto out;
	}

	debug(2, "%s: md attach\n", __func__);
	ret = PtlMDAttach(rq->me, mdesc, PTL_UNLINK, &rq->md);
	if (ret) {
	    gossip_err("%s: PtlMDAttach: %s\n", __func__, PtlErrorStr(ret));
	    ret = -EIO;
	    goto out;
	}
	debug(2, "%s: me %d, md %d\n", __func__, rq->me, rq->md);
    }

    /* now update it atomically with respect to the event stream from the NIC */
    mdesc.threshold = 1;
    debug(2, "%s: md update threshold to 1\n", __func__);
    ret = PtlMDUpdate(rq->md, NULL, &mdesc, eq);
    if (ret) {
	if (ret == PTL_MD_NO_UPDATE) {
	    /* cannot block, other thread may have processed the event for us */
	    debug(2, "%s: md update: no update\n", __func__);
	    ms = PTL_TIME_FOREVER;
	    goto restart;
	}
	gossip_err("%s: PtlMDUpdate: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }

#ifdef DEBUG_CNL_ODDITIES
    {
    debug(2, "insert another\n");
    ret = PtlMEInsert(mark_me, pma->pid, 0, -1ULL, PTL_UNLINK,
		      PTL_INS_BEFORE, &rq->tme);
    if (ret) {
	gossip_err("%s: PtlMEInsert: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }

    debug(2, "%s: md attach\n", __func__);
    mdesc.user_ptr = (void *) ((uintptr_t) mdesc.user_ptr + 1);
    ret = PtlMDAttach(rq->tme, mdesc, PTL_UNLINK, &rq->tmd);
    if (ret) {
	gossip_err("%s: PtlMDAttach: %s\n", __func__, PtlErrorStr(ret));
	ret = -EIO;
	goto out;
    }
    debug(2, "%s: me %d, md %d\n", __func__, rq->tme, rq->tmd);
    }
#endif


    debug(2, "%s: rq %p waiting incoming, len %lld peer %s tag %d seqno %u mb 0x%llx\n",
          __func__, rq, lld(total_size), pma->peername, tag, pma->seqno_in,
	  llu(mb));
    rq->state = RQ_WAITING_INCOMING;
    gen_mutex_lock(&list_mutex);
    qlist_add_tail(&rq->list, &q_recv_waiting_incoming);
    gen_mutex_unlock(&list_mutex);
    rq = NULL;  /* happy rq, keep it */

out:
    gen_mutex_unlock(&eq_mutex);
    if (rq) {
	/*
	 * Alloced, then found MD_NO_UPDATE, and it had completed on the
	 * unexpected.  Free this temp rq.  (Or error case too.)
	 */
	if (rq->me != PTL_INVALID_HANDLE)
	    PtlMEUnlink(rq->me);
	free(rq);
    }
    return ret;
}

static int bmip_post_recv_list(bmi_op_id_t *id, struct bmi_method_addr *remote_map,
			       void *const *buffers, const bmi_size_t *sizes,
			       int list_count, bmi_size_t tot_expected_len,
			       bmi_size_t *tot_actual_len __unused,
			       enum bmi_buffer_type buffer_flag __unused,
			       bmi_msg_tag_t tag, void *user_ptr,
			       bmi_context_id context_id, PVFS_hint hints
                               __unused)
{
    return post_recv(id, remote_map, list_count, buffers, sizes,
		     tot_expected_len, tag, user_ptr, context_id);
}

/* debugging */
#define show_queue(q) do { \
    fprintf(stderr, #q "\n"); \
    qlist_for_each_entry(w, &q, list) { \
	fprintf(stderr, "%s %p state %s len %llu tag 0x%llx mb 0x%0llx\n", \
		w->type == BMI_SEND ? "sq" : "rq", \
		w, state_name(w->state), \
		w->type == BMI_SEND ? llu(w->tot_len) : llu(w->actual_len), \
		llu(w->bmi_tag), llu(w->match_bits)); \
    } \
} while (0)

static void dump_queues(int sig __unused)
{
    struct bmip_work *w;

    /* debugging */
    show_queue(q_send_waiting_ack);
    show_queue(q_send_waiting_get);
    show_queue(q_recv_waiting_incoming);
    show_queue(q_recv_waiting_get);
    show_queue(q_recv_nonprepost);
    show_queue(q_unexpected_done);
    show_queue(q_done);
}

/*
 * Cancel.  Grab the eq lock to keep things from finishing as we are
 * freeing them.  Hopefully this won't lead to core dumps in future
 * test operations.
 */
static int bmip_cancel(bmi_op_id_t id, bmi_context_id context_id __unused)
{
    int ret;
    struct method_op *mop;
    struct bmip_work *w;

    gen_mutex_lock(&eq_mutex);
    __check_eq(0);
    mop = id_gen_fast_lookup(id);
    w = mop->method_data;
    fprintf(stderr, "%s: cancel %p state %s len %llu tag 0x%llx mb 0x%llx\n",
    	    __func__, w, state_name(w->state), llu(w->tot_len),
	    llu(w->bmi_tag), llu(w->match_bits));
    switch (w->state) {

    case SQ_WAITING_ACK:
	w->state = SQ_CANCELLED;
	goto link_done;

    case SQ_WAITING_GET:
	w->state = SQ_CANCELLED;
	ret = PtlMEUnlink(w->me);
	if (ret)
	    gossip_err("%s: PtlMEUnlink: %s\n", __func__, PtlErrorStr(ret));
	goto link_done;

    case RQ_WAITING_INCOMING:
	w->state = RQ_CANCELLED;
	ret = PtlMEUnlink(w->me);
	if (ret)
	    gossip_err("%s: PtlMEUnlink: %s\n", __func__, PtlErrorStr(ret));
	goto link_done;

    case RQ_WAITING_GET:
	w->state = RQ_CANCELLED;
	ret = PtlMDUnlink(w->md);
	if (ret)
	    /* complain, but might be okay if we raced with the completion */
	    gossip_err("%s: PtlMDUnlink: %s\n", __func__, PtlErrorStr(ret));
	goto link_done;

    case SQ_WAITING_USER_TEST:
    case RQ_WAITING_USER_TEST:
    case RQ_WAITING_USER_POST:
    case RQ_LEN_ERROR:
    case SQ_CANCELLED:
    case RQ_CANCELLED:
	/* nothing to do */
	break;
    }
    goto out;

link_done:
    gen_mutex_lock(&list_mutex);
    qlist_del(&w->list);
    qlist_add_tail(&w->list, &q_done);
    gen_mutex_unlock(&list_mutex);

out:
    gen_mutex_unlock(&eq_mutex);

    /* debugging */
    dump_queues(0);

    exit(1);
    return 0;
}

static const char *bmip_rev_lookup(struct bmi_method_addr *addr)
{
    struct bmip_method_addr *pma = addr->method_data;

    return pma->peername;
}

/*
 * Build and fill a Portals-specific method_addr structure.  This routine
 * copies the hostname if it needs it.
 */
static struct bmi_method_addr *bmip_alloc_method_addr(const char *hostname,
						  ptl_process_id_t pid,
						  int register_with_bmi)
{
    struct bmi_method_addr *map;
    struct bmip_method_addr *pma;
    bmi_size_t extra;
    int ret;

    /*
     * First search to see if this one already exists.
     */
    gen_mutex_lock(&pma_mutex);
    qlist_for_each_entry(pma, &pma_list, list) {
	if (pma->pid.pid == pid.pid && pma->pid.nid == pid.nid) {
	    /* relies on alloc_method_addr() working like it does */
	    map = &((struct bmi_method_addr *) pma)[-1];
	    debug(2, "%s: found matching peer %s\n", __func__, pma->peername);
	    goto out;
	}
    }

    /* room for a peername tacked on the end too, no more than 10 digits */
    extra = sizeof(*pma) + 2 * (strlen(hostname) + 1) + 10 + 1;

    map = bmi_alloc_method_addr(bmi_portals_method_id, extra);
    pma = map->method_data;

    pma->hostname = (void *) &pma[1];
    pma->peername = pma->hostname + strlen(hostname) + 1;
    strcpy(pma->hostname, hostname);
    /* for debug/error messages via BMI_addr_rev_lookup */
    sprintf(pma->peername, "%s:%d", hostname, pid.pid);

    pma->pid = pid;
    pma->seqno_in = 0;
    pma->seqno_out = 0;
    qlist_add(&pma->list, &pma_list);

    if (register_with_bmi) {
	ret = bmi_method_addr_reg_callback(map);
	if (!ret) {
	    gossip_err("%s: bmi_method_addr_reg_callback failed\n", __func__);
	    free(map);
	    map = NULL;
	}
    }
    debug(2, "%s: new peer %s\n", __func__, pma->peername);

out:
    gen_mutex_unlock(&pma_mutex);
    return map;
}


#if !(defined(__LIBCATAMOUNT__) || defined(__CRAYXT_COMPUTE_LINUX_TARGET) || defined(__CRAYXT_SERVICE))
/*
 * Clients give hostnames.  Convert these to Portals nids.  This routine
 * specific for Portals-over-IP (tcp or utcp).
 */
static int bmip_nid_from_hostname(const char *hostname, uint32_t *nid)
{
    struct hostent *he;

    he = gethostbyname(hostname);
    if (!he) {
	gossip_err("%s: gethostbyname cannot resolve %s\n", __func__, hostname);
	return 1;
    }
    if (he->h_length != 4) {
	gossip_err("%s: gethostbyname returns %d-byte addresses, hoped for 4\n",
		   __func__, he->h_length);
	return 1;
    }
    /* portals wants host byte order, apparently */
    *nid = htonl(*(uint32_t *) he->h_addr_list[0]);
    return 0;
}

/*
 * This is called from the server, on seeing an unexpected message from
 * a client.  Convert that to a method_addr.  If BMI has never seen it
 * before, register it with BMI.
 *
 * Ugh, since Portals is connectionless, we need to search through
 * all the struct bmip_method_addr to find the pid that sent
 * this to us.  While BMI has a list of all method_addrs somewhere,
 * it is not exported to us.  So we have to maintain our own list
 * structure.
 */
static struct bmi_method_addr *addr_from_nidpid(ptl_process_id_t pid)
{
    struct bmi_method_addr *map;
    struct in_addr inaddr;
    char *hostname;

    /* temporary, for ip 100.200.200.100 */
    hostname = malloc(INET_ADDRSTRLEN + 1);
    inaddr.s_addr = htonl(pid.nid);  /* ntop expects network format */
    inet_ntop(AF_INET, &inaddr, hostname, INET_ADDRSTRLEN);

    map = bmip_alloc_method_addr(hostname, pid, 1);
    free(hostname);

    return map;
}

#else

/*
 * Cray versions
 */
static int bmip_nid_from_hostname(const char *hostname, uint32_t *nid)
{
    int ret = -1;
    uint32_t v = 0;
    char *cp;

    /*
     * There is apparently no API for this on Cray.  Chop up the hostname,
     * knowing the format.  Also can look at /proc/cray_xt/nid on linux
     * nodes.
     */
    if (strncmp(hostname, "nid", 3) == 0) {
	v = strtoul(hostname + 3, &cp, 10);
	if (*cp != '\0') {
	    gossip_err("%s: convert nid<num> hostname %s failed\n", __func__,
		       hostname);
	    v = 0;
	} else {
	    ret = 0;
	}
    }
    if (ret)
	debug(0, "%s: no clue how to convert hostname %s\n", __func__,
	      hostname);
    *nid = v;
    return ret;
}

static struct bmi_method_addr *addr_from_nidpid(ptl_process_id_t pid)
{
    struct bmi_method_addr *map;
    char hostname[9];

    sprintf(hostname, "nid%05d", pid.nid);
    map = bmip_alloc_method_addr(hostname, pid, 1);
    return map;
}
#endif

/*
 * Break up a method string like:
 *   portals://hostname:pid/filesystem
 * into its constituent fields, storing them in an opaque
 * type, which is then returned.
 */
static struct bmi_method_addr *bmip_method_addr_lookup(const char *id)
{
    char *s, *cp, *cq;
    int ret;
    ptl_process_id_t pid;
    struct bmi_method_addr *map = NULL;

    /* parse hostname */
    s = string_key("portals", id);  /* allocs a string "node27:3334/pvfs2-fs" */
    if (!s)
	return NULL;
    cp = strchr(s, ':');
    if (!cp) {
	gossip_err("%s: no ':' found\n", __func__);
	goto out;
    }

    /* terminate hostname from s to cp */
    *cp = 0;
    ++cp;

    /* strip /filesystem part */
    cq = strchr(cp, '/');
    if (cq)
	*cq = 0;

    /* parse pid part */
    pid.pid = strtoul(cp, &cq, 10);
    if (cq == cp) {
	gossip_err("%s: invalid pid number\n", __func__);
	goto out;
    }
    if (*cq != '\0') {
	gossip_err("%s: extra characters after pid number\n", __func__);
	goto out;
    }

    ret = bmip_nid_from_hostname(s, &pid.nid);
    if (ret)
	goto out;

    /*
     * Lookup old one or Alloc new one, but don't call
     * bmi_method_addr_reg_callback---this is the client side, BMI is asking us
     * to look this up and will register it itself.
     */
    map = bmip_alloc_method_addr(s, pid, 0);

out:
    free(s);
    return map;
}

/*
 * "Listen" on a particular portal, specified in the address in pvfs2tab.
 */
static int unexpected_init(struct bmi_method_addr *listen_addr)
{
    struct bmip_method_addr *pma = listen_addr->method_data;
    int i, ret;

    unexpected_buf = malloc(UNEXPECTED_QUEUE_SIZE);
    if (!unexpected_buf) {
	ret = -ENOMEM;
	goto out;
    }

    ret = ensure_ni_initialized(pma, pma->pid);
    if (ret)
	goto out;

    /*
     * We use two, half-size, MDs.  When one fills up, it is unlinked and we
     * find out about it via an event.  The second one is used to give us time
     * to repost the first.  Sort of a circular buffer structure.  This is
     * hopefully better than wasting a full 8k for every small control message.
     */
    unexpected_need_repost_sum = 0;
    for (i=0; i<UNEXPECTED_NUM_MD; i++) {
	unexpected_is_posted[i] = 0;
	unexpected_need_repost[i] = 0;
	unexpected_repost(i);
    }

out:
    return ret;
}

static void unexpected_repost(int which)
{
    int ret;
    ptl_md_t mdesc;

    /* unlink used-up one */
    if (unexpected_is_posted[which]) {
	debug(1, "%s: trying unpost %d\n", __func__, which);
	ret = PtlMEUnlink(unexpected_me[which]);
	if (ret) {
	    gossip_err("%s: PtlMEUnlink %d: %s\n", __func__, which,
		       PtlErrorStr(ret));
	    return;
	}
	debug(1, "%s: unposted %d\n", __func__, which);
	unexpected_need_repost[which] = 0;
	unexpected_is_posted[which] = 0;
	--unexpected_need_repost_sum;
    }

    /* unexpected messages are limited by the API to a certain size */
    mdesc.start = unexpected_buf + which * (UNEXPECTED_QUEUE_SIZE / 2);
    mdesc.length = UNEXPECTED_QUEUE_SIZE / 2;
    mdesc.threshold = PTL_MD_THRESH_INF;
    mdesc.options = PTL_MD_OP_PUT | PTL_MD_EVENT_START_DISABLE
		  | PTL_MD_MAX_SIZE;
    mdesc.max_size = UNEXPECTED_MESSAGE_SIZE;
    mdesc.eq_handle = eq;
    mdesc.user_ptr = (void *) (uintptr_t) (UNEXPECTED_MD_INDEX_OFFSET + which);

    /*
     * Take any tag, as long as it has the unexpected bit set, and not
     * the long send bit.  Not sure if we need both bits for this.  This always
     * goes at the very end of the list, just in front of zero.  The nonpp
     * and unex ones can be comingled, as they select different things, but
     * they must come after the preposts and before the zero md.
     */
    ret = PtlMEInsert(zero_me, any_pid, match_bits_unexpected,
		      (0x3fffffffULL << 32) | 0xffffffffULL, PTL_UNLINK,
		      PTL_INS_BEFORE, &unexpected_me[which]);
    if (ret) {
	gossip_err("%s: PtlMEInsert: %s\n", __func__, PtlErrorStr(ret));
	return;
    }

    /*
     * Put data here when it matches.  Do not auto-unlink else a new md
     * may get stuck in and cause a false match in unexpected_md_index above.
     * Do it all manually.  Also have to make sure these do not get reused
     * in case things sitting in the queue haven't been looked at yet.  Maybe
     * need to use md.user_ptr, or look at the match bits.
     */
    ret = PtlMDAttach(unexpected_me[which], mdesc, PTL_RETAIN,
		      &unexpected_md[which]);
    if (ret)
	gossip_err("%s: PtlMDAttach: %s\n", __func__, PtlErrorStr(ret));

    unexpected_is_posted[which] = 1;
    debug(1, "%s: reposted %d\n", __func__, which);
}

static int unexpected_fini(void)
{
    int i, ret;

    for (i=0; i<UNEXPECTED_NUM_MD; i++) {
	/* MDs go away when MEs unlinked */
	ret = PtlMEUnlink(unexpected_me[i]);
	if (ret) {
	    gossip_err("%s: PtlMEUnlink %d: %s\n", __func__, i,
		       PtlErrorStr(ret));
	    return ret;
	}
    }
    free(unexpected_buf);
    return 0;
}

/*
 * Manage nonprepost buffers.
 */
static int nonprepost_init(void)
{
    int i, ret;

    nonprepost_buf = malloc(NONPREPOST_QUEUE_SIZE);
    if (!nonprepost_buf) {
	ret = -ENOMEM;
	goto out;
    }

    /*
     * See comments above for unexpected.
     */
    for (i=0; i<NONPREPOST_NUM_MD; i++) {
	nonprepost_is_posted[i] = 0;
	nonprepost_refcnt[i] = 0;
	nonprepost_repost(i);
    }

out:
    return ret;
}

static void nonprepost_repost(int which)
{
    int ret;
    ptl_md_t mdesc;
    static int count = 0;

    debug(0, "%s: WHICH %d\n", __func__, which);
    ++count;
    if (count > 2)
	exit(0);

    /* unlink used-up one */
    if (unexpected_is_posted[which]) {
	debug(1, "%s: trying unpost %d\n", __func__, which);
	ret = PtlMEUnlink(unexpected_me[which]);
	if (ret) {
	    gossip_err("%s: PtlMEUnlink %d: %s\n", __func__, which,
		       PtlErrorStr(ret));
	    return;
	}
	debug(1, "%s: unposted %d\n", __func__, which);
	unexpected_need_repost[which] = 0;
	unexpected_is_posted[which] = 0;
	--unexpected_need_repost_sum;
    }

    /* only short messages that fit max_size go in here */
    mdesc.start = nonprepost_buf + which * (NONPREPOST_QUEUE_SIZE / 2);
    mdesc.length = NONPREPOST_QUEUE_SIZE / 2;
    mdesc.threshold = PTL_MD_THRESH_INF;
    mdesc.options = PTL_MD_OP_PUT | PTL_MD_EVENT_START_DISABLE
		  | PTL_MD_MAX_SIZE;
    mdesc.max_size = NONPREPOST_MESSAGE_SIZE;
    mdesc.eq_handle = eq;
    mdesc.user_ptr = (void *) (uintptr_t) (NONPREPOST_MD_INDEX_OFFSET + which);

    /* XXX: maybe need manual unlink like for unexpecteds on CNL */

    /* also at the very end of the list */
    /* match anything as long as top two bits are zero */
    ret = PtlMEInsert(zero_me, any_pid, 0,
		      (0x3fffffffULL << 32) | 0xffffffffULL,
		      PTL_UNLINK, PTL_INS_BEFORE, &nonprepost_me[which]);
    if (ret) {
	gossip_err("%s: PtlMEInsert: %s\n", __func__, PtlErrorStr(ret));
	return;
    }

    /* put data here when it matches; when full, unlink it */
    ret = PtlMDAttach(nonprepost_me[which], mdesc, PTL_UNLINK,
		      &nonprepost_md[which]);
    if (ret) {
	gossip_err("%s: PtlMDAttach: %s\n", __func__, PtlErrorStr(ret));
	return;
    }

    nonprepost_is_posted[which] = 1;
}

static int nonprepost_fini(void)
{
    int i, ret;

    for (i=0; i<NONPREPOST_NUM_MD; i++) {
	if (nonprepost_refcnt[i] != 0)
	    gossip_err("%s: refcnt %d, should be zero\n", __func__,
		       nonprepost_refcnt[i]);
	if (!nonprepost_is_posted[i])
	    continue;
	/* MDs go away when MEs unlinked */
	ret = PtlMEUnlink(nonprepost_me[i]);
	if (ret) {
	    gossip_err("%s: PtlMEUnlink %d: %s\n", __func__, i,
		       PtlErrorStr(ret));
	}
    }
    free(nonprepost_buf);
    return 0;
}


static void *bmip_memalloc(bmi_size_t len, enum bmi_op_type send_recv __unused)
{
    return malloc(len);
}

static int bmip_memfree(void *buf, bmi_size_t len __unused,
			enum bmi_op_type send_recv __unused)
{
    free(buf);
    return 0;
}

static int bmip_unexpected_free(void *buf)
{
    free(buf);
    return 0;
}

/*
 * No need to track these internally.  Just search the entire queue.
 */
static int bmip_open_context(bmi_context_id context_id __unused)
{
    return 0;
}

static void bmip_close_context(bmi_context_id context_id __unused)
{
}

/*
 * Callers sometimes want to know odd pieces of information.  Satisfy
 * them.
 */
static int bmip_get_info(int option, void *param)
{
    int ret = 0;

    switch (option) {
	case BMI_CHECK_MAXSIZE:
	    /* reality is 2^31, but shrink to avoid negative int */
	    *(int *)param = (1UL << 31) - 1;
	    break;
	case BMI_GET_UNEXP_SIZE:
	    *(int *)param = UNEXPECTED_MESSAGE_SIZE;
	    break;
	default:
	    ret = -ENOSYS;
    }
    return ret;
}

/*
 * Used to set some optional parameters and random functions, like ioctl.
 */
static int bmip_set_info(int option, void *param)
{
    switch (option) {
    case BMI_DROP_ADDR: {
	struct bmi_method_addr *addr = param;
	struct bmip_method_addr *pma = addr->method_data;
	gen_mutex_lock(&pma_mutex);
	qlist_del(&pma->list);
	gen_mutex_unlock(&pma_mutex);
	free(addr);
	break;
    }
    case BMI_OPTIMISTIC_BUFFER_REG:
	break;
    default:
	/* Should return -ENOSYS, but return 0 for caller ease. */
	break;
    }
    return 0;
}

/*
 * This is called with a method_addr initialized by
 * bmip_method_addr_lookup.
 */
static int bmip_initialize(struct bmi_method_addr *listen_addr,
			   int method_id, int init_flags)
{
    int ret = -ENODEV, numint;

    debug(0, "%s: init\n", __func__);

    gen_mutex_lock(&ni_mutex);

    any_pid.nid = PTL_NID_ANY;
    any_pid.pid = PTL_PID_ANY;

    /* check params */
    if (!!listen_addr ^ (init_flags & BMI_INIT_SERVER)) {
	gossip_err("%s: server but no listen address\n", __func__);
	ret = -EIO;
	goto out;
    }

    bmi_portals_method_id = method_id;

    ret = PtlInit(&numint);
    if (ret) {
	gossip_err("%s: PtlInit failed\n", __func__);
	ret = -EIO;
	goto out;
    }

/*
 * utcp has shorter names for debug symbols; define catamount to these
 * even though it never prints anything.
 */
#ifndef PTL_DBG_ALL
#define  PTL_DBG_ALL PTL_DEBUG_ALL
#define  PTL_DBG_NI_ALL PTL_DEBUG_NI_ALL
#endif

    PtlNIDebug(PTL_INVALID_HANDLE, PTL_DBG_ALL | PTL_DBG_NI_ALL);
    /* PtlNIDebug(PTL_INVALID_HANDLE, PTL_DBG_ALL | 0x001f0000); */
    /* PtlNIDebug(PTL_INVALID_HANDLE, PTL_DBG_ALL | 0x00000000); */
    /* PtlNIDebug(PTL_INVALID_HANDLE, PTL_DBG_DROP | 0x00000000); */

    /* catamount has different debug symbols, but never prints anything */
    PtlNIDebug(PTL_INVALID_HANDLE, PTL_DEBUG_ALL | PTL_DEBUG_NI_ALL);
    /* PtlNIDebug(PTL_INVALID_HANDLE, PTL_DEBUG_DROP | 0x00000000); */

#if defined(__CRAYXT_SERVICE)
    /*
     * debug
     */
    signal(SIGUSR1, dump_queues);
#endif

    /*
     * Allocate and build MDs for a queue of unexpected messages from
     * all hosts.  Drop lock for coming NI init call.
     */
    if (init_flags & BMI_INIT_SERVER) {
	gen_mutex_unlock(&ni_mutex);
	ret = unexpected_init(listen_addr);
	if (ret)
	    PtlFini();
	return ret;
    }

out:
    gen_mutex_unlock(&ni_mutex);
    return ret;
}

/*
 * Shutdown.
 */
static int bmip_finalize(void)
{
    int ret;

    /* do not delete pmas, bmi will call DROP on each for us */

    /*
     * Assuming upper layer has called cancel/test on all the work
     * queue items, else we should probably walk the lists and purge those.
     */

    gen_mutex_lock(&ni_mutex);

    if (ni == PTL_INVALID_HANDLE)
	goto out;

    nonprepost_fini();
    if (unexpected_buf)
	unexpected_fini();

#if 0  /* example code:  stick this somewhere to test if the EQ is freeable */
    /* unexpected_fini(); */
    nonprepost_fini();
    ret = PtlMEUnlink(zero_me);
    if (ret)
	gossip_err("%s: PtlMEUnlink zero: %s\n", __func__, PtlErrorStr(ret));
    ret = PtlEQFree(eq);
    if (ret)
	gossip_err("%s: PtlEQFree: %s\n", __func__, PtlErrorStr(ret));
    printf("eqfree okay\n");
    exit(1);
#endif

    /* destroy connection structures */
    ret = PtlMEUnlink(mark_me);
    if (ret)
	gossip_err("%s: PtlMEUnlink mark: %s\n", __func__, PtlErrorStr(ret));

    ret = PtlMEUnlink(zero_me);
    if (ret)
	gossip_err("%s: PtlMEUnlink zero: %s\n", __func__, PtlErrorStr(ret));

    ret = PtlEQFree(eq);
    if (ret)
	gossip_err("%s: PtlEQFree: %s\n", __func__, PtlErrorStr(ret));

    ret = PtlNIFini(ni);
    if (ret)
	gossip_err("%s: PtlNIFini: %s\n", __func__, PtlErrorStr(ret));

    /* do not call this if runtime opened the nic, else oops in sysio */
    if (ni_init_dup == 0)
	PtlFini();

out:
    gen_mutex_unlock(&ni_mutex);
    return 0;
}

/*
 * All addresses are in the same netmask.
 */
static int bmip_query_addr_range(struct bmi_method_addr *mop __unused,
				 const char *wildcard __unused,
				 int netmask __unused)
{
    return 1;
}

const struct bmi_method_ops bmi_portals_ops =
{
    .method_name = "bmi_portals",
    .flags = 0,
    .initialize = bmip_initialize,
    .finalize = bmip_finalize,
    .set_info = bmip_set_info,
    .get_info = bmip_get_info,
    .memalloc = bmip_memalloc,
    .memfree = bmip_memfree,
    .unexpected_free = bmip_unexpected_free,
    .test = bmip_test,
    .testsome = bmip_testsome,
    .testcontext = bmip_testcontext,
    .testunexpected = bmip_testunexpected,
    .method_addr_lookup = bmip_method_addr_lookup,
    .post_send_list = bmip_post_send_list,
    .post_recv_list = bmip_post_recv_list,
    .post_sendunexpected_list = bmip_post_sendunexpected_list,
    .open_context = bmip_open_context,
    .close_context = bmip_close_context,
    .cancel = bmip_cancel,
    .rev_lookup_unexpected = bmip_rev_lookup,
    .query_addr_range = bmip_query_addr_range,
};


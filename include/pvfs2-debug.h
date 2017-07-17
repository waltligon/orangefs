/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file just defines debugging masks to be used with the gossip
 * logging utility.  All debugging masks for PVFS2 are kept here to make
 * sure we don't have collisions.
 */

#ifndef __PVFS2_DEBUG_H
#define __PVFS2_DEBUG_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#include <stdarg.h>
#include "pvfs2-types.h"

/* typedef PVFS_debug_mask is defined in pvfs2-types.h
 */

/* These macros let you deal with the 2-part nature of the mask */
#define __PVFS_DBGMSK_RT(l,r) (r)
#define __PVFS_DBGMSK_LT(l,r) (l)
#define PVFS_DBGMSK_RT(a) __PVFS_DBGMSK_RT(a)
#define PVFS_DBGMSK_LT(a) __PVFS_DBGMSK_LT(a)

#define PVFS_DEBUG_MASK(name) \
          extern const PVFS_debug_mask name
#define PVFS_DEBUG_MASK_DECL(name) \
          const PVFS_debug_mask name = { name ## _INIT }

#define GOSSIP_NO_DEBUG_INIT                  (uint64_t)0 , (uint64_t)0
PVFS_DEBUG_MASK(GOSSIP_NO_DEBUG);
#define GOSSIP_BMI_DEBUG_TCP_INIT             (uint64_t)0 , ((uint64_t)1 << 0)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_TCP);
#define GOSSIP_BMI_DEBUG_CONTROL_INIT         (uint64_t)0 , ((uint64_t)1 << 1)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_CONTROL);
#define GOSSIP_BMI_DEBUG_OFFSETS_INIT         (uint64_t)0 , ((uint64_t)1 << 2)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_OFFSETS);
#define GOSSIP_BMI_DEBUG_GM_INIT              (uint64_t)0 , ((uint64_t)1 << 3)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_GM);
#define GOSSIP_JOB_DEBUG_INIT                 (uint64_t)0 , ((uint64_t)1 << 4)
PVFS_DEBUG_MASK(GOSSIP_JOB_DEBUG);
#define GOSSIP_SERVER_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 5)
PVFS_DEBUG_MASK(GOSSIP_SERVER_DEBUG);
#define GOSSIP_STO_DEBUG_CTRL_INIT            (uint64_t)0 , ((uint64_t)1 << 6)
PVFS_DEBUG_MASK(GOSSIP_STO_DEBUG_CTRL);
#define GOSSIP_STO_DEBUG_DEFAULT_INIT         (uint64_t)0 , ((uint64_t)1 << 7)
PVFS_DEBUG_MASK(GOSSIP_STO_DEBUG_DEFAULT);
#define GOSSIP_FLOW_DEBUG_INIT                (uint64_t)0 , ((uint64_t)1 << 8)
PVFS_DEBUG_MASK(GOSSIP_FLOW_DEBUG);
#define GOSSIP_BMI_DEBUG_GM_MEM_INIT          (uint64_t)0 , ((uint64_t)1 << 9)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_GM_MEM);
#define GOSSIP_REQUEST_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 10)
PVFS_DEBUG_MASK(GOSSIP_REQUEST_DEBUG);
#define GOSSIP_FLOW_PROTO_DEBUG_INIT          (uint64_t)0 , ((uint64_t)1 << 11)
PVFS_DEBUG_MASK(GOSSIP_FLOW_PROTO_DEBUG);
#define GOSSIP_NCACHE_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 12)
PVFS_DEBUG_MASK(GOSSIP_NCACHE_DEBUG);
#define GOSSIP_CLIENT_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 13)
PVFS_DEBUG_MASK(GOSSIP_CLIENT_DEBUG);
#define GOSSIP_REQ_SCHED_DEBUG_INIT           (uint64_t)0 , ((uint64_t)1 << 14)
PVFS_DEBUG_MASK(GOSSIP_REQ_SCHED_DEBUG);
#define GOSSIP_ACACHE_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 15)
PVFS_DEBUG_MASK(GOSSIP_ACACHE_DEBUG);
#define GOSSIP_TROVE_DEBUG_INIT               (uint64_t)0 , ((uint64_t)1 << 16)
PVFS_DEBUG_MASK(GOSSIP_TROVE_DEBUG);
#define GOSSIP_TROVE_OP_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 17)
PVFS_DEBUG_MASK(GOSSIP_TROVE_OP_DEBUG);
#define GOSSIP_DIST_DEBUG_INIT                (uint64_t)0 , ((uint64_t)1 << 18)
PVFS_DEBUG_MASK(GOSSIP_DIST_DEBUG);
#define GOSSIP_BMI_DEBUG_IB_INIT              (uint64_t)0 , ((uint64_t)1 << 19)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_IB);
#define GOSSIP_DBPF_ATTRCACHE_DEBUG_INIT      (uint64_t)0 , ((uint64_t)1 << 20)
PVFS_DEBUG_MASK(GOSSIP_DBPF_ATTRCACHE_DEBUG);
#define GOSSIP_RACACHE_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 21)
PVFS_DEBUG_MASK(GOSSIP_RACACHE_DEBUG);
#define GOSSIP_LOOKUP_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 22)
PVFS_DEBUG_MASK(GOSSIP_LOOKUP_DEBUG);
#define GOSSIP_REMOVE_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 23)
PVFS_DEBUG_MASK(GOSSIP_REMOVE_DEBUG);
#define GOSSIP_GETATTR_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 24)
PVFS_DEBUG_MASK(GOSSIP_GETATTR_DEBUG);
#define GOSSIP_READDIR_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 25)
PVFS_DEBUG_MASK(GOSSIP_READDIR_DEBUG);
#define GOSSIP_IO_DEBUG_INIT                  (uint64_t)0 , ((uint64_t)1 << 26)
PVFS_DEBUG_MASK(GOSSIP_IO_DEBUG);
#define GOSSIP_DBPF_OPEN_CACHE_DEBUG_INIT     (uint64_t)0 , ((uint64_t)1 << 27)
PVFS_DEBUG_MASK(GOSSIP_DBPF_OPEN_CACHE_DEBUG);
#define GOSSIP_PERMISSIONS_DEBUG_INIT         (uint64_t)0 , ((uint64_t)1 << 28)
PVFS_DEBUG_MASK(GOSSIP_PERMISSIONS_DEBUG);
#define GOSSIP_CANCEL_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 29)
PVFS_DEBUG_MASK(GOSSIP_CANCEL_DEBUG);
#define GOSSIP_MSGPAIR_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 30)
PVFS_DEBUG_MASK(GOSSIP_MSGPAIR_DEBUG);
#define GOSSIP_CLIENTCORE_DEBUG_INIT          (uint64_t)0 , ((uint64_t)1 << 31)
PVFS_DEBUG_MASK(GOSSIP_CLIENTCORE_DEBUG);
#define GOSSIP_CLIENTCORE_TIMING_DEBUG_INIT   (uint64_t)0 , ((uint64_t)1 << 32)
PVFS_DEBUG_MASK(GOSSIP_CLIENTCORE_TIMING_DEBUG);
#define GOSSIP_SETATTR_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 33)
PVFS_DEBUG_MASK(GOSSIP_SETATTR_DEBUG);
#define GOSSIP_MKDIR_DEBUG_INIT               (uint64_t)0 , ((uint64_t)1 << 34)
PVFS_DEBUG_MASK(GOSSIP_MKDIR_DEBUG);
#define GOSSIP_VARSTRIP_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 35)
PVFS_DEBUG_MASK(GOSSIP_VARSTRIP_DEBUG);
#define GOSSIP_GETEATTR_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 36)
PVFS_DEBUG_MASK(GOSSIP_GETEATTR_DEBUG);
#define GOSSIP_SETEATTR_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 37)
PVFS_DEBUG_MASK(GOSSIP_SETEATTR_DEBUG);
#define GOSSIP_ENDECODE_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 38)
PVFS_DEBUG_MASK(GOSSIP_ENDECODE_DEBUG);
#define GOSSIP_DELEATTR_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 39)
PVFS_DEBUG_MASK(GOSSIP_DELEATTR_DEBUG);
#define GOSSIP_ACCESS_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 40)
PVFS_DEBUG_MASK(GOSSIP_ACCESS_DEBUG);
#define GOSSIP_ACCESS_DETAIL_DEBUG_INIT       (uint64_t)0 , ((uint64_t)1 << 41)
PVFS_DEBUG_MASK(GOSSIP_ACCESS_DETAIL_DEBUG);
#define GOSSIP_LISTEATTR_DEBUG_INIT           (uint64_t)0 , ((uint64_t)1 << 42)
PVFS_DEBUG_MASK(GOSSIP_LISTEATTR_DEBUG);
#define GOSSIP_PERFCOUNTER_DEBUG_INIT         (uint64_t)0 , ((uint64_t)1 << 43)
PVFS_DEBUG_MASK(GOSSIP_PERFCOUNTER_DEBUG);
#define GOSSIP_STATE_MACHINE_DEBUG_INIT       (uint64_t)0 , ((uint64_t)1 << 44)
PVFS_DEBUG_MASK(GOSSIP_STATE_MACHINE_DEBUG);
#define GOSSIP_DBPF_KEYVAL_DEBUG_INIT         (uint64_t)0 , ((uint64_t)1 << 45)
PVFS_DEBUG_MASK(GOSSIP_DBPF_KEYVAL_DEBUG);
#define GOSSIP_LISTATTR_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 46)
PVFS_DEBUG_MASK(GOSSIP_LISTATTR_DEBUG);
#define GOSSIP_DBPF_COALESCE_DEBUG_INIT       (uint64_t)0 , ((uint64_t)1 << 47)
PVFS_DEBUG_MASK(GOSSIP_DBPF_COALESCE_DEBUG);
#define GOSSIP_ACCESS_HOSTNAMES_INIT          (uint64_t)0 , ((uint64_t)1 << 48)
PVFS_DEBUG_MASK(GOSSIP_ACCESS_HOSTNAMES);
#define GOSSIP_FSCK_DEBUG_INIT                (uint64_t)0 , ((uint64_t)1 << 49)
PVFS_DEBUG_MASK(GOSSIP_FSCK_DEBUG);
#define GOSSIP_BMI_DEBUG_MX_INIT              (uint64_t)0 , ((uint64_t)1 << 50)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_MX);
#define GOSSIP_BSTREAM_DEBUG_INIT             (uint64_t)0 , ((uint64_t)1 << 51)
PVFS_DEBUG_MASK(GOSSIP_BSTREAM_DEBUG);
#define GOSSIP_BMI_DEBUG_PORTALS_INIT         (uint64_t)0 , ((uint64_t)1 << 52)
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_PORTALS);
#define GOSSIP_USER_DEV_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 53)
PVFS_DEBUG_MASK(GOSSIP_USER_DEV_DEBUG);
#define GOSSIP_DIRECTIO_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 54)
PVFS_DEBUG_MASK(GOSSIP_DIRECTIO_DEBUG);
#define GOSSIP_MGMT_DEBUG_INIT                (uint64_t)0 , ((uint64_t)1 << 55)
PVFS_DEBUG_MASK(GOSSIP_MGMT_DEBUG);
#define GOSSIP_MIRROR_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 56)
PVFS_DEBUG_MASK(GOSSIP_MIRROR_DEBUG);
#define GOSSIP_WIN_CLIENT_DEBUG_INIT          (uint64_t)0 , ((uint64_t)1 << 57)
PVFS_DEBUG_MASK(GOSSIP_WIN_CLIENT_DEBUG);
#define GOSSIP_SECURITY_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 58)
PVFS_DEBUG_MASK(GOSSIP_SECURITY_DEBUG);
#define GOSSIP_USRINT_DEBUG_INIT              (uint64_t)0 , ((uint64_t)1 << 59)
PVFS_DEBUG_MASK(GOSSIP_USRINT_DEBUG);
#define GOSSIP_SECCACHE_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 61)
PVFS_DEBUG_MASK(GOSSIP_SECCACHE_DEBUG);
#define GOSSIP_SIDCACHE_DEBUG_INIT            (uint64_t)0 , ((uint64_t)1 << 62)
PVFS_DEBUG_MASK(GOSSIP_SIDCACHE_DEBUG);
#define GOSSIP_UNEXP_DEBUG_INIT               (uint64_t)0 , ((uint64_t)1 << 63)
PVFS_DEBUG_MASK(GOSSIP_UNEXP_DEBUG);
#define GOSSIP_UNUSED_DEBUG_INIT              ((uint64_t)1 << 0) , (uint64_t)0
PVFS_DEBUG_MASK(GOSSIP_UNUSED_DEBUG);

#define GOSSIP_BMI_DEBUG_ALL_INIT (uint64_t)0,    \
                (PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_TCP_INIT)     | \
                 PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_CONTROL_INIT) | \
                 PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_GM_INIT)      | \
                 PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_OFFSETS_INIT) | \
                 PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_IB_INIT)      | \
                 PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_MX_INIT)      | \
                 PVFS_DBGMSK_RT(GOSSIP_BMI_DEBUG_PORTALS_INIT))
PVFS_DEBUG_MASK(GOSSIP_BMI_DEBUG_ALL);

#define GOSSIP_GETATTR_SECURITY_DEBUG_INIT (uint64_t)0,        \
                (PVFS_DBGMSK_RT(GOSSIP_GETATTR_DEBUG_INIT)   | \
                 PVFS_DBGMSK_RT(GOSSIP_SECURITY_DEBUG_INIT)) 
PVFS_DEBUG_MASK(GOSSIP_GETATTR_SECURITY_DEBUG);

#define GOSSIP_SETATTR_SECURITY_DEBUG_INIT (uint64_t)0,        \
                (PVFS_DBGMSK_RT(GOSSIP_SETATTR_DEBUG_INIT)   | \
                 PVFS_DBGMSK_RT(GOSSIP_SECURITY_DEBUG_INIT)) 
PVFS_DEBUG_MASK(GOSSIP_SETATTR_SECURITY_DEBUG);

#define __DEBUG_ALL_INIT ((uint64_t) -1) , ((uint64_t) -1)
PVFS_DEBUG_MASK(__DEBUG_ALL);

/* These are kmod mask items, disjoint from server and client */

#define GOSSIP_SUPER_DEBUG            ((uint64_t)1 << 0)
#define GOSSIP_INODE_DEBUG            ((uint64_t)1 << 1)
#define GOSSIP_FILE_DEBUG             ((uint64_t)1 << 2)
#define GOSSIP_DIR_DEBUG              ((uint64_t)1 << 3)
#define GOSSIP_UTILS_DEBUG            ((uint64_t)1 << 4)
#define GOSSIP_WAIT_DEBUG             ((uint64_t)1 << 5)
#define GOSSIP_ACL_DEBUG              ((uint64_t)1 << 6)
#define GOSSIP_DCACHE_DEBUG           ((uint64_t)1 << 7)
#define GOSSIP_DEV_DEBUG              ((uint64_t)1 << 8)
#define GOSSIP_NAME_DEBUG             ((uint64_t)1 << 9)
#define GOSSIP_BUFMAP_DEBUG           ((uint64_t)1 << 10)
#define GOSSIP_CACHE_DEBUG            ((uint64_t)1 << 11)
#define GOSSIP_PROC_DEBUG             ((uint64_t)1 << 12)
#define GOSSIP_XATTR_DEBUG            ((uint64_t)1 << 13)
#define GOSSIP_INIT_DEBUG             ((uint64_t)1 << 14)

#define GOSSIP_MAX_NR                 15
#define GOSSIP_MAX_DEBUG              (((uint64_t)1 << GOSSIP_MAX_NR) - 1)


/* function prototypes */
const char *PVFS_debug_get_next_debug_keyword(int position);
PVFS_debug_mask PVFS_kmod_eventlog_to_mask(const char *event_logging);
PVFS_debug_mask PVFS_debug_eventlog_to_mask(const char *event_logging);
char *PVFS_debug_mask_to_eventlog(PVFS_debug_mask mask);
char *PVFS_kmod_mask_to_eventlog(PVFS_debug_mask mask);

/* a private internal type */
typedef struct 
{
    const char *keyword;
    PVFS_debug_mask mask;
} __keyword_mask_t;

extern const __keyword_mask_t s_keyword_mask_map[];
extern const __keyword_mask_t s_kmod_keyword_mask_map[];
extern const int num_keyword_mask_map;
extern const int num_kmod_keyword_mask_map;

/* Gossip Debug Mask inline funcs combine multiple mask values into one
 * This is done at runtime so it is best avoided where possible but it
 * can be very convenient in some places
 */
static inline PVFS_debug_mask DBG_OR(int count, ...)
{
    PVFS_debug_mask mask = {GOSSIP_NO_DEBUG_INIT};
    va_list args;
    va_start(args, count);
    if (count < 0)
    {
        return mask;
    }
    while (count--)
    {
        PVFS_debug_mask mask2 = va_arg(args, PVFS_debug_mask);;
        mask.mask1 = mask.mask1 | mask2.mask1;
        mask.mask2 = mask.mask2 | mask2.mask2;
    }
    va_end(args);
    return mask;
}

static inline PVFS_debug_mask DBG_AND(int count, ...)
{
    PVFS_debug_mask mask = {__DEBUG_ALL_INIT};
    va_list args;
    va_start(args, count);
    if (count < 0)
    {
        return mask;
    }
    while (count--)
    {
        PVFS_debug_mask mask2 = va_arg(args, PVFS_debug_mask);;
        mask.mask1 = mask.mask1 & mask2.mask1;
        mask.mask2 = mask.mask2 & mask2.mask2;
    }
    va_end(args);
    return mask;
}

static inline PVFS_debug_mask DBG_NOT(PVFS_debug_mask mask)
{
    mask.mask1 = ~mask.mask1;
    mask.mask2 = ~mask.mask2;
    return mask;
}

static inline int DBG_TRUE(PVFS_debug_mask mask)
{
    return mask.mask1 || mask.mask2;
}


#if 0
/* map all config keywords to pvfs2 debug masks here */
static __keyword_mask_t s_keyword_mask_map[] =
{
    /* Log trove debugging info.  Same as 'trove'.*/
    { "storage", {GOSSIP_TROVE_DEBUG_INIT} },
    /* Log trove debugging info.  Same as 'storage'. */
    { "trove", {GOSSIP_TROVE_DEBUG_INIT} },
    /* Log trove operations. */
    { "trove_op", {GOSSIP_TROVE_OP_DEBUG_INIT} },
    /* Log network debug info. */
    { "network", {GOSSIP_BMI_DEBUG_ALL_INIT} },
    /* Log server info, including new operations. */
    { "server", {GOSSIP_SERVER_DEBUG_INIT} },
    /* Log client sysint info.  This is only useful for the client. */
    { "client", {GOSSIP_CLIENT_DEBUG_INIT} },
    /* Debug the varstrip distribution */
    { "varstrip", {GOSSIP_VARSTRIP_DEBUG_INIT} },
    /* Log job info */
    { "job", {GOSSIP_JOB_DEBUG_INIT} },
    /* Debug PINT_process_request calls.  EXTREMELY verbose! */
    { "request", {GOSSIP_REQUEST_DEBUG_INIT} },
    /* Log request scheduler events */
    { "reqsched", {GOSSIP_REQ_SCHED_DEBUG_INIT} },
    /* Log the flow protocol events, including flowproto_multiqueue */
    { "flowproto", {GOSSIP_FLOW_PROTO_DEBUG_INIT} },
    /* Log flow calls */
    { "flow", {GOSSIP_FLOW_DEBUG_INIT} },
    /* Debug the client name cache.  Only useful on the client. */
    { "ncache", {GOSSIP_NCACHE_DEBUG_INIT} },
    /* Debug read-ahead cache events.  Only useful on the client. */
    { "racache", GOSSIP_RACACHE_DEBUG },
    /* Debug the attribute cache.  Only useful on the client. */
    { "acache", {GOSSIP_ACACHE_DEBUG_INIT} },
    /* Log/Debug distribution calls */
    { "distribution", {GOSSIP_DIST_DEBUG_INIT} },
    /* Debug the server-side dbpf attribute cache */
    { "dbpfattrcache", {GOSSIP_DBPF_ATTRCACHE_DEBUG_INIT} },
    /* Debug the client lookup state machine. */
    { "lookup", {GOSSIP_LOOKUP_DEBUG_INIT} },
    /* Debug the client remove state macine. */
    { "remove", {GOSSIP_REMOVE_DEBUG_INIT} },
    /* Debug the server getattr state machine. */
    { "getattr", {GOSSIP_GETATTR_DEBUG_INIT} },
    /* Debug the server setattr state machine. */
    { "setattr", {GOSSIP_SETATTR_DEBUG_INIT} },
    /* vectored getattr server state machine */
    { "listattr", {GOSSIP_LISTATTR_DEBUG_INIT} },
    /* Debug the client and server get ext attributes SM. */
    { "geteattr", {GOSSIP_GETEATTR_DEBUG_INIT} },
    /* Debug the client and server set ext attributes SM. */
    { "seteattr", {GOSSIP_SETEATTR_DEBUG_INIT} },
    /* Debug the readdir operation (client and server) */
    { "readdir", {GOSSIP_READDIR_DEBUG_INIT} },
    /* Debug the mkdir operation (server only) */
    { "mkdir", {GOSSIP_MKDIR_DEBUG_INIT} },
    /* Debug the io operation (reads and writes) 
     * for both the client and server */
    { "io", {GOSSIP_IO_DEBUG_INIT} },
    /* Debug the server's open file descriptor cache */
    { "open_cache", {GOSSIP_DBPF_OPEN_CACHE_DEBUG_INIT} }, 
    /* Debug permissions checking on the server */
    { "permissions", {GOSSIP_PERMISSIONS_DEBUG_INIT} }, 
    /* Debug the cancel operation */
    { "cancel", {GOSSIP_CANCEL_DEBUG_INIT} },
    /* Debug the msgpair state machine */
    { "msgpair", {GOSSIP_MSGPAIR_DEBUG_INIT} },
    /* Debug the client core app */
    { "clientcore", {GOSSIP_CLIENTCORE_DEBUG_INIT} },
    /* Debug the client timing state machines (job timeout, etc.) */
    { "clientcore_timing", {GOSSIP_CLIENTCORE_TIMING_DEBUG_INIT} },
    /* network encoding */
    { "endecode", {GOSSIP_ENDECODE_DEBUG_INIT} },
    /* Show server file (metadata) accesses (both modify and read-only). */ 
    { "access", {GOSSIP_ACCESS_DEBUG_INIT} },
    /* Show more detailed server file accesses */
    { "access_detail", {GOSSIP_ACCESS_DETAIL_DEBUG_INIT} },
    /* Debug the listeattr operation */
    { "listeattr", {GOSSIP_LISTEATTR_DEBUG_INIT} },
    /* Debug the state machine management code */
    { "sm", {GOSSIP_STATE_MACHINE_DEBUG_INIT} },
    /* Debug the metadata dbpf keyval functions */
    { "keyval", {GOSSIP_DBPF_KEYVAL_DEBUG_INIT} },
    /* Debug the metadata sync coalescing code */
    { "coalesce", {GOSSIP_DBPF_COALESCE_DEBUG_INIT} },
    /* Display the  hostnames instead of IP addrs in debug output */
    { "access_hostnames", {GOSSIP_ACCESS_HOSTNAMES_INIT} },
    /* Show the client device events */
    { "user_dev", {GOSSIP_USER_DEV_DEBUG_INIT} },
    /* Debug the fsck tool */
    { "fsck", {GOSSIP_FSCK_DEBUG_INIT} },
    /* Debug the bstream code */
    { "bstream", {GOSSIP_BSTREAM_DEBUG_INIT} },
    /* Debug trove in direct io mode */
    { "directio", {GOSSIP_DIRECTIO_DEBUG_INIT} },
    /* Debug direct io thread management */
    { "mgmt", {GOSSIP_MGMT_DEBUG_INIT} },
    /* Debug mirroring process */
    { "mirror",{GOSSIP_MIRROR_DEBUG_INIT} },
    /* Windows client */
    { "win_client", {GOSSIP_WIN_CLIENT_DEBUG_INIT} },
    /* Debug robust security code */
    { "security", {GOSSIP_SECURITY_DEBUG_INIT} },
    /* Capability Cache */
    { "seccache", {GOSSIP_SECCACHE_DEBUG_INIT} },
    /* Client User Interface */
    { "usrint", {GOSSIP_USRINT_DEBUG_INIT} },
    /* Sidcache debugging */
    { "sidcache", {GOSSIP_SIDCACHE_DEBUG_INIT} },
    /* Everything except the periodic events.  Useful for debugging */
    { "verbose", { __DEBUG_ALL_INIT }
      /*& ~(GOSSIP_PERFCOUNTER_DEBUG | GOSSIP_STATE_MACHINE_DEBUG |
                       GOSSIP_ENDECODE_DEBUG | GOSSIP_USER_DEV_DEBUG)) */
    },
    /* No debug output */
    { "none", {GOSSIP_NO_DEBUG_INIT} },
    /* Everything */
    { "all",  {__DEBUG_ALL_INIT} }
};

static const int num_keyword_mask_map = (int)           \
        (sizeof(s_keyword_mask_map) / sizeof(__keyword_mask_t));
#endif

/*
#undef __DEBUG_ALL
*/

/* map all kmod keywords to kmod debug masks here */
#if 0
static __keyword_mask_t s_kmod_keyword_mask_map[] =
{
    {"super" , GOSSIP_SUPER_DEBUG},
    {"inode" , GOSSIP_INODE_DEBUG},
    {"file"  , GOSSIP_FILE_DEBUG},
    {"dir"   , GOSSIP_DIR_DEBUG},
    {"utils" , GOSSIP_UTILS_DEBUG},
    {"wait"  , GOSSIP_WAIT_DEBUG},
    {"acl"   , GOSSIP_ACL_DEBUG},
    {"dcache", GOSSIP_DCACHE_DEBUG},
    {"dev"   , GOSSIP_DEV_DEBUG},
    {"name"  , GOSSIP_NAME_DEBUG},
    {"bufmap", GOSSIP_BUFMAP_DEBUG},
    {"cache" , GOSSIP_CACHE_DEBUG},
    {"proc"  , GOSSIP_PROC_DEBUG},
    {"xattr" , GOSSIP_XATTR_DEBUG},
    {"init"  , GOSSIP_INIT_DEBUG},
    {"none"  , GOSSIP_NO_DEBUG},
    {"all"   , GOSSIP_MAX_DEBUG}
};
#endif


#endif /* __PVFS2_DEBUG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

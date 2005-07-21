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

#define GOSSIP_NO_DEBUG                (uint64_t)0
#define GOSSIP_BMI_DEBUG_TCP           ((uint64_t)1 << 0)
#define GOSSIP_BMI_DEBUG_CONTROL       ((uint64_t)1 << 1)
#define GOSSIP_BMI_DEBUG_OFFSETS       ((uint64_t)1 << 2)
#define GOSSIP_BMI_DEBUG_GM            ((uint64_t)1 << 3)
#define GOSSIP_JOB_DEBUG               ((uint64_t)1 << 4)
#define GOSSIP_SERVER_DEBUG            ((uint64_t)1 << 5)
#define GOSSIP_STO_DEBUG_CTRL          ((uint64_t)1 << 6)
#define GOSSIP_STO_DEBUG_DEFAULT       ((uint64_t)1 << 7)
#define GOSSIP_FLOW_DEBUG              ((uint64_t)1 << 8)
#define GOSSIP_BMI_DEBUG_GM_MEM        ((uint64_t)1 << 9)
#define GOSSIP_REQUEST_DEBUG           ((uint64_t)1 << 10)
#define GOSSIP_FLOW_PROTO_DEBUG        ((uint64_t)1 << 11)
#define GOSSIP_NCACHE_DEBUG            ((uint64_t)1 << 12)
#define GOSSIP_CLIENT_DEBUG            ((uint64_t)1 << 13)
#define GOSSIP_REQ_SCHED_DEBUG         ((uint64_t)1 << 14)
#define GOSSIP_ACACHE_DEBUG            ((uint64_t)1 << 15)
#define GOSSIP_TROVE_DEBUG             ((uint64_t)1 << 16)
#define GOSSIP_TROVE_OP_DEBUG          ((uint64_t)1 << 17)
#define GOSSIP_DIST_DEBUG              ((uint64_t)1 << 18)
#define GOSSIP_BMI_DEBUG_IB            ((uint64_t)1 << 19)
#define GOSSIP_DBPF_ATTRCACHE_DEBUG    ((uint64_t)1 << 20)
#define GOSSIP_MMAP_RCACHE_DEBUG       ((uint64_t)1 << 21)
#define GOSSIP_LOOKUP_DEBUG            ((uint64_t)1 << 22)
#define GOSSIP_REMOVE_DEBUG            ((uint64_t)1 << 23)
#define GOSSIP_GETATTR_DEBUG           ((uint64_t)1 << 24)
#define GOSSIP_READDIR_DEBUG           ((uint64_t)1 << 25)
#define GOSSIP_IO_DEBUG                ((uint64_t)1 << 26)
#define GOSSIP_DBPF_OPEN_CACHE_DEBUG   ((uint64_t)1 << 27)
#define GOSSIP_PERMISSIONS_DEBUG       ((uint64_t)1 << 28)
#define GOSSIP_CANCEL_DEBUG            ((uint64_t)1 << 29)
#define GOSSIP_MSGPAIR_DEBUG           ((uint64_t)1 << 30)
#define GOSSIP_CLIENTCORE_DEBUG        ((uint64_t)1 << 31)
#define GOSSIP_CLIENTCORE_TIMING_DEBUG ((uint64_t)1 << 32)
#define GOSSIP_SETATTR_DEBUG           ((uint64_t)1 << 33)
#define GOSSIP_MKDIR_DEBUG             ((uint64_t)1 << 34)
#define GOSSIP_VARSTRIP_DEBUG          ((uint64_t)1 << 35)
#define GOSSIP_GETEATTR_DEBUG          ((uint64_t)1 << 36)
#define GOSSIP_SETEATTR_DEBUG          ((uint64_t)1 << 37)
#define GOSSIP_ENDECODE_DEBUG          ((uint64_t)1 << 38)
#define GOSSIP_DELEATTR_DEBUG          ((uint64_t)1 << 39)

/* NOTE: if you want your gossip flag to be controlable from 
 * pvfs2-set-debugmask you have to add it in
 * src/common/misc/pvfs-debug.c
 */

#define GOSSIP_BMI_DEBUG_ALL (uint64_t)                               \
(GOSSIP_BMI_DEBUG_TCP + GOSSIP_BMI_DEBUG_CONTROL +                    \
 GOSSIP_BMI_DEBUG_GM + GOSSIP_BMI_DEBUG_OFFSETS + GOSSIP_BMI_DEBUG_IB)

uint64_t PVFS_debug_eventlog_to_mask(
    const char *event_logging);

char *PVFS_debug_get_next_debug_keyword(
    int position);

#endif /* __PVFS2_DEBUG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

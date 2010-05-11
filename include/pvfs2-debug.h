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
#define GOSSIP_ACCESS_DEBUG            ((uint64_t)1 << 40)
#define GOSSIP_ACCESS_DETAIL_DEBUG     ((uint64_t)1 << 41)
#define GOSSIP_LISTEATTR_DEBUG         ((uint64_t)1 << 42)
#define GOSSIP_PERFCOUNTER_DEBUG       ((uint64_t)1 << 43)
#define GOSSIP_STATE_MACHINE_DEBUG     ((uint64_t)1 << 44)
#define GOSSIP_DBPF_KEYVAL_DEBUG       ((uint64_t)1 << 45)
#define GOSSIP_LISTATTR_DEBUG          ((uint64_t)1 << 46)
#define GOSSIP_DBPF_COALESCE_DEBUG     ((uint64_t)1 << 47)
#define GOSSIP_ACCESS_HOSTNAMES        ((uint64_t)1 << 48)
#define GOSSIP_FSCK_DEBUG              ((uint64_t)1 << 49)
#define GOSSIP_BMI_DEBUG_MX            ((uint64_t)1 << 50)
#define GOSSIP_BSTREAM_DEBUG           ((uint64_t)1 << 51)
#define GOSSIP_BMI_DEBUG_PORTALS       ((uint64_t)1 << 52)
#define GOSSIP_USER_DEV_DEBUG          ((uint64_t)1 << 53)
#define GOSSIP_DIRECTIO_DEBUG          ((uint64_t)1 << 54)
#define GOSSIP_MGMT_DEBUG              ((uint64_t)1 << 55)
#define GOSSIP_IO_TIMING               ((uint64_t)1 << 56)

/* NOTE: if you want your gossip flag to be controllable from 
 * pvfs2-set-debugmask you have to add it in
 * src/common/misc/pvfs2-debug.c
 */

#define GOSSIP_BMI_DEBUG_ALL (uint64_t)                               \
(GOSSIP_BMI_DEBUG_TCP + GOSSIP_BMI_DEBUG_CONTROL +                    \
 GOSSIP_BMI_DEBUG_GM + GOSSIP_BMI_DEBUG_OFFSETS + GOSSIP_BMI_DEBUG_IB \
 + GOSSIP_BMI_DEBUG_MX + GOSSIP_BMI_DEBUG_PORTALS)

uint64_t PVFS_debug_eventlog_to_mask(
    const char *event_logging);

const char *PVFS_debug_get_next_debug_keyword(
    int position);

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

/*
 * To allow these masks to be settable from pvfs2-client-core,
 * edit pvfs2-debug.c to add human readable event mask strings
 * in s_kmod_keyword_mask_map[] array.
 */
uint64_t PVFS_kmod_eventlog_to_mask(
    const char *event_logging);

#endif /* __PVFS2_DEBUG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

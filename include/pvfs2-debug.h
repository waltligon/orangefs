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

enum
{
    GOSSIP_NO_DEBUG =                      0,
    GOSSIP_BMI_DEBUG_TCP =          (1 << 0),
    GOSSIP_BMI_DEBUG_CONTROL =      (1 << 1),
    GOSSIP_BMI_DEBUG_OFFSETS =      (1 << 2),
    GOSSIP_BMI_DEBUG_GM =           (1 << 3),
    GOSSIP_JOB_DEBUG =              (1 << 4),
    GOSSIP_SERVER_DEBUG =           (1 << 5),
    GOSSIP_STO_DEBUG_CTRL =         (1 << 6),
    GOSSIP_STO_DEBUG_DEFAULT =      (1 << 7),
    GOSSIP_FLOW_DEBUG =             (1 << 8),
    GOSSIP_BMI_DEBUG_GM_MEM =       (1 << 9),
    GOSSIP_REQUEST_DEBUG =          (1 << 10),
    GOSSIP_FLOW_PROTO_DEBUG =       (1 << 11),
    GOSSIP_NCACHE_DEBUG =           (1 << 12),
    GOSSIP_CLIENT_DEBUG =           (1 << 13),
    GOSSIP_REQ_SCHED_DEBUG =        (1 << 14),
    GOSSIP_ACACHE_DEBUG =           (1 << 15),
    GOSSIP_TROVE_DEBUG =            (1 << 16),
    GOSSIP_DIST_DEBUG =             (1 << 17),
    GOSSIP_BMI_DEBUG_IB =           (1 << 18),
    GOSSIP_DBPF_ATTRCACHE_DEBUG =   (1 << 19),
    GOSSIP_MMAP_RCACHE_DEBUG =      (1 << 20),
    GOSSIP_LOOKUP_DEBUG =           (1 << 21),
    GOSSIP_REMOVE_DEBUG =           (1 << 22),
    GOSSIP_GETATTR_DEBUG =          (1 << 23),

    GOSSIP_BMI_DEBUG_ALL = GOSSIP_BMI_DEBUG_TCP +
    GOSSIP_BMI_DEBUG_CONTROL + GOSSIP_BMI_DEBUG_GM +
    GOSSIP_BMI_DEBUG_OFFSETS + GOSSIP_BMI_DEBUG_IB
};

int PVFS_debug_eventlog_to_mask(
    char *event_logging);

char *PVFS_debug_get_next_debug_keyword(
    int position);

#endif /* __PVFS2_DEBUG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

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
    BMI_DEBUG_TCP =	    (1 << 0),
    BMI_DEBUG_CONTROL =	    (1 << 1),
    BMI_DEBUG_OFFSETS =	    (1 << 2),
    BMI_DEBUG_GM =	    (1 << 3),
    JOB_DEBUG =		    (1 << 4),
    SERVER_DEBUG =	    (1 << 5),
    STO_DEBUG_CTRL =	    (1 << 6),
    STO_DEBUG_DEFAULT =	    (1 << 7),
    FLOW_DEBUG =	    (1 << 8),
    BMI_DEBUG_GM_MEM =	    (1 << 9),
    REQUEST_DEBUG =	    (1 << 10),
    FLOW_PROTO_DEBUG =	    (1 << 11),
    DCACHE_DEBUG =	    (1 << 12),
    CLIENT_DEBUG =	    (1 << 13),
    REQ_SCHED_DEBUG =	    (1 << 14),
    PCACHE_DEBUG =	    (1 << 15),
    TROVE_DEBUG =           (1 << 16),

    BMI_DEBUG_ALL = BMI_DEBUG_TCP + BMI_DEBUG_CONTROL +
	+BMI_DEBUG_GM + BMI_DEBUG_OFFSETS
};

#endif /* __PVFS2_DEBUG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

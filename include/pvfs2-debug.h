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

/* If you add a new value to this enum, make sure it is a power of 2! */
enum
{
	BMI_DEBUG_TCP = 1,
	BMI_DEBUG_CONTROL = 2,
	BMI_DEBUG_OFFSETS = 4,
	BMI_DEBUG_GM = 8,
	JOB_DEBUG = 16,
	SERVER_DEBUG = 32,
	STO_DEBUG_CTRL = 64,
	STO_DEBUG_DEFAULT = 128,
	FLOW_DEBUG = 256,
	BMI_DEBUG_GM_MEM = 512,
	REQUEST_DEBUG = 1024,
	FLOW_PROTO_DEBUG = 2048,
	DCACHE_DEBUG = 4096,
	CLIENT_DEBUG = 8192,
	REQ_SCHED_DEBUG = 16384,
	PCACHE_DEBUG = 32768,

	BMI_DEBUG_ALL = BMI_DEBUG_TCP + BMI_DEBUG_CONTROL +
		+ BMI_DEBUG_GM + BMI_DEBUG_OFFSETS
};

#endif /* __PVFS2_DEBUG_H */

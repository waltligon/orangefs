/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains the interface to the pinode storage subsystem */

#ifndef __PINODE_H
#define __PINODE_H

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <gen-locks.h>
#include <llist.h>
#include <pvfs2-types.h>
#include <pvfs2-attr.h>
//#include <pvfs-dist.h>
//#include <pvfs-sysint.h>
#include <pint-sysint.h>

#if 0
/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
/* Pinode structure */
typedef struct
{
	pinode_reference pinode_ref; /* pinode reference - entity to uniquely
											  identify a pinode */ 
	gen_mutex_t *pinode_mutex;	  /* mutex lock */
	char *object_name;           /* name of the PVFS object */
	struct PVFS_object_attr attr;/* attributes of PVFS object */
	PVFS_bitfield mask;			  /* attribute mask */
	PVFS_size size;				  /* PVFS object size */
	struct timeval tstamp_handle;	  /* timestamp for handle consistency */
	struct timeval tstamp_attr;	  /* timestamp for attribute consistency */
	struct timeval tstamp_size;	  /* timestamp for size consistency */
}pinode, *pinode_p;
#endif
	
/* This typedef will change if we implement a different data structure for
 * pinode storage.
 */

/* UNCOMMENTED BY PHIL WHEN MOVING TO NEW TREE */
typedef llist_p pinode_storage_p;

/* Public interface */
int pcache_storage_init(pinode_storage_p pstore);
void pcache_storage_finalize(pinode_storage_p pstore);
int pcache_storage_add_new(pinode_storage_p pstore,pinode_p pinode_ptr);
int pcache_merge_pinodes(pinode *p1, pinode *p2);
int pcache_storage_get(pinode_storage_p pstore,pinode **pnode,\
	pinode_reference pinode_refn);
int pcache_rem(pinode_storage_p pstore,pinode_reference pinode_refn);
int pcache_lock(pinode_p pinode_ptr);
int pcache_unlock(pinode_p pinode_ptr);
pinode_p pcache_alloc(void);
void pcache_dealloc(pinode_p pinode_ptr);

#endif

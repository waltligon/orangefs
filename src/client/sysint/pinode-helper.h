/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _PINODE_HELPER_H
#define _PINODE_HELPER_H

#include <pcache.h>

/* Timeout values */
struct timeval handle_to;

int phelper_get_pinode(PVFS_pinode_reference pref,pinode **pinode_ptr,
		uint32_t attrmask, PVFS_credentials credentials);
int phelper_fill_timestamps(pinode *pnode);
int phelper_fill_attr(pinode *ptr,PVFS_object_attr attr);
int phelper_release_pinode(pinode *pinode_ptr);

#endif

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
struct timeval attr_to;
struct timeval size_to;

int phelper_get_pinode(pinode_reference pref,pinode **pinode_ptr,
		PVFS_bitfield attrmask, int valid_flags,
		PVFS_credentials credentials);
int phelper_refresh_pinode(PVFS_bitfield mask, pinode *pinode_ptr,
		pinode_reference pref,PVFS_credentials credentials);
int phelper_validate_pinode(pinode *pnode,int flags,PVFS_bitfield mask,
		PVFS_credentials credentials);
int modify_pinode(pinode *node,PVFS_object_attr attr,PVFS_bitfield mask);
int phelper_fill_timestamps(pinode *pnode);

#endif

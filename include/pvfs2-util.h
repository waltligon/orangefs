/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header includes prototypes for utility functions that may be useful
 * to implementors working at the pvfs2 system interface level.  
 */

#ifndef __PVFS2_UTIL_H
#define __PVFS2_UTIL_H

/* client side config file / option management */
int PVFS_util_parse_pvfstab(char* filename, pvfs_mntlist *mnt);
void PVFS_util_pvfstab_mntlist_free(pvfs_mntlist* e_p);

/* path management */
int PVFS_util_lookup_parent(char *filename, PVFS_fs_id fs_id, 
    PVFS_credentials credentials, PVFS_handle* handle);

#endif /* __PVFS2_UTIL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

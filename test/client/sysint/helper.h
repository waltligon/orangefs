/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#define ATTR_UID 1
#define ATTR_GID 2
#define ATTR_PERM 4
#define ATTR_ATIME 8
#define ATTR_CTIME 16
#define ATTR_MTIME 32
#define ATTR_TYPE 2048


#define MAX_PVFS_PATH_LEN  512

extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);
extern int PINT_get_base_dir(char *pathname,
                             char *out_base_dir, int out_max_len);
extern int PINT_remove_base_dir(char *pathname,
                                char *out_dir, int out_max_len);

/* make uid, gid and perms passed in later */
PVFS_handle lookup_parent_handle(char *filename, PVFS_fs_id fs_id);

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

/* results of parsing a pvfs2 tabfile, may contain more than one entry */
struct PVFS_util_tab_s
{
    int mntent_count;                      /* number of mnt entries */
    struct PVFS_sys_mntent *mntent_array;  /* mnt entries */
    char tabfile_name[PVFS_NAME_MAX];      /* name of tabfile */
};
typedef struct PVFS_util_tab_s PVFS_util_tab;

/* client side config file / option management */
const PVFS_util_tab* PVFS_util_parse_pvfstab(
    const char* tabfile);
int PVFS_util_init_defaults(
    void);
int PVFS_util_resolve(
    const char* local_path,
    PVFS_fs_id* out_fs_id,
    char* out_fs_path,
    int out_fs_path_max);
int PVFS_util_get_default_fsid(PVFS_fs_id* out_fs_id);


/* path management */
int PVFS_util_lookup_parent(
    char *filename,
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    PVFS_handle * handle);
int PVFS_util_remove_base_dir(
    char *pathname,
    char *out_dir,
    int out_max_len);
int PVFS_util_remove_dir_prefix(
    const char *pathname,
    const char *prefix,
    char *out_path,
    int out_max_len);

/* help out lazy humans */
void PVFS_util_make_size_human_readable(
    PVFS_size size,
    char *out_str,
    int max_out_len);

/* generic attribute conversion */
static inline int PVFS_util_object_to_sys_attr_mask(int obj_mask)
{
    int sys_mask = 0;

    if (obj_mask & PVFS_ATTR_COMMON_UID)
    {
        sys_mask |= PVFS_ATTR_SYS_UID;
    }
    if (obj_mask & PVFS_ATTR_COMMON_GID)
    {
        sys_mask |= PVFS_ATTR_SYS_GID;
    }
    if (obj_mask & PVFS_ATTR_COMMON_PERM)
    {
        sys_mask |= PVFS_ATTR_SYS_PERM;
    }
    if (obj_mask & PVFS_ATTR_COMMON_ATIME)
    {
        sys_mask |= PVFS_ATTR_SYS_ATIME;
    }
    if (obj_mask & PVFS_ATTR_COMMON_CTIME)
    {
        sys_mask |= PVFS_ATTR_SYS_CTIME;
    }
    if (obj_mask & PVFS_ATTR_COMMON_MTIME)
    {
        sys_mask |= PVFS_ATTR_SYS_MTIME;
    }
    if (obj_mask & PVFS_ATTR_COMMON_TYPE)
    {
        sys_mask |= PVFS_ATTR_SYS_TYPE;
    }
    if (obj_mask & PVFS_ATTR_DATA_SIZE)
    {
        sys_mask |= PVFS_ATTR_DATA_SIZE;
    }
    if (obj_mask & PVFS_ATTR_SYMLNK_TARGET)
    {
        sys_mask |= PVFS_ATTR_SYS_LNK_TARGET;
    }
    return sys_mask;
}

#endif /* __PVFS2_UTIL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header includes prototypes for utility functions that may be
 * useful to implementors working at the pvfs2 system interface level.
 */

#ifndef __PVFS2_UTIL_H
#define __PVFS2_UTIL_H

#include "pvfs2.h"
#include "pvfs2-types.h"

/* Define min macro with pvfs2 prefix */
#ifndef PVFS_util_min
#define PVFS_util_min(x1,x2) ((x1) > (x2))? (x2):(x1)
#endif

/* results of parsing a pvfs2 tabfile, may contain more than one entry */
struct PVFS_util_tab_s
{
    int mntent_count;                      /* number of mnt entries */
    struct PVFS_sys_mntent *mntent_array;  /* mnt entries */
    char tabfile_name[PVFS_NAME_MAX];      /* name of tabfile */
};
typedef struct PVFS_util_tab_s PVFS_util_tab;

/* client side default credential generation */
void PVFS_util_gen_credentials(
    PVFS_credentials *credentials);

/* returns the currently set umask */
int PVFS_util_get_umask(void);

/*
  shallow copies the credentials into a newly allocated returned
  credential object.  returns NULL on failure.
*/
PVFS_credentials *PVFS_util_dup_credentials(
    PVFS_credentials *credentials);

void PVFS_util_release_credentials(
    PVFS_credentials *credentials);

int PVFS_util_copy_sys_attr(
    PVFS_sys_attr *dest, PVFS_sys_attr *src);

void PVFS_util_release_sys_attr(PVFS_sys_attr *attr);

int PVFS_util_init_defaults(void);

/* client side config file / option management */
const PVFS_util_tab* PVFS_util_parse_pvfstab(
    const char* tabfile);
int PVFS_util_resolve(
    const char* local_path,
    PVFS_fs_id* out_fs_id,
    char* out_fs_path,
    int out_fs_path_max);
int PVFS_util_get_default_fsid(
    PVFS_fs_id* out_fs_id);
int PVFS_util_add_dynamic_mntent(
    struct PVFS_sys_mntent *mntent);
int PVFS_util_remove_internal_mntent(
    struct PVFS_sys_mntent *mntent);
void PVFS_sys_free_mntent(
    struct PVFS_sys_mntent *mntent);

void PVFS_util_make_size_human_readable(
    PVFS_size size,
    char *out_str,
    int max_out_len,
    int use_si_units);

PVFS_time PVFS_util_get_current_time(void);

static inline int PVFS_util_object_to_sys_attr_mask(
    int obj_mask)
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

static inline int PVFS2_translate_mode(int mode)
{
    int ret = 0, i = 0;
    static int modes[9] =
    {
        S_IXOTH, S_IWOTH, S_IROTH,
        S_IXGRP, S_IWGRP, S_IRGRP,
        S_IXUSR, S_IWUSR, S_IRUSR
    };
    static int pvfs2_modes[9] =
    {
        PVFS_O_EXECUTE, PVFS_O_WRITE, PVFS_O_READ,
        PVFS_G_EXECUTE, PVFS_G_WRITE, PVFS_G_READ,
        PVFS_U_EXECUTE, PVFS_U_WRITE, PVFS_U_READ,
    };

    for(i = 0; i < 9; i++)
    {
        if (mode & modes[i])
        {
            ret |= pvfs2_modes[i];
        }
    }
    return ret;
}

#endif /* __PVFS2_UTIL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

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

#ifndef PVFS_util_max
#define PVFS_util_max(x1,x2) ((x1) > (x2)) ? (x1) : (x2)
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
    const PVFS_credentials *credentials);

struct PVFS_sys_mntent* PVFS_util_gen_mntent(
    char* config_server,
    char* fs_name);

void PVFS_util_gen_mntent_release(struct PVFS_sys_mntent* mntent);

void PVFS_util_release_credentials(
    PVFS_credentials *credentials);

int PVFS_util_copy_sys_attr(
    PVFS_sys_attr *dest_attr,
    PVFS_sys_attr *src_attr);
void PVFS_util_release_sys_attr(
    PVFS_sys_attr *attr);

int PVFS_util_init_defaults(void);

/* client side config file / option management */
const PVFS_util_tab* PVFS_util_parse_pvfstab(
    const char* tabfile);
void PINT_release_pvfstab(void);
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

int PVFS_util_get_mntent_copy(
    PVFS_fs_id fs_id,
    struct PVFS_sys_mntent *out_mntent);
int PVFS_util_copy_mntent(
    struct PVFS_sys_mntent *dest_mntent,
    struct PVFS_sys_mntent *src_mntent);
void PVFS_util_free_mntent(
    struct PVFS_sys_mntent *mntent);

void PVFS_util_make_size_human_readable(
    PVFS_size size,
    char *out_str,
    int max_out_len,
    int use_si_units);

uint32_t PVFS_util_sys_to_object_attr_mask(
    uint32_t sys_attrmask);

uint32_t PVFS_util_object_to_sys_attr_mask( 
    uint32_t obj_mask);

int32_t PVFS_util_translate_mode(int mode, int suid);

#endif /* __PVFS2_UTIL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

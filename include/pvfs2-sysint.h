/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \defgroup sysint PVFS2 system interface
 *
 *  The PVFS2 system interface provides functionality used for everyday
 *  interaction.  Both the PVFS2 kernel support and the PVFS2 ROMIO
 *  implementation build on this interface.
 *
 * @{
 */

/** \file
 *  Declarations for the PVFS2 system interface.
 */

#ifndef __PVFS_SYSINT_H
#define __PVFS_SYSINT_H

#include "pvfs2-types.h"
#include "pvfs2-request.h"

/** Options supported by get_info() and set_info(). */
enum PVFS_sys_setinfo_opt
{
    PVFS_SYS_NCACHE_TIMEOUT_MSECS = 1,
    PVFS_SYS_ACACHE_TIMEOUT_MSECS,
    PVFS_SYS_MSG_TIMEOUT_SECS,
    PVFS_SYS_MSG_RETRY_LIMIT,
    PVFS_SYS_MSG_RETRY_DELAY_MSECS,
};

/** Holds a non-blocking system interface operation handle. */
typedef PVFS_id_gen_t PVFS_sys_op_id;

/** Describes attributes for a file, directory, or symlink. */
struct PVFS_sys_attr_s
{
    PVFS_uid owner;
    PVFS_gid group;
    PVFS2_ALIGN_VAR(PVFS_permissions, perms);
    PVFS_time atime;
    PVFS_time mtime;
    PVFS_time ctime;
    PVFS_size size;
    PVFS2_ALIGN_VAR(char *, link_target);/* NOTE: caller must free if valid */
    PVFS2_ALIGN_VAR(int32_t, dfile_count); /* Changed to int32_t so that size of structure does not change */
    PVFS2_ALIGN_VAR(char*, dist_name);   /* NOTE: caller must free if valid */
    PVFS2_ALIGN_VAR(char*, dist_params); /* NOTE: caller must free if valid */
    PVFS_size dirent_count;
    PVFS_ds_type objtype;
    PVFS_flags flags;
    uint32_t mask;
    PVFS_size blksize;
};
typedef struct PVFS_sys_attr_s PVFS_sys_attr;

/** Describes a PVFS2 file system. */
struct PVFS_sys_mntent
{
    char **pvfs_config_servers;	/* addresses of servers with config info */
    int32_t num_pvfs_config_servers; /* changed to int32_t so that size of structure does not change */
    char *the_pvfs_config_server; /* first of the entries above that works */
    char *pvfs_fs_name;		/* name of PVFS2 file system */
    enum PVFS_flowproto_type flowproto;	/* flow protocol */
    enum PVFS_encoding_type encoding;   /* wire data encoding */
    /* fs id, filled in by system interface when it looks up the fs */
    PVFS_fs_id fs_id;

    /* Default number of dfiles mount option value */
    int32_t default_num_dfiles; /* int32_t for portable, fixed size structure */
    /* Check to determine whether the mount process must perform the integrity checks on the config files */
    int32_t integrity_check;
    /* the following fields are included for convenience;
     * useful if the file system is "mounted" */
    char *mnt_dir;		/* local mount path */
    char *mnt_opts;		/* full option list */
};

/** Describes file distribution parameters. */
struct PVFS_sys_dist_s
{
    char* name;
    void* params;
};
typedef struct PVFS_sys_dist_s PVFS_sys_dist;

/**********************************************************************/
/* Structures that Hold the results of various system interface calls */
/**********************************************************************/

/** Holds results of a lookup operation (reference to object). */
struct PVFS_sysresp_lookup_s
{
    PVFS_object_ref ref;
};
typedef struct PVFS_sysresp_lookup_s PVFS_sysresp_lookup;

/** Holds results of a getattr operation (attributes of object). */
struct PVFS_sysresp_getattr_s
{
    PVFS_sys_attr attr;
};
typedef struct PVFS_sysresp_getattr_s PVFS_sysresp_getattr;

/* setattr */
/* no data returned in setattr response */

/** Holds results of a mkdir operation (reference to new directory). */
struct PVFS_sysresp_mkdir_s
{
    PVFS_object_ref ref;
};
typedef struct PVFS_sysresp_mkdir_s PVFS_sysresp_mkdir;

/** Holds results of a create operation (reference to new file). */
struct PVFS_sysresp_create_s
{
    PVFS_object_ref ref;
};
typedef struct PVFS_sysresp_create_s PVFS_sysresp_create;

/* remove */
/* no data returned in remove response */

/* rename */
/* no data returned in rename response */

/** Holds results of a symlink operation (reference to new symlink). */
struct PVFS_sysresp_symlink_s
{
    PVFS_object_ref ref;
};
typedef struct PVFS_sysresp_symlink_s PVFS_sysresp_symlink;

/** Holds results of a readlink operation (string name of target). */
struct PVFS_sysresp_readlink_s
{
    char *target;
};
typedef struct PVFS_sysresp_readlink_s PVFS_sysresp_readlink;

/** Holds results of an I/O operation (total number of bytes read/written). */
struct PVFS_sysresp_io_s
{
    PVFS_size total_completed;
};
typedef struct PVFS_sysresp_io_s PVFS_sysresp_io;

/** Holds results of a readdir operation (position token, directory version
 *  information, array of directory entries).
 */
struct PVFS_sysresp_readdir_s
{
    PVFS_ds_position token;
    PVFS_dirent *dirent_array;
    uint32_t pvfs_dirent_outcount; /* uint32_t for portable, fixed size structure */
    uint64_t directory_version;
};
typedef struct PVFS_sysresp_readdir_s PVFS_sysresp_readdir;

/** Holds results of a readdirplus operation (position token, directory version
 *  information, array of directory entries, array of stat error codes and array of
 *  attribute information).
 */
struct PVFS_sysresp_readdirplus_s
{
    PVFS_ds_position token;
    PVFS_dirent   *dirent_array;
    uint32_t        pvfs_dirent_outcount; /* uint32_t for portable, fixed size structure */
    uint64_t       directory_version;
    PVFS_error    *stat_err_array; 
    PVFS_sys_attr *attr_array;
};
typedef struct PVFS_sysresp_readdirplus_s PVFS_sysresp_readdirplus;


/* truncate */
/* no data returned in truncate response */

struct PVFS_sysresp_statfs_s
{
    PVFS_statfs statfs_buf;
    int32_t server_count; /* int32_t for portable, fixed size structure */
};
typedef struct PVFS_sysresp_statfs_s PVFS_sysresp_statfs;

struct PVFS_sysresp_getparent_s
{
    PVFS_object_ref parent_ref;
    char basename[PVFS_NAME_MAX];
};
typedef struct PVFS_sysresp_getparent_s PVFS_sysresp_getparent;

/** Holds results of a geteattr_list operation (attributes of object). */
struct PVFS_sysresp_geteattr_s
{
    PVFS_ds_keyval *val_array;
    PVFS_error *err_array;
};
typedef struct PVFS_sysresp_geteattr_s PVFS_sysresp_geteattr;

/* seteattr */
/* no data returned in seteattr response */

/* deleattr */
/* no data returned in deleattr response */

/** Holds results of a listeattr_list operation (keys of object). */
struct PVFS_sysresp_listeattr_s
{
    PVFS_ds_position token;
    int32_t         nkey;
    PVFS_ds_keyval *key_array;
};
typedef struct PVFS_sysresp_listeattr_s PVFS_sysresp_listeattr;


/****************************************/
/* system interface function prototypes */
/****************************************/

int PVFS_sys_initialize(
    uint64_t default_debug_mask);
int PVFS_sys_fs_add(
    struct PVFS_sys_mntent* mntent);
int PVFS_isys_fs_add(
    struct PVFS_sys_mntent* mntent,
    PVFS_sys_op_id *op_id,
    void *user_ptr);
int PVFS_sys_fs_remove(
    struct PVFS_sys_mntent* mntent);
int PVFS_sys_finalize(
    void);

/*
  NOTE: the following values are to be used by
  PVFS_sys(.*)_lookup as the "follow_link" argument.

  All symlinks are resolved if they are part of a
  longer pathname. (i.e. partial path symlink resolution)

  These values dictate what to do when the final object
  looked up is a symlink.  Using PVFS2_LOOKUP_LINK_NO_FOLLOW,
  the symlink object (i.e. handle) will be returned.
  Using PVFS2_LOOKUP_LINK_FOLLOW, the symlink target will be
  continue to be resolved until a non symlink object type
  is resolved -- and this resolved object will be returned
*/
#define PVFS2_LOOKUP_LINK_NO_FOLLOW 0
#define PVFS2_LOOKUP_LINK_FOLLOW    1

PVFS_error PVFS_isys_ref_lookup(
    PVFS_fs_id fs_id,
    char *relative_pathname,
    PVFS_object_ref parent_ref,
    const PVFS_credentials *credentials,
    PVFS_sysresp_lookup * resp,
    int32_t follow_link,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_ref_lookup(
    PVFS_fs_id fs_id,
    char *relative_pathname,
    PVFS_object_ref parent_ref,
    const PVFS_credentials *credentials,
    PVFS_sysresp_lookup * resp,
    int32_t follow_link,
    PVFS_hint hints);

PVFS_error PVFS_sys_lookup(
    PVFS_fs_id fs_id,
    char *name,
    const PVFS_credentials *credentials,
    PVFS_sysresp_lookup * resp,
    int32_t follow_link,
    PVFS_hint hints);

PVFS_error PVFS_isys_getattr(
    PVFS_object_ref ref,
    uint32_t attrmask,
    const PVFS_credentials *credentials,
    PVFS_sysresp_getattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_getattr(
    PVFS_object_ref ref,
    uint32_t attrmask,
    const PVFS_credentials *credentials,
    PVFS_sysresp_getattr *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_setattr(
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_setattr(
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_isys_mkdir(
    char *entry_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sysresp_mkdir *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_mkdir(
    char *entry_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sysresp_mkdir *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credentials *credentials,
    PVFS_sysresp_readdir *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credentials *credentials,
    PVFS_sysresp_readdir *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_readdirplus(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credentials *credentials,
    uint32_t attrmask,
    PVFS_sysresp_readdirplus *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_readdirplus(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credentials *credentials,
    uint32_t attrmask,
    PVFS_sysresp_readdirplus *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_create(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sys_dist *dist,
    PVFS_sys_layout *layout,
    PVFS_sysresp_create *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_create(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sys_dist *dist,
    PVFS_sysresp_create *resp,
    PVFS_sys_layout *layout,
    PVFS_hint hints);

PVFS_error PVFS_isys_remove(
    char *entry_name,
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_remove(
    char *entry_name,
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_isys_rename(
    char *old_entry,
    PVFS_object_ref old_parent_ref,
    char *new_entry,
    PVFS_object_ref new_parent_ref,
    const PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_rename(
    char *old_entry,
    PVFS_object_ref old_parent_ref,
    char *new_entry,
    PVFS_object_ref new_parent_ref,
    const PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_isys_symlink(
    char *entry_name,
    PVFS_object_ref parent_ref,
    char *target,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sysresp_symlink *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_symlink(
    char *entry_name,
    PVFS_object_ref parent_ref,
    char *target,
    PVFS_sys_attr attr,
    const PVFS_credentials *credentials,
    PVFS_sysresp_symlink *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_io(
    PVFS_object_ref ref,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    const PVFS_credentials *credentials,
    PVFS_sysresp_io *resp,
    enum PVFS_io_type type,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

#define PVFS_isys_read(x1,x2,x3,x4,x5,x6,y,x7,x8,x9) \
PVFS_isys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_READ,x7,x8,x9)

#define PVFS_isys_write(x1,x2,x3,x4,x5,x6,y,x7,x8,x9) \
PVFS_isys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_WRITE,x7,x8,x9)

PVFS_error PVFS_sys_io(
    PVFS_object_ref ref,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    const PVFS_credentials *credentials,
    PVFS_sysresp_io *resp,
    enum PVFS_io_type type,
    PVFS_hint hints);

#define PVFS_sys_read(x1,x2,x3,x4,x5,x6,y,z) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_READ,z)

#define PVFS_sys_write(x1,x2,x3,x4,x5,x6,y,z) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_WRITE,z)

PVFS_error PVFS_isys_truncate(
    PVFS_object_ref ref,
    PVFS_size size,
    const PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_truncate(
    PVFS_object_ref ref,
    PVFS_size size,
    const PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_sys_getparent(
    PVFS_fs_id fs_id,
    char *entry_name,
    const PVFS_credentials *credentials,
    PVFS_sysresp_getparent *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_flush(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_flush(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_isys_statfs(
    PVFS_fs_id fs_id,
    const PVFS_credentials *credentials,
    PVFS_sysresp_statfs *statfs,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_statfs(
    PVFS_fs_id fs_id,
    const PVFS_credentials *credentials,
    PVFS_sysresp_statfs *resp,
    PVFS_hint hints);

PVFS_sys_dist* PVFS_sys_dist_lookup(
    const char* dist_identifier);

PVFS_error PVFS_sys_dist_free(
    PVFS_sys_dist* dist);

PVFS_error PVFS_sys_dist_setparam(
    PVFS_sys_dist* dist,
    const char* param,
    void* value);

PVFS_error PVFS_isys_geteattr(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_ds_keyval *key_p,
    PVFS_sysresp_geteattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_geteattr(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_ds_keyval *key_p,
    PVFS_ds_keyval *val_p,
    PVFS_hint hints);

PVFS_error PVFS_isys_geteattr_list(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    int32_t nkey,
    PVFS_ds_keyval *key_p,
    PVFS_sysresp_geteattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_geteattr_list(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    int32_t nkey,
    PVFS_ds_keyval *key_p,
    PVFS_sysresp_geteattr *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_seteattr(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_ds_keyval *key_p,
    PVFS_ds_keyval *val_p,
    int32_t flags,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_seteattr(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_ds_keyval *key_p,
    PVFS_ds_keyval *val_p,
    int32_t flags,
    PVFS_hint hints);

PVFS_error PVFS_isys_seteattr_list(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    int32_t nkey,
    PVFS_ds_keyval *key_array,
    PVFS_ds_keyval *val_array,
    int32_t flags,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_seteattr_list(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    int32_t nkey,
    PVFS_ds_keyval *key_array,
    PVFS_ds_keyval *val_array,
    int32_t flags,
    PVFS_hint hints);

PVFS_error PVFS_isys_deleattr(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_ds_keyval *key_p,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_deleattr(
    PVFS_object_ref ref,
    const PVFS_credentials *credentials,
    PVFS_ds_keyval *key_p,
    PVFS_hint hints);

PVFS_error PVFS_isys_listeattr(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t nkey,
    const PVFS_credentials *credentials,
    PVFS_sysresp_listeattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_listeattr(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t nkey,
    const PVFS_credentials *credentials,
    PVFS_sysresp_listeattr *resp,
    PVFS_hint hints);

PVFS_error PVFS_sys_set_info(
    enum PVFS_sys_setinfo_opt option,
    unsigned int arg);

PVFS_error PVFS_sys_get_info(
    enum PVFS_sys_setinfo_opt option,
    unsigned int* arg);

/* exported test functions for isys calls */

int PVFS_sys_testsome(
    PVFS_sys_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms);

int PVFS_sys_wait(
    PVFS_sys_op_id op_id,
    const char *in_op_str,
    int *out_error);

int PVFS_sys_cancel(PVFS_sys_op_id op_id);

#endif

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  Declarations for the PVFS2 system interface.
 */

/** \defgroup sysint PVFS2 system interface
 *
 *  The PVFS2 system interface provides functionality used for everyday
 *  interaction.  Both the PVFS2 kernel support and the PVFS2 ROMIO
 *  implementation build on this interface.
 *
 * @{
 */

#ifndef __PVFS_SYSINT_H
#define __PVFS_SYSINT_H

#ifndef __KERNEL__
#include <limits.h>
#endif

#include "pvfs2-types.h"
#include "pvfs2-request.h"

/** Holds a non-blocking system interface operation handle. */
typedef PVFS_id_gen_t PVFS_sys_op_id;

/** Describes attributes for a file, directory, or symlink. */
struct PVFS_sys_attr_s
{
    PVFS_uid owner;
    PVFS_gid group;
    PVFS_permissions perms;
    PVFS_time atime;
    PVFS_time mtime;
    PVFS_time ctime;
    PVFS_size size;
    char *link_target; /* NOTE: caller must free if valid */
    int dfile_count;
    PVFS_ds_type objtype;
    uint32_t mask;
};
typedef struct PVFS_sys_attr_s PVFS_sys_attr;

/* Describes active file systems. */
struct PVFS_sys_mntent
{
    char **pvfs_config_servers;	/* addresses of servers with config info */
    int num_pvfs_config_servers;
    char *the_pvfs_config_server; /* first of the entries above that works */
    char *pvfs_fs_name;		/* name of PVFS2 file system */
    enum PVFS_flowproto_type flowproto;	/* flow protocol */
    enum PVFS_encoding_type encoding;   /* wire data encoding */
    /* fs id, filled in by system interface when it looks up the fs */
    PVFS_fs_id fs_id;           

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
    uint64_t directory_version;
    int pvfs_dirent_outcount;
    PVFS_dirent *dirent_array;
};
typedef struct PVFS_sysresp_readdir_s PVFS_sysresp_readdir;

/* truncate */
/* no data returned in truncate response */

struct PVFS_sysresp_statfs_s
{
    PVFS_statfs statfs_buf;
    int server_count;
};
typedef struct PVFS_sysresp_statfs_s PVFS_sysresp_statfs;

struct PVFS_sysresp_getparent_s
{
    PVFS_object_ref parent_ref;
    char basename[PVFS_NAME_MAX];
};
typedef struct PVFS_sysresp_getparent_s PVFS_sysresp_getparent;

/* system interface functions */

int PVFS_sys_initialize(
    uint64_t default_debug_mask);
int PVFS_sys_fs_add(
    struct PVFS_sys_mntent* mntent);
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
    PVFS_credentials *credentials,
    PVFS_sysresp_lookup * resp,
    int follow_link,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_ref_lookup(
    PVFS_fs_id fs_id,
    char *relative_pathname,
    PVFS_object_ref parent_ref,
    PVFS_credentials *credentials,
    PVFS_sysresp_lookup * resp,
    int follow_link);

PVFS_error PVFS_sys_lookup(
    PVFS_fs_id fs_id,
    char *name,
    PVFS_credentials *credentials,
    PVFS_sysresp_lookup * resp,
    int follow_link);

PVFS_error PVFS_isys_getattr(
    PVFS_object_ref ref,
    uint32_t attrmask,
    PVFS_credentials *credentials,
    PVFS_sysresp_getattr *resp,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_getattr(
    PVFS_object_ref ref,
    uint32_t attrmask,
    PVFS_credentials *credentials,
    PVFS_sysresp_getattr *resp);

PVFS_error PVFS_isys_setattr(
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_setattr(
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials);

PVFS_error PVFS_isys_mkdir(
    char *entry_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sysresp_mkdir *resp,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_mkdir(
    char *entry_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sysresp_mkdir *resp);

PVFS_error PVFS_isys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int pvfs_dirent_incount,
    PVFS_credentials *credentials,
    PVFS_sysresp_readdir *resp,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int pvfs_dirent_incount,
    PVFS_credentials *credentials,
    PVFS_sysresp_readdir *resp);

PVFS_error PVFS_isys_create(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sys_dist *dist,
    PVFS_sysresp_create *resp,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_create(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sys_dist *dist,
    PVFS_sysresp_create *resp);

PVFS_error PVFS_isys_remove(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_remove(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_credentials *credentials);

PVFS_error PVFS_isys_rename(
    char *old_entry,
    PVFS_object_ref old_parent_ref,
    char *new_entry,
    PVFS_object_ref new_parent_ref,
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_rename(
    char *old_entry,
    PVFS_object_ref old_parent_ref,
    char *new_entry,
    PVFS_object_ref new_parent_ref,
    PVFS_credentials *credentials);

PVFS_error PVFS_isys_symlink(
    char *entry_name,
    PVFS_object_ref parent_ref,
    char *target,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sysresp_symlink *resp,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_symlink(
    char *entry_name,
    PVFS_object_ref parent_ref,
    char *target,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_sysresp_symlink *resp);

PVFS_error PVFS_isys_io(
    PVFS_object_ref ref,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    PVFS_credentials *credentials,
    PVFS_sysresp_io *resp,
    enum PVFS_io_type type,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_io(
    PVFS_object_ref ref,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    PVFS_credentials *credentials,
    PVFS_sysresp_io *resp,
    enum PVFS_io_type type);

#define PVFS_sys_read(x1,x2,x3,x4,x5,x6,y) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_READ)

#define PVFS_sys_write(x1,x2,x3,x4,x5,x6,y) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_WRITE)

PVFS_error PVFS_isys_truncate(
    PVFS_object_ref ref,
    PVFS_size size,
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_truncate(
    PVFS_object_ref ref,
    PVFS_size size,
    PVFS_credentials *credentials);

PVFS_error PVFS_sys_getparent(
    PVFS_fs_id fs_id,
    char *entry_name,
    PVFS_credentials *credentials,
    PVFS_sysresp_getparent *resp);

PVFS_error PVFS_isys_flush(
    PVFS_object_ref ref,
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_sys_flush(
    PVFS_object_ref ref,
    PVFS_credentials *credentials);

PVFS_error PVFS_sys_statfs(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_sysresp_statfs *resp);

PVFS_sys_dist* PVFS_sys_dist_lookup(
    const char* dist_identifier);

PVFS_error PVFS_sys_dist_free(
    PVFS_sys_dist* dist);

PVFS_error PVFS_sys_dist_setparam(
    PVFS_sys_dist* dist,
    const char* param,
    void* value);

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

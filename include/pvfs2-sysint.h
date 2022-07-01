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

/** thus must matche the def of PVFS_object_attrmask in src/proto/pvfs2-attr.h */
typedef uint64_t PVFS_system_attrmask;

/* I don't think this struct needs all of this alignment stuff.
 * This struct is never sent or received over the wire - it is
 * copied to/from an object attr struct, which isn't even aligned
 * since it is encoded in an aligned way before/after transmission.
 * The only reason I can think to align this is to avoid misaligned
 * access at runtime, but this struct isn't used all that much, other
 * than the afore mentioned copying, and besides the compiler knows
 * how to do alignment if it is needed.  All the PVFS2_ALIGN_VAR
 * stuff should probably come out.
 */

/** This is a sys_attr because it is visible to user (programmer).  
 * It is NOT sent between the client and serverx but only from the
 * system interface (user, PVFS_sys_...) to the client library.
 * It is converted to an object attr before being sent to the server.
 * Mask is 64bit for now. **/

/** This struct is passed to sys_create, sys_mkdir, sys_getattr, and sys_setattr.
 * Different fields make sense at different times depending on where it is going.
 * For example, it makes no sense to try to set file size, theonly thing you can
 * ever go is Get it.  Object type can be set wheen you create the object (though
 * it is implicit in the call) and you can get it later, but you cannot change it
 * after creating.  And owner, can be set a create time, reset later, and read for
 * display.  Values passed at the wrong time are to be ignored.
 */
/** Describes attributes for a file, directory, or symlink. */
struct PVFS_sys_attr_s
{
    /* commoon */
    PVFS_ds_type objtype; /* CG */
    PVFS_flags flags;     /* CSG */
    PVFS_system_attrmask mask;
    PVFS_uid owner;       /* CSG */
    PVFS_gid group;       /* CSG */
    PVFS2_ALIGN_VAR(PVFS_permissions, perms);       /* CSG */
    PVFS_time atime;      /* CSG */
    PVFS_time mtime;      /* CSG */
    PVFS_time ctime;      /* CSG */
    PVFS_time ntime;      /* CSG */
    /* link */
    PVFS2_ALIGN_VAR(char *, link_target);/**< NOTE: caller must free if valid */ /* CG */
    /* file */
    /* all file attributes are directory hints when objtype is directory */
    PVFS_size size; /* this is where file size is returned */                    /* G */
    PVFS2_ALIGN_VAR(int32_t, dfile_count);                                       /* CG */
    PVFS2_ALIGN_VAR(char *, dist_name);  /**< NOTE: caller must free if valid */ /* CG */
    PVFS2_ALIGN_VAR(char *, dist_params);/**< NOTE: caller must free if valid */ /* CG */
    PVFS_size blksize; /* ???? Is this used anywhere ???? */
    PVFS2_ALIGN_VAR(uint32_t, mirror_copies_count); /* dfile SID count */        /* CG */
    uint32_t stuffed; /* probably obsolete */
    /* directory */
    PVFS_size dirent_count;                                  /* G */
    /* these describe the dir target */
    PVFS2_ALIGN_VAR(int32_t, distr_dir_servers_initial);     /* CSG */
    PVFS2_ALIGN_VAR(int32_t, distr_dir_servers_max);         /* CSG */
    PVFS2_ALIGN_VAR(int32_t, distr_dir_split_size);          /* CSG */
    /* these are hints for subdirs */
    PVFS2_ALIGN_VAR(int32_t, hint_distr_dir_servers_initial);/* CSG */
    PVFS2_ALIGN_VAR(int32_t, hint_distr_dir_servers_max);    /* CSG */
    PVFS2_ALIGN_VAR(int32_t, hint_distr_dir_split_size);     /* CSG */
};
typedef struct PVFS_sys_attr_s PVFS_sys_attr;

/* this helper function assumes attr is a pointer to a PVFS_sys_attr
 * struct.  It first frees any of the internal pointers (link target
 * dist_name, dist_param et. al. then then frees main struct.  Any
 * modifications to this struct should be checked against the code
 * for this function.
 */
PVFS_error PVFS_sys_attr_free(PVFS_sys_attr *attr);

/** Describes a PVFS2 file system. */
struct PVFS_sys_mntent
{
    char **pvfs_config_servers;         /**< addresses of servers with config info */
    int32_t num_pvfs_config_servers;
    char *the_pvfs_config_server;       /**< first of the entries above that works */
    char *pvfs_fs_name;                 /**< name of PVFS2 file system */
    enum PVFS_flowproto_type flowproto;	/**< flow protocol */
    enum PVFS_encoding_type encoding;   /**< wire data encoding */
    PVFS_fs_id fs_id;                   /**< fs id, filled in by system interface
                                         *   when it looks up the fs */
    int32_t default_num_dfiles;         /**< Default number of dfiles mount option value */
    char *bmi_opts;                     /**< Comma-separated list of BMI options */
    int32_t integrity_check;            /**< Check to determine whether the mount process
                                         *   must perform the integrity checks on the
                                         *   config files */
    /* the following fields are included for convenience;
     * useful if the file system is "mounted" */
    char *mnt_dir;                      /**< local mount path */
    char *mnt_opts;                     /**< full option list */
};

/** Describes file distribution parameters. */
struct PVFS_sys_dist_s
{
    char *name;
    void *params;
};
typedef struct PVFS_sys_dist_s PVFS_sys_dist;

/**********************************************************************/
/* Structures that Hold the results of various system interface calls */
/**********************************************************************/

/** Holds results of a lookup operation (reference to object). */
/*  if error_path is passed in NULL then nothing returned on error */
/*  otherwise up to error_path_size chars of unresolved path */
/*  segments are passed out in null terminated string */
struct PVFS_sysresp_lookup_s
{
    PVFS_object_ref ref;
    char           *error_path;       /* on error, the unresolved path segments */
    int             error_path_size;  /* size of the buffer provided by the user */
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
    PVFS_dirent     *dirent_array;
    uint32_t         pvfs_dirent_outcount;
    uint64_t         directory_version;
};
typedef struct PVFS_sysresp_readdir_s PVFS_sysresp_readdir;

/** Holds results of a readdirplus operation (position token,
 * directory version information, array of directory entries,
 * array of stat error codes and array of attribute information).
 */
struct PVFS_sysresp_readdirplus_s
{
    PVFS_ds_position token;
    PVFS_dirent     *dirent_array;
    uint32_t         pvfs_dirent_outcount;
    uint64_t         directory_version;
    PVFS_error      *stat_err_array; 
    PVFS_sys_attr   *attr_array;
};
typedef struct PVFS_sysresp_readdirplus_s PVFS_sysresp_readdirplus;


/* truncate */
/* no data returned in truncate response */

struct PVFS_sysresp_statfs_s
{
    PVFS_statfs statfs_buf;
    int32_t     server_count;
};
typedef struct PVFS_sysresp_statfs_s PVFS_sysresp_statfs;

struct PVFS_sysresp_getparent_s
{
    PVFS_object_ref parent_ref;
    char            basename[PVFS_NAME_MAX];
};
typedef struct PVFS_sysresp_getparent_s PVFS_sysresp_getparent;

/** Holds results of geteattr_list operation (attributes of object). */
struct PVFS_sysresp_geteattr_s
{
    PVFS_ds_keyval *val_array;
    PVFS_error     *err_array;
};
typedef struct PVFS_sysresp_geteattr_s PVFS_sysresp_geteattr;

/* seteattr */
/* no data returned in seteattr response */

/* atomiceattr */
struct PVFS_sysresp_atomiceattr_s
{
    int             nkey;
    PVFS_error     *err_array;
    PVFS_ds_keyval *val_array;
};
typedef struct PVFS_sysresp_atomiceattr_s PVFS_sysresp_atomiceattr;

/* deleattr */
/* no data returned in deleattr response */

/** Holds results of a listeattr_list operation (keys of object). */
struct PVFS_sysresp_listeattr_s
{
    PVFS_ds_position token;
    int32_t          nkey;
    PVFS_ds_keyval  *key_array;
};
typedef struct PVFS_sysresp_listeattr_s PVFS_sysresp_listeattr;


/****************************************/
/* system interface function prototypes */
/****************************************/

int PVFS_sys_initialize(PVFS_debug_mask default_debug_mask);

int PVFS_sys_fs_add(struct PVFS_sys_mntent *mntent);

int PVFS_isys_fs_add(struct PVFS_sys_mntent *mntent,
                     PVFS_sys_op_id *op_id,
                     void *user_ptr);

int PVFS_sys_fs_remove(struct PVFS_sys_mntent *mntent);

int PVFS_sys_finalize(void);

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
    const PVFS_credential *credential,
    PVFS_sysresp_lookup *resp,
    int32_t follow_link,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_ref_lookup(
    PVFS_fs_id fs_id,
    char *relative_pathname,
    PVFS_object_ref parent_ref,
    const PVFS_credential *credential,
    PVFS_sysresp_lookup *resp,
    int32_t follow_link,
    PVFS_hint hints);

PVFS_error PVFS_sys_lookup(
    PVFS_fs_id fs_id,
    char *name,
    const PVFS_credential *credential,
    PVFS_sysresp_lookup *resp,
    int32_t follow_link,
    PVFS_hint hints);

/** sysint interfaces for getattr mask is 32bit for now **/
PVFS_error PVFS_isys_getattr(
    PVFS_object_ref ref,
    PVFS_system_attrmask attrmask,
    const PVFS_credential *credential,
    PVFS_sysresp_getattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_getattr(
    PVFS_object_ref ref,
    PVFS_system_attrmask attrmask,
    const PVFS_credential *credential,
    PVFS_sysresp_getattr *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_setattr(
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_setattr(
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_isys_mkdir(
    char *entry_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
    PVFS_sysresp_mkdir *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_mkdir(
    char *entry_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
    PVFS_sysresp_mkdir *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credential *credential,
    PVFS_sysresp_readdir *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credential *credential,
    PVFS_sysresp_readdir *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_readdirplus(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credential *credential,
    PVFS_system_attrmask attrmask,
    PVFS_sysresp_readdirplus *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_readdirplus(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credential *credential,
    PVFS_system_attrmask attrmask,
    PVFS_sysresp_readdirplus *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_create(
    char *entry_name,
    PVFS_object_ref ref,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
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
    const PVFS_credential *credential,
    PVFS_sys_dist *dist,
    PVFS_sysresp_create *resp,
    PVFS_sys_layout *layout,
    PVFS_hint hints);

PVFS_error PVFS_isys_remove(
    char *entry_name,
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_remove(
    char *entry_name,
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_isys_rename(
    char *old_entry,
    PVFS_object_ref old_parent_ref,
    char *new_entry,
    PVFS_object_ref new_parent_ref,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_rename(
    char *old_entry,
    PVFS_object_ref old_parent_ref,
    char *new_entry,
    PVFS_object_ref new_parent_ref,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_isys_symlink(
    char *entry_name,
    PVFS_object_ref parent_ref,
    char *target,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
    PVFS_sysresp_symlink *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_symlink(
    char *entry_name,
    PVFS_object_ref parent_ref,
    char *target,
    PVFS_sys_attr attr,
    const PVFS_credential *credential,
    PVFS_sysresp_symlink *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_io(
    PVFS_object_ref ref,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    const PVFS_credential *credential,
    PVFS_sysresp_io *resp,
    enum PVFS_io_type type,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

/** Macro for convenience read is a call to io */
#define PVFS_isys_read(x1,x2,x3,x4,x5,x6,y,x7,x8,x9) \
PVFS_isys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_READ,x7,x8,x9)

/** Macro for convenience write is a call to io */
#define PVFS_isys_write(x1,x2,x3,x4,x5,x6,y,x7,x8,x9) \
PVFS_isys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_WRITE,x7,x8,x9)

PVFS_error PVFS_sys_io(
    PVFS_object_ref ref,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    const PVFS_credential *credential,
    PVFS_sysresp_io *resp,
    enum PVFS_io_type type,
    PVFS_hint hints);

/** Macro for convenience read is a call to io */
#define PVFS_sys_read(x1,x2,x3,x4,x5,x6,y,z) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_READ,z)

/** Macro for convenience write is a call to io */
#define PVFS_sys_write(x1,x2,x3,x4,x5,x6,y,z) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_WRITE,z)

PVFS_error PVFS_isys_truncate(
    PVFS_object_ref ref,
    PVFS_size size,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_truncate(
    PVFS_object_ref ref,
    PVFS_size size,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_sys_getparent(
    PVFS_fs_id fs_id,
    char *entry_name,
    const PVFS_credential *credential,
    PVFS_sysresp_getparent *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_flush(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_flush(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_isys_statfs(
    PVFS_fs_id fs_id,
    const PVFS_credential *credential,
    PVFS_sysresp_statfs *statfs,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_statfs(
    PVFS_fs_id fs_id,
    const PVFS_credential *credential,
    PVFS_sysresp_statfs *resp,
    PVFS_hint hints);

PVFS_sys_dist *PVFS_sys_dist_lookup(
    const char *dist_identifier);

PVFS_error PVFS_sys_dist_free(
    PVFS_sys_dist *dist);

PVFS_error PVFS_sys_dist_setparam(
    PVFS_sys_dist *dist,
    const char *param,
    void *value);

PVFS_error PVFS_dist_pv_pairs_extract_and_add(
    const char * pv_pairs,
    void * dist);

PVFS_error PVFS_dist_pv_pair_split(
    const char * pv_pair,
    void *dist);

PVFS_error PVFS_isys_geteattr(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_ds_keyval *key_p,
    PVFS_sysresp_geteattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_geteattr(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_ds_keyval *key_p,
    PVFS_ds_keyval *val_p,
    PVFS_hint hints);

PVFS_error PVFS_isys_geteattr_list(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    int32_t nkey,
    PVFS_ds_keyval *key_p,
    PVFS_sysresp_geteattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_geteattr_list(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    int32_t nkey,
    PVFS_ds_keyval *key_p,
    PVFS_sysresp_geteattr *resp,
    PVFS_hint hints);

PVFS_error PVFS_isys_seteattr(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_ds_keyval *key_p,
    PVFS_ds_keyval *val_p,
    int32_t flags,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_seteattr(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_ds_keyval *key_p,
    PVFS_ds_keyval *val_p,
    int32_t flags,
    PVFS_hint hints);

PVFS_error PVFS_isys_seteattr_list(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    int32_t nkey,
    PVFS_ds_keyval *key_array,
    PVFS_ds_keyval *val_array,
    int32_t flags,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_seteattr_list(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    int32_t nkey,
    PVFS_ds_keyval *key_array,
    PVFS_ds_keyval *val_array,
    int32_t flags,
    PVFS_hint hints);

PVFS_error PVFS_sys_atomiceattr(
        PVFS_object_ref ref,
        const PVFS_credential *credential,
        PVFS_ds_keyval *key_p,
        PVFS_ds_keyval *old_val_p,
        PVFS_ds_keyval *new_val_p,
        PVFS_sysresp_atomiceattr *resp_p,
        int32_t flags,
        int32_t opcode,
        PVFS_hint hints);

PVFS_error PVFS_isys_atomiceattr_list(
        PVFS_object_ref ref,
        const PVFS_credential *credential,
        int32_t nkey,
        PVFS_ds_keyval *key_array,
        PVFS_ds_keyval *old_val_array,
        PVFS_ds_keyval *new_val_array,
        PVFS_sysresp_atomiceattr *resp_p,
        int32_t flags,
        PVFS_sys_op_id *op_id,
        int32_t opcode,
        PVFS_hint hints,
        void *user_ptr);

PVFS_error PVFS_sys_atomiceattr_list(
        PVFS_object_ref ref,
        const PVFS_credential *credential,
        int32_t nkey,
        PVFS_ds_keyval *key_array,
        PVFS_ds_keyval *old_val_array,
        PVFS_ds_keyval *new_val_array,
        PVFS_sysresp_atomiceattr *resp_p,
        int32_t flags,
        int32_t opcode,
        PVFS_hint hints);

PVFS_error PVFS_isys_deleattr(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_ds_keyval *key_p,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_deleattr(
    PVFS_object_ref ref,
    const PVFS_credential *credential,
    PVFS_ds_keyval *key_p,
    PVFS_hint hints);

PVFS_error PVFS_isys_listeattr(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t nkey,
    const PVFS_credential *credential,
    PVFS_sysresp_listeattr *resp,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_sys_listeattr(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t nkey,
    const PVFS_credential *credential,
    PVFS_sysresp_listeattr *resp,
    PVFS_hint hints);

PVFS_error PVFS_sys_set_info(
    enum PVFS_sys_setinfo_opt option,
    unsigned int arg);

PVFS_error PVFS_sys_get_info(
    enum PVFS_sys_setinfo_opt option,
    unsigned int *arg);

/* exported test functions for isys calls */
int PVFS_sys_testany(
    PVFS_sys_op_id *op_id_array,
    int *op_count,
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms);

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

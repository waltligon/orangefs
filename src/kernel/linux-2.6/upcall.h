/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __UPCALL_H
#define __UPCALL_H

#include "pvfs2-sysint.h"

/* Sanitized this header file to fix
 * 32-64 bit interaction issues between
 * client-core and device
 */
typedef struct
{
    int32_t async_vfs_io;
    int32_t buf_index;
    int32_t count;
    int32_t __pad1;
    int64_t offset;
    PVFS_object_ref refn;
    enum PVFS_io_type io_type;
    int32_t readahead_size;
} pvfs2_io_request_t;

typedef struct
{
    int32_t buf_index;
    int32_t count;
    PVFS_object_ref refn;
    enum PVFS_io_type io_type;
    int32_t __pad1;
} pvfs2_iox_request_t;

typedef struct
{
    int32_t sym_follow;
    int32_t __pad1;
    PVFS_object_ref parent_refn;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_lookup_request_t;

typedef struct
{
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attributes;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_create_request_t;

typedef struct
{
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attributes;
    char entry_name[PVFS2_NAME_LEN];
    char target[PVFS2_NAME_LEN];
} pvfs2_symlink_request_t;

typedef struct
{
    PVFS_object_ref refn;
    uint32_t        mask;
    uint32_t        __pad1;
} pvfs2_getattr_request_t;

typedef struct
{
    PVFS_object_ref refn;
    PVFS_sys_attr attributes;
} pvfs2_setattr_request_t;

typedef struct
{
    PVFS_object_ref parent_refn;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_remove_request_t;

typedef struct
{
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attributes;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_mkdir_request_t;

typedef struct
{
    PVFS_object_ref refn;
    PVFS_ds_position token;
    int32_t max_dirent_count;
    int32_t buf_index;
} pvfs2_readdir_request_t;

typedef struct
{
    PVFS_object_ref refn;
    PVFS_ds_position token;
    int32_t max_dirent_count;
    uint32_t mask;
    int32_t  buf_index;
    int32_t  __pad1;
} pvfs2_readdirplus_request_t;

typedef struct
{
    PVFS_object_ref old_parent_refn;
    PVFS_object_ref new_parent_refn;
    char d_old_name[PVFS2_NAME_LEN];
    char d_new_name[PVFS2_NAME_LEN];
} pvfs2_rename_request_t;

typedef struct
{
    PVFS_fs_id fs_id;
    int32_t    __pad1;
} pvfs2_statfs_request_t;

typedef struct
{
    PVFS_object_ref refn;
    PVFS_size size;
} pvfs2_truncate_request_t;

typedef struct
{
    PVFS_object_ref refn;
} pvfs2_mmap_ra_cache_flush_request_t;

typedef struct
{
    char pvfs2_config_server[PVFS_MAX_SERVER_ADDR_LEN];
} pvfs2_fs_mount_request_t;

typedef struct
{
    int32_t id;
    PVFS_fs_id fs_id;
    char pvfs2_config_server[PVFS_MAX_SERVER_ADDR_LEN];
} pvfs2_fs_umount_request_t;

typedef struct 
{
    PVFS_object_ref refn;
    int32_t key_sz;
    int32_t __pad1;
    char key[PVFS_MAX_XATTR_NAMELEN];
} pvfs2_getxattr_request_t;

typedef struct
{
    PVFS_object_ref refn;
    PVFS_keyval_pair keyval;
    int32_t   flags;
    int32_t   __pad1;
} pvfs2_setxattr_request_t;

typedef struct 
{
    PVFS_object_ref refn;
    int32_t  requested_count;
    int32_t  __pad1;
    PVFS_ds_position token;
} pvfs2_listxattr_request_t;

typedef struct 
{
    PVFS_object_ref refn;
    int32_t key_sz;
    int32_t __pad1;
    char key[PVFS_MAX_XATTR_NAMELEN];
} pvfs2_removexattr_request_t;

typedef struct
{
    uint64_t op_tag;
} pvfs2_op_cancel_t;

typedef struct
{
    PVFS_object_ref refn;
} pvfs2_fsync_request_t;

enum pvfs2_param_request_type
{   
    PVFS2_PARAM_REQUEST_SET = 1,
    PVFS2_PARAM_REQUEST_GET = 2
};  
    
enum pvfs2_param_request_op
{   
    PVFS2_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS = 1,
    PVFS2_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT = 2,
    PVFS2_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT = 3,
    PVFS2_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE = 4,
    PVFS2_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS = 5,
    PVFS2_PARAM_REQUEST_OP_PERF_HISTORY_SIZE = 6,
    PVFS2_PARAM_REQUEST_OP_PERF_RESET = 7,
    PVFS2_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS = 8,
    PVFS2_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT = 9,
    PVFS2_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT = 10,
    PVFS2_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE = 11,
    PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_TIMEOUT_MSECS = 12,
    PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_HARD_LIMIT = 13,
    PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_SOFT_LIMIT = 14,
    PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_RECLAIM_PERCENTAGE = 15,
};  
    
typedef struct
{
    enum pvfs2_param_request_type type;
    enum pvfs2_param_request_op op;
    int64_t value;
} pvfs2_param_request_t;

enum pvfs2_perf_count_request_type
{
    PVFS2_PERF_COUNT_REQUEST_ACACHE = 1,
    PVFS2_PERF_COUNT_REQUEST_NCACHE = 2,
    PVFS2_PERF_COUNT_REQUEST_STATIC_ACACHE = 3,
};
typedef struct
{
    enum pvfs2_perf_count_request_type type;
    int32_t __pad1;
} pvfs2_perf_count_request_t;

typedef struct
{
    PVFS_fs_id fsid;
    int32_t    __pad1;
} pvfs2_fs_key_request_t;

typedef struct
{
    int32_t type;
    int32_t __pad1;
    PVFS_credentials credentials;
    int pid;
    int tgid;
    /* currently trailer is used only by readx/writex (iox) */
    PVFS_size  trailer_size;
    PVFS2_ALIGN_VAR(char *, trailer_buf);

    union
    {
	pvfs2_io_request_t io;
        pvfs2_iox_request_t iox;
	pvfs2_lookup_request_t lookup;
	pvfs2_create_request_t create;
	pvfs2_symlink_request_t sym;
	pvfs2_getattr_request_t getattr;
	pvfs2_setattr_request_t setattr;
	pvfs2_remove_request_t remove;
	pvfs2_mkdir_request_t mkdir;
	pvfs2_readdir_request_t readdir;
	pvfs2_readdirplus_request_t readdirplus;
	pvfs2_rename_request_t rename;
        pvfs2_statfs_request_t statfs;
        pvfs2_truncate_request_t truncate;
        pvfs2_mmap_ra_cache_flush_request_t ra_cache_flush;
        pvfs2_fs_mount_request_t fs_mount;
        pvfs2_fs_umount_request_t fs_umount;
        pvfs2_getxattr_request_t getxattr;
        pvfs2_setxattr_request_t setxattr;
        pvfs2_listxattr_request_t listxattr;
        pvfs2_removexattr_request_t removexattr;
        pvfs2_op_cancel_t cancel;
        pvfs2_fsync_request_t fsync;
        pvfs2_param_request_t param;
        pvfs2_perf_count_request_t perf_count;
        pvfs2_fs_key_request_t fs_key;
    } req;
} pvfs2_upcall_t;

#endif /* __UPCALL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

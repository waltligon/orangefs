/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Definitions of downcalls used in Linux kernel module.
 */

/* TODO: we might want to try to avoid this inclusion  */
#include "pvfs2-sysint.h"

#ifndef __DOWNCALL_H
#define __DOWNCALL_H

/*
 * Sanitized the device-client core interaction
 * for clean 32-64 bit usage
 */
typedef struct
{
    int64_t amt_complete; 
} pvfs2_io_response_t;

typedef struct
{
    int64_t amt_complete; 
} pvfs2_iox_response_t;

typedef struct
{
    PVFS_object_ref refn;
} pvfs2_lookup_response_t;

typedef struct
{
    PVFS_object_ref refn;
} pvfs2_create_response_t;

typedef struct
{
    PVFS_object_ref refn;
} pvfs2_symlink_response_t;

typedef struct
{
    PVFS_sys_attr attributes;
    char link_target[PVFS2_NAME_LEN];
} pvfs2_getattr_response_t;

/* the setattr response is a blank downcall */
typedef struct
{
} pvfs2_setattr_response_t;

/* the remove response is a blank downcall */
typedef struct
{
} pvfs2_remove_response_t;

typedef struct
{
    PVFS_object_ref refn;
} pvfs2_mkdir_response_t;

/* duplication of some system interface structures so that I don't have to allocate extra memory */
struct pvfs2_dirent
{
    char *d_name;
    int   d_length;
    PVFS_handle handle;
};

/* the readdir response is a blank downcall (i.e. all these fields are obtained from the trailer) */
typedef struct
{
    PVFS_ds_position token;
    uint64_t directory_version;
    uint32_t  __pad2;
    uint32_t pvfs_dirent_outcount;
    struct pvfs2_dirent *dirent_array;
} pvfs2_readdir_response_t;

/* the readdirplus response is a blank downcall (i.e. all these fields are obtained from the trailer). Do not change the order of the fields. Change it only if readdir_response_t is changing */
typedef struct
{
    PVFS_ds_position token;
    uint64_t directory_version;
    uint32_t  __pad2;
    uint32_t pvfs_dirent_outcount;
    struct pvfs2_dirent *dirent_array;
    PVFS_error  *stat_err_array;
    PVFS_sys_attr *attr_array;
} pvfs2_readdirplus_response_t;

/* the rename response is a blank downcall */
typedef struct
{
} pvfs2_rename_response_t;

typedef struct
{
    int64_t block_size;
    int64_t blocks_total;
    int64_t blocks_avail;
    int64_t files_total;
    int64_t files_avail;
} pvfs2_statfs_response_t;

/* the truncate response is a blank downcall */
typedef struct
{
} pvfs2_truncate_response_t;

typedef struct
{
    PVFS_fs_id fs_id;
    int32_t id;
    PVFS_handle root_handle;
} pvfs2_fs_mount_response_t;

/* the umount response is a blank downcall */
typedef struct
{
} pvfs2_fs_umount_response_t;

/* the getxattr response is the attribute value */

typedef struct {
    int32_t val_sz;
    int32_t __pad1;
    char val[PVFS_MAX_XATTR_VALUELEN];
} pvfs2_getxattr_response_t;

/* the setxattr response is a blank downcall */

typedef struct {
} pvfs2_setxattr_response_t;

/* the listxattr response is an array of attribute names */

typedef struct {
    int32_t  returned_count;
    int32_t __pad1;
    PVFS_ds_position token;
    char key[PVFS_MAX_XATTR_LISTLEN*PVFS_MAX_XATTR_NAMELEN];
    int32_t  keylen;
    int32_t  __pad2;
    int32_t  lengths[PVFS_MAX_XATTR_LISTLEN];
} pvfs2_listxattr_response_t;
/* the removexattr response is a blank downcall */

typedef struct {
} pvfs2_removexattr_response_t;


/* the cancel response is a blank downcall */
typedef struct
{
} pvfs2_cancel_response_t;

/* the fsync response is a blank downcall */
typedef struct
{
} pvfs2_fsync_response_t;

typedef struct
{
    int64_t value;
} pvfs2_param_response_t;

#define PERF_COUNT_BUF_SIZE 4096
typedef struct
{
    char buffer[PERF_COUNT_BUF_SIZE];
} pvfs2_perf_count_response_t;

#define FS_KEY_BUF_SIZE 4096
typedef struct
{
    int32_t fs_keylen;
    int32_t __pad1;
    char    fs_key[FS_KEY_BUF_SIZE];
} pvfs2_fs_key_response_t;

typedef struct
{
    int32_t type;
    PVFS_error status;
    /* currently trailer is used only by readdir and readdirplus */
    PVFS_size  trailer_size;
    PVFS2_ALIGN_VAR(char *, trailer_buf);

    union
    {
	pvfs2_io_response_t io;
        pvfs2_iox_response_t iox;
	pvfs2_lookup_response_t lookup;
	pvfs2_create_response_t create;
	pvfs2_symlink_response_t sym;
	pvfs2_getattr_response_t getattr;
/* 	pvfs2_setattr_response_t setattr; */
/*      pvfs2_remove_response_t remove; */
	pvfs2_mkdir_response_t mkdir;
/*	pvfs2_readdir_response_t readdir; */
/*	pvfs2_readdirplus_response_t readdirplus; */
/*      pvfs2_rename_response_t rename; */
	pvfs2_statfs_response_t statfs;
/* 	pvfs2_truncate_response_t truncate; */
        pvfs2_fs_mount_response_t fs_mount;
/* 	pvfs2_fs_umount_response_t fs_umount; */
        pvfs2_getxattr_response_t getxattr;
/*      pvfs2_setxattr_response_t setxattr */
        pvfs2_listxattr_response_t listxattr;
/*      pvfs2_removexattr_response_t removexattr; */
/* 	pvfs2_cancel_response_t cancel; */
/* 	pvfs2_fsync_response_t fsync; */
        pvfs2_param_response_t param;
        pvfs2_perf_count_response_t perf_count;
        pvfs2_fs_key_response_t fs_key;
    } resp;
} pvfs2_downcall_t;

#endif /* __DOWNCALL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

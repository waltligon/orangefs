/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __UPCALL_H
#define __UPCALL_H

#include "pvfs2-sysint.h"

#define PVFS2_MAX_MOUNT_OPT_LEN        0x00000080

typedef struct
{
    int buf_index;
    size_t count;
    loff_t offset;
    PVFS_object_ref refn;
    enum PVFS_io_type io_type;
    loff_t readahead_size;
} pvfs2_io_request_t;

typedef struct
{
    int sym_follow;
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
    char entry_name[PVFS2_NAME_LEN];
    PVFS_object_ref parent_refn;
    char target[PVFS2_NAME_LEN];
    PVFS_sys_attr attributes;
} pvfs2_symlink_request_t;

typedef struct
{
    PVFS_object_ref refn;
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
    int max_dirent_count;
} pvfs2_readdir_request_t;

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
    char pvfs2_config_server[PVFS_MAX_SERVER_ADDR_LEN];
    int id;
    PVFS_fs_id fs_id;
} pvfs2_fs_umount_request_t;

typedef struct
{
    int type;
    PVFS_credentials credentials;

    union
    {
	pvfs2_io_request_t io;
	pvfs2_lookup_request_t lookup;
	pvfs2_create_request_t create;
	pvfs2_symlink_request_t sym;
	pvfs2_getattr_request_t getattr;
	pvfs2_setattr_request_t setattr;
	pvfs2_remove_request_t remove;
	pvfs2_mkdir_request_t mkdir;
	pvfs2_readdir_request_t readdir;
	pvfs2_rename_request_t rename;
        pvfs2_statfs_request_t statfs;
        pvfs2_truncate_request_t truncate;
        pvfs2_mmap_ra_cache_flush_request_t ra_cache_flush;
        pvfs2_fs_mount_request_t fs_mount;
        pvfs2_fs_umount_request_t fs_umount;
    } req;
} pvfs2_upcall_t;

#endif /* __UPCALL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

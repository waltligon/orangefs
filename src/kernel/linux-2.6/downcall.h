/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* TODO: we might want to try to avoid this inclusion  */
#include "pvfs2-sysint.h"

#ifndef __DOWNCALL_H
#define __DOWNCALL_H

typedef struct
{
    size_t amt_complete;
} pvfs2_io_response_t;

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

typedef struct
{
    int dirent_count;
    PVFS_object_ref refn[MAX_DIRENT_COUNT];
    char d_name[MAX_DIRENT_COUNT][PVFS2_NAME_LEN];
    int d_name_len[MAX_DIRENT_COUNT];
    PVFS_ds_position token;
} pvfs2_readdir_response_t;

/* the rename response is a blank downcall */
typedef struct
{
} pvfs2_rename_response_t;

typedef struct
{
    long block_size;
    long blocks_total;
    long blocks_avail;
    long files_total;
    long files_avail;
} pvfs2_statfs_response_t;

/* the truncate response is a blank downcall */
typedef struct
{
} pvfs2_truncate_response_t;

typedef struct
{
    PVFS_fs_id fs_id;
    PVFS_handle root_handle;
} pvfs2_fs_mount_response_t;

typedef struct
{
    int type;
    int status;

    union
    {
	pvfs2_io_response_t io;
	pvfs2_lookup_response_t lookup;
	pvfs2_create_response_t create;
	pvfs2_symlink_response_t sym;
	pvfs2_getattr_response_t getattr;
/* 	pvfs2_setattr_response_t setattr; */
/*      pvfs2_remove_response_t remove; */
	pvfs2_mkdir_response_t mkdir;
	pvfs2_readdir_response_t readdir;
/*      pvfs2_rename_response_t rename; */
	pvfs2_statfs_response_t statfs;
/* 	pvfs2_truncate_response_t truncate; */
        pvfs2_fs_mount_response_t fs_mount;
    } resp;
} pvfs2_downcall_t;



#endif /* __DOWNCALL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

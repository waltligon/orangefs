#ifndef __UPCALL_H
#define __UPCALL_H

typedef struct
{
    char *buf;
    size_t count;
    loff_t *offset;
} pvfs2_read_request_t;

typedef struct
{
    char *buf;
    size_t count;
    loff_t *offset;
    struct qstr *d_name;
} pvfs2_write_request_t;

typedef struct
{
    PVFS_pinode_reference parent_refn;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_lookup_request_t;

typedef struct
{
    PVFS_pinode_reference parent_refn;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_create_request_t;

typedef struct
{
    PVFS_pinode_reference refn;
} pvfs2_getattr_request_t;

typedef struct
{
    PVFS_pinode_reference parent_refn;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_remove_request_t;

typedef struct
{
    PVFS_pinode_reference parent_refn;
    char d_name[PVFS2_NAME_LEN];
} pvfs2_mkdir_request_t;

typedef struct
{
    PVFS_pinode_reference refn;
    PVFS_ds_position token;
    int max_dirent_count;
} pvfs2_readdir_request_t;


typedef struct
{
    int type;

    union
    {
        pvfs2_read_request_t read;
        pvfs2_write_request_t write;
        pvfs2_lookup_request_t lookup;
        pvfs2_create_request_t create;
        pvfs2_getattr_request_t getattr;
        pvfs2_remove_request_t remove;
        pvfs2_mkdir_request_t mkdir;
        pvfs2_readdir_request_t readdir;
    } req;
} pvfs2_upcall_t;

#endif /* __UPCALL_H */

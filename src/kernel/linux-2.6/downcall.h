
/* TODO: we might want to try to avoid this inclusion  */
#include "pvfs2-sysint.h"

#ifndef __DOWNCALL_H
#define __DOWNCALL_H

typedef struct
{
    int stuff;
} pvfs2_read_response_t;

typedef struct
{
    int stuff;
} pvfs2_write_response_t;

typedef struct
{
    PVFS_pinode_reference refn;
} pvfs2_lookup_response_t;

typedef struct
{
    PVFS_pinode_reference refn;
} pvfs2_create_response_t;

typedef struct
{
    PVFS_sys_attr attributes;
} pvfs2_getattr_response_t;

/* the remove response is a blank downcall */
typedef struct
{
} pvfs2_remove_response_t;

typedef struct
{
    PVFS_pinode_reference refn;
} pvfs2_mkdir_response_t;

typedef struct
{
    int dirent_count;
    PVFS_pinode_reference refn[MAX_DIRENT_COUNT];
    char d_name[MAX_DIRENT_COUNT][PVFS2_NAME_LEN];
    int d_name_len[MAX_DIRENT_COUNT];
    PVFS_ds_position token;
} pvfs2_readdir_response_t;

typedef struct
{
    int type;
    int status;

    union
    {
        pvfs2_read_response_t read;
        pvfs2_write_response_t write;
        pvfs2_lookup_response_t lookup;
        pvfs2_create_response_t create;
        pvfs2_getattr_response_t getattr;
/*         pvfs2_remove_response_t remove; */
        pvfs2_mkdir_response_t mkdir;
        pvfs2_readdir_response_t readdir;
    } resp;
} pvfs2_downcall_t;



#endif /* __DOWNCALL_H */

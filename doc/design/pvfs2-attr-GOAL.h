/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_ATTR_H
#define __PVFS2_ATTR_H

#include "pvfs2-types.h"
#include "pvfs2-storage.h"

/* attributes specific to metadata objects */
struct PVFS_metafile_attr_s
{
    /* TODO: removed temporarily */
    /* PVFS_Dist *dist; */
    uint32_t dist_size;
    PVFS_handle *datafile_array;
    uint32_t datafile_count;
};
typedef struct PVFS_metafile_attr_s PVFS_metafile_attr;

/* attributes specific to datafile objects */
struct PVFS_datafile_attr_s
{
    PVFS_size size;
};
typedef struct PVFS_datafile_attr_s PVFS_datafile_attr;

/* attributes specific to directory objects */
struct PVFS_directory_attr_s
{
    /* undefined */
};
typedef struct PVFS_directory_attr_s PVFS_directory_attr;

/* attributes specific to symlinks */
struct PVFS_symlink_attr_s
{
    /* undefined */
};
typedef struct PVFS_symlink_attr_s PVFS_symlink_attr;

/* generic attributes; applies to all objects */
struct PVFS_object_attr_s
{
    PVFS_uid owner;
    PVFS_gid group;
    PVFS_permissions perms;
    PVFS_time atime;
    PVFS_time mtime;
    PVFS_time ctime;
    PVFS_ds_type objtype;
    union
    {
	PVFS_metafile_attr meta;
	PVFS_datafile_attr data;
	PVFS_directory_attr dir;
	PVFS_symlink_attr sym;
    }
    u;
};
typedef struct PVFS_object_attr_s PVFS_object_attr;

#endif /* __PVFS2_ATTR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

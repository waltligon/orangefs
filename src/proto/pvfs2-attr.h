/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_ATTR_H
#define __PVFS2_ATTR_H

/* This file contains declarations of the PVFS objects
 *
 * I think we want to keep extended attributes in a separate structure
 * from PVFS_object_attr.  The reasoning for this is simple; they're
 * extra stuff, and for many operations we won't want to require that
 * the extended stuff get passed back and forth.
 *
 * Likewise for file distribution information; it would be silly to pass
 * that information back on every stat call.  We simply don't need it
 * unless we specifically ask for it.
 */

#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pint-distribution.h"

/* PVFS filesystem objects */


/*attributes*/
/* MetaFile
 *
 */
struct PVFS_metafile_attr_s
{
    /* distribution info */
    PVFS_Dist *dist;
    /* array of datafile handles */
    PVFS_handle *dfh;
    /* Number of datafiles */
    uint32_t nr_datafiles;
    /* size of the distribution info */
    PVFS_size dist_size;
};
typedef struct PVFS_metafile_attr_s PVFS_metafile_attr;

/* DataFile
 *
 */
struct PVFS_datafile_attr_s
{
    /* May be used by I/O server to report size of datafile
     * to metaserver to help in total file size calculation 
     */
    PVFS_size size;
};
typedef struct PVFS_datafile_attr_s PVFS_datafile_attr;

/* Directory
 *
 */
struct PVFS_directory_attr_s
{
    PVFS_handle dfh;
};
typedef struct PVFS_directory_attr_s PVFS_directory_attr;

/* Symlink
 *
 */
struct PVFS_symlink_attr_s
{
    /*char* target; */
};
typedef struct PVFS_symlink_attr_s PVFS_symlink_attr;

struct PVFS_object_eattr
{

};
typedef struct PVFS_object_eattr PVFS_object_eattr;

/* Attributes
 */
struct PVFS_object_attr
{
    PVFS_uid owner;
    PVFS_gid group;
    PVFS_permissions perms;
    PVFS_time atime;
    PVFS_time mtime;
    PVFS_time ctime;
    uint32_t mask;     /* indicates which fields are currently valid */
    PVFS_ds_type objtype;	/* Type of PVFS Filesystem object */
    union
    {
	PVFS_metafile_attr meta;
	PVFS_datafile_attr data;
	PVFS_directory_attr dir;
	PVFS_symlink_attr sym;
    }
    u;
};
typedef struct PVFS_object_attr PVFS_object_attr;

struct PVFS_attr_extended
{

};
typedef struct PVFS_attr_extended PVFS_attr_extended;

#endif /* __PVFS2_ATTR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

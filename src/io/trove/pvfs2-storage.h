/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_STORAGE_H
#define __PVFS2_STORAGE_H

#include "pvfs2-types.h"

/************************************************************
 * Types and structures specific to the storage interface
 */

/* key/value descriptors */
struct PVFS_ds_keyval_s
{
    void *buffer;
    /* size of memory region pointed to by buffer */
    int32_t buffer_sz;
    /* size of data read into buffer (only valid after a read) */
    int32_t read_sz;
};
typedef struct PVFS_ds_keyval_s PVFS_ds_keyval;

/* contiguous range of handles */
struct PVFS_handle_extent_s
{
    PVFS_handle first;
    PVFS_handle last;
};
typedef struct PVFS_handle_extent_s PVFS_handle_extent;

/* an array of contiguous ranges of handles */
struct PVFS_handle_extent_array_s
{
    uint32_t extent_count;
    PVFS_handle_extent *extent_array;
};
typedef struct PVFS_handle_extent_array_s PVFS_handle_extent_array;


/* vtag; contents not yet defined */
struct PVFS_vtag_s
{
    /* undefined */
};
typedef struct PVFS_vtag_s PVFS_vtag;

/* dataspace attributes that are not explicitly stored within the
 * dataspace itself.
 *
 * Note: this is what is being stored by the trove code as the attributes
 * right now.  Do we need to have a separation between the attributes as
 * sent across the wire/to the user vs. what is stored in trove?  Is that
 * already done?
 */
struct PVFS_ds_attributes_s
{
    PVFS_fs_id fs_id;		/* REQUIRED */
    PVFS_handle handle;		/* REQUIRED */
    PVFS_ds_type type;		/* REQUIRED */
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;
    /* NOTE: PUT NON-STORED AT THE BOTTOM!!! */
    PVFS_size b_size;		/* bstream size */
    PVFS_size k_size;		/* keyval size; # of keys */
};
typedef struct PVFS_ds_attributes_s PVFS_ds_attributes;

struct PVFS_ds_storedattr_s
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_ds_type type;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;
};
typedef struct PVFS_ds_storedattr_s PVFS_ds_storedattr;

#define PVFS_ds_attr_to_stored(__from, __to)	        \
do {						        \
    (__to) = * ((PVFS_ds_storedattr *) &(__from));	\
} while (0);

#define PVFS_ds_stored_to_attr(__from, __to, __b_size, __k_size)	\
do {									\
    * ((PVFS_ds_storedattr *) &(__to)) = (__from);			\
    (__to).b_size = (__b_size);						\
    (__to).k_size = (__k_size);						\
} while (0);

#endif /* __PVFS2_STORAGE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains the declarations and types for the PVFS 
 * storage interface
 */
#ifndef __PVFS2_STORAGE_H
#define __PVFS2_STORAGE_H

#include <pvfs2-types.h>

/************************************************************
 * Types and structures specific to the storage interface
 */

/* unique identifier for each collection that is managed by the storage
 * interface within a specific storage space
 */
typedef int32_t PVFS_coll_id;

/* user defined types that may be associated with dataspaces */
typedef int32_t PVFS_ds_type;

/* unique identifiers associated with dataspace operations */
typedef int64_t PVFS_ds_id;

/* the state of completed operations */
typedef int32_t PVFS_ds_state;

/* flags used to modify the behavior of operations */
typedef int32_t PVFS_ds_flags;

/* Bit values of flags for various dspace operations */
/* TODO: do we use a flag to get a vtag back or do we use VTAG_RETURN? */
enum {
    DSPACE_SYNC = 1, /* sync storage on completion of operation */
    DSPACE_PREALLOC = 2, /* preallocate space (for resize op only) */
    DSPACE_CALC_VTAG = 4, /* calculate and return a vtag (?) */
};

/* used to keep up with the current position when iterating through
 * key/value spaces
 */
typedef int32_t PVFS_ds_position;

/* vtag */
struct PVFS_vtag
{
    int foo; /* TODO: we haven't defined what vtags look like yet */
};
typedef struct PVFS_vtag PVFS_vtag_s;

/* dataspace attributes that are not explicitly stored within the
 * dataspace itself.
 *
 * Note: this is what is being stored by the trove code as the attributes
 * right now.  Do we need to have a separation between the attributes as
 * sent across the wire/to the user vs. what is stored in trove?  Is that
 * already done?
 */
struct PVFS_ds_attributes
{
    PVFS_fs_id fs_id; /* REQUIRED */
    PVFS_handle handle; /* REQUIRED */
    PVFS_type type; /* REQUIRED */
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
    /* NOTE: PUT NON-STORED AT THE BOTTOM!!! */
    PVFS_size b_size; /* bstream size */
    PVFS_size k_size; /* keyval size; # of keys */
};
typedef struct PVFS_ds_attributes PVFS_ds_attributes_s;

struct PVFS_ds_storedattr
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_type type;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
};
typedef struct PVFS_ds_storedattr PVFS_ds_storedattr_s;

#define PVFS_ds_attr_to_stored(__from, __to)	        \
do {						        \
    (__to) = * ((PVFS_ds_storedattr_s *) &(__from));	\
} while (0);

#define PVFS_ds_stored_to_attr(__from, __to, __b_size, __k_size)	\
do {									\
    * ((PVFS_ds_storedattr_s *) &(__to)) = (__from);			\
    (__to).b_size = (__b_size);						\
    (__to).k_size = (__k_size);						\
} while (0);

/* key descriptors for use in key/value spaces */
struct PVFS_ds_key
{
    /* TODO: not really defined yet */
    char* name; 
    int32_t length; /* NOTE: Should include NULL terminator on strings */
};
typedef struct PVFS_ds_key PVFS_ds_key_s;

struct PVFS_ds_keyval
{
    /* TODO: not really defined yet */
    void   *buffer;
    int32_t buffer_sz;
};
typedef struct PVFS_ds_keyval PVFS_ds_keyval_s;

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */

#endif /* __PVFS2_STORAGE_H */

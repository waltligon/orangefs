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
 */
struct PVFS_ds_attributes
{
	int foo;  /* TODO: we don't have any attributes defined yet */
};
typedef struct PVFS_ds_attributes PVFS_ds_attributes_s;

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

#endif /* __PVFS2_STORAGE_H */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_TYPES_H
#define __TROVE_TYPES_H


/* PVFS type mappings */
#include <pvfs2-types.h>
#include <pvfs2-storage.h>

typedef PVFS_handle          TROVE_handle;
typedef PVFS_size            TROVE_size;
typedef PVFS_offset          TROVE_offset;
typedef PVFS_ds_id           TROVE_op_id;
typedef PVFS_coll_id         TROVE_coll_id;
typedef PVFS_ds_type         TROVE_ds_type;
typedef PVFS_vtag_s          TROVE_vtag_s;
typedef PVFS_ds_flags        TROVE_ds_flags;
typedef PVFS_ds_keyval_s     TROVE_keyval_s;
typedef PVFS_ds_position     TROVE_ds_position;
typedef PVFS_ds_attributes_s TROVE_ds_attributes_s;
typedef PVFS_ds_state        TROVE_ds_state;

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4
 */

#endif

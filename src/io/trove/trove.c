/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup troveint
 *
 *  Trove interface routines.
 *
 *  This file holds the top-level routines for Trove.  These routines 
 *  are responsible for mapping Trove calls to specific underlying
 *  implementations.
 *
 *  Currently there is just one implementation, DBPF (database plus
 *  files), but there could be more.
 */

#include <stdlib.h>

#include "gossip.h"
#include "trove.h"
#include "trove-internal.h"

extern struct TROVE_keyval_ops  *keyval_method_table[];
extern struct TROVE_dspace_ops  *dspace_method_table[];
extern struct TROVE_bstream_ops *bstream_method_table[];
extern struct TROVE_mgmt_ops    *mgmt_method_table[];

struct PINT_perf_counter* PINT_server_pc = NULL;

int TROVE_db_cache_size_bytes = 0;
int TROVE_shm_key_hint = 0;
int TROVE_max_concurrent_io = 16;

extern TROVE_method_callback global_trove_method_callback;

/** Initiate reading from a contiguous region in a bstream into a
 *  contiguous region in memory.
 */
int trove_bstream_read_at(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    void* buffer,
    TROVE_size* inout_size_p,
    TROVE_offset offset,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_read_at(
           coll_id,
           handle,
           buffer,
           inout_size_p,
           offset,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate writing from a contiguous region in memory into a
 *  contiguous region in a bstream.
 */
int trove_bstream_write_at(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    void* buffer,
    TROVE_size* inout_size_p,
    TROVE_offset offset,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_write_at(
           coll_id,
           handle,
           buffer,
           inout_size_p,
           offset,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate resizing of a bstream.  This may be used to grow or
 *  shrink a bstream.  It does not guarantee that space is actually
 *  allocated; rather it changes the logical size of the bstream.
 */
int trove_bstream_resize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size* inout_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_resize(
           coll_id,
           handle,
           inout_size_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_bstream_validate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_validate(
           coll_id,
           handle,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate reading from a list of regions in a bstream into
 *  a list of regions in memory.  Sizes of individual regions
 *  in lists need not match, but total sizes must be equal.
 */
int trove_bstream_read_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char** mem_offset_array,
    TROVE_size* mem_size_array,
    int mem_count,
    TROVE_offset* stream_offset_array,
    TROVE_size* stream_size_array,
    int stream_count,
    TROVE_size* out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_read_list(
           coll_id,
           handle,
           mem_offset_array,
           mem_size_array,
           mem_count,
           stream_offset_array,
           stream_size_array,
           stream_count,
           out_size_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate writing from a list of regions in memory into a
 *  list of regions in a bstream.  Sizes of individual regions
 *  in lists need not match, but total sizes must be equal.
 */
int trove_bstream_write_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char** mem_offset_array,
    TROVE_size* mem_size_array,
    int mem_count,
    TROVE_offset* stream_offset_array,
    TROVE_size* stream_size_array,
    int stream_count,
    TROVE_size* out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_write_list(
           coll_id,
           handle,
           mem_offset_array,
           mem_size_array,
           mem_count,
           stream_offset_array,
           stream_size_array,
           stream_count,
           out_size_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate movement of all data to storage devices for a specific
 *  bstream.
 */
int trove_bstream_flush(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
                     PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return bstream_method_table[method_id]->bstream_flush(
           coll_id,
           handle,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate read of a single keyword/value pair.
 */
int trove_keyval_read(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    /* Check arguments */
    if (key_p->buffer_sz < 2)
        return -TROVE_EINVAL;
    if(!(flags & TROVE_BINARY_KEY))
    {
        if (((char *)key_p->buffer)[key_p->buffer_sz-1] != 0)
            return -TROVE_EINVAL;
    }
    return keyval_method_table[method_id]->keyval_read(
           coll_id,
           handle,
           key_p,
           val_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate write of a single keyword/value pair.
 *
 *  Expects val_p->buffer to be user allocated and val_p->buffer_sz to
 *  be size of buffer allocated.
 *  If data is too large for buffer, returns error (Cannot allocate
 *  Memory) and returns needed size in val_p->read_sz
 */
int trove_keyval_write(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    /* Check arguments */
    if (key_p->buffer_sz < 2)
        return -TROVE_EINVAL;
    if(!(flags & TROVE_BINARY_KEY))
    {
        if (((char *)key_p->buffer)[key_p->buffer_sz-1] != 0)
            return -TROVE_EINVAL;
    }
    return keyval_method_table[method_id]->keyval_write(
           coll_id,
           handle,
           key_p,
           val_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate removal of a keyword/value pair from a given data space.
 */
int trove_keyval_remove(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return keyval_method_table[method_id]->keyval_remove(
           coll_id,
           handle,
           key_p,
           val_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_keyval_validate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return keyval_method_table[method_id]->keyval_validate(
           coll_id,
           handle,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_keyval_iterate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_position* position_p,
    TROVE_keyval_s* key_array,
    TROVE_keyval_s* val_array,
    int* inout_count_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return keyval_method_table[method_id]->keyval_iterate(
           coll_id,
           handle,
           position_p,
           key_array,
           val_array,
           inout_count_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_keyval_iterate_keys(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_position* position_p,
    TROVE_keyval_s* key_array,
    int* inout_count_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return keyval_method_table[method_id]->keyval_iterate_keys(
           coll_id,
           handle,
           position_p,
           key_array,
           inout_count_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate read of multiple keyword/value pairs from the same
 *  data space as a single operation.
 */
int trove_keyval_read_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_array,
    TROVE_keyval_s* val_array,
    TROVE_ds_state* err_array,
    int count,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    int i;
    method_id = global_trove_method_callback(coll_id);
    /* Check arguments */
    for (i = 0; i < count; i++)
    {
        if (key_array[i].buffer_sz < 2)
            return -TROVE_EINVAL;
        if(!(flags & TROVE_BINARY_KEY))
        {
            if (((char *)key_array[i].buffer)[key_array[i].buffer_sz-1] != 0)
                return -TROVE_EINVAL;
        }
    }
    return keyval_method_table[method_id]->keyval_read_list(
           coll_id,
           handle,
           key_array,
           val_array,
           err_array,
           count,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate storing of multiple keyword/value pairs to the same
 *  data space as a single operation.
 */
int trove_keyval_write_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_array,
    TROVE_keyval_s* val_array,
    int count,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    int rc = 0, i;
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    /* Check arguments */
    for (i = 0; i < count; i++)
    {
        if (key_array[i].buffer_sz < 2)
        {
            return -TROVE_EINVAL;
        }
        if(!(flags & TROVE_BINARY_KEY))
        {
            if (((char *)key_array[i].buffer)[key_array[i].buffer_sz-1] != 0)
            {
                return -TROVE_EINVAL;
            }
        }
    }
    rc = keyval_method_table[method_id]->keyval_write_list(
           coll_id,
           handle,
           key_array,
           val_array,
           count,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
    return rc;
}

/** Initiate storing of multiple keyword/value pairs to the same
 *  data space as a single operation.
 */
int trove_keyval_remove_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_array,
    TROVE_keyval_s* val_array,
    int *error_array,
    int count,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    int i;
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    /* Check arguments */
    for (i = 0; i < count; i++)
    {
        if (key_array[i].buffer_sz < 2)
            return -TROVE_EINVAL;
        if(!(flags & TROVE_BINARY_KEY))
        {
            if (((char *)key_array[i].buffer)[key_array[i].buffer_sz-1] != 0)
                return -TROVE_EINVAL;
        }
    }
    return keyval_method_table[method_id]->keyval_remove_list(
           coll_id,
           handle,
           key_array,
           val_array,
           error_array,
           count,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate movement of all keyword/value pairs to storage for a given
 *  data space.
 */
int trove_keyval_flush(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return keyval_method_table[method_id]->keyval_flush(
           coll_id,
           handle,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_keyval_get_handle_info(TROVE_coll_id coll_id,
                                 TROVE_handle handle,
                                 TROVE_ds_flags flags,
                                 TROVE_keyval_handle_info *info,
                                 void * user_ptr,
                                 TROVE_context_id context_id,
                                 TROVE_op_id *out_op_id_p,
                 PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return keyval_method_table[method_id]->keyval_get_handle_info(
        coll_id,
        handle,
        flags,
        info,
        user_ptr,
        context_id,
        out_op_id_p,
    hints);
}

/** Initiate creation of multiple new data spaces.
 */
int trove_dspace_create_list(
    TROVE_coll_id coll_id,
    TROVE_handle_extent_array* handle_extent_array,
    TROVE_handle* out_handle_array,
    int count,
    TROVE_ds_type type,
    TROVE_keyval_s* hint,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_create_list(
           coll_id,
           handle_extent_array,
           out_handle_array,
           count,
           type,
           hint,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate creation of a new data space.
 */
int trove_dspace_create(
    TROVE_coll_id coll_id,
    TROVE_handle_extent_array* handle_extent_array,
    TROVE_handle* out_handle,
    TROVE_ds_type type,
    TROVE_keyval_s* hint,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_create(
           coll_id,
           handle_extent_array,
           out_handle,
           type,
           hint,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate removal of a list of data spaces.
 */
int trove_dspace_remove_list(
    TROVE_coll_id coll_id,
    TROVE_handle* handle_array,
    TROVE_ds_state* error_array,
    int count,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_remove_list(
           coll_id,
           handle_array,
           error_array,
           count,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

/** Initiate removal of a data space.
 */
int trove_dspace_remove(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_remove(
           coll_id,
           handle,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_dspace_iterate_handles(
    TROVE_coll_id coll_id,
    TROVE_ds_position* position_p,
    TROVE_handle* handle_array,
    int* inout_count_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_iterate_handles(
           coll_id,
           position_p,
           handle_array,
           inout_count_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_dspace_verify(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_type* type,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_verify(
           coll_id,
           handle,
           type,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate retrieval of attributes for a given data space.
 */
int trove_dspace_getattr(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_attributes_s* ds_attr_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_getattr(
           coll_id,
           handle,
           ds_attr_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

/** Initiate retrieval of attributes for a list of handles.
 */
int trove_dspace_getattr_list(
    TROVE_coll_id coll_id,
         int nhandles,
    TROVE_handle *handle_array,
    TROVE_ds_attributes_s *ds_attr_p,
         TROVE_ds_state  *error_array,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    int method_id;

    method_id = global_trove_method_callback(coll_id);
    if (method_id < 0) {
        return -TROVE_EINVAL;
    }
    return dspace_method_table[method_id]->dspace_getattr_list(
           coll_id,
           nhandles,
           handle_array,
           ds_attr_p,
           error_array,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_dspace_setattr(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_attributes_s* ds_attr_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p,
    PVFS_hint  hints)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_setattr(
           coll_id,
           handle,
           ds_attr_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p,
           hints);
}

int trove_dspace_cancel(
    TROVE_coll_id coll_id,
    TROVE_op_id id,
    TROVE_context_id context_id)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_cancel(
           coll_id,
           id,
           context_id);
}

/** Test for completion of a single trove operation.
 */
int trove_dspace_test(
    TROVE_coll_id coll_id,
    TROVE_op_id id,
    TROVE_context_id context_id,
    int* out_count_p,
    TROVE_vtag_s* vtag,
    void** returned_user_ptr_p,
    TROVE_ds_state* state_p,
    int max_idle_time_ms)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_test(
           coll_id,
           id,
           context_id,
           out_count_p,
           vtag,
           returned_user_ptr_p,
           state_p,
           max_idle_time_ms);
}

/** Test for completion of one or more trove operations.
 */
int trove_dspace_testsome(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id,
    TROVE_op_id* ds_id_array,
    int* inout_count_p,
    int* out_index_array,
    TROVE_vtag_s* vtag_array,
    void** returned_user_ptr_array,
    TROVE_ds_state* state_array,
    int max_idle_time_ms)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_testsome(
           coll_id,
           context_id,
           ds_id_array,
           inout_count_p,
           out_index_array,
           vtag_array,
           returned_user_ptr_array,
           state_array,
           max_idle_time_ms);
}

/** Test for completion of any trove operation within a given context.
 */
int trove_dspace_testcontext(
    TROVE_coll_id coll_id,
    TROVE_op_id* ds_id_array,
    int* inout_count_p,
    TROVE_ds_state* state_array,
    void** user_ptr_array,
    int max_idle_time_ms,
    TROVE_context_id context_id)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return dspace_method_table[method_id]->dspace_testcontext(
           coll_id,
           ds_id_array,
           inout_count_p,
           state_array,
           user_ptr_array,
           max_idle_time_ms,
           context_id);
}

int trove_collection_geteattr(
    TROVE_coll_id coll_id,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return mgmt_method_table[method_id]->collection_geteattr(
           coll_id,
           key_p,
           val_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_collection_seteattr(
    TROVE_coll_id coll_id,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return mgmt_method_table[method_id]->collection_seteattr(
           coll_id,
           key_p,
           val_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_collection_deleattr(
    TROVE_coll_id coll_id,
    TROVE_keyval_s* key_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return mgmt_method_table[method_id]->collection_deleattr(
           coll_id,
           key_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_collection_getinfo(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id,
    TROVE_coll_getinfo_options opt,
    void* parameter)
{
    TROVE_method_id method_id;
    method_id = global_trove_method_callback(coll_id);
    return mgmt_method_table[method_id]->collection_getinfo(
           coll_id,
           context_id,
           opt,
           parameter);
}

int trove_collection_setinfo(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id,
    int option,
    void* parameter)
{
    TROVE_method_id method_id;
    if(option == TROVE_DB_CACHE_SIZE_BYTES)
    {
        TROVE_db_cache_size_bytes = *((int *)parameter);
        return 0;
    }
    if(option == TROVE_SHM_KEY_HINT)
    {
        TROVE_shm_key_hint = *((int*)parameter);
        return(0);
    }
    if(option == TROVE_MAX_CONCURRENT_IO)
    {
        TROVE_max_concurrent_io = *((int*)parameter);
        return(0);
    }
    method_id = global_trove_method_callback(coll_id);
    return mgmt_method_table[method_id]->collection_setinfo(
           method_id,
           coll_id,
           context_id,
           option,
           parameter);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


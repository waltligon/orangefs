/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "trove.h"
#include "trove-internal.h"

extern struct TROVE_keyval_ops  *keyval_method_table[];
extern struct TROVE_dspace_ops  *dspace_method_table[];
extern struct TROVE_bstream_ops *bstream_method_table[];
extern struct TROVE_mgmt_ops    *mgmt_method_table[];

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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
           out_op_id_p);
}

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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
           out_op_id_p);
}

int trove_bstream_resize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size* inout_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return bstream_method_table[method_id]->bstream_resize(
           coll_id,
           handle,
           inout_size_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_bstream_validate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return bstream_method_table[method_id]->bstream_validate(
           coll_id,
           handle,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
}

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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
           out_op_id_p);
}

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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
           out_op_id_p);
}

int trove_bstream_flush(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return bstream_method_table[method_id]->bstream_flush(
           coll_id,
           handle,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_keyval_read(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
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
           out_op_id_p);
}

int trove_keyval_write(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_p,
    TROVE_keyval_s* val_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
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
           out_op_id_p);
}

int trove_keyval_remove(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return keyval_method_table[method_id]->keyval_remove(
           coll_id,
           handle,
           key_p,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_keyval_validate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return keyval_method_table[method_id]->keyval_validate(
           coll_id,
           handle,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
           out_op_id_p);
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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
           out_op_id_p);
}

int trove_keyval_read_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_keyval_s* key_array,
    TROVE_keyval_s* val_array,
    int count,
    TROVE_ds_flags flags,
    TROVE_vtag_s* vtag,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return keyval_method_table[method_id]->keyval_read_list(
           coll_id,
           handle,
           key_array,
           val_array,
           count,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
}

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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return keyval_method_table[method_id]->keyval_write_list(
           coll_id,
           handle,
           key_array,
           val_array,
           count,
           flags,
           vtag,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_keyval_flush(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return keyval_method_table[method_id]->keyval_flush(
           coll_id,
           handle,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_dspace_create(
    TROVE_coll_id coll_id,
    TROVE_handle_extent_array* handle_extent_array,
    TROVE_handle* out_handle,
    TROVE_ds_type type,
    TROVE_keyval_s* hint,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return dspace_method_table[method_id]->dspace_create(
           coll_id,
           handle_extent_array,
           out_handle,
           type,
           hint,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_dspace_remove(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return dspace_method_table[method_id]->dspace_remove(
           coll_id,
           handle,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return dspace_method_table[method_id]->dspace_verify(
           coll_id,
           handle,
           type,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_dspace_getattr(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_attributes_s* ds_attr_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return dspace_method_table[method_id]->dspace_getattr(
           coll_id,
           handle,
           ds_attr_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_dspace_setattr(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_attributes_s* ds_attr_p,
    TROVE_ds_flags flags,
    void* user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id* out_op_id_p)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return dspace_method_table[method_id]->dspace_setattr(
           coll_id,
           handle,
           ds_attr_p,
           flags,
           user_ptr,
           context_id,
           out_op_id_p);
}

int trove_dspace_cancel(
    TROVE_coll_id coll_id,
    TROVE_op_id id,
    TROVE_context_id context_id)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return dspace_method_table[method_id]->dspace_cancel(
           coll_id,
           id,
           context_id);
}

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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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

int trove_dspace_testcontext(
    TROVE_coll_id coll_id,
    TROVE_op_id* ds_id_array,
    int* inout_count_p,
    TROVE_ds_state* state_array,
    void** user_ptr_array,
    int max_idle_time_ms,
    TROVE_context_id context_id)
{
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return mgmt_method_table[method_id]->collection_seteattr(
           coll_id,
           key_p,
           val_p,
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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
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
    int method_id;

    method_id = map_coll_id_to_method(coll_id);
    if (method_id < 0) {
        return -1; /* NEED STATUS TYPE FOR THIS */
    }
    return mgmt_method_table[method_id]->collection_setinfo(
           coll_id,
           context_id,
           option,
           parameter);
}


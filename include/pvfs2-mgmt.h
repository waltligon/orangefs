/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header includes prototypes for management functions.  */

#ifndef __PVFS2_MGMT_H
#define __PVFS2_MGMT_H

#include "pvfs2-types.h"

/* low level statfs style information for each server */
/* see PVFS_mgmt_statfs_all() */
struct PVFS_mgmt_server_stat
{
    PVFS_fs_id fs_id;
    PVFS_size bytes_available;
    PVFS_size bytes_total;
    uint64_t ram_total_bytes;
    uint64_t ram_free_bytes;
    uint64_t uptime_seconds;
    uint64_t handles_available_count;
    uint64_t handles_total_count;
    const char* bmi_address;
    int server_type;
};

/* performance monitoring statistics */
struct PVFS_mgmt_perf_stat
{
    int32_t valid_flag;	    /* is this entry valid? */
    uint32_t id;	    /* timestep id */
    uint64_t start_time_ms; /* start time of perf set, ms since epoch */
    int64_t write;	    /* bytes written */
    int64_t read;	    /* bytes read */
    int64_t metadata_write; /* # of modifying metadata ops */
    int64_t metadata_read;  /* # of non-modifying metadata ops */
};
endecode_fields_7_struct(
    PVFS_mgmt_perf_stat,
    int32_t, valid_flag,
    uint32_t, id,
    uint64_t, start_time_ms,
    int64_t, write,
    int64_t, read,
    int64_t, metadata_write,
    int64_t, metadata_read)

/* low level information about individual server level objects */
struct PVFS_mgmt_dspace_info
{
    PVFS_error error_code;	/* error code for this query */
    PVFS_handle handle;		/* handle this struct refers to */
    PVFS_ds_type type;		/* type of object */
    PVFS_size b_size;		/* size of bstream (if applicable) */
    PVFS_size k_size;		/* number of keyvals (if applicable) */
    PVFS_handle dirdata_handle; /* directory data handle (if applicable) */
};
endecode_fields_6_struct(PVFS_mgmt_dspace_info,
  PVFS_error, error_code,
  PVFS_handle, handle,
  PVFS_ds_type, type,
  PVFS_size, b_size,
  PVFS_size, k_size,
  PVFS_handle, dirdata_handle)

/* individual datapoint from event monitoring */
struct PVFS_mgmt_event
{
    int32_t api;
    int32_t operation;
    int64_t value;
    PVFS_id_gen_t id;
    int32_t flags;
    int32_t tv_sec;
    int32_t tv_usec;
};
endecode_fields_7_struct(PVFS_mgmt_event,
    int32_t, api,
    int32_t, operation,
    int64_t, value,
    PVFS_id_gen_t, id,
    int32_t, flags,
    int32_t, tv_sec,
    int32_t, tv_usec)

/* values which may be or'd together in the flags field above */
enum
{
    PVFS_MGMT_IO_SERVER = 1,
    PVFS_MGMT_META_SERVER = 2
};


int PVFS_mgmt_count_servers(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    int server_type,
    int* count);

int PVFS_mgmt_get_server_array(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    int server_type,
    PVFS_BMI_addr_t *addr_array,
    int* inout_count_p);

int PVFS_mgmt_noop(
    PVFS_fs_id,
    PVFS_credentials *credentials,
    PVFS_BMI_addr_t addr);

const char* PVFS_mgmt_map_addr(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_BMI_addr_t addr,
    int* server_type);

int PVFS_mgmt_setparam_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    int64_t value,
    PVFS_BMI_addr_t *addr_array,
    int64_t* old_value_array,
    int count,
    PVFS_error_details *details);

int PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    int64_t value,
    int64_t* old_value_array,
    PVFS_error_details *details);

int PVFS_mgmt_statfs_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_server_stat *stat_array,
    PVFS_BMI_addr_t *addr_array,
    int count,
    PVFS_error_details *details);

int PVFS_mgmt_statfs_all(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_server_stat *stat_array,
    int* inout_count_p,
    PVFS_error_details *details);

int PVFS_mgmt_perf_mon_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_perf_stat** perf_matrix,
    uint64_t* end_time_ms_array,
    PVFS_BMI_addr_t *addr_array,
    uint32_t* next_id_array,
    int server_count,
    int history_count,
    PVFS_error_details *details);

int PVFS_mgmt_event_mon_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_event **event_matrix,
    PVFS_BMI_addr_t *addr_array,
    int server_count,
    int event_count,
    PVFS_error_details *details);

int PVFS_mgmt_toggle_admin_mode(
    PVFS_credentials *credentials,
    int on_flag);

int PVFS_mgmt_iterate_handles_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_handle **handle_matrix,
    int* handle_count_array,
    PVFS_ds_position *position_array,
    PVFS_BMI_addr_t *addr_array,
    int server_count,
    PVFS_error_details *details);

int PVFS_mgmt_get_dfile_array(
    PVFS_object_ref ref,
    PVFS_credentials *credentials,
    PVFS_handle *dfile_array,
    int dfile_count);

int PVFS_imgmt_remove_object(
    PVFS_object_ref object_ref, 
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

int PVFS_mgmt_remove_object(
    PVFS_object_ref object_ref, 
    PVFS_credentials *credentials);

int PVFS_imgmt_remove_dirent(
    PVFS_object_ref parent_ref,
    char *entry,
    PVFS_credentials *credentials,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

int PVFS_mgmt_remove_dirent(
    PVFS_object_ref parent_ref,
    char *entry,
    PVFS_credentials *credentials);

#endif /* __PVFS2_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

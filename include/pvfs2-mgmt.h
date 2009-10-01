/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/* NOTE: if you make any changes to the encoding definitions in this file,
 * please update the PVFS2_PROTO_VERSION in pvfs2-req-proto.h accordingly
 */

/** \defgroup mgmtint PVFS2 management interface
 *
 *  The PVFS2 management interface provides functionality used to check
 *  file system status and to repair damaged file systems.  Both blocking
 *  and nonblocking calls are provided for most operations.
 *
 * @{
 */

/** \file
 *  Declarations for the PVFS2 management interface.
 */

#ifndef __PVFS2_MGMT_H
#define __PVFS2_MGMT_H

#include "pvfs2-sysint.h"
#include "pvfs2-types.h"

/* non-blocking mgmt operation handle */
typedef PVFS_id_gen_t PVFS_mgmt_op_id;

/* low level statfs style information for each server */
/* see PVFS_mgmt_statfs_all() */
struct PVFS_mgmt_server_stat 
{
    PVFS_fs_id fs_id;
    PVFS_size bytes_available;
    PVFS_size bytes_total;
    uint64_t ram_total_bytes;
    uint64_t ram_free_bytes;
    uint64_t load_1;
    uint64_t load_5;
    uint64_t load_15;
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
    int32_t dspace_queue;   /* # of metadata dspace ops in the queue */
    int32_t keyval_queue;   /* # of metadata keyval ops in the queue */
    int32_t reqsched;       /* # of currently scheduled request posted */
};
endecode_fields_11_struct(
    PVFS_mgmt_perf_stat,
    int32_t, valid_flag,
    uint32_t, id,
    uint64_t, start_time_ms,
    int64_t, write,
    int64_t, read,
    int64_t, metadata_write,
    int64_t, metadata_read,
    int32_t, dspace_queue,
    int32_t, keyval_queue,
    int32_t, reqsched,
    skip4,);

/* low level information about individual server level objects */
struct PVFS_mgmt_dspace_info
{
    PVFS_error error_code;	/* error code for this query */
    PVFS_handle handle;		/* handle this struct refers to */
    PVFS_ds_type type;		/* type of object */
    PVFS_size b_size;		/* size of bstream (if applicable) */
    PVFS_handle dirdata_handle; /* directory data handle (if applicable) */
};
endecode_fields_7_struct(
  PVFS_mgmt_dspace_info,
  PVFS_error, error_code,
  skip4,,
  PVFS_handle, handle,
  PVFS_ds_type, type,
  skip4,,
  PVFS_size, b_size,
  PVFS_handle, dirdata_handle);

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
endecode_fields_8_struct(
    PVFS_mgmt_event,
    int32_t, api,
    int32_t, operation,
    int64_t, value,
    PVFS_id_gen_t, id,
    int32_t, flags,
    int32_t, tv_sec,
    int32_t, tv_usec,
    skip4,);

/* values which may be or'd together in the flags field above */
enum
{
    PVFS_MGMT_IO_SERVER = 1,
    PVFS_MGMT_META_SERVER = 2
};

PVFS_error PVFS_mgmt_count_servers(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    int server_type,
    int *count);

PVFS_error PVFS_mgmt_get_server_array(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    int server_type,
    PVFS_BMI_addr_t *addr_array,
    int *inout_count_p);

PVFS_error PVFS_imgmt_noop(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_BMI_addr_t addr,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_noop(
    PVFS_fs_id,
    PVFS_credentials *credentials,
    PVFS_BMI_addr_t addr,
    PVFS_hint hints);

const char* PVFS_mgmt_map_addr(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_BMI_addr_t addr,
    int* server_type);

PVFS_error PVFS_imgmt_setparam_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    struct PVFS_mgmt_setparam_value *value,
    PVFS_BMI_addr_t *addr_array,
    int count,
    PVFS_error_details *details,
    PVFS_hint hints,
    PVFS_mgmt_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_mgmt_setparam_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    struct PVFS_mgmt_setparam_value *value,
    PVFS_BMI_addr_t *addr_array,
    int count,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    struct PVFS_mgmt_setparam_value *value,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_mgmt_setparam_single(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    struct PVFS_mgmt_setparam_value *value,
    char *server_addr_str,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_statfs_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_server_stat *stat_array,
    PVFS_BMI_addr_t *addr_array,
    int count,
    PVFS_error_details *details,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_statfs_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_server_stat *stat_array,
    PVFS_BMI_addr_t *addr_array,
    int count,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_mgmt_statfs_all(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_server_stat *stat_array,
    int *inout_count_p,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_perf_mon_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_perf_stat **perf_matrix,
    uint64_t *end_time_ms_array,
    PVFS_BMI_addr_t *addr_array,
    uint32_t* next_id_array,
    int server_count,
    int history_count,
    PVFS_error_details *details,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_perf_mon_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_perf_stat** perf_matrix,
    uint64_t *end_time_ms_array,
    PVFS_BMI_addr_t *addr_array,
    uint32_t *next_id_array,
    int server_count,
    int history_count,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_event_mon_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_event** event_matrix,
    PVFS_BMI_addr_t *addr_array,
    int server_count,
    int event_count,
    PVFS_error_details *details,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_event_mon_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_event **event_matrix,
    PVFS_BMI_addr_t *addr_array,
    int server_count,
    int event_count,
    PVFS_error_details *details,
    PVFS_hint hints);


PVFS_error PVFS_imgmt_iterate_handles_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_handle **handle_matrix,
    int *handle_count_array,
    PVFS_ds_position *position_array,
    PVFS_BMI_addr_t *addr_array,
    int server_count,
    int flags,
    PVFS_error_details *details,
    PVFS_hint hints,
    PVFS_mgmt_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_mgmt_iterate_handles_list(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_handle **handle_matrix,
    int *handle_count_array,
    PVFS_ds_position *position_array,
    PVFS_BMI_addr_t *addr_array,
    int server_count,
    int flags,
    PVFS_error_details *details,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_get_dfile_array(
    PVFS_object_ref ref,
    PVFS_credentials *credentials,
    PVFS_handle *dfile_array,
    int dfile_count,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_get_dfile_array(
    PVFS_object_ref ref,
    PVFS_credentials *credentials,
    PVFS_handle *dfile_array,
    int dfile_count,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_remove_object(
    PVFS_object_ref object_ref,
    PVFS_credentials *credentials,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_remove_object(
    PVFS_object_ref object_ref,
    PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_remove_dirent(
    PVFS_object_ref parent_ref,
    char *entry,
    PVFS_credentials *credentials,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_remove_dirent(
    PVFS_object_ref parent_ref,
    char *entry,
    PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_create_dirent(
    PVFS_object_ref parent_ref,
    char *entry,
    PVFS_handle entry_handle,
    PVFS_credentials *credentials,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_create_dirent(
    PVFS_object_ref parent_ref,
    char *entry,
    PVFS_handle entry_handle,
    PVFS_credentials *credentials,
    PVFS_hint hints);

PVFS_error PVFS_imgmt_get_dirdata_handle(
    PVFS_object_ref parent_ref,
    PVFS_handle *out_dirdata_handle,
    PVFS_credentials *credentials,
    PVFS_mgmt_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_mgmt_get_dirdata_handle(
    PVFS_object_ref parent_ref,
    PVFS_handle *out_dirdata_handle,
    PVFS_credentials *credentials,
    PVFS_hint hints);

int PVFS_mgmt_wait(
    PVFS_mgmt_op_id op_id,
    const char *in_op_str,
    int *out_error);

int PVFS_mgmt_testsome(
    PVFS_mgmt_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms);

int PVFS_mgmt_cancel(PVFS_mgmt_op_id op_id);

PVFS_error PVFS_imgmt_repair_file(
    char *object_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_handle handle,
    PVFS_sysresp_create *resp,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

PVFS_error PVFS_mgmt_repair_file(
    char *object_name,
    PVFS_object_ref parent_ref,
    PVFS_sys_attr attr,
    PVFS_credentials *credentials,
    PVFS_handle handle,
    PVFS_sysresp_create *resp);
                        
int PVFS_mgmt_get_config(
    const PVFS_fs_id* fsid,
    PVFS_BMI_addr_t* addr,
    char* fs_buf,
    int fs_buf_size);

PVFS_error PVFS_mgmt_map_handle(
    PVFS_fs_id fs_id,
    PVFS_handle handle,
    PVFS_BMI_addr_t *addr);

#endif /* __PVFS2_MGMT_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this file contains prototypes for the job interface */

#ifndef __JOB_H
#define __JOB_H

#include <flow.h>
#include <bmi.h>
#include <id-generator.h>
#include <pvfs2-types.h>
#include <pvfs2-storage.h>
#include <pvfs2-req-proto.h>

/* TODO: need to add list operations for both BMI and Trove */

typedef id_gen_t job_id_t;

/* used to report the status of jobs upon completion */
typedef struct job_status
{
	/* the comments indicate which type of job will fill in which fields */
	int error_code;          /* returned by all operations */
	PVFS_size actual_size; /* read_at, write_at, resize, bmi_recv */
	PVFS_vtag_s* vtag;       /* most trove operations */
	PVFS_ds_position position; /* iterate, iterate_keys */
	PVFS_handle handle;     /* dspace_create */
	PVFS_ds_attributes_s ds_attr;  /* getattr */
	PVFS_ds_type type;      /* dspace_verify */
	PVFS_fs_id coll_id;   /* fs_lookup */
	int count;            /* keyval_iterate */
} job_status_s;

enum job_flags
{
	JOB_NO_IMMED_COMPLETE = 1
};

/******************************************************************
 * management functions 
 */

int job_initialize(
	int flags);

int job_finalize(
	void);


/******************************************************************
 * job posting functions 
 */

/* network send */
int job_bmi_send(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* network send (list of buffers) */
int job_bmi_send_list(
	bmi_addr_t addr,
	void** buffer_list,
	bmi_size_t* size_list,
	int list_count,
	bmi_size_t total_size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* network receive */
int job_bmi_recv(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* network receive (list of buffers) */
int job_bmi_recv_list(
	bmi_addr_t addr,
	void** buffer_list,
	bmi_size_t* size_list,
	int list_count,
	bmi_size_t total_expected_size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* unexpected network receive */
int job_bmi_unexp(
	struct BMI_unexpected_info* bmi_unexp_d,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id,
	enum job_flags flags);

/* request scheduler post */
int job_req_sched_post(
	struct PVFS_server_req_s* in_request,
	void* user_ptr, 
	job_status_s* out_status_p,
	job_id_t* id);

/* request scheduler release */
int job_req_sched_release(
	job_id_t in_completed_id,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* out_id);

/* complex I/O operation (disk, net, or mem) */
int job_flow(
	flow_descriptor* flow_d,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* storage byte stream write */
int job_trove_bstream_write_at(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_offset offset,
	void* buffer,
	PVFS_size size,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* storage byte stream read */
int job_trove_bstream_read_at(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_offset offset,
	void* buffer,
	PVFS_size size,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* storage key/value read */
int job_trove_keyval_read(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_ds_keyval_s *key_p,
	PVFS_ds_keyval_s *val_p,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* storage key/value read */
int job_trove_keyval_read_list(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_ds_keyval_s *key_array,
	PVFS_ds_keyval_s *val_array,
	int count,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* storage key/value write */
int job_trove_keyval_write(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_ds_keyval_s *key_p,
	PVFS_ds_keyval_s *val_p,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* read generic dspace attributes */
int job_trove_dspace_getattr(
	PVFS_coll_id coll_id, 
	PVFS_handle handle,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* write generic dspace attributes */
int job_trove_dspace_setattr(
	PVFS_coll_id coll_id, 
	PVFS_handle handle,
	PVFS_ds_attributes_s* ds_attr_p,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* resize (truncate or preallocate) a storage byte stream */
int job_trove_bstream_resize(
	PVFS_coll_id coll_id, 
	PVFS_handle handle,
	PVFS_size size,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* check consistency of a bytestream for a given vtag */
int job_trove_bstream_validate(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* remove a key/value entry */
int job_trove_keyval_remove(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_ds_keyval_s* key_p,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* check consistency of a key/value pair for a given vtag */
int job_trove_keyval_validate(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* iterate through all of the key/value pairs for a data space */
int job_trove_keyval_iterate(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_ds_position position,
	PVFS_ds_keyval_s* key_array,
	PVFS_ds_keyval_s* val_array,
	int count,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* iterate through all of the keys for a data space */
int job_trove_keyval_iterate_keys(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_ds_position position,
	PVFS_ds_keyval_s* key_array,
	int count,
	PVFS_ds_flags flags,
	PVFS_vtag_s* vtag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* create a new data space object */
int job_trove_dspace_create(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	PVFS_handle bitmask,
	PVFS_ds_type type,
	void* hint,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* remove an entire data space object (byte stream and key/value) */
int job_trove_dspace_remove(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* verify that a given dataspace exists and discover its type */
int job_trove_dspace_verify(
	PVFS_coll_id coll_id,
	PVFS_handle handle,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* create a new file system */
int job_trove_fs_create(
	char* collname,
	PVFS_coll_id new_coll_id,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);
	
/* remove an existing file system */
int job_trove_fs_remove(
	char* collname,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* lookup a file system based on a string name */
int job_trove_fs_lookup(
	char* collname,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* set extended attributes for a file system */
int job_trove_fs_seteattr(
	PVFS_coll_id coll_id,
	PVFS_ds_keyval_s *key_p,
	PVFS_ds_keyval_s *val_p,
	PVFS_ds_flags flags,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/* read extended attributes for a file system */
int job_trove_fs_geteattr(
	PVFS_coll_id coll_id,
	PVFS_ds_keyval_s *key_p,
	PVFS_ds_keyval_s *val_p,
	PVFS_ds_flags flags,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id);

/******************************************************************
 * job test/wait for completion functions 
 */

int job_test(
	job_id_t id,
	int* out_count_p,
	void** returned_user_ptr_p,
	job_status_s* out_status_p,
	int timeout_ms);

int job_testsome(
	job_id_t* id_array,
	int* inout_count_p,
	int* out_index_array,
	void** returned_user_ptr_array,
	job_status_s* out_status_array_p,
	int timeout_ms);

int job_testworld(
	job_id_t* out_id_array_p,
	int* inout_count_p,
	void** returned_user_ptr_array,
	job_status_s* out_status_array_p,
	int timeout_ms);

#endif /* __JOB_H */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this file contains prototypes for the job interface */

#ifndef __JOB_H
#define __JOB_H

#include <inttypes.h>

#include "flow.h"
#include "bmi.h"
#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-req-proto.h"
#include "pint-dev.h"

typedef PVFS_id_gen_t job_id_t;
typedef PVFS_context_id job_context_id;
/* integer type; large enough to hold a pointer */
typedef intptr_t job_aint;

#define JOB_MAX_CONTEXTS 16

/* used to report the status of jobs upon completion */
typedef struct job_status
{
    /* the comments indicate which type of job will fill in which fields */
    job_aint status_user_tag;   /* tag supplied by caller */
    int error_code;		/* returned by all operations */
    PVFS_size actual_size;	/* read_at, write_at, resize, bmi_recv */
    PVFS_vtag *vtag;		/* most trove operations */
    PVFS_ds_position position;	/* iterate, iterate_keys, iterate_handles */
    PVFS_handle handle;		/* dspace_create */
    PVFS_ds_type type;		/* dspace_verify */
    PVFS_fs_id coll_id;		/* fs_lookup */
    int count;			/* keyval_iterate, iterate_handles */
}
job_status_s;

enum job_flags
{
    JOB_NO_IMMED_COMPLETE = 1
};

/******************************************************************
 * management functions 
 */

int job_initialize(int flags);

int job_finalize(void);

int job_open_context(job_context_id* context_id);

void job_close_context(job_context_id context_id);

/******************************************************************
 * job posting functions 
 */

/* network send */
int job_bmi_send(PVFS_BMI_addr_t addr,
		 void *buffer,
		 bmi_size_t size,
		 bmi_msg_tag_t tag,
		 enum bmi_buffer_type buffer_type,
		 int send_unexpected,
		 void *user_ptr,
		 job_aint status_user_tag,
		 job_status_s * out_status_p,
		 job_id_t * id,
		 job_context_id context_id);

/* network send (list of buffers) */
int job_bmi_send_list(PVFS_BMI_addr_t addr,
		      void **buffer_list,
		      bmi_size_t * size_list,
		      int list_count,
		      bmi_size_t total_size,
		      bmi_msg_tag_t tag,
		      enum bmi_buffer_type buffer_type,
		      int send_unexpected,
		      void *user_ptr,
		      job_aint status_user_tag,
		      job_status_s * out_status_p,
		      job_id_t * id,
		      job_context_id context_id);

/* network receive */
int job_bmi_recv(PVFS_BMI_addr_t addr,
		 void *buffer,
		 bmi_size_t size,
		 bmi_msg_tag_t tag,
		 enum bmi_buffer_type buffer_type,
		 void *user_ptr,
		 job_aint status_user_tag,
		 job_status_s * out_status_p,
		 job_id_t * id,
		 job_context_id context_id);

/* network receive (list of buffers) */
int job_bmi_recv_list(PVFS_BMI_addr_t addr,
		      void **buffer_list,
		      bmi_size_t * size_list,
		      int list_count,
		      bmi_size_t total_expected_size,
		      bmi_msg_tag_t tag,
		      enum bmi_buffer_type buffer_type,
		      void *user_ptr,
		      job_aint status_user_tag,
		      job_status_s * out_status_p,
		      job_id_t * id,
		      job_context_id context_id);

/* unexpected network receive */
int job_bmi_unexp(struct BMI_unexpected_info *bmi_unexp_d,
		  void *user_ptr,
		  job_aint status_user_tag,
		  job_status_s * out_status_p,
		  job_id_t * id,
		  enum job_flags flags,
		  job_context_id context_id);

int job_bmi_cancel(job_id_t id,
		   job_context_id context_id);

/* unexpected device receive */
int job_dev_unexp(struct PINT_dev_unexp_info* dev_unexp_d,
		  void* user_ptr,
		  job_aint status_user_tag,
		  job_status_s * out_status_p,
		  job_id_t* id,
		  job_context_id context_id);

/* device write */
int job_dev_write(void* buffer,
		  int size,
		  PVFS_id_gen_t tag,
		  enum PINT_dev_buffer_type buffer_type,
		  void* user_ptr,
		  job_aint status_user_tag,
		  job_status_s * out_status_p,
		  job_id_t * id,
		  job_context_id context_id);

/* device write list */
int job_dev_write_list(void** buffer_list,
		       int* size_list,
		       int list_count,
		       int total_size,
		       PVFS_id_gen_t tag,
		       enum PINT_dev_buffer_type buffer_type,
		       void* user_ptr,
		       job_aint status_user_tag,
		       job_status_s* out_status_p,
		       job_id_t* id,
		       job_context_id context_id);

/* request scheduler post */
int job_req_sched_post(struct PVFS_server_req *in_request,
		       int req_index,
		       void *user_ptr,
		       job_aint status_user_tag,
		       job_status_s * out_status_p,
		       job_id_t * id,
		       job_context_id context_id);

int job_req_sched_post_timer(int msecs,
		       void *user_ptr,
		       job_aint status_user_tag,
		       job_status_s * out_status_p,
		       job_id_t * id,
		       job_context_id context_id);

/* request scheduler release */
int job_req_sched_release(job_id_t in_completed_id,
			  void *user_ptr,
			  job_aint status_user_tag,
			  job_status_s * out_status_p,
			  job_id_t * out_id,
			  job_context_id context_id);


/* complex I/O operation (disk, net, or mem) */
int job_flow(flow_descriptor * flow_d,
	     void *user_ptr,
	     job_aint status_user_tag,
	     job_status_s * out_status_p,
	     job_id_t * id,
	     job_context_id context_id);

int job_flow_cancel(job_id_t id, job_context_id context_id);

/* storage byte stream write */
int job_trove_bstream_write_at(PVFS_fs_id coll_id,
			       PVFS_handle handle,
			       PVFS_offset offset,
			       void *buffer,
			       PVFS_size size,
			       PVFS_ds_flags flags,
			       PVFS_vtag * vtag,
			       void *user_ptr,
			       job_aint status_user_tag,
			       job_status_s * out_status_p,
			       job_id_t * id,
			       job_context_id context_id);

/* storage byte stream read */
int job_trove_bstream_read_at(PVFS_fs_id coll_id,
			      PVFS_handle handle,
			      PVFS_offset offset,
			      void *buffer,
			      PVFS_size size,
			      PVFS_ds_flags flags,
			      PVFS_vtag * vtag,
			      void *user_ptr,
			      job_aint status_user_tag,
			      job_status_s * out_status_p,
			      job_id_t * id,
			      job_context_id context_id);

/* byte stream flush to storage */
int job_trove_bstream_flush(PVFS_fs_id coll_id,
			    PVFS_handle handle,
			    PVFS_ds_flags flags,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id);
	
/* storage key/value read */
int job_trove_keyval_read(PVFS_fs_id coll_id,
			  PVFS_handle handle,
			  PVFS_ds_keyval * key_p,
			  PVFS_ds_keyval * val_p,
			  PVFS_ds_flags flags,
			  PVFS_vtag * vtag,
			  void *user_ptr,
			  job_aint status_user_tag,
			  job_status_s * out_status_p,
			  job_id_t * id,
			  job_context_id context_id);

/* storage key/value read */
int job_trove_keyval_read_list(PVFS_fs_id coll_id,
			       PVFS_handle handle,
			       PVFS_ds_keyval * key_array,
			       PVFS_ds_keyval * val_array,
			       int count,
			       PVFS_ds_flags flags,
			       PVFS_vtag * vtag,
			       void *user_ptr,
			       job_aint status_user_tag,
			       job_status_s * out_status_p,
			       job_id_t * id,
			       job_context_id context_id);

/* storage key/value write */
int job_trove_keyval_write(PVFS_fs_id coll_id,
			   PVFS_handle handle,
			   PVFS_ds_keyval * key_p,
			   PVFS_ds_keyval * val_p,
			   PVFS_ds_flags flags,
			   PVFS_vtag * vtag,
			   void *user_ptr,
			   job_aint status_user_tag,
			   job_status_s * out_status_p,
			   job_id_t * id,
			   job_context_id context_id);

/* flush keyval data to storage */
int job_trove_keyval_flush(PVFS_fs_id coll_id,
			    PVFS_handle handle,
			    PVFS_ds_flags flags,
			    void * user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id);

/* read generic dspace attributes */
int job_trove_dspace_getattr(PVFS_fs_id coll_id,
			     PVFS_handle handle,
			     void *user_ptr,
                             PVFS_ds_attributes *out_ds_attr_ptr,
			     job_aint status_user_tag,
			     job_status_s * out_status_p,
			     job_id_t * id,
			     job_context_id context_id);

/* write generic dspace attributes */
int job_trove_dspace_setattr(PVFS_fs_id coll_id,
			     PVFS_handle handle,
			     PVFS_ds_attributes * ds_attr_p,
                             PVFS_ds_flags flags,
			     void *user_ptr,
			     job_aint status_user_tag,
			     job_status_s * out_status_p,
			     job_id_t * id,
			     job_context_id context_id);

/* resize (truncate or preallocate) a storage byte stream */
int job_trove_bstream_resize(PVFS_fs_id coll_id,
			     PVFS_handle handle,
			     PVFS_size size,
			     PVFS_ds_flags flags,
			     PVFS_vtag * vtag,
			     void *user_ptr,
			     job_aint status_user_tag,
			     job_status_s * out_status_p,
			     job_id_t * id,
			     job_context_id context_id);

/* check consistency of a bytestream for a given vtag */
int job_trove_bstream_validate(PVFS_fs_id coll_id,
			       PVFS_handle handle,
			       PVFS_vtag * vtag,
			       void *user_ptr,
			       job_aint status_user_tag,
			       job_status_s * out_status_p,
			       job_id_t * id,
			       job_context_id context_id);

/* remove a key/value entry */
int job_trove_keyval_remove(PVFS_fs_id coll_id,
			    PVFS_handle handle,
			    PVFS_ds_keyval * key_p,
			    PVFS_ds_flags flags,
			    PVFS_vtag * vtag,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id);

/* check consistency of a key/value pair for a given vtag */
int job_trove_keyval_validate(PVFS_fs_id coll_id,
			      PVFS_handle handle,
			      PVFS_vtag * vtag,
			      void *user_ptr,
			      job_aint status_user_tag,
			      job_status_s * out_status_p,
			      job_id_t * id,
			      job_context_id context_id);

/* iterate through all of the key/value pairs for a data space */
int job_trove_keyval_iterate(PVFS_fs_id coll_id,
			     PVFS_handle handle,
			     PVFS_ds_position position,
			     PVFS_ds_keyval * key_array,
			     PVFS_ds_keyval * val_array,
			     int count,
			     PVFS_ds_flags flags,
			     PVFS_vtag * vtag,
			     void *user_ptr,
			     job_aint status_user_tag,
			     job_status_s * out_status_p,
			     job_id_t * id,
			     job_context_id context_id);

/* iterate through all of the keys for a data space */
int job_trove_keyval_iterate_keys(PVFS_fs_id coll_id,
				  PVFS_handle handle,
				  PVFS_ds_position position,
				  PVFS_ds_keyval * key_array,
				  int count,
				  PVFS_ds_flags flags,
				  PVFS_vtag * vtag,
				  void *user_ptr,
				  job_aint status_user_tag,
				  job_status_s * out_status_p,
				  job_id_t * id,
				  job_context_id context_id);

/* iterates through all handles in a collection */
int job_trove_dspace_iterate_handles(PVFS_fs_id coll_id,
				     PVFS_ds_position position,
				     PVFS_handle* handle_array,
				     int count,
				     PVFS_ds_flags flags,
				     PVFS_vtag* vtag,
				     void* user_ptr,
				     job_aint status_user_tag,
				     job_status_s* out_status_p,
				     job_id_t* id,
				     job_context_id context_id);

/* create a new data space object */
int job_trove_dspace_create(PVFS_fs_id coll_id,
			    PVFS_handle_extent_array *handle_extent_array,
			    PVFS_ds_type type,
			    void *hint,
                            PVFS_ds_flags flags,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id);

/* remove an entire data space object (byte stream and key/value) */
int job_trove_dspace_remove(PVFS_fs_id coll_id,
			    PVFS_handle handle,
                            PVFS_ds_flags flags,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id);

/* verify that a given dataspace exists and discover its type */
int job_trove_dspace_verify(PVFS_fs_id coll_id,
			    PVFS_handle handle,
                            PVFS_ds_flags flags,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id);

int job_trove_dspace_cancel(PVFS_fs_id coll_id,
			    job_id_t id,
			    job_context_id context_id);

/* create a new file system */
int job_trove_fs_create(char *collname,
			PVFS_fs_id new_coll_id,
			void *user_ptr,
			job_aint status_user_tag,
			job_status_s * out_status_p,
			job_id_t * id,
			job_context_id context_id);

/* remove an existing file system */
int job_trove_fs_remove(char *collname,
			void *user_ptr,
			job_aint status_user_tag,
			job_status_s * out_status_p,
			job_id_t * id,
			job_context_id context_id);

/* lookup a file system based on a string name */
int job_trove_fs_lookup(char *collname,
			void *user_ptr,
			job_aint status_user_tag,
			job_status_s * out_status_p,
			job_id_t * id,
			job_context_id context_id);

/* set extended attributes for a file system */
int job_trove_fs_seteattr(PVFS_fs_id coll_id,
			  PVFS_ds_keyval * key_p,
			  PVFS_ds_keyval * val_p,
			  PVFS_ds_flags flags,
			  void *user_ptr,
			  job_aint status_user_tag,
			  job_status_s * out_status_p,
			  job_id_t * id,
			  job_context_id context_id);

/* read extended attributes for a file system */
int job_trove_fs_geteattr(PVFS_fs_id coll_id,
			  PVFS_ds_keyval * key_p,
			  PVFS_ds_keyval * val_p,
			  PVFS_ds_flags flags,
			  void *user_ptr,
			  job_aint status_user_tag,
			  job_status_s * out_status_p,
			  job_id_t * id,
			  job_context_id context_id);



/******************************************************************
 * job test/wait for completion functions 
 */

int job_test(job_id_t id,
	     int *out_count_p,
	     void **returned_user_ptr_p,
	     job_status_s * out_status_p,
	     int timeout_ms,
	     job_context_id context_id);

int job_testsome(job_id_t * id_array,
		 int *inout_count_p,
		 int *out_index_array,
		 void **returned_user_ptr_array,
		 job_status_s * out_status_array_p,
		 int timeout_ms,
		 job_context_id context_id);

int job_testcontext(job_id_t * out_id_array_p,
		  int *inout_count_p,
		  void **returned_user_ptr_array,
		  job_status_s * out_status_array_p,
		  int timeout_ms,
		  job_context_id context_id);

#endif /* __JOB_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

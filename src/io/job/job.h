/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this file contains prototypes for the job interface */

#ifndef __JOB_H
#define __JOB_H

#include <inttypes.h>

#include "src/io/flow/flow.h"
#include "bmi.h"
#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-req-proto.h"
#include "pint-dev.h"
#include "src/server/request-scheduler/request-scheduler.h"

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
    PVFS_size actual_size;	/* resize, bmi_recv */
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


#define JOB_TIMEOUT_INF (-1)

/******************************************************************
 * management functions
 */

int job_initialize(int flags);

int job_finalize(void);

int job_open_context(job_context_id* context_id);

void job_close_context(job_context_id context_id);

int job_reset_timeout(job_id_t id, int timeout_sec);

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
                 job_context_id context_id,
                 int timeout_sec,
                 PVFS_hint hints);

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
                      job_context_id context_id,
                      int timeout_sec,
                      PVFS_hint hints);

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
                 job_context_id context_id,
                 int timeout_sec,
                 PVFS_hint hints);

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
                      job_context_id context_id,
                      int timeout_sec,
                      PVFS_hint hints);

/* unexpected network receive */
int job_bmi_unexp(struct BMI_unexpected_info *bmi_unexp_d,
                  void *user_ptr,
                  job_aint status_user_tag,
                  job_status_s * out_status_p,
                  job_id_t * id,
                  enum job_flags flags,
                  job_context_id context_id);

int job_bmi_unexp_cancel(job_id_t id);

int job_bmi_cancel(job_id_t id,
                   job_context_id context_id);

/* unexpected device receive */
int job_dev_unexp(struct PINT_dev_unexp_info* dev_unexp_d,
                  void* user_ptr,
                  job_aint status_user_tag,
                  job_status_s * out_status_p,
                  job_id_t* id,
                  enum job_flags flags,
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
int job_req_sched_post(enum PVFS_server_op op,
                       PVFS_fs_id fs_id,
                       PVFS_handle handle,
                       enum PINT_server_req_access_type access_type,
                       enum PINT_server_sched_policy sched_policy,
		       void *user_ptr,
		       job_aint status_user_tag,
		       job_status_s * out_status_p,
		       job_id_t * id,
		       job_context_id context_id);

/* change the mode */
int job_req_sched_change_mode(enum PVFS_server_mode mode,
                              void *user_ptr,
                              job_aint status_user_tag,
                              job_status_s *out_status_p,
                              job_id_t *id,
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
             job_context_id context_id,
             int timeout_sec,
             PVFS_hint hints);

int job_flow_cancel(job_id_t id, job_context_id context_id);

/* storage byte stream write */
int job_trove_bstream_write_list(PVFS_fs_id coll_id,
                                 PVFS_handle handle,
                                 char **mem_offset_array,
                                 PVFS_size *mem_size_array,
                                 int mem_count,
                                 PVFS_offset *stream_offset_array,
                                 PVFS_size *stream_size_array,
                                 int stream_count,
                                 PVFS_size *out_size_p,
                                 PVFS_ds_flags flags,
                                 PVFS_vtag *vtag,
                                 void * user_ptr,
                                 job_aint status_user_tag,
                                 job_status_s * out_status_p,
                                 job_id_t * id,
                                 job_context_id context_id,
                                 PVFS_hint hints);


/* storage byte stream read */

int job_trove_bstream_read_list(PVFS_fs_id coll_id,
                                PVFS_handle handle,
                                char **mem_offset_array,
                                PVFS_size *mem_size_array,
                                int mem_count,
                                PVFS_offset *stream_offset_array,
                                PVFS_size *stream_size_array,
                                int stream_count,
                                PVFS_size *out_size_p,
                                PVFS_ds_flags flags,
                                PVFS_vtag *vtag,
                                void * user_ptr,
                                job_aint status_user_tag,
                                job_status_s * out_status_p,
                                job_id_t * id,
                                job_context_id context_id,
                                PVFS_hint hints);

/* byte stream flush to storage */
int job_trove_bstream_flush(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s * out_status_p,
                            job_id_t * id,
                            job_context_id context_id,
                            PVFS_hint hints);

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
                          job_context_id context_id,
                          PVFS_hint hints);

/* storage key/value read list */
int job_trove_keyval_read_list(PVFS_fs_id coll_id,
                               PVFS_handle handle,
                               PVFS_ds_keyval * key_array,
                               PVFS_ds_keyval * val_array,
                               PVFS_error * err_array,
                               int count,
                               PVFS_ds_flags flags,
                               PVFS_vtag * vtag,
                               void *user_ptr,
                               job_aint status_user_tag,
                               job_status_s * out_status_p,
                               job_id_t * id,
                               job_context_id context_id,
                               PVFS_hint hints);

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
                           job_context_id context_id,
                           PVFS_hint hints);

/* storage key/value write list */
int job_trove_keyval_write_list(PVFS_fs_id coll_id,
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
                                job_context_id context_id,
                                PVFS_hint hints);

/* flush keyval data to storage */
int job_trove_keyval_flush(PVFS_fs_id coll_id,
                           PVFS_handle handle,
                           PVFS_ds_flags flags,
                           void * user_ptr,
                           job_aint status_user_tag,
                           job_status_s * out_status_p,
                           job_id_t * id,
                           job_context_id context_id,
                           PVFS_hint hints);

/* get handle info for a keyval */
int job_trove_keyval_get_handle_info(PVFS_fs_id coll_id,
                                     PVFS_handle handle,
                                     PVFS_ds_flags flags,
                                     PVFS_ds_keyval_handle_info *info,
                                     void *user_ptr,
                                     job_aint status_user_tag,
                                     job_status_s * out_status_p,
                                     job_id_t * id,
                                     job_context_id context_id,
                                     PVFS_hint hints);

/* read generic dspace attributes */
int job_trove_dspace_getattr(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             void *user_ptr,
                             PVFS_ds_attributes *out_ds_attr_ptr,
                             job_aint status_user_tag,
                             job_status_s * out_status_p,
                             job_id_t * id,
                             job_context_id context_id,
                             PVFS_hint hints);

/* read generic dspace attributes for a set of handles */
int job_trove_dspace_getattr_list(PVFS_fs_id coll_id,
                                  int nhandles,
                                  PVFS_handle *handle_array,
                                  void *user_ptr,
                                  PVFS_error *out_error_array,
                                  PVFS_ds_attributes *out_ds_attr_ptr,
                                  job_aint status_user_tag,
                                  job_status_s *out_status_p,
                                  job_id_t *id,
                                  job_context_id context_id,
                                  PVFS_hint hints);

/* write generic dspace attributes */
int job_trove_dspace_setattr(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             PVFS_ds_attributes * ds_attr_p,
                             PVFS_ds_flags flags,
                             void *user_ptr,
                             job_aint status_user_tag,
                             job_status_s * out_status_p,
                             job_id_t * id,
                             job_context_id context_id,
                             PVFS_hint hints);

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
                             job_context_id context_id,
                             PVFS_hint hints);

/* check consistency of a bytestream for a given vtag */
int job_trove_bstream_validate(PVFS_fs_id coll_id,
                               PVFS_handle handle,
                               PVFS_vtag * vtag,
                               void *user_ptr,
                               job_aint status_user_tag,
                               job_status_s * out_status_p,
                               job_id_t * id,
                               job_context_id context_id,
                               PVFS_hint hints);

/* remove a key/value entry */
int job_trove_keyval_remove(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_keyval * key_p,
                            PVFS_ds_keyval * val_p,
                            PVFS_ds_flags flags,
                            PVFS_vtag * vtag,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s * out_status_p,
                            job_id_t * id,
                            job_context_id context_id,
                            PVFS_hint hints);

/* remove a list of key/value entries */
int job_trove_keyval_remove_list(PVFS_fs_id coll_id,
                                  PVFS_handle handle,
                                  PVFS_ds_keyval * key_a,
                                  PVFS_ds_keyval * val_a,
                                  int * error_a,
                                  int count,
                                  PVFS_ds_flags flags,
                                  PVFS_vtag * vtag,
                                  void *user_ptr,
                                  job_aint status_user_tag,
                                  job_status_s * out_status_p,
                                  job_id_t * id,
                                  job_context_id context_id,
                                  PVFS_hint hints);

/* check consistency of a key/value pair for a given vtag */
int job_trove_keyval_validate(PVFS_fs_id coll_id,
                              PVFS_handle handle,
                              PVFS_vtag * vtag,
                              void *user_ptr,
                              job_aint status_user_tag,
                              job_status_s * out_status_p,
                              job_id_t * id,
                              job_context_id context_id,
                              PVFS_hint hints);

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
                             job_context_id context_id,
                             PVFS_hint hints);

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
                                  job_context_id context_id,
                                  PVFS_hint hints);

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
                            job_context_id context_id,
                            PVFS_hint hints);

/* create a set of new data space objects */
int job_trove_dspace_create_list(PVFS_fs_id coll_id,
			    PVFS_handle_extent_array *handle_extent_array,
                            PVFS_handle* out_handle_arry,
                            int count,
			    PVFS_ds_type type,
			    void *hint,
                            PVFS_ds_flags flags,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id,
                            PVFS_hint hints);

/* remove an entire data space object (byte stream and key/value) */
int job_trove_dspace_remove(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s * out_status_p,
                            job_id_t * id,
                            job_context_id context_id,
                            PVFS_hint hints);

/* remove a list of data space objects (byte stream and key/value) */
int job_trove_dspace_remove_list(PVFS_fs_id coll_id,
			    PVFS_handle* handle_array,
                            PVFS_error *out_error_array,
                            int count,
                            PVFS_ds_flags flags,
			    void *user_ptr,
			    job_aint status_user_tag,
			    job_status_s * out_status_p,
			    job_id_t * id,
			    job_context_id context_id,
                            PVFS_hint hints);

/* verify that a given dataspace exists and discover its type */
int job_trove_dspace_verify(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s * out_status_p,
                            job_id_t * id,
                            job_context_id context_id,
                            PVFS_hint hints);

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
                          job_context_id context_id,
                          PVFS_hint hints);

/* read extended attributes for a file system */
int job_trove_fs_geteattr(PVFS_fs_id coll_id,
                          PVFS_ds_keyval * key_p,
                          PVFS_ds_keyval * val_p,
                          PVFS_ds_flags flags,
                          void *user_ptr,
                          job_aint status_user_tag,
                          job_status_s * out_status_p,
                          job_id_t * id,
                          job_context_id context_id,
                          PVFS_hint hints);

int job_null(
    int error_code,
    void *user_ptr,
    job_aint status_user_tag,
    job_status_s * out_status_p,
    job_id_t * id,
    job_context_id context_id);

int job_precreate_pool_fill(
    PVFS_handle precreate_pool,
    PVFS_fs_id fsid,
    PVFS_handle* precreate_handle_array,
    int precreate_handle_count,
    void *user_ptr,
    job_aint status_user_tag,
    job_status_s * out_status_p,
    job_id_t * id,
    job_context_id context_id,
    PVFS_hint hints);
 
int job_precreate_pool_fill_signal_error(
    PVFS_handle precreate_pool,
    PVFS_fs_id fsid,
    int error_code,
    void *user_ptr,
    job_aint status_user_tag,
    job_status_s * out_status_p,
    job_id_t * id,
    job_context_id context_id);

int job_precreate_pool_check_level(
    PVFS_handle precreate_pool,
    PVFS_fs_id fsid,
    int low_threshold,
    void *user_ptr,
    job_aint status_user_tag,
    job_status_s * out_status_p,
    job_id_t * id,
    job_context_id context_id);

int job_precreate_pool_iterate_handles(
    PVFS_fs_id fsid,
    PVFS_ds_position position,
    PVFS_handle* handle_array,
    int count,
    PVFS_ds_flags flags,
    PVFS_vtag* vtag,
    void* user_ptr,
    job_aint status_user_tag,
    job_status_s* out_status_p,
    job_id_t* id,
    job_context_id context_id,
    PVFS_hint hints);

int job_precreate_pool_get_handles(
    PVFS_fs_id fsid,
    int count,
    const char** servers,
    PVFS_handle* handle_array,
    PVFS_ds_flags flags,
    void *user_ptr,
    job_aint status_user_tag,
    job_status_s * out_status_p,
    job_id_t * id,
    job_context_id context_id,
    PVFS_hint hints);

int job_precreate_pool_register_server(
    const char* host, 
    PVFS_fs_id fsid, 
    PVFS_handle pool_handle, 
    int count);
 
int job_precreate_pool_lookup_server(
    const char* host, 
    PVFS_fs_id fsid, 
    PVFS_handle* pool_handle);
  
void job_precreate_pool_set_index(
    int server_index);

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
 * vim: ts=8 sts=4 sw=4 expandtab
 */

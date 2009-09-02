/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \defgroup troveint Trove storage interface
 *
 *  The Trove storage interface provides functionality used for
 *  interaction with underlying storage devices.  The PVFS2 server
 *  builds on this interface, and the default Flow protocol also
 *  builds on it.
 *
 * @{
 */

/** \file
 *  Declarations and prototypes for Trove storage interface.
 */

#ifndef __TROVE_H
#define __TROVE_H

#include <limits.h>
#include <errno.h>

#include "pvfs2-config.h"
#include "pvfs2-debug.h"
#include "pvfs2-req-proto.h"

#include "trove-types.h"

#define TROVE_MAX_CONTEXTS                  16
#define TROVE_DEFAULT_TEST_TIMEOUT          10

enum
{
    TROVE_ITERATE_START = PVFS_ITERATE_START,
    TROVE_ITERATE_END   = PVFS_ITERATE_END 
};

enum
{
    TROVE_PLAIN_FILE,
    TROVE_DIR,
};

/* TROVE_ds_flags */
/* TROVE operation flags */
enum
{
    TROVE_SYNC                   = 1,
    TROVE_ATOMIC                 = 1 << 1,
    TROVE_FORCE_REQUESTED_HANDLE = 1 << 2,

    /* keyval_write and keyval_write_list */
    TROVE_NOOVERWRITE            = 1 << 3, 
    TROVE_ONLYOVERWRITE          = 1 << 4,

    TROVE_DB_CACHE_MMAP          = 1 << 5,
    TROVE_DB_CACHE_SYS           = 1 << 6,
    TROVE_KEYVAL_HANDLE_COUNT    = 1 << 7,
    TROVE_BINARY_KEY             = 1 << 8, /* tell trove this is a binary key */
    TROVE_KEYVAL_ITERATE_REMOVE  = 1 << 9  /* tell trove to delete keyvals as it iterates */
};

enum
{
    TROVE_EXP_ROOT_SQUASH = 1,
    TROVE_EXP_READ_ONLY   = 2,
    TROVE_EXP_ALL_SQUASH  = 4,
};

/* get/setinfo option flags */
enum
{
    TROVE_COLLECTION_HANDLE_RANGES,
    TROVE_COLLECTION_HANDLE_TIMEOUT,
    TROVE_COLLECTION_ATTR_CACHE_KEYWORDS,
    TROVE_COLLECTION_ATTR_CACHE_SIZE,
    TROVE_COLLECTION_ATTR_CACHE_MAX_NUM_ELEMS,
    TROVE_COLLECTION_ATTR_CACHE_INITIALIZE,
    TROVE_DB_CACHE_SIZE_BYTES,
    TROVE_ALT_AIO_MODE,
    TROVE_MAX_CONCURRENT_IO,
    TROVE_COLLECTION_COALESCING_HIGH_WATERMARK,
    TROVE_COLLECTION_COALESCING_LOW_WATERMARK,
    TROVE_COLLECTION_META_SYNC_MODE,
    TROVE_COLLECTION_IMMEDIATE_COMPLETION,
    TROVE_SHM_KEY_HINT,
    TROVE_DIRECTIO_THREADS_NUM,
    TROVE_DIRECTIO_OPS_PER_QUEUE,
    TROVE_DIRECTIO_TIMEOUT
};

/** Initializes the Trove layer.  Must be called before any other Trove
 *  functions.
 */
int trove_initialize(
    TROVE_method_id method_id,
    TROVE_method_callback method_callback,
    char *stoname,
    TROVE_ds_flags flags);

int trove_finalize(TROVE_method_id method_id);

int trove_migrate(TROVE_method_id method_id, const char* stoname);

int trove_open_context(
    TROVE_coll_id coll_id,
    TROVE_context_id *context_id);

int trove_close_context(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id);

int trove_collection_clear(
    TROVE_method_id method_id,
    TROVE_coll_id coll_id);

int trove_storage_create(
    TROVE_method_id method_id,
    char *stoname,
    void *user_ptr,
    TROVE_op_id *out_op_id_p);

int trove_storage_remove(
    TROVE_method_id method_id,
    char *stoname,
    void *user_ptr,
    TROVE_op_id *out_op_id_p);

int trove_collection_create(
/* char *stoname, */
    char *collname,
    TROVE_coll_id new_coll_id,
    void *user_ptr,
    TROVE_op_id *out_op_id_p);

int trove_collection_remove(
    TROVE_method_id method_id,
/* char *stoname, */
    char *collname,
    void *user_ptr,
    TROVE_op_id *out_op_id_p);

int trove_collection_lookup(
    TROVE_method_id method_id,
/* char *stoname, */
    char *collname,
    TROVE_coll_id *out_coll_id_p,
    void *user_ptr,
    TROVE_op_id *out_op_id_p);

int trove_collection_iterate(
    TROVE_method_id method_id,
    TROVE_ds_position *inout_position_p,
    TROVE_keyval_s *name_array,
    TROVE_coll_id *coll_id_array,
    int *inout_count_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s *vtag,
    void *user_ptr,
    TROVE_op_id *out_op_id_p);

int trove_bstream_read_at(
                          TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  void *buffer,
			  TROVE_size *inout_size_p,
			  TROVE_offset offset,
			  TROVE_ds_flags flags,
			  TROVE_vtag_s *vtag, 
			  void *user_ptr,
			  TROVE_context_id context_id,
			  TROVE_op_id *out_op_id_p,
              PVFS_hint hints);

int trove_bstream_write_at(
                           TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   void *buffer,
			   TROVE_size *inout_size_p,
			   TROVE_offset offset,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *vtag,
			   void *user_ptr,
			   TROVE_context_id context_id,
			   TROVE_op_id *out_op_id_p,
               PVFS_hint hints);

int trove_bstream_resize(
                         TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_size *inout_size_p,
			 TROVE_ds_flags flags,
			 TROVE_vtag_s *vtag,
			 void *user_ptr,
			 TROVE_context_id context_id,
			 TROVE_op_id *out_op_id_p,
             PVFS_hint hints);

int trove_bstream_validate(
                           TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *vtag,
			   void *user_ptr,
			   TROVE_context_id context_id,
			   TROVE_op_id *out_op_id_p,
               PVFS_hint hints);

int trove_bstream_read_list(
                            TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    char **mem_offset_array, 
			    TROVE_size *mem_size_array,
			    int mem_count,
			    TROVE_offset *stream_offset_array, 
			    TROVE_size *stream_size_array,
			    int stream_count,
			    TROVE_size *out_size_p,
			    TROVE_ds_flags flags, 
			    TROVE_vtag_s *vtag,
			    void *user_ptr,
			    TROVE_context_id context_id,
			    TROVE_op_id *out_op_id_p,
                PVFS_hint hints);

int trove_bstream_write_list(
                             TROVE_coll_id coll_id,
			     TROVE_handle handle,
			     char **mem_offset_array, 
			     TROVE_size *mem_size_array,
			     int mem_count,
			     TROVE_offset *stream_offset_array, 
			     TROVE_size *stream_size_array,
			     int stream_count,
			     TROVE_size *out_size_p,
			     TROVE_ds_flags flags, 
			     TROVE_vtag_s *vtag,
			     void *user_ptr,
			     TROVE_context_id context_id,
			     TROVE_op_id *out_op_id_p,
                 PVFS_hint hints);

int trove_bstream_flush(TROVE_coll_id coll_id,
                        TROVE_handle handle,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
            PVFS_hint hints);

int trove_keyval_read(
		      TROVE_coll_id coll_id,
		      TROVE_handle handle,
		      TROVE_keyval_s *key_p,
		      TROVE_keyval_s *val_p,
		      TROVE_ds_flags flags,
		      TROVE_vtag_s *vtag, 
		      void *user_ptr,
		      TROVE_context_id context_id,
		      TROVE_op_id *out_op_id_p,
              PVFS_hint hints);

int trove_keyval_write(
		       TROVE_coll_id coll_id,
		       TROVE_handle handle,
		       TROVE_keyval_s *key_p,
		       TROVE_keyval_s *val_p,
		       TROVE_ds_flags flags,
		       TROVE_vtag_s *vtag,
		       void *user_ptr,
		       TROVE_context_id context_id,
		       TROVE_op_id *out_op_id_p,
               PVFS_hint hints);

int trove_keyval_remove(
			TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_keyval_s *key_p,
                        TROVE_keyval_s *val_p,
			TROVE_ds_flags flags,
			TROVE_vtag_s *vtag,
			void *user_ptr,
		        TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
            PVFS_hint hints);

int trove_keyval_validate(
			  TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  TROVE_ds_flags flags,
			  TROVE_vtag_s *vtag,
			  void* user_ptr,
		          TROVE_context_id context_id,
			  TROVE_op_id *out_op_id_p,
              PVFS_hint hints);

int trove_keyval_iterate(
			 TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_ds_position *position_p,
			 TROVE_keyval_s *key_array,
			 TROVE_keyval_s *val_array,
			 int *inout_count_p,
			 TROVE_ds_flags flags,
			 TROVE_vtag_s *vtag,
			 void *user_ptr,
		         TROVE_context_id context_id,
			 TROVE_op_id *out_op_id_p,
             PVFS_hint hints);

int trove_keyval_iterate_keys(
			      TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      TROVE_ds_position *position_p,
			      TROVE_keyval_s *key_array,
			      int *inout_count_p,
			      TROVE_ds_flags flags,
			      TROVE_vtag_s *vtag,
			      void *user_ptr,
			      TROVE_context_id context_id,
			      TROVE_op_id *out_op_id_p,
                  PVFS_hint hints);

int trove_keyval_read_list(TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   TROVE_keyval_s *key_array,
			   TROVE_keyval_s *val_array,
                           TROVE_ds_state *err_array,
			   int count,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *vtag,
			   void *user_ptr,
			   TROVE_context_id context_id,
			   TROVE_op_id *out_op_id_p,
               PVFS_hint hints);

int trove_keyval_write_list(
			    TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    TROVE_keyval_s *key_array,
			    TROVE_keyval_s *val_array,
			    int count,
			    TROVE_ds_flags flags,
			    TROVE_vtag_s *vtag,
			    void *user_ptr,
			    TROVE_context_id context_id,
			    TROVE_op_id *out_op_id_p,
                PVFS_hint hints);

int trove_keyval_remove_list(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_keyval_s *key_array,
                             TROVE_keyval_s *val_array,
                             int *error_array,
                             int count,
                             TROVE_ds_flags flags,
                             TROVE_vtag_s *vtag,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p,
                             PVFS_hint hints);

int trove_keyval_flush(TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
            PVFS_hint hints);

int trove_keyval_get_handle_info(TROVE_coll_id coll_id,
                                 TROVE_handle handle,
                                 TROVE_ds_flags flags,
                                 TROVE_keyval_handle_info *info,
                                 void * user_ptr,
                                 TROVE_context_id context_id,
                                 TROVE_op_id *out_op_id_p,
                                 PVFS_hint hints);

int trove_dspace_create(TROVE_coll_id coll_id,
			TROVE_handle_extent_array *handle_extent_array,
                        TROVE_handle *out_handle,
			TROVE_ds_type type,
			TROVE_keyval_s *hint,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
            PVFS_hint hints);

int trove_dspace_create_list(TROVE_coll_id coll_id,
			TROVE_handle_extent_array *handle_extent_array,
                        TROVE_handle *out_handle_array,
                        int count,
			TROVE_ds_type type,
			TROVE_keyval_s *hint,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
                        PVFS_hint hints);

int trove_dspace_remove(TROVE_coll_id coll_id,
			TROVE_handle handle,
                        TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
            PVFS_hint hints);

int trove_dspace_remove_list(TROVE_coll_id coll_id,
			TROVE_handle* handle_array,
                	TROVE_ds_state  *error_array,
                        int count,
                        TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
                        PVFS_hint hints);

int trove_dspace_iterate_handles(TROVE_coll_id coll_id,
				 TROVE_ds_position *position_p,
				 TROVE_handle *handle_array,
				 int *inout_count_p,
				 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_context_id context_id,
				 TROVE_op_id *out_op_id_p);

int trove_dspace_verify(TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_ds_type *type,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_context_id context_id,
			TROVE_op_id *out_op_id_p,
            PVFS_hint hints);


int trove_dspace_getattr(TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_ds_attributes_s *ds_attr_p,
			 TROVE_ds_flags flags,
			 void *user_ptr,
			 TROVE_context_id context_id,
			 TROVE_op_id *out_op_id_p,
             PVFS_hint hints);

int trove_dspace_getattr_list(TROVE_coll_id coll_id,
                         int nhandles,
                         TROVE_handle *handle_array,
                         TROVE_ds_attributes_s *ds_attr_p,
                	 TROVE_ds_state  *error_array,
                         TROVE_ds_flags flags,
                         void* user_ptr,
                         TROVE_context_id context_id,
                         TROVE_op_id* out_op_id_p,
                         PVFS_hint hints);

int trove_dspace_setattr(TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_ds_attributes_s *ds_attr_p,
			 TROVE_ds_flags flags,
			 void *user_ptr,
			 TROVE_context_id context_id,
			 TROVE_op_id *out_op_id_p,
             PVFS_hint hints);

int trove_dspace_cancel(TROVE_coll_id coll_id,
                        TROVE_op_id id,
                        TROVE_context_id context_id);

int trove_dspace_test(TROVE_coll_id coll_id,
		      TROVE_op_id id,
		      TROVE_context_id context_id,
		      int *out_count_p,
		      TROVE_vtag_s *vtag,
		      void **returned_user_ptr_p,
		      TROVE_ds_state *state_p,
		      int max_idle_time_ms);

int trove_dspace_testsome(TROVE_coll_id coll_id,
			  TROVE_context_id context_id,
			  TROVE_op_id *ds_id_array,
			  int *inout_count_p,
			  int *out_index_array,
			  TROVE_vtag_s *vtag_array,
			  void **returned_user_ptr_array,
			  TROVE_ds_state *state_array,
			  int max_idle_time_ms);

int trove_dspace_testcontext(TROVE_coll_id coll_id,
			     TROVE_op_id *ds_id_array,
			     int *inout_count_p,
			     TROVE_ds_state *state_array,
			     void** user_ptr_array,
			     int max_idle_time_ms,
			     TROVE_context_id context_id);

int trove_collection_geteattr(
			      TROVE_coll_id coll_id,
			      TROVE_keyval_s *key_p,
			      TROVE_keyval_s *val_p,
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_context_id context_id,
			      TROVE_op_id *out_op_id_p);

int trove_collection_seteattr(
			      TROVE_coll_id coll_id,
			      TROVE_keyval_s *key_p,
			      TROVE_keyval_s *val_p,
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_context_id context_id,
			      TROVE_op_id *out_op_id_p);

int trove_collection_getinfo(
			     TROVE_coll_id coll_id,
			     TROVE_context_id context_id,
			     TROVE_coll_getinfo_options opt,
			     void *parameter);

int trove_collection_setinfo(
			     TROVE_coll_id coll_id,
			     TROVE_context_id context_id,
			     int option,
			     void *parameter);

#endif

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_INTERNAL_H
#define __TROVE_INTERNAL_H

#include <trove-types.h>

int map_coll_id_to_method(int coll_id);


/* These structures contains the function pointers that should be provided
 * by valid trove "method" implementations
 */

struct TROVE_bstream_ops
{
    int (*bstream_read_at)(
			   TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   void *buffer,
			   TROVE_size *inout_size_p,
			   TROVE_offset offset,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *out_vtag, 
			   void *user_ptr,
			   TROVE_op_id *out_op_id_p);
    
    int (*bstream_write_at)(
			    TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    void *buffer,
			    TROVE_size *inout_size_p,
			    TROVE_offset offset,
			    TROVE_ds_flags flags,
			    TROVE_vtag_s *inout_vtag,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);
    
    int (*bstream_resize)(
			  TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  TROVE_size *inout_size_p,
			  TROVE_ds_flags flags,
			  TROVE_vtag_s *vtag,
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);
    
    int (*bstream_validate)(
			    TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    TROVE_ds_flags flags,
			    TROVE_vtag_s *vtag,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);
    
    int (*bstream_read_list)(
			     TROVE_coll_id coll_id,
			     TROVE_handle handle,
			     char **mem_offset_array, 
			     TROVE_size *mem_size_array,
			     int mem_count,
			     TROVE_offset *stream_offset_array, 
			     TROVE_size *stream_size_array,
			     int stream_count,
			     TROVE_size *out_size_p, /* status indicates partial */
			     TROVE_ds_flags flags, 
			     TROVE_vtag_s *out_vtag,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p);
    
    int (*bstream_write_list)(
			      TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      char **mem_offset_array, 
			      TROVE_size *mem_size_array,
			      int mem_count,
			      TROVE_offset *stream_offset_array, 
			      TROVE_size *stream_size_array,
			      int stream_count,
			      TROVE_size *out_size_p, /* status indicates partial */
			      TROVE_ds_flags flags, 
			      TROVE_vtag_s *inout_vtag,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p);
};

struct TROVE_keyval_ops
{
    int (*keyval_read)(
		       TROVE_coll_id coll_id,
		       TROVE_handle handle,
		       TROVE_keyval_s *key_p,
		       TROVE_keyval_s *val_p,
		       TROVE_ds_flags flags,
		       TROVE_vtag_s *out_vtag, 
		       void *user_ptr,
		       TROVE_op_id *out_op_id_p);
    
    int (*keyval_write)(
			TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_keyval_s *key_p,
			TROVE_keyval_s *val_p,
			TROVE_ds_flags flags,
			TROVE_vtag_s *inout_vtag,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);
    
    int (*keyval_remove)(
			 TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_keyval_s *key_p,
			 TROVE_ds_flags flags,
			 TROVE_vtag_s *inout_vtag,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);
    
    int (*keyval_validate)(
			   TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *inout_vtag,
			   void* user_ptr,
			   TROVE_op_id *out_op_id_p);
    
    int (*keyval_iterate)(
			  TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  TROVE_ds_position *inout_position_p,
			  TROVE_keyval_s *out_key_array,
			  TROVE_keyval_s *out_val_array,
			  int *inout_count_p,
			  TROVE_ds_flags flags,
			  TROVE_vtag_s *inout_vtag,
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);
    
    int (*keyval_iterate_keys)(
			       TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_ds_position *inout_position_p,
			       TROVE_keyval_s *out_key_array,
			       int *inout_count_p,
			       TROVE_ds_flags flags,
			       TROVE_vtag_s *vtag,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p);
    
    int (*keyval_read_list)(
			    TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    TROVE_keyval_s *key_array,
			    TROVE_keyval_s *val_array,
			    int count,
			    TROVE_ds_flags flags,
			    TROVE_vtag_s *out_vtag,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);
    
    int (*keyval_write_list)(
			     TROVE_coll_id coll_id,
			     TROVE_handle handle,
			     TROVE_keyval_s *key_array,
			     TROVE_keyval_s *val_array,
			     int count,
			     TROVE_ds_flags flags,
			     TROVE_vtag_s *inout_vtag,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p);
};

struct TROVE_dspace_ops
{
    int (*dspace_create)(
			 TROVE_coll_id coll_id,
			 TROVE_handle *handle,
			 TROVE_handle bitmask,
			 TROVE_ds_type type,
			 TROVE_keyval_s *hint, /* TODO: figure out what this is! */
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);
    
    int (*dspace_remove)(
			 TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);

    int (*dspace_iterate_handles)(
			 	  TROVE_coll_id coll_id,
				  TROVE_ds_position *position_p,
			 	  TROVE_handle *handle_array,
				  int *inout_count_p,
		 		  TROVE_ds_flags flags,
				  TROVE_vtag_s *vtag,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p);

    int (*dspace_verify)(
			 TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_ds_type *type, /* TODO: define types! */
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);
    
    int (*dspace_getattr)(
			  TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  TROVE_ds_attributes_s *ds_attr_p, 
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);
    
    int (*dspace_setattr)(
			  TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  TROVE_ds_attributes_s *ds_attr_p, 
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);
    
    int (*dspace_test)(
		       TROVE_coll_id coll_id,
		       TROVE_op_id ds_id,
		       int *out_count_p,
		       TROVE_vtag_s *vtag,
		       void **returned_user_ptr_p,
		       TROVE_ds_state *out_state_p);
    
    int (*dspace_testsome)(
			   TROVE_coll_id coll_id,
			   TROVE_op_id *ds_id_array,
			   int *inout_count_p,
			   int *out_index_array,
			   TROVE_vtag_s *vtag_array,
			   void **returned_user_ptr_array,
			   TROVE_ds_state *out_state_array);
};

struct TROVE_mgmt_ops
{
    int (*initialize)(
		      char *stoname,
		      TROVE_ds_flags flags,
		      char **method_name_p,
		      int method_id);
    
    int (*finalize)(void);
    
    int (*storage_create)(
			  char *stoname,
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);
    
    int (*storage_remove)(
			  char *stoname,
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);
    
    int (*collection_create)(
			     /* char *stoname, */
			     char *collname,
			     TROVE_coll_id new_coll_id,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p);
    
    int (*collection_remove)(
			     /* char *stoname, */
			     char *collname,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p);
    
    int (*collection_lookup)(
			     /* char *stoname, */
			     char *collname,
			     TROVE_coll_id *coll_id_p,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p);
    
    /* Note: setinfo and getinfo always return immediately */
    int (*collection_setinfo)(
			      TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      int option,
			      void *parameter);
    
    int (*collection_getinfo)(
			      TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      int option,
			      void *parameter);
    
    int (*collection_seteattr)(
			       TROVE_coll_id coll_id,
			       TROVE_keyval_s *key_p,
			       TROVE_keyval_s *val_p,
			       TROVE_ds_flags flags,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p);
    
    int (*collection_geteattr)(
			       TROVE_coll_id coll_id,
			       TROVE_keyval_s *key_p,
			       TROVE_keyval_s *val_p,
			       TROVE_ds_flags flags,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p);
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4
 */

#endif

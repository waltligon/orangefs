/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_PROTO_H
#define __TROVE_PROTO_H

#include <trove.h>
#include <trove-types.h>

/* NOTE: ONLY PROTOTYPES FOR EXTERNAL API FUNCTIONS SHOULD BE IN HERE */


int trove_initialize(char *stoname,
		     TROVE_ds_flags flags,
		     char **method_name_p,
		     int method_id);

int trove_finalize(void);

int trove_storage_create(char *stoname,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);

int trove_storage_remove(char *stoname,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);

int trove_collection_create(/* char *stoname, */
			    char *collname,
			    TROVE_coll_id new_coll_id,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);

int trove_collection_remove(/* char *stoname, */
			    char *collname,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);

int trove_collection_lookup(/* char *stoname, */
			    char *collname,
			    TROVE_coll_id *out_coll_id_p,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);

int trove_collection_iterate(TROVE_ds_position *inout_position_p,
			     TROVE_keyval_s *name_array,
			     TROVE_coll_id *coll_id_array,
			     int *inout_count_p,
			     TROVE_ds_flags flags,
			     TROVE_vtag_s *vtag,
			     void *user_ptr,
			     TROVE_op_id *out_op_id_p);

/* NOTE: DON'T PUT COMMENTS IN THIS SECTION!  THESE ARE USED TO AUTOMATICALLY GENERATE CODE
 */
/* BEGIN AUTOGEN PROTOTYPES */
int trove_bstream_read_at(TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  void *buffer,
			  TROVE_size *inout_size_p,
			  TROVE_offset offset,
			  TROVE_ds_flags flags,
			  TROVE_vtag_s *vtag, 
			  void *user_ptr,
			  TROVE_op_id *out_op_id_p);

int trove_bstream_write_at(TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   void *buffer,
			   TROVE_size *inout_size_p,
			   TROVE_offset offset,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *vtag,
			   void *user_ptr,
			   TROVE_op_id *out_op_id_p);

int trove_bstream_resize(TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_size *inout_size_p,
			 TROVE_ds_flags flags,
			 TROVE_vtag_s *vtag,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);

int trove_bstream_validate(TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *vtag,
			   void *user_ptr,
			   TROVE_op_id *out_op_id_p);

int trove_bstream_read_list(TROVE_coll_id coll_id,
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
			    TROVE_op_id *out_op_id_p);

int trove_bstream_write_list(TROVE_coll_id coll_id,
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
			     TROVE_op_id *out_op_id_p);

int trove_bstream_flush(TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);

int trove_keyval_read(
		      TROVE_coll_id coll_id,
		      TROVE_handle handle,
		      TROVE_keyval_s *key_p,
		      TROVE_keyval_s *val_p,
		      TROVE_ds_flags flags,
		      TROVE_vtag_s *vtag, 
		      void *user_ptr,
		      TROVE_op_id *out_op_id_p);

int trove_keyval_write(
		       TROVE_coll_id coll_id,
		       TROVE_handle handle,
		       TROVE_keyval_s *key_p,
		       TROVE_keyval_s *val_p,
		       TROVE_ds_flags flags,
		       TROVE_vtag_s *vtag,
		       void *user_ptr,
		       TROVE_op_id *out_op_id_p);

int trove_keyval_remove(
			TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_keyval_s *key_p,
			TROVE_ds_flags flags,
			TROVE_vtag_s *vtag,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);

int trove_keyval_validate(
			  TROVE_coll_id coll_id,
			  TROVE_handle handle,
			  TROVE_ds_flags flags,
			  TROVE_vtag_s *vtag,
			  void* user_ptr,
			  TROVE_op_id *out_op_id_p);

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
			 TROVE_op_id *out_op_id_p);

int trove_keyval_iterate_keys(
			      TROVE_coll_id coll_id,
			      TROVE_handle handle,
			      TROVE_ds_position *position_p,
			      TROVE_keyval_s *key_array,
			      int *inout_count_p,
			      TROVE_ds_flags flags,
			      TROVE_vtag_s *vtag,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p);

int trove_keyval_read_list(
			   TROVE_coll_id coll_id,
			   TROVE_handle handle,
			   TROVE_keyval_s *key_array,
			   TROVE_keyval_s *val_array,
			   int count,
			   TROVE_ds_flags flags,
			   TROVE_vtag_s *vtag,
			   void *user_ptr,
			   TROVE_op_id *out_op_id_p);

int trove_keyval_write_list(
			    TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    TROVE_keyval_s *key_array,
			    TROVE_keyval_s *val_array,
			    int count,
			    TROVE_ds_flags flags,
			    TROVE_vtag_s *vtag,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p);

int trove_keyval_flush(TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);

int trove_dspace_create(TROVE_coll_id coll_id,
			TROVE_handle_extent_array *handle_extent_array,
                        TROVE_handle *out_handle,
			TROVE_ds_type type,
			TROVE_keyval_s *hint,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);

int trove_dspace_remove(TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);

int trove_dspace_iterate_handles(TROVE_coll_id coll_id,
				 TROVE_ds_position *position_p,
				 TROVE_handle *handle_array,
				 int *inout_count_p,
				 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_op_id *out_op_id_p);

int trove_dspace_verify(TROVE_coll_id coll_id,
			TROVE_handle handle,
			TROVE_ds_type *type,
			TROVE_ds_flags flags,
			void *user_ptr,
			TROVE_op_id *out_op_id_p);


int trove_dspace_getattr(TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_ds_attributes_s *ds_attr_p,
			 TROVE_ds_flags flags,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);


int trove_dspace_setattr(TROVE_coll_id coll_id,
			 TROVE_handle handle,
			 TROVE_ds_attributes_s *ds_attr_p,
			 TROVE_ds_flags flags,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p);


int trove_dspace_test(TROVE_coll_id coll_id,
		      TROVE_op_id id,
		      int *out_count_p,
		      TROVE_vtag_s *vtag,
		      void **returned_user_ptr_p,
		      TROVE_ds_state *state_p);

int trove_dspace_testsome(
			  TROVE_coll_id coll_id,
			  TROVE_op_id *ds_id_array,
			  int *inout_count_p,
			  int *out_index_array,
			  TROVE_vtag_s *vtag_array,
			  void **returned_user_ptr_array,
			  TROVE_ds_state *state_array);

int trove_collection_geteattr(
			      TROVE_coll_id coll_id,
			      TROVE_keyval_s *key_p,
			      TROVE_keyval_s *val_p,
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p);

int trove_collection_seteattr(
			      TROVE_coll_id coll_id,
			      TROVE_keyval_s *key_p,
			      TROVE_keyval_s *val_p,
			      TROVE_ds_flags flags,
			      void *user_ptr,
			      TROVE_op_id *out_op_id_p);

int trove_collection_getinfo(
			     TROVE_coll_id coll_id,
			     int option,
			     void *parameter);

int trove_collection_setinfo(
			     TROVE_coll_id coll_id,
			     int option,
			     void *parameter);
/* END AUTOGEN PROTOTYPES */

#endif

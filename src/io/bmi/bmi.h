/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains the primary application interface to the BMI
 * system.
 */

#ifndef __BMI_H
#define __BMI_H

#include <bmi_types.h>

/* used to describe unexpected message arrivals */
struct unexpected_info
{
	bmi_error_code_t error_code;
	bmi_addr_t addr;
	void* buffer;
	bmi_size_t size;
	bmi_msg_tag_t tag;
};

int BMI_initialize(
	const char* method_list, 
	const char* listen_addr,
	bmi_flag_t flags);

int BMI_finalize(
	void);

int BMI_post_send(
	bmi_op_id_t* id, 
	bmi_addr_t dest, 
	void* buffer,
	bmi_size_t size, 
	bmi_flag_t buffer_flag, 
	bmi_msg_tag_t tag, 
	void* user_ptr);

int BMI_post_sendunexpected(
	bmi_op_id_t* id, 
	bmi_addr_t dest, 
	void* buffer,
	bmi_size_t size, 
	bmi_flag_t buffer_flag, 
	bmi_msg_tag_t tag, 
	void* user_ptr);
	
int BMI_post_recv(
	bmi_op_id_t* id, 
	bmi_addr_t src, 
	void* buffer,
	bmi_size_t expected_size, 
	bmi_size_t* actual_size, 
	bmi_flag_t buffer_flag, 
	bmi_msg_tag_t tag, 
	void* user_ptr);
	
int BMI_test(
	bmi_op_id_t id, 
	int* outcount, 
	bmi_error_code_t* error_code,
	bmi_size_t* actual_size, 
	void** user_ptr);

int BMI_testsome(
	int incount, 
	bmi_op_id_t* id_array, 
	int* outcount, 
	int* index_array, 
	bmi_error_code_t* error_code_array, 
	bmi_size_t* actual_size_array, 
	void** user_ptr_array);

int BMI_testunexpected(
	int incount, 
	int* outcount, 
	struct unexpected_info* info_array);

int BMI_wait(
	bmi_op_id_t id, 
	int* outcount, 
	bmi_error_code_t* error_code,
	bmi_size_t* actual_size, 
	void** user_ptr);

int BMI_waitsome(
	int incount, 
	bmi_op_id_t* id_array, 
	int* outcount, 
	int* index_array, 
	bmi_error_code_t* error_code_array, 
	bmi_size_t* actual_size_array, 
	void** user_ptr_array);

int BMI_waitunexpected(
	int incount, 
	int* outcount, 
	struct unexpected_info* info_array);

void* BMI_memalloc(
	bmi_addr_t addr, 
	bmi_size_t size, 
	bmi_flag_t send_recv);

int BMI_memfree(
	bmi_addr_t addr, 
	void* buffer, 
	bmi_size_t size, 
	bmi_flag_t send_recv);

int BMI_set_info(
	bmi_addr_t addr, 
	int option, 
	void* inout_parameter);

int BMI_get_info(
	bmi_addr_t addr, 
	int option, 
	void* inout_parameter);

int BMI_addr_lookup(
	bmi_addr_t* new_addr, 
	const char* id_string);

int BMI_post_send_list(
	bmi_op_id_t* id,
	bmi_addr_t dest,
	void** buffer_list,
	bmi_size_t* size_list,
	int list_count,
	/* "total_size" is the sum of the size list */
	bmi_size_t total_size, 
	bmi_flag_t buffer_flag,
	bmi_msg_tag_t tag,
	void* user_ptr);

int BMI_post_recv_list(
	bmi_op_id_t* id,
	bmi_addr_t src,
	void** buffer_list,
	bmi_size_t* size_list,
	int list_count,
	/* "total_expected_size" is the sum of the size list */
	bmi_size_t total_expected_size,
	/* "total_actual_size" is the aggregate amt that was received */
	bmi_size_t* total_actual_size,
	bmi_flag_t buffer_flag,
	bmi_msg_tag_t tag,
	void* user_ptr);

int BMI_post_sendunexpected_list(
	bmi_op_id_t* id,
	bmi_addr_t dest,
	void** buffer_list,
	bmi_size_t* size_list,
	int list_count,
	/* "total_size" is the sum of the size list */
	bmi_size_t total_size, 
	bmi_flag_t buffer_flag,
	bmi_msg_tag_t tag,
	void* user_ptr);

#endif /* __BMI_H */

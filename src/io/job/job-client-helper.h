/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this file contains prototypes for a few helper functions to be used
 * with the job interface 
 */

#ifndef __JOB_CLIENT_HELPER_H
#define __JOB_CLIENT_HELPER_H

#include <job.h>

int job_bmi_send_timeout(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	int timeout_seconds,
	job_status_s* out_status_p);

int job_bmi_recv_timeout(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	int timeout_seconds,
	job_status_s* out_status_p);

int job_bmi_send_blocking(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	job_status_s* out_status_p);

int job_bmi_recv_blocking(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	job_status_s* out_status_p);

int job_waitall_blocking(
	job_id_t* id_array,
	int in_count,
	void** returned_user_ptr_array,
	job_status_s* out_status_array_p);
	
int job_waitall_timeout(
	job_id_t* id_array,
	int in_count,
	int timeout_seconds,
	void** completed_user_ptr_array,
	job_status_s* completed_status_array);
	

#endif /* __JOB_CLIENT_HELPER_H */

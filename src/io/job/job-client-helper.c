/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include<errno.h>
#include<string.h>
#include<job-client-helper.h>

/* this file contains helper functions for the job interface */

/* job_bmi_send_timeout()
 *
 * posts a bmi send job and performs any waiting necessary to complete
 * it.  times out after timeout_seconds.
 *
 * returns 0 on success, -errno on failure
 */
int job_bmi_send_timeout(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	int timeout_seconds,
	job_status_s* out_status_p)
{
	return(-ENOSYS);
}

/* job_bmi_recv_timeout()
 *
 * posts a bmi receive job and performs any waiting necessary to complete
 * it.  times out after timeout_seconds.
 *
 * returns 0 on success, -errno on failure
 */
int job_bmi_recv_timeout(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	int timeout_seconds,
	job_status_s* out_status_p)
{
	return(-ENOSYS);
}

/* job_bmi_send_blocking()
 *
 * posts a bmi send job and performs any waiting necessary to complete
 * it.  DOES NOT TIMEOUT OR RETURN UNTIL JOB COMPLETES
 *
 * returns 0 on success, -errno on failure
 */
int job_bmi_send_blocking(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	job_status_s* out_status_p)
{
	job_id_t block_id;
	int count = 0;
	int ret = -1;

	/* post the BMI send using the job interface */
	ret = job_bmi_send(addr, buffer, size, tag,
		buffer_flag, send_unexpected, NULL, out_status_p, &block_id);
	if(ret < 0)
	{
		/* failure; return error code */
		return(ret);
	}
	if(ret == 0)
	{
		/* need to wait until the job finishes */
		ret = job_test(block_id, &count, NULL, out_status_p, -1);
		return(ret);
	}

	return(0);
}

/* job_bmi_recv_blocking()
 *
 * posts a bmi receive job and performs any waiting necessary to complete
 * it.  DOES NOT TIMEOUT OR RETURN UNTIL JOB COMPLETES
 *
 * returns 0 on success, -errno on failure
 */
int job_bmi_recv_blocking(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	job_status_s* out_status_p)
{
	job_id_t block_id;
	int count = 0;
	int ret = -1;

	/* post the BMI recv using the job interface */
	ret = job_bmi_recv(addr, buffer, size, tag, buffer_flag, NULL,
		out_status_p, &block_id);
	if(ret < 0)
	{
		/* failure; return error code */
		return(ret);
	}
	if(ret == 0)
	{
		/* need to wait for completion */
		ret = job_test(block_id, &count, NULL, out_status_p, -1);
		return(ret);
	}

	return(0);
}

/* job_waitall_blocking()
 *
 * waits for completion of _all_ jobs specified in id_array.  DOES NOT
 * TIMEOUT OR RETURN UNTIL ALL JOBS COMPLETE
 *
 * returns 0 on success, -errno on failure
 */
int job_waitall_blocking(
	job_id_t* id_array,
	int in_count,
	void** returned_user_ptr_array,
	job_status_s* out_status_array_p)
{

	int ret = -1;
	int* internal_index_array = NULL;

	/* setup an index array to use internally */
	internal_index_array = (int*)alloca(in_count*sizeof(int));
	if(!internal_index_array)
	{	
		return(-ENOMEM);
	}

	/* attempt to wait on some of the pending jobs */
	ret = job_testsome(id_array, &in_count,
		internal_index_array,
		returned_user_ptr_array,
		out_status_array_p, -1);
	if(ret < 0)
	{
		return(ret);
	}

	return(0);
}
	
/* job_waitall_timeout()
 *
 * waits for completion of _all_jobs specified in id_array.  times out
 * after timeout_seconds.
 *
 * returns 0 on success, -errno on failure
 */	
int job_bmi_waitall_timeout(
	job_id_t* id_array,
	int in_count,
	int timeout_seconds,
	void** completed_user_ptr_array,
	job_status_s* completed_status_array)
{
	return(-ENOSYS);
}
	


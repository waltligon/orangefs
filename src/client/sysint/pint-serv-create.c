/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Create server request processing implementation */

#include <pint-servreq.h>

extern PVFS_msg_tag_t get_next_session_tag();

static int createreq_alloc(void *pjob,void *preq,bmi_addr_t server,\
		PVFS_credentials credentials,int *sz);
static int createack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

/* pint_serv_create()
 *
 * handles the server interaction for the "create" request
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_create(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job, void *req, PVFS_credentials\
		credentials,bmi_addr_t *serv_arg)
{
	int ret = 0;
	bmi_addr_t server_addr = *serv_arg;	/* PVFS address type structure */
	int req_size = 0, ack_size = 0;
	job_status_s status1;
	PVFS_servreq_create *arg = (PVFS_servreq_create *)req;
	PVFS_msg_tag_t bmi_connection_id = get_next_session_tag();
	
	
	/* Fill in parent pinode reference */
	/*parent_reference.handle = req->parent_handle;
	parent_reference.fs_id = req->fs_id;*/

	/* Create and fill jobs for request and response */
	
	/* Fill in the jobs */
	/* Request job */
	ret = createreq_alloc((void *)req_job,arg,server_addr,credentials,\
			&req_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto createreq_alloc_failure;
	}

	/* Post a blocking send job */
	ret = job_bmi_send_blocking(server_addr,(*req_job),req_size,
			bmi_connection_id,BMI_PRE_ALLOC,1,&status1);
	if (ret < 0)
	{
		goto send_failure;
	}
	else if (ret == 1)
	{
		/* Check status */
		if (status1.error_code != 0)
		{
			ret = -EINVAL;
			goto send_failure;
		}
	}

	/* Response job */
	ret = createack_alloc((void *)ack_job,arg,server_addr,&ack_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto send_failure;
	}

	/* Post a blocking receive job */
	ret = job_bmi_recv_blocking(server_addr,(*ack_job),ack_size,
			bmi_connection_id, BMI_PRE_ALLOC,&status1);
	if (ret < 0)
	{
		goto recv_failure;
	}
	else if (ret == 1)
	{
		/* Check status */
		if (status1.error_code != 0 || status1.actual_size != ack_size)
		{
			ret = -EINVAL;
			goto recv_failure;
		}
	}
	/* Check server error status */
	if ((*ack_job)->status != 0)
	{
		ret = (*ack_job)->status;
		goto recv_failure;
	}

  	return(0); 

recv_failure:
	sysjob_free(server_addr,(*ack_job),ack_size,BMI_RECV_BUFFER,NULL);

send_failure:
	sysjob_free(server_addr,(*req_job),req_size,BMI_SEND_BUFFER,NULL);

createreq_alloc_failure:
	return(ret);
	
}


/* createreq_alloc()
 *
 * sets up a create request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int createreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials, int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	PVFS_servreq_create *req = (PVFS_servreq_create *)preq;
	int size = 0;

	/* Fill up the request structure */
	size = sizeof(struct PVFS_server_req_s); 
	(*serv_req) = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}

	/* Set up the request for create */
	(*serv_req)->op = PVFS_SERV_CREATE;	
	/* (*serv_req)->u.create.bucket_ID = req->bucket_ID; */
	(*serv_req)->u.create.fs_id = req->fs_id;
	(*serv_req)->u.create.object_type = req->object_type;

	(*serv_req)->credentials = credentials;	
	(*serv_req)->rsize = size; 
	*sz = size;

	return(0);
}


/* createack_alloc()
 *
 * sets up a create acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int createack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;

	/* Fill up the response structure */
	size = sizeof(struct PVFS_server_resp_s); 
	/* Alloc memory for response structure */
	*serv_resp = BMI_memalloc(server,size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for create */
	(*serv_resp)->op = PVFS_SERV_CREATE;	
	(*serv_resp)->rsize = size;

	*sz = size;

	return(0);

}


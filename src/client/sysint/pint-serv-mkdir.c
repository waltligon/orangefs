/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Make Directory server request processing implementation */

#include <pint-servreq.h>

static int mkdirreq_alloc(void *pjob,void *preq,bmi_addr_t server,\
		PVFS_credentials credentials,int *sz);
static int mkdirack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

/* pint_serv_mkdir()
 *
 * handles the server interaction for the "mkdir" request
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_mkdir(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job,void *req, PVFS_credentials\
		credentials, bmi_addr_t *serv_arg)
{
	int ret = -1,req_size = 0, ack_size = 0;
   bmi_addr_t server_addr = *serv_arg;	 /*PVFS address type structure*/ 
	job_status_s status1;
	PVFS_servreq_mkdir *arg = (PVFS_servreq_mkdir *)req;

	/* Create and fill jobs for request and response */

	/* Fill in the jobs */
	/* Request job */
	ret = mkdirreq_alloc((void *)req_job,arg,server_addr,credentials,\
			&req_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto job_fill_failure;
	}

	/* Post a blocking send job */
	ret = job_bmi_send_blocking(server_addr,(*req_job),req_size,
			0,BMI_PRE_ALLOC,1,&status1);
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
	ret = mkdirack_alloc((void *)ack_job,arg,server_addr,&ack_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto send_failure;
	}

	/* Post a blocking receive job */
	ret = job_bmi_recv_blocking(server_addr,(*ack_job),ack_size,
			0,BMI_PRE_ALLOC,&status1);
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

job_fill_failure:
		return(ret);

}	  

/* mkdirreq_alloc()
 *
 * sets up a mkdir request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int mkdirreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials,int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0;
	PVFS_servreq_mkdir *req = (PVFS_servreq_mkdir *)preq;

	/* Fill up the job structure */
	size = sizeof(struct PVFS_server_req_s); 
	/* Alloc memory for request structure */
	*serv_req = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}
	memset(*serv_req,0,sizeof(struct PVFS_server_req_s)); 

	/* Set up the request for mkdir */
	(*serv_req)->op = PVFS_SERV_MKDIR;	
  	/* Copy handle from pinode number to server structure */
	(*serv_req)->u.mkdir.bucket = req->bucket;
	(*serv_req)->u.mkdir.handle_mask = req->handle_mask;
	(*serv_req)->u.mkdir.fs_id = req->fs_id;
	(*serv_req)->u.mkdir.attr = req->attr;
	(*serv_req)->u.mkdir.attrmask = req->attrmask;
	(*serv_req)->rsize = size;
	(*serv_req)->credentials = credentials;

	*sz = size;

	return(0);
}

/* mkdirack_alloc()
 *
 * sets up a mkdir acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int mkdirack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;
	
	/* Fill up the job structure */
	size = sizeof(struct PVFS_server_resp_s); 
	/* Alloc memory for request structure */
	*serv_resp = BMI_memalloc(server,size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for mkdir */
	memset(*serv_resp,0,sizeof(struct PVFS_server_resp_s)); 
	(*serv_resp)->op = PVFS_SERV_MKDIR;	
	(*serv_resp)->rsize = size;

	*sz = size;

	return(0);
}


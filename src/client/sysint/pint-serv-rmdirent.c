/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Remove Directory Entry server request processing implementation */

#include <pint-servreq.h>

static int rmdirentreq_alloc(void *pjob,void *preq,bmi_addr_t server,\
		PVFS_credentials,int *sz);
static int rmdirentack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

/* pint_serv_rmdirent()
 *
 * handles the server interaction for the "rmdirent" request
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_rmdirent(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job,void *req, PVFS_credentials\
		credentials, bmi_addr_t *serv_arg)
{
	int ret = -1,req_size = 0, ack_size = 0;
   bmi_addr_t server_addr = *serv_arg;	 /* PVFS address type structure */ 
	job_status_s status1;
	PVFS_servreq_rmdirent *arg = (PVFS_servreq_rmdirent *)req;
	PVFS_msg_tag_t bmi_connection_id = get_next_session_tag();

	/* Create and fill jobs for request and response */

	/* Fill in the jobs */
	/* Request job */
	ret = rmdirentreq_alloc((void *)req_job,arg,server_addr,\
			credentials,&req_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto job_fill_failure;
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
	ret = rmdirentack_alloc((void *)ack_job,arg,server_addr,&ack_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto send_failure;
	}

	/* Post a blocking receive job */
	ret = job_bmi_recv_blocking(server_addr,(*ack_job),ack_size,
			bmi_connection_id,BMI_PRE_ALLOC,&status1);
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

/* rmdirentreq_alloc()
 *
 * sets up a rmdirent request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int rmdirentreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials, int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0,name_sz = 0;
	PVFS_servreq_rmdirent *req = (PVFS_servreq_rmdirent *)preq;

	name_sz = strlen(req->entry);
	/* Fill up the job structure */
	size = sizeof(struct PVFS_server_req_s) + name_sz + 1; 
	/* Alloc memory for request structure */
	*serv_req = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}
	memset(*serv_req,0,size); 

	/* Point name to allocated memory */
	(*serv_req)->u.rmdirent.entry = (PVFS_string)((char *)(*serv_req) +\
		sizeof(struct PVFS_server_req_s));


	/* Set up the request for rmdirent */
	(*serv_req)->op = PVFS_SERV_RMDIRENT;	
	(*serv_req)->u.rmdirent.parent_handle = req->parent_handle;

	strncpy((*serv_req)->u.rmdirent.entry,req->entry,name_sz);
	(*serv_req)->u.rmdirent.entry[name_sz] = '\0';
	(*serv_req)->u.rmdirent.fs_id = req->fs_id;
	(*serv_req)->rsize = size;
	(*serv_req)->credentials = credentials;

	*sz = size;

	return(0);
}

/* rmdirent_alloc()
 *
 * sets up a rmdirent acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int rmdirentack_alloc(void *pjob,void *preq,bmi_addr_t server,
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

	/* Set up the response for rmdirent */
	memset(*serv_resp,0,size); 
	(*serv_resp)->op = PVFS_SERV_RMDIRENT;	

	(*serv_resp)->rsize = size;
	*sz = size;

	return(0);
}


/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Set attribute server request processing implementation */

#include <pint-servreq.h>

extern PVFS_msg_tag_t get_next_session_tag();

static int setattrreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_size handle_size,PVFS_credentials credentials,int *sz);
static int setattrack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

/* pint_serv_setattr()
 *
 * handles the server interaction for the "setattr" request
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_setattr(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job,void *req,PVFS_credentials\
		credentials, PVFS_size handlesize, bmi_addr_t *serv_arg)
{
	int ret = -1,req_size = 0, ack_size = 0;
   bmi_addr_t server_addr = *serv_arg;	 /*PVFS address type structure*/ 
	job_status_s status1;
	PVFS_servreq_setattr *arg = (PVFS_servreq_setattr *)req;
	PVFS_msg_tag_t bmi_connection_id = get_next_session_tag();

	/* Create and fill jobs for request and response */

	/* Fill in the jobs */
	/* Request job */
	ret = setattrreq_alloc(req_job,arg,server_addr,handlesize,\
			credentials,&req_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto req_alloc_failure;
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
	ret = setattrack_alloc((void *)ack_job,arg,server_addr,&ack_size);
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

req_alloc_failure:
		return(ret);

}	  

/* setattrreq_alloc()
 *
 * sets up a setattr request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int setattrreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_size handle_size,PVFS_credentials credentials,int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0;
	PVFS_servreq_setattr *req = (PVFS_servreq_setattr *)preq;
	int type = req->attr.objtype;

	/* Fill up the job structure */
	/* Set size based on object type */
	if (type == ATTR_META)
		size = sizeof(struct PVFS_server_req_s) + handle_size; 
	else
		size = sizeof(struct PVFS_server_req_s); 

	/* Alloc memory for request structure */
	*serv_req = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}

	memset(*serv_req,0,size); 

	/* Set up the request for setattr */
	(*serv_req)->op = PVFS_SERV_SETATTR;	
	/* Copy the attribute mask */
	(*serv_req)->u.setattr.attrmask = req->attrmask;	
	(*serv_req)->u.setattr.attr = req->attr;	
  	/*copy handle from pinode number to server structure*/
	(*serv_req)->u.setattr.handle = (int64_t)req->handle;
	(*serv_req)->u.setattr.fs_id = req->fs_id;

	/* Based on object type fill up the union */
	switch(type)
	{
		case ATTR_META:
#if 0
			/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
			(*serv_req)->u.setattr.attr.u.meta.dist = req->attr.u.meta.dist;
#endif
			(*serv_req)->u.setattr.attr.u.meta.nr_datafiles =\
				req->attr.u.meta.nr_datafiles;
			(*serv_req)->u.setattr.attr.u.meta.dfh = ((PVFS_handle *)(*serv_req)\
				+ sizeof(struct PVFS_server_req_s));
			/* Copy the handles */
			memcpy((*serv_req)->u.setattr.attr.u.meta.dfh,\
					req->attr.u.meta.dfh,handle_size);

		case ATTR_DATA:

		case ATTR_DIR:

		case ATTR_SYM:

		default:

	}
	(*serv_req)->rsize = size;
	(*serv_req)->credentials = credentials;

	*sz = size;

	return(0);
}

/* setattrack_alloc
 *
 * sets up a setattr acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int setattrack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;
	
	/* Fill up the job structure */
	size = sizeof(struct PVFS_server_resp_s); 
	/* Alloc memory for server response structure */
	*serv_resp = BMI_memalloc(server,size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for setattr */
	memset(*serv_resp,0,sizeof(struct PVFS_server_resp_s)); 
	(*serv_resp)->op = PVFS_SERV_SETATTR;	
	(*serv_resp)->rsize = size;

	*sz = size;

	return(0);
}


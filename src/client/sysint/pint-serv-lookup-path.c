/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Lookup-Path Server Request Processing Implementation */

#include <pint-servreq.h>

static int max_seg = 0;

static int lookuppath_req_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials, int *sz);
static int lookuppath_ack_alloc(void *pjob,void *presp,bmi_addr_t server,
		int *sz);

/* pint_serv_lookup_path()
 *
 * handles the server interaction for the "lookup_path" request 
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_lookup_path(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job,void *req,PVFS_credentials\
		credentials, bmi_addr_t *serv_arg)
{
   int ret = -1,req_size = 0,ack_size = 0,num_seg = 0;
   bmi_addr_t server_addr = *serv_arg;   /* PVFS address type structure */ 
	job_status_s status1;
	PVFS_servreq_lookup_path *arg = (PVFS_servreq_lookup_path *)req; 
	
	/* Get the total no. of segments */
	get_no_of_segments(arg->path,&num_seg);
	if (num_seg < 0)
	{
		return(ret);
	}
	if (num_seg > max_seg)
		max_seg = num_seg;

	/* Create and fill jobs request and response */
	/* Request job */
	ret = lookuppath_req_alloc((void *)req_job,arg,server_addr,credentials,\
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
	ret = lookuppath_ack_alloc(ack_job,arg,server_addr,&ack_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto send_failure;
	}

	/* Post a blocking recv job */
	ret = job_bmi_recv_blocking(server_addr,(*ack_job),ack_size,0,
		BMI_PRE_ALLOC,&status1);
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
		/* Free the fname in PVFS object */
		/*lookuppathreq_free(&object,0);*/
		return(-ENOMEM);

}

/* lookupreq_alloc()
 *
 * sets up a lookup_path request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int lookuppath_req_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials,int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0, name_sz = 0;
	PVFS_servreq_lookup_path *req = (PVFS_servreq_lookup_path *)preq;

	name_sz = strlen(req->path);
	/* Fill up the request structure */
	size = sizeof(struct PVFS_server_req_s) + name_sz + 1;
	*serv_req = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}
		
	/* Point name to allocated memory */
	(*serv_req)->u.lookup_path.path = ((char *)(*serv_req) +
			sizeof(struct PVFS_server_req_s));
	if (!(*serv_req)->u.lookup_path.path)
	{
		BMI_memfree(server,(*serv_req),size,BMI_SEND_BUFFER);
		return(-ENOMEM);
	}
	/* Set up the request for lookup_path */
	(*serv_req)->op = PVFS_SERV_LOOKUP_PATH;	
	(*serv_req)->u.lookup_path.starting_handle = req->starting_handle;
	/* copy a null terminated string to another */
	strncpy((*serv_req)->u.lookup_path.path,req->path,name_sz);
	(*serv_req)->u.lookup_path.path[name_sz] = '\0';
	(*serv_req)->u.lookup_path.fs_id = req->fs_id; /* Assign FS ID */
	(*serv_req)->u.lookup_path.attrmask = req->attrmask;

  	/*set the size to the size of request header+actual req info*/	
	(*serv_req)->rsize = size; 
	(*serv_req)->credentials = credentials;
	*sz = size;
	
	return(0);

}

/* lookupack_alloc()
 *
 * sets up a lookup_path acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int lookuppath_ack_alloc(void *pjob,void *presp,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = pjob;
	int size = 0;

	/* Size of the response structure */
	size = sizeof(struct PVFS_server_resp_s) +
		max_seg * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr)) ;  
	*serv_resp = BMI_memalloc(server,size, BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for lookup_path */
	memset(*serv_resp,0,sizeof(struct PVFS_server_resp_s));  
	(*serv_resp)->op = PVFS_SERV_LOOKUP_PATH;	

	/* Point handle array to allocated memory */
	(*serv_resp)->u.lookup_path.handle_array = (PVFS_handle *)(((char *)\
		(*serv_resp)) + sizeof(struct PVFS_server_resp_s));

	/* Point attribute array to allocated memory */
	(*serv_resp)->u.lookup_path.attr_array = (PVFS_object_attr *)(((char *)\
		(*serv_resp)) + sizeof(struct PVFS_server_resp_s) +\
		max_seg * sizeof(PVFS_handle));

  	/*set the size to the size of response header+actual resp info*/	
	(*serv_resp)->rsize = size; 
	*sz = size;
	
	return(0);

}


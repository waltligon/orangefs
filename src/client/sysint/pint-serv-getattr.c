/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Get attribute server request processing implementation */

#include <pint-servreq.h>
#include <PINT-reqproto-encode.h>
#define REQ_ENC_FORMAT 0
#define MAX_DATAFILES 10

static int getattrreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials,int *sz);
static int getattrack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

/* pint_serv_getattr()
 *
 * handles the server interaction for the "getattr" request
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_getattr(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job,void *req,PVFS_credentials\
		credentials,bmi_addr_t *serv_arg)
{
	int ret = -1,req_size = 0;
   bmi_addr_t server_addr = *serv_arg;		 /*PVFS address type structure*/ 
	job_status_s status1;
	PVFS_servreq_getattr *arg = (PVFS_servreq_getattr *)req;
	PVFS_msg_tag_t bmi_connection_id = get_next_session_tag();
	struct PINT_encoded_msg encoded;
        struct PINT_decoded_msg decoded;
        int max_resp_size = 0;
        void * wire_resp = NULL;

	/* figure out how big the response is going to be */

	if (arg->attrmask == ATTR_META)
	{
		max_resp_size = sizeof(struct PVFS_server_resp_s) + \
			(MAX_DATAFILES * sizeof( PVFS_handle ) );
	}
	else
	{
		max_resp_size = sizeof(struct PVFS_server_resp_s);
	}

	/* Create and fill jobs for request and response */

	/* Fill in the jobs */
	/* Request job */
	ret = getattrreq_alloc((void *)req_job,arg,server_addr,\
			credentials,&req_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto job_fill_failure;
	}

	/* encode to the on-the-wire format */
	ret = PINT_encode(req_job, PINT_ENCODE_REQ, &encoded, server_addr, \
			REQ_ENC_FORMAT);
	if (ret < 0)
	{
		goto enc_failure;
	}

	/* Post a blocking send job */
	ret = job_bmi_send_blocking(server_addr,encoded.buffer_list[0],
			encoded.size_list[0],bmi_connection_id,BMI_PRE_ALLOC,1,&status1);
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
	debug_print_type(encoded.buffer_list[0], 0);
	printf(" sent\n");

	wire_resp = (void*)BMI_memalloc(server_addr, (bmi_size_t) max_resp_size, BMI_RECV_BUFFER);
	if (!wire_resp)
	{
		ret = -ENOMEM;
		goto send_failure;
	}

	/* Response job */
	/*ret = getattrack_alloc((void *)ack_job,arg,server_addr,&ack_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto send_failure;
	}*/

	/* Post a blocking receive job */
	ret = job_bmi_recv_blocking(server_addr,wire_resp,(bmi_size_t) max_resp_size,
			bmi_connection_id,BMI_PRE_ALLOC,&status1);
	if (ret < 0)
	{
		goto recv_failure;
	}
	else if (ret == 1)
	{
		/* Check status */
		if (status1.error_code != 0 || status1.actual_size != max_resp_size)
		{
			ret = -EINVAL;
			goto recv_failure;
		}
	}

	/* decode msg from wire format here */
	ret = PINT_decode(      wire_resp,
				PINT_ENCODE_REQ,
 				&decoded,
				server_addr,
				status1.actual_size,
				NULL);
	if (ret < 0)
	{
		ret = (-EINVAL);
		goto dec_failure;
	}

	debug_print_type(decoded.buffer, 1);
	printf(" recv'd\n");

	/* Check server error status */
	if (((struct PVFS_server_resp_s *)decoded.buffer)->status != 0)
	{
		ret = ((struct PVFS_server_resp_s *)decoded.buffer)->status;
		goto recv_failure;
	}

	/* cleanup some craplets of memory that are lying around*/

	/*must call release for the decode at some point*/
	(*ack_job) = (struct PVFS_server_resp_s **)&decoded.buffer;

	/* release leftovers from the encoded send buffer */
	PINT_encode_release(    &encoded,
				PINT_ENCODE_REQ,
				0);

	/* also don't need the on-the-wire response anymore */
	sysjob_free(server_addr,wire_resp,(bmi_size_t)max_resp_size,BMI_RECV_BUFFER,NULL);

  	return(0); 

dec_failure:
	PINT_decode_release(    &decoded,
				PINT_ENCODE_REQ,
				0);

recv_failure:
		sysjob_free(server_addr,wire_resp,(bmi_size_t)max_resp_size,BMI_RECV_BUFFER,NULL);

send_failure:
	PINT_encode_release(    &encoded,
				PINT_ENCODE_REQ,
				0);

enc_failure:
		sysjob_free(server_addr,(*req_job),(bmi_size_t)req_size,BMI_SEND_BUFFER,NULL);

job_fill_failure:
		return(ret);
}	  

/* getattrreq_alloc()
 *
 * sets up a getattr request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int getattrreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials,int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0;
	PVFS_servreq_getattr *req = (PVFS_servreq_getattr *)preq;

	/* Fill up the job structure */
	size = sizeof(struct PVFS_server_req_s); 
	/* Alloc memory for request structure */
	*serv_req = BMI_memalloc(server,(bmi_size_t)size,BMI_SEND_BUFFER);
	if (!serv_req)
	{
		return(-ENOMEM);
	}

	memset(*serv_req,0,sizeof(struct PVFS_server_req_s)); 

	/* Set up the request for getattr */
	(*serv_req)->op = PVFS_SERV_GETATTR;	
	/* Copy the attribute mask */
	(*serv_req)->u.getattr.attrmask = req->attrmask;	
  	/*copy handle from pinode number to server structure*/
	(*serv_req)->u.getattr.handle = (int64_t)req->handle;
	(*serv_req)->u.getattr.fs_id = req->fs_id;
	(*serv_req)->rsize = size;
	(*serv_req)->credentials = credentials;

	*sz = size;

	return(0);
}

/* getattrack_alloc()
 *
 * sets up a getattr acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int getattrack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;
	
	/* Fill up the job structure */
	size = sizeof(struct PVFS_server_resp_s); 
	/* Alloc memory for request structure */
	*serv_resp = BMI_memalloc(server,(bmi_size_t)size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for getattr */
	memset(*serv_resp,0,sizeof(struct PVFS_server_resp_s)); 
	(*serv_resp)->op = PVFS_SERV_GETATTR;	

	memset(&(*serv_resp)->u.getattr.attr,0,sizeof(PVFS_object_attr));
	(*serv_resp)->rsize = size;

	*sz = size;

	return(0);
		
}


/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Getconfig server request processing implementation */

#include <pint-servreq.h>
#include <string.h>
#include <PINT-reqproto-encode.h>

#define REQ_ENC_FORMAT 0

extern PVFS_msg_tag_t get_next_session_tag(void);
extern void debug_print_type(void* thing, int type);

static int getconfigreq_alloc(void *pjob,void *preq,bmi_addr_t server,\
		PVFS_credentials credentials,int *sz);
static int getconfigack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

/* pint_serv_getconfig()
 *
 * handles the server interaction for the "getconfig" request
 *
 * returns 0 on success, -errno on failure
 */
int pint_serv_getconfig(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **ack_job, void *req, PVFS_credentials\
		credentials, bmi_addr_t *serv_arg)
{
	int ret = -1;
	int req_size = 0;
	bmi_addr_t server_addr = *serv_arg;	/* PVFS address type structure */
	job_status_s status1;
	PVFS_servreq_getconfig *arg = (PVFS_servreq_getconfig *)req;
	PVFS_msg_tag_t bmi_connection_id = get_next_session_tag();
	struct PINT_encoded_msg encoded;
	struct PINT_decoded_msg decoded;
	int ssize = arg->max_strsize;
	int max_resp_size = sizeof(struct PVFS_server_resp_s) + 2 * ssize;
	void * wire_resp = NULL;
	
	/* Create and fill jobs for request and response */
	
	/* Fill in the jobs */
	/* Request job */
	ret = getconfigreq_alloc((void *)req_job,arg,server_addr,credentials,\
			&req_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto job_fill_failure;
	}

	printf("encode to the on-the-wire format\n");
	/* encode to the on-the-wire format */
	ret = PINT_encode(*req_job, PINT_ENCODE_REQ, &encoded, server_addr, \
			REQ_ENC_FORMAT);
	if (ret < 0)
	{
		goto enc_failure;
	}

	printf("post blocking send job\n");

	/* Post a blocking send job */
	ret = job_bmi_send_blocking(server_addr, encoded.buffer_list[0], encoded.size_list[0],
			bmi_connection_id, BMI_PRE_ALLOC,1,&status1);
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
	debug_print_type(*req_job, 0);
	printf(" sent\n");

	printf("alloc space for recv\n");
	wire_resp = BMI_memalloc(server_addr, (bmi_size_t)max_resp_size, BMI_RECV_BUFFER);
	if (!wire_resp)
	{
		ret = -ENOMEM;
		goto send_failure;
	}

	printf("Post a blocking receive job\n");
	/* Post a blocking receive job */
	ret = job_bmi_recv_blocking(server_addr,wire_resp,(bmi_size_t)max_resp_size,
			bmi_connection_id, BMI_PRE_ALLOC,&status1);
	if (ret < 0)
	{
		goto recv_failure;
	}

	else if (ret == 1)
	{
		/* Check status */
		if (status1.error_code != 0 || status1.actual_size > max_resp_size
				|| status1.actual_size <= sizeof(struct PVFS_server_resp_s))
		{
			ret = -EINVAL;
			goto recv_failure;
		}
	}

	printf("decode msg from wire format here\n");
	/* decode msg from wire format here */
	ret = PINT_decode(	wire_resp,
				PINT_ENCODE_REQ,
				&decoded,
				server_addr,
				status1.actual_size,
				NULL);
	if (ret < 0)
	{
		ret = (-EINVAL);
		goto recv_failure;
	}
	debug_print_type(decoded.buffer, 1);
	printf(" recv'd\n");

	/* Check server error status */
	if (((struct PVFS_server_resp_s *)decoded.buffer)->status != 0)
	{
		ret = ((struct PVFS_server_resp_s *)decoded.buffer)->status;
		goto dec_failure;
	}

	/*must call release for the decode at some point*/
	(*ack_job) = (struct PVFS_server_req_s **)&decoded.buffer;

	/* release leftovers from the encoded send buffer */
	PINT_encode_release(    &encoded,
				PINT_ENCODE_REQ,
				0);

	/* also don't need the on-the-wire response anymore */
	sysjob_free(server_addr,wire_resp,(bmi_size_t)max_resp_size,BMI_RECV_BUFFER,NULL);

	/* Response job */
	/*ret = getconfigack_alloc((void *)decoded.buffer,arg,server_addr,_size);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto send_failure;
	}*/

  	return(0); 

dec_failure:
	printf("dec_failure\n");
	PINT_decode_release(    &decoded,
				PINT_ENCODE_REQ,
				0);

recv_failure:
	printf("recv_failure\n");
	sysjob_free(server_addr,wire_resp,(bmi_size_t)max_resp_size,BMI_RECV_BUFFER,NULL);

send_failure:
	printf("send_failure\n");
	PINT_encode_release(    &encoded,
				PINT_ENCODE_REQ,
				0);

enc_failure:
	printf("enc_failure\n");
	sysjob_free(server_addr,(*req_job),(bmi_size_t)req_size,BMI_SEND_BUFFER,NULL);

job_fill_failure:
	printf("job_fill_failure\n");
	return(ret);
	
}


/* getconfigreq_alloc()
 *
 * sets up a getconfig request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int getconfigreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		PVFS_credentials credentials, int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	PVFS_servreq_getconfig *req = (PVFS_servreq_getconfig *)preq;
	int size = 0;
	int name_sz = strlen(req->fs_name);

	/* Fill up the request structure */
	size = sizeof(struct PVFS_server_req_s) + name_sz + 1; 
	(*serv_req) = BMI_memalloc(server,(bmi_size_t)size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}
	memset(*serv_req,0,sizeof(struct PVFS_server_req_s));

	/* Point filesystem name to allocated memory */
	(*serv_req)->u.getconfig.fs_name = ((PVFS_string)(*serv_req) + 
													sizeof(struct PVFS_server_req_s));
	if (!(*serv_req)->u.getconfig.fs_name)
	{
		BMI_memfree(server,(*serv_req),(bmi_size_t)size,BMI_SEND_BUFFER);
		return(-ENOMEM);
	}

	/* Set up the request for getconfig */
	(*serv_req)->op = PVFS_SERV_GETCONFIG;	
	/* Copy the filesystem name */
	strncpy((*serv_req)->u.getconfig.fs_name,req->fs_name,name_sz);
	(*serv_req)->u.getconfig.fs_name[name_sz] = '\0';
	(*serv_req)->u.getconfig.max_strsize = MAX_STRING_SIZE;

	(*serv_req)->rsize = size; 
	(*serv_req)->credentials = credentials;	
	*sz = size;

	return(0);
}


/* getconfigack_alloc()
 *
 * sets up a getconfig acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int getconfigack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;

	/* Fill up the response structure */
	size = sizeof(struct PVFS_server_resp_s) + 2 * MAX_STRING_SIZE; 
	/* Alloc memory for response structure */
	*serv_resp = BMI_memalloc(server,(bmi_size_t)size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for getconfig */
	(*serv_resp)->op = PVFS_SERV_GETCONFIG;	

	/* Point meta_mapping to allocated memory */
	(*serv_resp)->u.getconfig.meta_server_mapping = (PVFS_string)((char *)\
				(*serv_resp) + sizeof(struct PVFS_server_resp_s));

	/* Point io_mapping to allocated memory */
	(*serv_resp)->u.getconfig.io_server_mapping = (PVFS_string)((char *)\
				(*serv_resp) + sizeof(struct PVFS_server_resp_s) +\
				MAX_STRING_SIZE);
	
	(*serv_resp)->rsize = size;
	*sz = size;

	return(0);
}


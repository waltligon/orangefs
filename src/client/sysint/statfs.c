/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* File System Status Function Implementation */

#if 0
/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
#include <pinode.h>
#endif
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pvfs2-req-proto.h>

static int mstatreq_alloc(void *pjob,void *preq,bmi_addr_t server,int *sz);
static int iostatreq_alloc(void *pjob,void *preq,bmi_addr_t server,int *sz);
static int mstatack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);
static int iostatack_alloc(void *pjob,void *presp,bmi_addr_t server,int *sz);

extern pinode_storage_p pstore;
extern struct metaserv_table mtable;
extern struct ioserv_table iotable;

/* PVFS_sys_statfs()
 *
 * Obtain the status of the file system via select attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_statfs(PVFS_sysreq_statfs *req, PVFS_sysresp_statfs *resp)
{
	/*struct PVFS_server_req_s *req_job = NULL;*/		/* server request  */
	/*struct PVFS_server_resp_s *ack_job = NULL;*/		/* server response */
	int ret = -1,req_size = 0, ack_size = 0,failure = 0;
	bmi_addr_t *server_addr;				/* PVFS address type structure */
	char *server = NULL;
	int index = 0, i = 0, num_send_pending = 0,num_recv_pending = 0;
	job_id_t *send_id_array = NULL, *recv_id_array = NULL;
	struct PVFS_server_req_s **req_array = NULL;
	struct PVFS_server_resp_s **ack_array = NULL;
	int (*reqfunc_p)(void *,void*,bmi_addr_t,int*);
	int (*ackfunc_p)(void *,void*,bmi_addr_t,int*);
	job_status_s *status_array = NULL;
	post_info sinfo;    /* send/recv post info */
	
	/* Read the config file to obtain metaservers and I/O servers */
	
	/* Allocate memory for the server array */
	server_addr = (bmi_addr_t *)malloc(sizeof(bmi_addr_t)\
			* (mtable.number + iotable.number));
	if (!server_addr)
	{
		ret = -ENOMEM;
		goto addr_alloc_failure;
	}
	memset(server_addr,0,(sizeof(bmi_addr_t) * (mtable.number 
					+ iotable.number)));

	/* Allocate memory for Send ID array */
	send_id_array = (job_id_t *)malloc(sizeof(job_id_t) * 
			(mtable.number + iotable.number));
	if (!send_id_array)
	{
		ret = -ENOMEM;
		goto id_alloc_failure;
	}
	memset(send_id_array,0,(sizeof(job_id_t)*(mtable.number +
				iotable.number)));

	/* Allocate memory for Receive ID array */
	recv_id_array = (job_id_t *)malloc(sizeof(job_id_t) * 
			(mtable.number + iotable.number));
	if (!recv_id_array)
	{
		ret = -ENOMEM;
		goto id_alloc_failure;
	}
	memset(recv_id_array,0,(sizeof(job_id_t)*(mtable.number +
				iotable.number)));

	/* Allocate memory for status array */
	status_array = (job_status_s *)malloc(sizeof(job_status_s)\
			* (mtable.number + iotable.number));
	if (!status_array)
	{
		ret = -ENOMEM;
		goto status_alloc_failure;
	}
	memset(status_array,0,(sizeof(job_status_s) * (mtable.number 
					+ iotable.number)));
	/* Allocate memory for request array */
	req_array = (struct PVFS_server_req_s **)malloc(sizeof(struct\
				PVFS_server_req_s *) * (mtable.number + iotable.number));
	if (!req_array)
	{
		ret = -ENOMEM;
		goto job_fill_failure;
	}
	/* Allocate memory for response array */
	ack_array = (struct PVFS_server_resp_s **)malloc(sizeof(struct\
				PVFS_server_resp_s *) * (mtable.number + iotable.number));
	if (!ack_array)
	{
		ret = -ENOMEM;
		goto job_fill_failure;
	}

	/* Create metaserver,ioserver request jobs */
	for(index = 0;index < (mtable.number+iotable.number);index++)
	{
		if (index >= mtable.number)
		{
			server = iotable.table_p[index - mtable.number].name;
			reqfunc_p = iostatreq_alloc;
			/*ackfunc_p = iostatack_alloc;*/
		}
		else
		{
			server = mtable.table_p[index].name;
			reqfunc_p = mstatreq_alloc;
			/*ackfunc_p = mstatack_alloc;*/
		}

   	/* Get the metaserver for the file */
		ret = BMI_addr_lookup(&(server_addr[index]),server);   
		if (ret < 0)
		{
			ret = -ENOMEM;
			goto job_fill_failure;
		}
		
		/* Send request job */
		ret = (*reqfunc_p)((void *)(&(req_array[index])),req,server_addr[index],
				&req_size);
		if (ret < 0)
		{
			ret = -ENOMEM;
			/* First time failure implies no bmi jobs to be freed
			 * so just free the array ptr 
			 */
			if (index == 0)
				goto job_fill_failure;
			else
				goto send_failure;
		}

		/* Build a send info structure */
		sinfo.addr = server_addr[index];
		sinfo.buffer = (struct PVFS_server_req_s *)req_array[index];
		sinfo.size = req_size;
		sinfo.expected_size = req_size;
		sinfo.tag = 0;
		sinfo.flag = BMI_EXT_ALLOC;
		sinfo.unexpected = 1;
		sinfo.user_ptr = NULL;
		sinfo.status_p = &(status_array[index]);
		sinfo.id = &send_id_array[index];
		sinfo.failure = &failure;
		sinfo.errval = 0;
		
		/* Post and set status */
		job_postreq(&sinfo);

		num_send_pending++;
	}/* for */

	/* Block only if atleast a single job didn't complete
	 * immediately
	 */
	if (num_send_pending > 0)
	{
		/* Block on waitall */
		job_waitblock(send_id_array,num_send_pending,NULL,
					status_array,&failure);
	}

	/* Create array of recvs and post them */

	for(index = 0;index < num_send_pending;index++)
	{
		/* If id is invalid go to next job */
		if (send_id_array[index] <= 0) 
		{
			recv_id_array[index] = send_id_array[index];
			continue;
		}
		if (index >= mtable.number)
			ackfunc_p = iostatack_alloc;
		else
			ackfunc_p = mstatack_alloc;

		/* Response job */
		ret = (*ackfunc_p)((void *)&(ack_array[index]),req,server_addr[index],
				&ack_size);
		if (ret < 0)
		{
			ret = -ENOMEM;
			if (index == 0)
				goto send_failure;
			else
				goto recv_failure;
		}

		/* Build a receive info structure */
		sinfo.addr = server_addr[index];
		sinfo.buffer = ack_array[index];
		sinfo.size = ack_size;
		sinfo.expected_size = 0;
		sinfo.tag = 0;
		sinfo.flag = BMI_EXT_ALLOC;
		sinfo.unexpected = 0;
		sinfo.user_ptr = NULL;
		sinfo.status_p = &(status_array[index]);
		sinfo.id = &recv_id_array[index];
		sinfo.failure = &failure;
		sinfo.errval = 0;
		
		/* Post and set status */
		job_postack(&sinfo);

		num_recv_pending++;
	}

	/* Block only if atleast a single job did not complete 
	 * immediately 
	 */
	if (num_recv_pending > 0)
	{
		/* Block on waitall */
		job_waitblock(recv_id_array,num_recv_pending,NULL,
					status_array,&failure);
	}

	/* Check for failure */
	if (failure == 1) 
	{
		/* Set the error code */
		ret = sinfo.errval;
		if (num_send_pending == 0)
			/* No recv jobs allocated */
			goto send_failure;
		else 
			goto recv_failure;
	}
	/* Be absolutely sure that all fields are zero */
	memset(&resp->statfs,0,sizeof(PVFS_statfs));

	
	/* Aggregate the statistics */
	for(i = 0; i < (mtable.number + iotable.number);i++)
	{
#define METASTAT ack_array[i]->u.statfs.stat.u.mstat 
#define IODSTAT ack_array[i]->u.statfs.stat.u.iostat

		/* Check if response is from metaserver or ioserver */
		switch(ack_array[i]->op)
		{
			case PVFS_SERV_STATFS:
				resp->statfs.mstat.filetotal += METASTAT.filetotal;
				break;

			case PVFS_SERV_IOSTATFS:
				resp->statfs.iostat.blksize += IODSTAT.blksize;
				resp->statfs.iostat.blkfree += IODSTAT.blkfree;
				resp->statfs.iostat.blktotal += IODSTAT.blktotal;
				resp->statfs.iostat.filetotal += IODSTAT.filetotal;
				resp->statfs.iostat.filefree += IODSTAT.filefree;
				break;

			default:
				/* Should never come here */
				ret = -EINVAL;
				goto recv_failure;
		}
	}
	
	/* Deallocate */
	for(i = 0;i < mtable.number + iotable.number; i++)
	{
		sysjob_free(server_addr[i],req_array[i],req_size,BMI_SEND_BUFFER,NULL);
		sysjob_free(server_addr[i],ack_array[i],ack_size,BMI_RECV_BUFFER,NULL);
	}
	free(ack_array);
	free(req_array);
	
	if (status_array)
		free(status_array);

	if (recv_id_array)
		free(recv_id_array);

	if (send_id_array)
		free(send_id_array);

	if (server_addr)
		free(server_addr);

	return(0);

recv_failure:
	for(i = 0;i < (mtable.number + iotable.number); i++)
	{
		sysjob_free(server_addr[i],ack_array[i],ack_size,BMI_RECV_BUFFER,NULL);
	}
send_failure:
	for(i = 0;i < mtable.number + iotable.number; i++)
	{
		sysjob_free(server_addr[i],req_array[i],req_size,BMI_SEND_BUFFER,NULL);
	}
job_fill_failure:
	if (ack_array)
		free(ack_array);
	if (req_array)
		free(req_array);
	if (status_array)
		free(status_array);

status_alloc_failure:
	if (recv_id_array)
		free(recv_id_array);
	if (send_id_array)
		free(send_id_array);

id_alloc_failure:
	if (server_addr)
		free(server_addr);

addr_alloc_failure:
	/* Shutdown all structures that have been initialized */
	/*PVFS_sys_finalize();*/

	return(ret);
}


/* mstatreq_alloc()
 *
 * sets up a metaserver statfs request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int mstatreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0;
	PVFS_sysreq_statfs *req = (PVFS_sysreq_statfs *)preq;

	/* Fill up the request structure */
	size = sizeof(struct PVFS_server_req_s); 
	/* Alloc memory for request structure */
	(*serv_req) = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}

	/* Set up the request for statfs */
	(*serv_req)->op = PVFS_SERV_STATFS;	
	(*serv_req)->u.statfs.server_type = 0;	
	(*serv_req)->rsize = size; 
	(*serv_req)->credentials = req->credentials; 

	*sz = size;
	
	return(0);
}

/* iostatreq_alloc()
 *
 * sets up a I/O server statfs request using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int iostatreq_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_req_s **serv_req = (struct PVFS_server_req_s **)pjob;
	int size = 0;
	PVFS_sysreq_statfs *req = (PVFS_sysreq_statfs *)preq;

	/* Fill up the response structure */
	size = sizeof(struct PVFS_server_req_s); 
	/* Alloc memory for request structure */
	(*serv_req) = BMI_memalloc(server,size,BMI_SEND_BUFFER);
	if (!(*serv_req))
	{
		return(-ENOMEM);
	}

	/* Set up the request for statfs */
	(*serv_req)->op = PVFS_SERV_IOSTATFS;	
	(*serv_req)->u.statfs.server_type = 0;	
	(*serv_req)->rsize = size; 
	(*serv_req)->credentials = req->credentials; 

	*sz = size;

	return(0);
}

/* mstatack_alloc()
 *
 * sets up a statfs acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int mstatack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;

	/* Fill up the response structure */
	size = sizeof(struct PVFS_server_resp_s); 
	/* Alloc memory for request structure */
	(*serv_resp) = BMI_memalloc(server,size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the request for statfs */
	memset(*serv_resp,0,sizeof(struct PVFS_server_resp_s)); 
	(*serv_resp)->op = PVFS_SERV_STATFS;	

	(*serv_resp)->u.statfs.stat.u.mstat.filetotal = 0;
	(*serv_resp)->rsize = size;

	*sz = size;

	return(0);
}

/* iostatack_alloc()
 *
 * sets up a statfs acknowledgement using a pre-existing job structure
 *
 * returns 0 on success, -errno on failure
 */
static int iostatack_alloc(void *pjob,void *preq,bmi_addr_t server,
		int *sz)
{
	struct PVFS_server_resp_s **serv_resp = (struct PVFS_server_resp_s **)pjob;
	int size = 0;
	/*PVFS_sysreq_statfs *req = (PVFS_sysreq_statfs *)preq;*/

	/* Fill up the response structure */
	size = sizeof(struct PVFS_server_resp_s); 
	/* Alloc memory for response structure */
	(*serv_resp) = BMI_memalloc(server,size,BMI_RECV_BUFFER);
	if (!(*serv_resp))
	{
		return(-ENOMEM);
	}

	/* Set up the response for statfs */
	memset(*serv_resp,0,sizeof(struct PVFS_server_resp_s)); 
	(*serv_resp)->op = PVFS_SERV_IOSTATFS;	

	(*serv_resp)->u.statfs.stat.u.iostat.blkfree = 0;
	(*serv_resp)->u.statfs.stat.u.iostat.blksize = 0;
	(*serv_resp)->u.statfs.stat.u.iostat.blktotal = 0;
	(*serv_resp)->u.statfs.stat.u.iostat.filetotal = 0;
	(*serv_resp)->u.statfs.stat.u.iostat.filefree = 0;
	(*serv_resp)->rsize = size;
	
	*sz = size;

	return(0);
}


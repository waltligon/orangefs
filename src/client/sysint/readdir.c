/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Read Directory Function Implementation */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-servreq.h>
#include <config-manage.h>

/* PVFS_sys_readdir()
 *
 * read a directory with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_readdir(PVFS_sysreq_readdir *req, PVFS_sysresp_readdir *resp)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1;
	pinode_p pinode_ptr = NULL;
	bmi_addr_t serv_addr1;	/* PVFS address type structure */
	char *server1 = NULL;
	int cflags = 0,vflags = 0;
	PVFS_bitfield attr_mask;
	PVFS_servreq_readdir req_readdir;
	
	/* Revalidate directory handle */
	/* Get the directory pinode */
	vflags = 0;
	attr_mask = ATTR_BASIC;
	ret = phelper_get_pinode(req->pinode_refn,&pinode_ptr,
			attr_mask, vflags, req->credentials);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	/* Validate the pinode */
	ret = phelper_validate_pinode(pinode_ptr,cflags,attr_mask,req->credentials);
	if (ret < 0)
	{
			goto pinode_get_failure;
	}
	/* Free the pinode */
	PINT_pcache_pinode_dealloc(pinode_ptr);

	/* Read directory server request */

	/* Query the BTI to get initial meta server */
	ret = config_bt_map_bucket_to_server(&server1,req->pinode_refn.handle,\
	  		req->pinode_refn.fs_id);
	if (ret < 0)
	{
		goto get_bucket_failure;
	}
	ret = BMI_addr_lookup(&serv_addr1,server1);
	if (ret < 0)
	{
		goto get_bucket_failure;
	}
	/* Deallocate allocated memory */
	if (server1)
		free(server1);

	/* Fill in the parameters */
	req_readdir.handle = req->pinode_refn.handle;
	req_readdir.fs_id = req->pinode_refn.fs_id;
	req_readdir.token = req->token;
	req_readdir.pvfs_dirent_count = req->pvfs_dirent_incount;
	 
	/* server request */
	ret = pint_serv_readdir(&req_job,&ack_job,&req_readdir,req->credentials,\
			&serv_addr1); 
	if (ret < 0)
	{
		goto readdir_failure;
	}
	/* New entry pinode reference */
	resp->token = ack_job->u.readdir.token;
	resp->pvfs_dirent_outcount = ack_job->u.readdir.pvfs_dirent_count;
	memcpy(resp->dirent_array,ack_job->u.readdir.pvfs_dirent_array,\
			sizeof(PVFS_dirent) * ack_job->u.readdir.pvfs_dirent_count);

	/* Free the jobs */
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	return(0);

readdir_failure:
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

get_bucket_failure:
	if (server1)
		free(server1);

pinode_get_failure:
	/* Free the pinode */
	if (pinode_ptr)
		pcache_pinode_dealloc(pinode_ptr);

	return(ret);
}

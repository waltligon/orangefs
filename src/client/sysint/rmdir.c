/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Remove Directory Function Implementation */

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "config-manage.h"
#include "pcache.h"

static int do_crdirent(char *name,pinode_reference parent,\
	PVFS_handle entry_handle,PVFS_credentials credentials,bmi_addr_t addr);

/* PVFS_sys_rmdir()
 *
 * Remove a directory with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_rmdir(PVFS_sysreq_rmdir *req)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr1,serv_addr2;	/* PVFS address type structure */
	char *server1 = NULL,*server2 = NULL;
	int cflags = 0,name_sz = 0;
	int item_found = 0;
	PVFS_bitfield mask;
	pinode_reference entry;
	PVFS_servreq_rmdir req_rmdir;
	PVFS_servreq_rmdirent req_rmdirent;
	
	/* Allocate the pinode */
	ret = PINT_pcache_pinode_alloc(&pinode_ptr);
	if (!pinode_ptr)
	{
		ret = -ENOMEM;
		goto pinode_alloc_failure;
	}
	/* Revalidate the parent handle */
	/* Get the parent pinode */
	cflags = HANDLE_VALIDATE;
	/* Search in pinode cache */
	ret = PINT_pcache_lookup(req->parent_refn,&pinode_ptr);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	/* Is pinode present? */
	if (pinode_ptr->pinode_ref.handle != -1)
	{
		cflags = HANDLE_VALIDATE;
		mask = ATTR_BASIC;
		ret = phelper_validate_pinode(pinode_ptr,cflags,mask,req->credentials);
		if (ret < 0)
		{
			goto pinode_get_failure;
		}
	}

	/* Remove directory entry server request */

	/* Query the BTI to get initial meta server */
	ret = config_bt_map_bucket_to_server(&server1,req->parent_refn.handle,\
			req->parent_refn.fs_id);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	ret = BMI_addr_lookup(&serv_addr1,server1);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}
	name_sz = strlen(req->entry_name);
	/* Fill in the parameters */
	req_rmdirent.entry = (PVFS_string)malloc(name_sz + 1);
	if (!req_rmdirent.entry)
	{
		ret = -ENOMEM;
		goto addr_lookup_failure;	
	}
	strncpy(req_rmdirent.entry,req->entry_name,name_sz);
	req_rmdirent.entry[name_sz] = '\0'; 
	req_rmdirent.parent_handle = req->parent_refn.handle;
	req_rmdirent.fs_id = req->parent_refn.fs_id;
	 
	/* server request */
	ret = pint_serv_rmdirent(&req_job,&ack_job,&req_rmdirent,\
			req->credentials,&serv_addr1); 
	if (ret < 0)
	{
		goto rmdirent_failure;
	}
	/* New entry pinode reference */
	entry.handle = ack_job->u.rmdirent.entry_handle;
	entry.fs_id = req->parent_refn.fs_id;

	/* Free the jobs */
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	/* Remove directory server request */

	/* Fill in the parameters */
	req_rmdir.handle = entry.handle;
	req_rmdir.fs_id = entry.fs_id;

	/* Query the BTI to get initial meta server */
	ret = config_bt_map_bucket_to_server(&server2,entry.handle,entry.fs_id);
	if (ret < 0)
	{
		goto remove_map_failure;
	}
	ret = BMI_addr_lookup(&serv_addr2,server2);
	if (ret < 0)
	{
		goto remove_addr_lookup_failure;
	}
	/* Do a Remove directory entry with a possible rollback  */
	ret = pint_serv_rmdir(&req_job,&ack_job,&req_rmdir,\
			req->credentials,&serv_addr2);
	if (ret < 0)
	{
		goto remove_failure; 
	}
	
	/* Remove from pinode cache */
	ret = PINT_pcache_remove(entry,&item_ptr);
	if (ret < 0)
	{
		goto remove_failure;
	}
	/* Free the pinode removed from cache */
	PINT_pcache_pinode_dealloc(item_ptr);
	
	/* Create and fill in a dentry and add it to the dcache */
	ret = PINT_dcache_remove(req->entry_name,req->parent_refn,\
			&item_found);
	if (ret < 0)
	{
		goto remove_failure;
	}

	sysjob_free(serv_addr2,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr2,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	return(0);

remove_failure:
	if (req_job)
		sysjob_free(serv_addr2,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	if (ack_job)
		sysjob_free(serv_addr2,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	
remove_addr_lookup_failure:
	if (server2)
		free(server2);

remove_map_failure:
	/* Make a crdirent request to the server */
	ret = do_crdirent(req->entry_name,req->parent_refn,entry.handle,\
			req->credentials,serv_addr1);

rmdirent_failure:
	if (req_rmdirent.entry)
		free(req_rmdirent.entry);

addr_lookup_failure:
	if (server1)
		free(server1);

pinode_get_failure:
	if (req_job)
		sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	if (ack_job)
		sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	
	/* Free the pinode */
	if (pinode_ptr)
		PINT_pcache_pinode_dealloc(pinode_ptr);

pinode_alloc_failure:

	return(ret);
}

/* do_crdirent 
 *
 * perform a create directory entry server request
 *
 * returns 0 on success, -errno on error
 */
static int do_crdirent(char *name,pinode_reference parent,\
		PVFS_handle entry_handle,PVFS_credentials credentials,bmi_addr_t addr)
{
	struct PVFS_server_req_s *req_job = NULL;
	struct PVFS_server_resp_s *ack_job = NULL;
	PVFS_servreq_createdirent req_crdirent;
	int ret = 0,name_sz = strlen(name);

	/* Fill in the arguments */
	req_crdirent.name = (char *)malloc(name_sz + 1);
	if (!req_crdirent.name)
	{
		return(-ENOMEM);	
	}
	strncpy(req_crdirent.name,name,name_sz);
	req_crdirent.name[name_sz] = '\0';
	req_crdirent.new_handle = entry_handle;
	req_crdirent.parent_handle = parent.handle;
	req_crdirent.fs_id = parent.fs_id;

	/* Make the crdirent request */
	ret = pint_serv_crdirent(&req_job,&ack_job,&req_crdirent,credentials,\
			&addr);
	if (ret < 0)
	{
		/* Free memory allocated for the name */
		if (req_crdirent.name)
			free(req_crdirent.name);
		return(ret);
	}

	return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

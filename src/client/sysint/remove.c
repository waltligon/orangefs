/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Remove Function Implementation */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-dcache.h>
#include <pint-servreq.h>

#if 0
static int get_bmi_address(bmi_addr_t *io_addr_array, int32_count num_io,\
		PVFS_handle *handle_array);
static int do_crdirent(char *name,PVFS_handle parent,PVFS_fs_id fsid,\
		PVFS_handle entry_handle,bmi_addr_t addr);
#endif

extern pcache pvfs_pcache; 

/* PVFS_sys_remove()
 *
 * Remove a file with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_remove(PVFS_sysreq_remove *req, PVFS_sysresp_remove *resp)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1,req_size = 0,ack_size = 0, ioserv_cnt = 0;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr1,serv_addr2;	/* PVFS address type structure */
	char *server1 = NULL,*server2 = NULL;
	int cflags = 0,name_sz = 0;
	PVFS_bitfield mask;
	bmi_addr_t *io_addr_array = NULL;
	struct timeval cur_time;
	pinode_reference entry,parent_reference;
	PVFS_servreq_remove req_remove;
	PVFS_servreq_createdirent req_crdirent;
	int item_found;

	/* lookup meta file */
	req->entry_name
	/* refresh parent pinode */
	attr_mask = ATTR_BASIC | ATTR_META;
	ret = phelper_get_pinode(req->parent_refn,&pinode_ptr,
			attr_mask, req->credentials );
	if (ret < 0)
	{
		goto pinode_get_failure;
	}

	/* check permsission on parent */
	
	req->credentials
	/* getattr the meta file */
	/* send remove message to the meta file */
	/* rmdirent the dir entry */
	/* send remove messages to each of the data file servers */
	


}
#if 0

	/* Fill in parent pinode reference */
	parent_reference.handle = req->parent_handle;
	parent_reference.fs_id = req->fs_id;

	/* Revalidate the parent handle */
	/* Get the parent pinode */
	vflags = 0;
	attr_mask = ATTR_BASIC + ATTR_META;
	/* Get the pinode either from cache or from server */
	ret = phelper_get_pinode(parent_reference,pvfs_pcache,&pinode_ptr,\
			attr_mask, vflags, req->credentials );
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	cflags = HANDLE_REUSE;
	mask = ATTR_BASIC + ATTR_META;
	ret = phelper_validate_pinode(pinode_ptr,cflags,mask);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}

	/* Remove directory entry server request */

	/* Query the BTI to get initial meta server */
	/* TODO: Uncomment this after implementation !!! */
	/*ret = bt_map_bucket_to_server(&server1,req->parent_handle,req->fs_id);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}*/
	ret = BMI_addr_lookup(&serv_addr1,server1);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	name_sz = strlen(req->entry_name);
	/* Fill in the parameters */
	req_rmdirent.entry = (PVFS_string)malloc(name_sz + 1);
	if (!req_rmdirent.entry)
	{
		ret = -ENOMEM;
		goto pinode_get_failure;	
	}
	strncpy(req_rmdir.entry,req->entry_name,name_sz);
	req_rmdirent.entry[name_sz] = '\0'; 
	req_rmdirent.parent_handle = req->parent_handle;
	req_rmdirent.fs_id = req->fs_id;
	 
	/* server request */
	ret = pint_serv_rmdirent(&req_job,&ack_job,&req_rmdirent,&serv_addr1); 
	if (ret < 0)
	{
		goto rmdirent_failure;
	}
	/* Removed entry pinode reference */
	entry.handle = ack_job->u.rmdirent.entry_handle;
	entry.fs_id = req->fs_id;

	/* Free the jobs */
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	/* Make a "remove" request to all I/O servers */
	/* Store the I/O server count */
	ioserv_cnt = pinode_ptr->attr.u.meta.nr_datafiles;
	/* Allocate the I/O server address array */
	io_addr_array = (bmi_addr_t *)malloc(sizeof(bmi_addr_t) * ioserv_cnt);
	if (!io_addr_array)
	{
		ret = -ENOMEM;
		goto rmdirent_failure; 
	}
	/* Convert all handles to BMI_addresses */
	ret = get_bmi_address(io_addr_array,ioserv_cnt,\
			pinode_attr->attr.u.meta.dfh);
	if (ret < 0)
	{	
		goto get_address_failure;
	}
			
	/* Make server request */
	for(i = 0;i < ioserv_cnt;i++)
	{
		/* Fill in the parameters */
		req_remove.handle = pinode_attr->attr.u.meta.dfh[i];
		req_remove.fs_id = entry.fs_id;
	
		/* Do a remove with a possible rollback  */
		ret = pint_serv_remove(&req_job,&ack_job,&req_remove,req->credentials,\
				io_addr_array[i]);
		if (ret < 0)
		{
			goto io_remove_failure; 
		}

		/* Free the req,ack jobs */
		sysjob_free(io_addr_array[i],ack_job,ack_job->rsize,BMI_RECV_BUFFER,\
				NULL);
		sysjob_free(io_addr_array[i],req_job,req_job->rsize,BMI_SEND_BUFFER,\
				NULL);
	}
	
	/* Make a "remove" request to metaserver */
	/* Query the BTI to get initial meta server */
	/* TODO: Uncomment this after implementation !!! */
	/*ret = bt_map_bucket_to_server(&server2,entry->handle,req->fs_id);
	if (ret < 0)
	{
		goto remove_map_bucket_failure;
	}*/
	ret = BMI_addr_lookup(&serv_addr2,server2);
	if (ret < 0)
	{
		goto remove_addr_lookup_failure;
	}
	/* Make server request */
	req_remove.handle = entry->handle;
	req_remove.fs_id = entry->fs_id;
	
	ret = pint_serv_remove(&req_job,&ack_job,&req_remove,req->credentials,\
			&serv_addr2);
	if (ret < 0)
	{
		goto io_remove_failure;
	}

	/* Remove from pinode cache */
	/* TODO: Need we track the error as otherwise the entire operation
	 * has completed? */
	ret = pcache_remove(pvfs_pcache,entry,&item_ptr);
	/*if (ret < 0)
	{
		goto remove_failure;
	}*/
	/* Free the pinode removed from cache */
	/*pcache_pinode_dealloc(item_ptr);*/
	
	/* Remove the dentry from the dcache */
	/* TODO: Need we track the error as otherwise the entire operation
	 * has completed? */
	ret = PINT_dcache_remove(req->entry_name,parent_reference,\
			&item_found);
	/*if (ret < 0)
	{
		goto remove_failure;
	}*/

	sysjob_free(serv_addr2,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr2,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	return(0);

io_remove_failure:
	/* Need to create datafiles on selected I/O servers */
	for(j = 0;j < i;j++)
	{
		/* TODO: Get the bucket from the handle */	
		/*req_create.bucket = pinode_attr->attr.u.meta.dfh[i];
		req_create.handle_mask = x;*/
		req_create.fs_id = req->fs_id;
		req_create.type = pinode_attr->attr.objtype;

		/* Server request */
		ret = pint_serv_create(&req_job,&ack_job,&req_create,\
				req->credentials,io_addr_array[j]);
		if (ret < 0)
		{
			goto get_address_failure;	
		}

		/* Free the req,ack jobs */
		sysjob_free(io_addr_array[j],ack_job,ack_job->rsize,BMI_RECV_BUFFER,\
				NULL);
		sysjob_free(io_addr_array[j],req_job,req_job->rsize,BMI_SEND_BUFFER,\
				NULL);
	}
	/* Make a "create directory entry" server request */
	ret = do_crdirent(req->entry_name,req->parent_handle,req->fs_id,
				entry.handle,serv_addr1);
	
get_address_failure:
	if (io_addr_array)
		free(io_addr_array);

rmdirent_failure:
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	if (req_rmdirent.entry)
		free(req_rmdirent.entry);

addr_lookup_failure:
	if (server1)
		free(server1);

pinode_get_failure:
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	
	/* Free the pinode */
	if (pinode_ptr)
		pcache_pinode_dealloc(pinode_ptr);

	return(ret);
}

/* do_create 
 *
 * perform a create server request
 *
 * returns 0 on success, -errno on error
 */
static int do_create(char *name,PVFS_handle parent,PVFS_fs_id fsid,\
		PVFS_handle entry_handle,bmi_addr_t addr)
{
	struct PVFS_server_req_s *req_job = NULL;
	struct PVFS_server_resp_s *ack_job = NULL;
	PVFS_servreq_crdirent req_crdirent;
	int ret = 0;

	/* Fill in the arguments */
	req_crdirent.name = (char *)malloc(strlen(name) + 1);
	if (!req_crdirent.name)
	{
		return(-ENOMEM);	
	}
	req_crdirent.new_handle = entry_handle;
	req_crdirent.parent_handle = parent;
	req_crdirent.fs_id = fsid;

	/* Make the crdirent request */
	ret = pint_serv_crdirent(&req_job,&ack_job,&req_crdirent,&addr);
	if (ret < 0)
	{
		/* Free memory allocated for the name */
		if (req_crdirent.name)
			free(req_crdirent.name);
		return(ret);
	}

	return(0);
}

/* do_crdirent 
 *
 * perform a create directory entry server request
 *
 * returns 0 on success, -errno on error
 */
static int do_crdirent(char *name,PVFS_handle parent,PVFS_fs_id fsid,\
		PVFS_handle entry_handle,bmi_addr_t addr)
{
	struct PVFS_server_req_s *req_job = NULL;
	struct PVFS_server_resp_s *ack_job = NULL;
	PVFS_servreq_crdirent req_crdirent;
	int ret = 0;

	/* Fill in the arguments */
	req_crdirent.name = (char *)malloc(strlen(name) + 1);
	if (!req_crdirent.name)
	{
		return(-ENOMEM);	
	}
	req_crdirent.new_handle = entry_handle;
	req_crdirent.parent_handle = parent;
	req_crdirent.fs_id = fsid;

	/* Make the crdirent request */
	ret = pint_serv_crdirent(&req_job,&ack_job,&req_crdirent,&addr);
	if (ret < 0)
	{
		/* Free memory allocated for the name */
		if (req_crdirent.name)
			free(req_crdirent.name);
		return(ret);
	}

	return(0);
}

/*	get_bmi_address
 *
 * obtains an array of BMI addresses given an array of handles
 *
 * return 0 on success, -errno on failure
 */
int get_bmi_address(bmi_addr_t *io_addr_array, int32_count num_io,\
		PVFS_handle *handle_array)
{
	int index = 0;

	for(index = 0;index < num_io; index++)
	{
		/* Query the BTI to get initial meta server */
		/* TODO: Uncomment this!!! */
		/*ret = bt_map_bucket_to_server(&server2,entry.handle);
		if (ret < 0)
		{
			goto remove_map_failure;
		}*/	
		ret = BMI_addr_lookup(&serv_addr2,server2);
		if (ret < 0)
		{
			goto remove_addr_lookup_failure;
		}
	}
	return(0);
}

#endif

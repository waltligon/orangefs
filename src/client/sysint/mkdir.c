/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Make Directory Function Implementation */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-dcache.h>
#include <pint-servreq.h>
#include <config-manage.h>

static int do_lookup(PVFS_string name,pinode_reference parent,\
		PVFS_bitfield mask,PVFS_credentials cred,pinode_reference *entry);
static int do_rmdir(PVFS_handle entry_handle,PVFS_fs_id fsid,\
		bmi_addr_t addr, PVFS_credentials credentials);

extern pcache pvfs_pcache;

/* PVFS_sys_mkdir()
 *
 * create a directory with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_mkdir(PVFS_sysreq_mkdir *req, PVFS_sysresp_mkdir *resp)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1;
	pinode_p pinode_ptr = NULL, item_ptr = NULL;
	bmi_addr_t serv_addr1,serv_addr2;	/* PVFS address type structure */
	char *server1 = NULL,*server2 = NULL;
	int item = 0;
	int cflags = 0,name_sz = 0;
	PVFS_bitfield mask;
	struct timeval cur_time;
	PVFS_handle meta_bucket = 0, bucket_mask = 0;
	pinode_reference entry;
	PVFS_servreq_mkdir req_mkdir;
	PVFS_servreq_createdirent req_crdirent;
	
	/* Fill in parent pinode reference */
	//parent_reference = req->parent_refn;

	/* Allocate a pinode */
	ret = pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto pinode_alloc_failure;
	}
	/* Check for pinode existence */

	/* Lookup handle(if it exists) in dcache */
	ret = PINT_dcache_lookup(req->entry_name,req->parent_refn,&entry);
	if (ret < 0)
	{
		/* Entry not in dcache */

		/* Set the attribute mask */
		mask = ATTR_BASIC;
		/* Entry not in dcache, do a lookup */
		ret = do_lookup(req->entry_name,req->parent_refn,mask,\
				req->credentials,&entry);
		if (ret < 0)
		{
			entry.handle = -1;
		}
	}	
	/* Do only if pinode exists */
	if (entry.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		/* Search in pinode cache */
		ret = pcache_lookup(&pvfs_pcache,entry,pinode_ptr);
		if (ret < 0)
		{
			goto pinode_get_failure;
		}
		/* Is pinode present? */
		if (pinode_ptr->pinode_ref.handle != -1)
		{
			/* Pinode is present, remove it */
			ret = pcache_remove(&pvfs_pcache,entry,&item_ptr);
			if (ret < 0)
			{
				goto pinode_remove_failure;
			}
			/* Free the pinode removed from the cache */
			pcache_pinode_dealloc(item_ptr);

			/* Free previously allocated pinode */
			/*if (pinode_ptr)
				pcache_pinode_dealloc(pinode_ptr); */
		}
		/* Remove dcache entry */
		/* At this stage, dcache entry does exist. 
		 * This needs to handled the right way */
		ret = PINT_dcache_remove(req->entry_name,req->parent_refn,&item);
		if (ret < 0)
		{
			goto pinode_remove_failure;
		}
	}
	
	/* Revalidate the parent handle */
	/* Get the parent pinode */
	cflags = HANDLE_VALIDATE;
	/* Search in pinode cache */
	ret = pcache_lookup(&pvfs_pcache,req->parent_refn,pinode_ptr);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	/* Is pinode present? */
	if (pinode_ptr->pinode_ref.handle != -1)
	{
		cflags = HANDLE_VALIDATE;
		mask = req->attrmask; /* Make sure ATTR_BASIC is selected */
		ret = phelper_validate_pinode(pinode_ptr,cflags,mask,req->credentials);
		if (ret < 0)
		{
			goto pinode_remove_failure;
		}
	}

	/* Free the pinode allocated */
	if (pinode_ptr)
		pcache_pinode_dealloc(pinode_ptr);

	/* Make directory server request */

	/* Query the BTI to get initial meta server */
	ret = config_bt_get_next_meta_bucket(req->parent_refn.fs_id,\
			&server1,&meta_bucket,&bucket_mask);
	if (ret < 0)
	{
		goto pinode_remove_failure;
	}
	ret = BMI_addr_lookup(&serv_addr1,server1);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}
	name_sz = strlen(req->entry_name);
	/* Fill in the parameters */
	req_mkdir.bucket = meta_bucket ; 
	req_mkdir.handle_mask = bucket_mask;
	req_mkdir.fs_id = req->parent_refn.fs_id;
	req_mkdir.attr = req->attr;
	req_mkdir.attrmask = req->attrmask;
	 
	/* server request */
	ret = pint_serv_mkdir(&req_job,&ack_job,&req_mkdir,req->credentials,\
			&serv_addr1); 
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}
	/* New entry pinode reference */
	entry.handle = ack_job->u.mkdir.handle;
	entry.fs_id = req->parent_refn.fs_id;
	/* Free the jobs */
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	/* Create directory entry server request */

	/* Fill in the parameters */
	req_crdirent.name = (PVFS_string)malloc(name_sz + 1);
	if (!req_crdirent.name)
	{
		ret = -ENOMEM;
		goto addr_lookup_failure;	
	}
	strncpy(req_crdirent.name,req->entry_name,name_sz);
	req_crdirent.name[name_sz] = '\0'; 
	req_crdirent.new_handle = entry.handle;
	req_crdirent.parent_handle = req->parent_refn.handle;
	req_crdirent.fs_id = req->parent_refn.fs_id;

	/* Query the BTI to get initial meta server */
	ret = config_bt_map_bucket_to_server(&server2,req->parent_refn.handle,\
			req->parent_refn.fs_id);
	if (ret < 0)
	{
		goto crdirent_map_failure;
	}
	ret = BMI_addr_lookup(&serv_addr2,server2);
	if (ret < 0)
	{
		goto crdirent_addr_lookup_failure;
	}
	/* Do a create directory entry with a possible rollback  */
	ret = pint_serv_crdirent(&req_job,&ack_job,&req_crdirent,req->credentials,\
			&serv_addr2);
	if (ret < 0)
	{
		goto crdirent_failure; 
	}
	
	/* Create and fill in a pinode and add it to the cache */
	ret = pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
		goto crdirent_failure;
	}
	/* Fill in the pinode */
	pinode_ptr->pinode_ref.handle = entry.handle;
	pinode_ptr->pinode_ref.fs_id = req->parent_refn.fs_id;
	pinode_ptr->mask = req->attrmask;
	pinode_ptr->attr = req->attr;
	/* Get time */
	ret = gettimeofday(&cur_time,NULL);
	if (ret < 0)
	{
		goto crdirent_failure;
	}
	/* Fill in handle timestamp */
	pinode_ptr->tstamp_handle.tv_sec = cur_time.tv_sec + handle_to.tv_sec;
	pinode_ptr->tstamp_handle.tv_usec = cur_time.tv_usec + handle_to.tv_usec;
	/* Fill in attribute timestamp */
	pinode_ptr->tstamp_attr.tv_sec = cur_time.tv_sec + attr_to.tv_sec;
	pinode_ptr->tstamp_attr.tv_usec = cur_time.tv_usec + attr_to.tv_usec;
	/* Fill in size timestamp */
	if (req->attrmask & ATTR_SIZE)
	{
		pinode_ptr->tstamp_size.tv_sec = cur_time.tv_sec + size_to.tv_sec;
		pinode_ptr->tstamp_size.tv_usec = cur_time.tv_usec + size_to.tv_usec;
	}
	/* Add to cache */
	ret = pcache_insert(&pvfs_pcache,pinode_ptr);
	if (ret < 0)
	{
		goto crdirent_failure;
	}
	
	/* Create and fill in a dentry and add it to the dcache */
	ret = PINT_dcache_insert(req->entry_name,entry,req->parent_refn);
	if (ret < 0)
	{
		goto crdirent_failure;
	}

	/* Fill up the response */
	resp->pinode_refn.handle = entry.handle;

	return(0);

crdirent_failure:
		/* Need to send a rmdir request to the server */
		ret = do_rmdir(entry.handle,req->parent_refn.fs_id,serv_addr1,\
				req->credentials);
	
crdirent_addr_lookup_failure:
	if (server2)
		free(server2);

crdirent_map_failure:
	if (req_crdirent.name)
		free(req_crdirent.name);

addr_lookup_failure:
	if (server1)
		free(server1);

pinode_remove_failure:
   /* Free the pinode allocated but not added to list */
	if (pinode_ptr)
		pcache_pinode_dealloc(pinode_ptr);

pinode_get_failure:
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	
	/* Free the pinode */
	if (pinode_ptr)
		pcache_pinode_dealloc(pinode_ptr);

pinode_alloc_failure:
	return(ret);
}

/* do_rmdir 
 *
 * perform a server rmdir request
 *
 * returns 0 on success, -errno on error
 */
static int do_rmdir(PVFS_handle entry_handle,PVFS_fs_id fsid,\
		bmi_addr_t addr, PVFS_credentials credentials)
{
	struct PVFS_server_req_s *req_job = NULL;
	struct PVFS_server_resp_s *ack_job = NULL;
	PVFS_servreq_rmdir req_rmdir;
	int ret = 0;

	/* Fill in the arguments */
	req_rmdir.handle = entry_handle;
	req_rmdir.fs_id = fsid;

	/* Make the rmdir request */
	ret = pint_serv_rmdir(&req_job,&ack_job,&req_rmdir,credentials,&addr);
	if (ret < 0)
	{
		return(ret);
	}

	return(0);
}

/* do_lookup()
 *
 * perform a server lookup path request
 *
 * returns 0 on success, -errno on failure
 */
static int do_lookup(PVFS_string name,pinode_reference parent,\
		PVFS_bitfield mask,PVFS_credentials cred,pinode_reference *entry)
{
	struct PVFS_server_req_s *req_job;
	struct PVFS_server_resp_s *ack_job;
	PVFS_servreq_lookup_path req_args;
	char *server = NULL;
	bmi_addr_t serv_addr;
	int ret = 0,count = 0;

	/* Get Metaserver in BMI URL format using the bucket table 
	 * interface 
	 */
	ret = config_bt_map_bucket_to_server(&server,parent.handle,parent.fs_id);
	if (ret < 0)
	{
		goto map_to_server_failure;
	}
	ret = BMI_addr_lookup(&serv_addr,server);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}

	/* Fill in the arguments */
	req_args.path = (PVFS_string)malloc(strlen(name) + 1);
	if (!req_args.path)
	{
		ret = -ENOMEM;
		goto addr_lookup_failure;
	}
	strncpy(req_args.path,name,strlen(name));
	req_args.path[strlen(name)] = '\0';
	req_args.starting_handle = parent.handle;
	req_args.fs_id = parent.fs_id;
	req_args.attrmask = mask;

	/* Make a lookup_path server request to get the handle and
	 * attributes of segment
	 */
	ret = pint_serv_lookup_path(&req_job,&ack_job,&req_args,\
			cred,&serv_addr);
	if (ret < 0)
	{
		goto lookup_path_failure;
	}
	count = ack_job->u.lookup_path.count;
	if (count != 1)
	{
		ret = -EINVAL;
		goto lookup_path_failure;
	}

	/* Fill up pinode reference for entry */
	entry->handle = ack_job->u.lookup_path.handle_array[1];

	/* Free the jobs */
	sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

	return(0);

lookup_path_failure:
	if (req_args.path)
		free(req_args.path);
addr_lookup_failure:
	if (server)
		free(server);

map_to_server_failure:
	sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

	return(ret);
}

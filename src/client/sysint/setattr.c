/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Set Attribute Function Implementation */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-servreq.h>
#include <config-manage.h>

extern pcache pvfs_pcache;
extern struct dcache pvfs_dcache;

/* PVFS_sys_setattr()
 *
 * sets attributes for a particular PVFS file 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_setattr(PVFS_sysreq_setattr *req)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1, flags = 0;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr;				/* PVFS address type structure */
	char *server = NULL;
	PVFS_bitfield mask = req->attrmask;
	pinode_reference entry;
	PVFS_servreq_setattr req_args;
	PVFS_size handlesize = 0;

	/* Validate the handle */
	/* If size to be fetched, distribution needs to be fetched
	 * along with other desired metadata
	 */
	if ((mask & ATTR_SIZE) == ATTR_SIZE)
		mask = mask | ATTR_META;

	/* Fill in pinode reference */
	entry.handle = req->pinode_refn.handle;
	entry.fs_id = req->pinode_refn.fs_id;
	
	/* Get the pinode */
	ret = pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto pinode_alloc_failure;
	}
	/* Lookup the entry...may or may not exist in the cache */
	ret = pcache_lookup(&pvfs_pcache,entry,pinode_ptr);
	if (ret < 0)
	{
		goto pcache_lookup_failure;
	}
	/* Validate the pinode for handle reuse */
	/* Check if pinode was returned */
	if (pinode_ptr->pinode_ref.handle != -1)
	{
		flags = HANDLE_VALIDATE;
		mask = mask | ATTR_BASIC;	
		ret = phelper_validate_pinode(pinode_ptr,flags,mask,req->credentials);
		if (ret < 0)
		{
			goto pcache_lookup_failure;
		}
	}
	/* Free the previously allocated pinode */
	//pcache_pinode_dealloc(pinode_ptr);

	/* TODO: If no pinode, assume handle is valid.Is this ok? */
	

	/* Make a setattr request */

	/* Get the server thru the BTI using the handle */
	ret = config_bt_map_bucket_to_server(&server,entry.handle,entry.fs_id);
	if (ret < 0)
	{
		goto pcache_lookup_failure;
	}
	ret = BMI_addr_lookup(&serv_addr,server);
	if (ret < 0)
	{
		goto map_to_server_failure;
	}

	/* Fill in the parameters */
	req_args.handle = entry.handle;
	req_args.fs_id = entry.fs_id;
	req_args.attrmask = mask;
	req_args.attr = req->attr;
	if (mask & ATTR_META)
	{
		handlesize = req->attr.u.meta.nr_datafiles * sizeof(PVFS_handle);
	}
	/* Make a server setattr request */	
	ret = pint_serv_setattr(&req_job,&ack_job,&req_args,req->credentials,\
			handlesize,&serv_addr);
	if (ret < 0)
	{
		goto map_to_server_failure;
	}

	/* Remove the pinode only if it is in the cache */
	/* Note: Until an error returning scheme is decided
	 * this will return a success even in case of error
	 * as setattr is complete and there is no way to know
	 * that some problem occurred
	 */
	if (pinode_ptr->pinode_ref.handle != -1)
	{
		ret = pcache_remove(&pvfs_pcache,entry,&item_ptr);
		if (ret < 0)
		{
			goto pinode_remove_failure;
		}
		/* Free the pinode returned */
		pcache_pinode_dealloc(item_ptr);
	}
	else
	{
		/* Fill in the pinode reference for the new pinode */
		pinode_ptr->pinode_ref = entry;
	}

	/* Modify pinode to reflect changed attributes */
	ret =	modify_pinode(pinode_ptr,req->attr,req->attrmask);
	if (ret < 0)
	{
		goto pinode_remove_failure;
	}
		
	/* Add the pinode and return */
	ret = pcache_insert(&pvfs_pcache,pinode_ptr);
	if (ret < 0)
	{
		goto pinode_remove_failure;
	}

	return(0);

pinode_remove_failure:
	if (ack_job)
		sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	if (req_job)
		sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

map_to_server_failure:
	if (server)
		free(server);

pcache_lookup_failure:
	pcache_pinode_dealloc(pinode_ptr);

pinode_alloc_failure:

	return(ret);
}

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Lookup Function Implementation */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-dcache.h>
#include <pint-servreq.h>
#include <config-manage.h>

extern pcache pvfs_pcache;

/* PVFS_sys_lookup()
 *
 * performs a lookup for a particular PVFS file in order to determine
 * its pinode reference for further operations
 *
 * steps in lookup
 * --------------
 * 1. Further steps depend on whether a path segment is in the dcache
 * or not.
 * 2. Determine first segment of path and determine if if it is in
 * the dcache. If not, the entire path is looked up if possible.
 * The first metaserver is determined using the root handle which
 * is obtained from the configuration management interface. The
 * server returns information for as many segments as it has 
 * metadata for. Path permissions are checked. For each segment
 * returned information is placed in dcache and pinode cache.
 * The first segment of the remaining path is determined and then
 * step 1 or step 2 is repeated.
 * 3. If in dcache, validate it. If not valid, refetch it. Check
 * path permissions. If all ok, then check for next segment of 
 * remaining path in dcache and repeat step 1 or step 2.
 * 4. At the end of step 1/2, update the last returned handle to
 * be the parent handle. This is used to determine the next server
 * for the lookup_path request to go to. 
 * 5. After the path is completely looked up, the final segment handle
 * is returned.
 * 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_lookup(PVFS_sysreq_lookup *req, PVFS_sysresp_lookup *resp)
{
    /* Initialization */   
    struct PVFS_server_req_s *req_job = NULL;	 /* server request */
    struct PVFS_server_resp_s *ack_job = NULL; /* server response */
    int ret = -1, i = 0,tflags = 0;
    pinode *entry_pinode = NULL, *pinode_ptr = NULL;
    char *server = NULL, *segment = NULL, *path = NULL;
    bmi_addr_t serv_addr;
    int start_seg = 0, vflags = 0, cflags = 0, num_seg = 0;
    int start_path = 0, end_path = 0;
    PVFS_handle parent_handle, final_handle = 0;
    PVFS_bitfield attr_mask;
    pinode_reference entry,parent;
    PVFS_servreq_lookup_path req_args;

    /* Get the total number of segments */
    get_no_of_segments(req->name,&num_seg);

    /* Get root handle using bucket table interface */
    ret = config_fsi_get_root_handle(req->fs_id,&parent_handle);
    if (ret < 0)
    {
	return(ret);
    }

    /* Get next segment */
    ret = get_next_segment(req->name,&segment,&start_seg);
    if (ret < 0)
    {
	return(ret);
    }

    /* Get the full path */
    path = (char *)malloc(strlen(req->name) + 1);
    if (!path)
    {
	ret = -ENOMEM;
	goto path_alloc_failure;
    }
    strncpy(path,req->name,strlen(req->name));
    path[strlen(req->name)] = '\0';

    /* Fill the parent pinode, parent handle is root handle */
    parent.handle = parent_handle;
    parent.fs_id = req->fs_id;

	/* Is any segment still to be looked up? */
    while(segment && ((end_path + 1) < strlen(req->name)))
    {
	/* Search in the dcache */
	ret = PINT_dcache_lookup(segment,parent,&entry);
	if(ret < 0)
	{
	    goto dcache_lookup_failure;
	}
	/* No errors */
	/* Was entry in cache? */
	if (entry.handle == PINT_DCACHE_HANDLE_INVALID)
	{
	    /* Entry not in dcache */

	    /* Need to pass the arguments */
	    req_args.path = (char *)malloc(strlen(path) + 1);
	    if (!req_args.path)
	    {
		ret = -ENOMEM;
		goto dcache_lookup_failure;
	    }
	    req_args.starting_handle = parent_handle;
	    strncpy(req_args.path,path,strlen(path));
	    req_args.path[strlen(path)] = '\0';
	    req_args.fs_id = req->fs_id;
	    req_args.attrmask = ATTR_BASIC;

			/* Get Metaserver in BMI URL format using the bucket table 
			 * interface */
	    ret = config_bt_map_bucket_to_server(&server,parent.handle,\
						 parent.fs_id);
	    if (ret < 0)
	    {
		goto map_to_server_failure;
	    }
	    ret = BMI_addr_lookup(&serv_addr,server);
	    if (ret < 0)
	    {
		goto map_to_server_failure;
	    }
	    /* Free the server */
	    if (server)
		free(server);

			/* Make a lookup_path server request to get the handle and
			 * attributes of segment
			 */
	    ret = pint_serv_lookup_path(&req_job,&ack_job,&req_args,\
					req->credentials,&serv_addr);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }
			
	    /* Repeat for number of handles returned */
	    for(i = 0; i < ack_job->u.lookup_path.count; i++)
	    {
				/* Fill up pinode reference for entry */
		entry.handle = ack_job->u.lookup_path.handle_array[i];
		entry.fs_id = req->fs_id;

				/* Add entry to dcache */
		ret = PINT_dcache_insert(segment,entry,parent);
		if (ret < 0)
		{
		    goto lookup_path_failure;
		}
				/* Add to pinode cache */
		ret = pcache_pinode_alloc(&pinode_ptr); 	
		if (ret < 0)
		{
		    ret = -ENOMEM;
		    goto lookup_path_failure ;
		}
				/* Fill the pinode */
		pinode_ptr->pinode_ref.handle = entry.handle;
		pinode_ptr->pinode_ref.fs_id = entry.fs_id;
		pinode_ptr->attr = ack_job->u.lookup_path.attr_array[i];
		pinode_ptr->mask = req_args.attrmask;

				/* Check permissions for path */
		ret = check_perms(pinode_ptr->attr,req->credentials.perms,\
				  req->credentials.uid, req->credentials.gid);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}
				/* Fill in the timestamps */
		tflags = HANDLE_TSTAMP+ ATTR_TSTAMP;	
		ret = phelper_fill_timestamps(pinode_ptr,tflags);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}
					
				/* Set the size timestamp - size was not fetched */
		pinode_ptr->tstamp_size.tv_sec = 0;
		pinode_ptr->tstamp_size.tv_usec = 0;

				/* Add to the pinode list */
		ret = pcache_insert(&pvfs_pcache,pinode_ptr);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}
				
				/* If it is the final object handle,save it!! */
		num_seg--;
		if (!num_seg)
		{
		    final_handle = pinode_ptr->pinode_ref.handle;
		}
				/* Get next segment */
		ret = get_next_segment(req->name,&segment,&start_seg);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}
		
				/* Update the parent handle with the handle of last segment
				 * that has been looked up
				 */
		parent.handle = entry.handle;
				
	    }/* For */

	    /* Get the remaining part of the path to be looked up */
	    ret = get_next_path(path,ack_job->u.lookup_path.count,&start_path,
				&end_path);
	    if (ret < 0)
	    {
		goto check_perms_failure;
	    }
	}
	else
	{
	    /* A Dcache hit! */
	    /* Get pinode from pinode cache */
	    /* If no pinode exists no error, will fetch 
	     * attributes of segment and add a new pinode
	     */
	    cflags = HANDLE_VALIDATE + ATTR_VALIDATE;
	    vflags = 0;
	    attr_mask = ATTR_BASIC;
	    /* Get the pinode from the cache */
	    ret = phelper_get_pinode(entry,&pvfs_pcache,&entry_pinode,
				     attr_mask,vflags,cflags,req->credentials);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }

	    /* Check permissions for path */
	    ret = check_perms(pinode_ptr->attr,req->credentials.perms,\
			      req->credentials.uid,req->credentials.gid);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }

	    /* Get next path */
	    ret = get_next_path(path,1,&start_path,&end_path);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }

	    /* If it is the final object handle, save it!! */
	    num_seg -= 1;
	    if (!num_seg)
	    {
		final_handle = entry_pinode->pinode_ref.handle;
	    }
	}

	/* Get next segment */
	ret = get_next_segment(req->name,&segment,&start_seg);
	if (ret < 0)
	{
	    goto lookup_path_failure;
	}
		
	/* Update the parent handle */
	parent.handle = entry.handle;

	/* Free request,ack jobs */
	sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

    }/* end of while */

    /* Fill response structure */
    resp->pinode_refn.handle = final_handle; 
    resp->pinode_refn.fs_id = req->fs_id;

    return(0);
 
 check_perms_failure:
    /* Free pinode allocated */
    pcache_pinode_dealloc(pinode_ptr);

 lookup_path_failure:
    /* Free the recently allocated pinode */
    if (entry_pinode)
	free(entry_pinode);
    if (ack_job)
	sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
    if (req_job)
	sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

 map_to_server_failure:
    if (server)
	free(server);
    /* Free memory allocated for name */
    if (req_args.path)
	free(req_args.path);

 dcache_lookup_failure:
    /* Free the path string */
    if (path)
	free(path);

 path_alloc_failure:
    if (segment)
	free(segment);

    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Lookup Function Implementation */
#include <malloc.h>
#include <assert.h>
#include <string.h>

#include "pcache.h"
#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "PINT-reqproto-encode.h"

#define REQ_ENC_FORMAT 0
/* TODO: figure out the maximum number of handles a given metafile can have*/
#define MAX_HANDLES_PER_METAFILE 10

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
    struct PVFS_server_req_s req_p;	 /* server request */
    struct PVFS_server_resp_s *ack_p = NULL; /* server response */
    int ret = -1, i = 0;
    int max_msg_sz, name_sz;
    struct PINT_decoded_msg decoded;

    pinode *entry_pinode = NULL, *pinode_ptr = NULL;
    char *server = NULL, *segment = NULL, *path = NULL;
    bmi_addr_t serv_addr;
    int total_segments = 0, num_segments_remaining = 0;
    int start_path = 0, end_path = 0, path_len = 0;
    PVFS_handle final_handle = 0;
    PVFS_bitfield attr_mask;
    pinode_reference entry, parent;

    /*print args to make sure we're sane*/
    gossip_ldebug(CLIENT_DEBUG,"req->\n\tname: %s\n\tfs_id: %d\n\tcredentials:\n\t\tuid: %d\n\t\tgid: %d\n\t\tperms: %d\n",req->name,req->fs_id, req->credentials.uid, req->credentials.gid, req->credentials.perms);

    /* NOTE: special case is that we're doing a lookup on the root handle (which
     * we got during the getconfig) so we want to check to see if we're looking
     * up "/"; if so, then get the root handle from the bucket table interface
     * and return
     */
    parent.fs_id = req->fs_id;

    ret = PINT_bucket_get_root_handle(req->fs_id,&parent.handle);
    if (ret < 0)
    {
	return(ret);
    }

    if (!strcmp(req->name, "/"))
    {
	resp->pinode_refn.handle = parent.handle;
	resp->pinode_refn.fs_id = req->fs_id;
	return(0);
    }

    /* Get  the total number of segments */
    get_no_of_segments(req->name, &num_segments_remaining);
    total_segments = num_segments_remaining;

    /* make sure we're asking for something reasonable */
    if(num_segments_remaining < 1)
    {
	return(-EINVAL);
    }

    /* do dcache lookups here to shorten the path as much as possible */

    name_sz = strlen(req->name) + 1;
    path = (char *)malloc(name_sz);
    if (path == NULL)
    {
        ret = -ENOMEM;
        goto return_error;
    }
    memcpy(path, req->name, name_sz);

    /* traverse the path as much as we can via the dcache */

    /* send server messages here */
    while(num_segments_remaining > 0)
    {

	max_msg_sz = sizeof(struct PVFS_server_resp_s) + num_segments_remaining * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));
	gossip_ldebug(CLIENT_DEBUG,"max msg size = %d \n",max_msg_sz);
	name_sz = strlen(path) + 1;
	req_p.op     = PVFS_SERV_LOOKUP_PATH;
	req_p.rsize = sizeof(struct PVFS_server_req_s) + name_sz;
	req_p.credentials = req->credentials;

	/* update the pointer to the copy we already have */
	req_p.u.lookup_path.path = path;
	req_p.u.lookup_path.fs_id = parent.fs_id;
	req_p.u.lookup_path.starting_handle = parent.handle;
	req_p.u.lookup_path.attrmask = ATTR_BASIC;

        /* Get Metaserver in BMI URL format using the bucket table 
         * interface */
	ret = PINT_bucket_map_to_server(&serv_addr, parent.handle,
						 parent.fs_id);
	if (ret < 0)
	{
	    goto return_error;
	}

	ret = PINT_server_send_req(serv_addr, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
	    goto return_error;
	}
	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->u.lookup_path.count < 1)
	{
	    gossip_ldebug(CLIENT_DEBUG,"server returned 0 entries\n");
	    goto return_error;
	}

	num_segments_remaining -= ack_p->u.lookup_path.count;

	for(i = 0; i < ack_p->u.lookup_path.count; i++)
	{

	    entry.handle = ack_p->u.lookup_path.handle_array[i];
	    entry.fs_id = req->fs_id;

	    //segment = path_element(i);
	    ret = get_path_element(path, &segment, i);
	    if (ret < 0)
	    {
		goto return_error;
	    }

	    /* Add entry to dcache */
	    ret = PINT_dcache_insert(segment, entry, parent);
	    if (ret < 0)
	    {
		goto return_error;
	    }
	    /* Add to pinode cache */
	    ret = PINT_pcache_pinode_alloc(&pinode_ptr); 	
	    if (ret < 0)
	    {
		ret = -ENOMEM;
		goto return_error;
	    }
	    /* Fill the pinode */
	    pinode_ptr->pinode_ref.handle = entry.handle;
	    pinode_ptr->pinode_ref.fs_id = entry.fs_id;
	    pinode_ptr->mask = req_p.u.lookup_path.attrmask;

	    if ((i+1 == ack_p->u.lookup_path.count) && 
		((ack_p->rsize % sizeof(struct PVFS_server_resp_s)) == 0))
	    {
		/* the attributes on the last item may not be valid,  so if
		 * we're on the last path segment, and we didn't get an attr
		 * structure for set everything to zero.
		 */
		memset(&pinode_ptr->attr, 0, sizeof(PVFS_object_attr));
	    }
	    else
	    {
		pinode_ptr->attr = ack_p->u.lookup_path.attr_array[i];
	    }

	    /* Check permissions for path */
	    ret = check_perms(pinode_ptr->attr,req->credentials.perms,
				  req->credentials.uid, req->credentials.gid);
	    if (ret < 0)
	    {
		goto return_error;
	    }

	    /* Fill in the timestamps */
	    ret = phelper_fill_timestamps(pinode_ptr);
	    if (ret < 0)
	    {
		goto return_error;
	    }
					
	    /* Set the size timestamp - size was not fetched */
	    pinode_ptr->size_flag = SIZE_INVALID;

	    /* Add to the pinode list */
	    ret = PINT_pcache_insert(pinode_ptr);
	    if (ret < 0)
	    {
		goto return_error;
	    }
	}
	/* If it is the final object handle,save it!! */
	if (num_segments_remaining == 0)
	{
	    final_handle = pinode_ptr->pinode_ref.handle;
	}

	/*get rid of the old path*/
	if (path != NULL)
	    free(path);

	/* get the next chunk of the path to send */
	ret = get_next_path(req->name,&path,total_segments - num_segments_remaining);
	if (ret < 0)
	{
	    goto return_error;
	}

	/* Update the parent handle with the handle of last segment
	 * that has been looked up
	 */
	parent.handle = entry.handle;
    }

    resp->pinode_refn.handle = entry.handle;
    resp->pinode_refn.fs_id = entry.fs_id;

    return(0);
    
return_error:
    return(ret);

#if 0

    /* Get  the total number of segments */
    get_no_of_segments(req->name,&num_seg);

    /* make sure we're asking for something reasonable */
    if(num_seg < 1)
    {
	return(-EINVAL);
    }

    req_p = (struct PVFS_server_req_s *) malloc(sizeof(struct PVFS_server_req_s));
    if (req_p == NULL)
    {
        assert(0);
    }

    /* Get root handle using bucket table interface */
    ret = PINT_bucket_get_root_handle(req->fs_id,&parent_handle);
    if (ret < 0)
    {
	return(ret);
    }

    /* Get next segment */
    ret = get_next_segment(req->name,&segment,&start_seg);
    if (ret < 0)
    {
	assert(0);
	return(ret);
    }

    /* Get the full path */
    path_len = strlen(req->name) + 1; /* include null terminator */
    path = (char *)malloc(path_len);
    if (path == NULL)
    {
	ret = -ENOMEM;
	goto path_alloc_failure;
    }
    memcpy(path, req->name, path_len);

    /* Fill the parent pinode, parent handle is root handle */
    parent.handle = parent_handle;
    parent.fs_id = req->fs_id;

	/* Is any segment still to be looked up? */
    while(segment && ((end_path + 1) < strlen(req->name)))
    {
        gossip_ldebug(CLIENT_DEBUG,"looking up segment = %s\n", segment);
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
	    gossip_ldebug(CLIENT_DEBUG,"not in dcache\n");
	    /* Entry not in dcache */

	    /* send server request here */
	    /* TODO: IS THIS A REASONABLE MAXIMUM MESSAGE SIZE?  I HAVE NO IDEA */
	    /* 
	     * Q: what is the largest response for a lookup?
	     * Q: what's the largest number of handles any meta file have?
	     * (?? number of buckets in the system)
	     *
	     * total number of segments in the path * sizeof(PVFS_handle) + 
	     * total number of segments in the path * sizeof(PVFS_object_attr) +
	     * total number data files for each segment
	     *
	     * user/program/file1.dat
	     * 3 segments, user and program are metafiles, file1 is a data file
	     * largest response = 2 * (max # of handles for a metafile) + 
	     *
	     */

	    max_msg_sz = sizeof(struct PVFS_server_resp_s) + num_seg * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));
	    gossip_ldebug(CLIENT_DEBUG,"max msg size = %d \n",max_msg_sz);
	    name_sz = strlen(path) + 1;
	    req_p->op     = PVFS_SERV_LOOKUP_PATH;
	    req_p->rsize = sizeof(struct PVFS_server_req_s) + name_sz;
	    req_p->credentials = req->credentials;

	    /* Need to pass the arguments */
	    req_p->u.lookup_path.path = (char *)malloc(name_sz);
	    if (!req_p->u.lookup_path.path)
	    {
		ret = -ENOMEM;
		goto dcache_lookup_failure;
	    }
	    memcpy(req_p->u.lookup_path.path, path, name_sz);
	    req_p->u.lookup_path.fs_id = req->fs_id;
	    req_p->u.lookup_path.starting_handle = parent_handle;
	    req_p->u.lookup_path.attrmask = ATTR_BASIC;

		/* Get Metaserver in BMI URL format using the bucket table 
		 * interface */
	    ret = PINT_bucket_map_to_server(&serv_addr, parent.handle,
						 parent.fs_id);
	    if (ret < 0)
	    {
		goto map_to_server_failure;
	    }
	    /* Free the server */
	    /*free(server);
	    server = NULL;*/

	/* Make a lookup_path server request to get the handle and
	 * attributes of segment
	 */

	    ret = PINT_server_send_req(serv_addr, req_p, max_msg_sz, &decoded);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }
	    ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	    /* Repeat for number of handles returned */
	    for(i = 0; i < ack_p->u.lookup_path.count; i++)
	    {

		/* TODO: rework [p|d]cache here, we need to lookup the handle
		 * that was provided in the lookup response, if the pinode
		 * exists, update it, otherwise, alloc a new one and add it
		 * to the cache.  we obviously don't have anything in the dcache
		 * if we're in this part of the code, just alloc a new one of
		 * those.
		 */
				/* Fill up pinode reference for entry */
		entry.handle = ack_p->u.lookup_path.handle_array[i];
		entry.fs_id = req->fs_id;

				/* Add entry to dcache */
		ret = PINT_dcache_insert(segment, entry, parent);
		if (ret < 0)
		{
		    goto lookup_path_failure;
		}
				/* Add to pinode cache */
		ret = PINT_pcache_pinode_alloc(&pinode_ptr); 	
		if (ret < 0)
		{
		    ret = -ENOMEM;
		    goto lookup_path_failure ;
		}
		/* Fill the pinode */
		pinode_ptr->pinode_ref.handle = entry.handle;
		pinode_ptr->pinode_ref.fs_id = entry.fs_id;
		pinode_ptr->attr = ack_p->u.lookup_path.attr_array[i];
		pinode_ptr->mask = req_p->u.lookup_path.attrmask;

		/* Check permissions for path */
		ret = check_perms(pinode_ptr->attr,req->credentials.perms,
				  req->credentials.uid, req->credentials.gid);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}

		/* Fill in the timestamps */
		ret = phelper_fill_timestamps(pinode_ptr);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}
					
		/* Set the size timestamp - size was not fetched */
		pinode_ptr->size_flag = SIZE_INVALID;

		/* Add to the pinode list */
		ret = PINT_pcache_insert(pinode_ptr);
		if (ret < 0)
		{
		    goto check_perms_failure;
		}
				
		/* If it is the final object handle,save it!! */
		num_seg--;
		if (num_seg == 0)
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
	    gossip_ldebug(CLIENT_DEBUG,"GET_NEXT_PATH: %s %d %d %d",path, ack_p->u.lookup_path.count,start_path, end_path);
	    ret = get_next_path(path,ack_p->u.lookup_path.count,&start_path,
				&end_path);
	    if (ret < 0)
	    {
		goto check_perms_failure;
	    }

	    /* Free request,ack jobs */
	    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	    free(req_p->u.lookup_path.path);
	    free(req_p);

	}
	else
	{
	    gossip_ldebug(CLIENT_DEBUG,"dcache hit\n");
	    /* A Dcache hit! */
	    /* Get pinode from pinode cache */
	    /* If no pinode exists no error, will fetch 
	     * attributes of segment and add a new pinode
	     */
	    vflags = 0;
	    attr_mask = ATTR_BASIC;
	    /* Get the pinode from the cache */
	    ret = phelper_get_pinode(entry,&entry_pinode,
				     attr_mask,req->credentials);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }

	    /* Check permissions for path */
	    ret = check_perms(entry_pinode->attr,req->credentials.perms,
			      req->credentials.uid,req->credentials.gid);
	    if (ret < 0)
	    {
		goto lookup_path_failure;
	    }

	    /* advance one segment in the path */
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

    }/* end of while */

    /* Fill response structure */
    resp->pinode_refn.handle = final_handle; 

    return(0);
 
check_perms_failure:
printf("check_perms_failure\n");
    /* Free pinode allocated */
    PINT_pcache_pinode_dealloc(pinode_ptr);

lookup_path_failure:
printf("lookup_path_failure\n");
    /* Free the recently allocated pinode */
    if (entry_pinode != NULL)
	free(entry_pinode);
    if (ack_p != NULL)
	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

map_to_server_failure:
printf("map_to_server_failure\n");
    if(server != NULL)
	free(server);
    /* Free memory allocated for name */
    if (req_p->u.lookup_path.path != NULL)
	free(req_p->u.lookup_path.path);

dcache_lookup_failure:
printf("dcache_lookup_failure\n");
    /* Free the path string */
    if (path != NULL)
	free(path);
    if (req_p != NULL)
    {
	free(req_p);
    }

path_alloc_failure:
printf("path_alloc_failure\n");
    if (segment != NULL)
	free(segment);

    return(ret);
#endif
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

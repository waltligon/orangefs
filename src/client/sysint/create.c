/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Create Function Implementation */

#include <assert.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "pcache.h"
#include "PINT-reqproto-encode.h"
#include "pvfs-distribution.h"

#define REQ_ENC_FORMAT 0

static void copy_attributes(PVFS_object_attr *new,PVFS_object_attr old,
	int handle_count, PVFS_handle *handle_array);

/* PVFS_sys_create()
 *
 * create a PVFS file with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_create(PVFS_sysreq_create *req, PVFS_sysresp_create *resp)
{
	struct PVFS_server_req_s req_p;			/* server request */
	struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
	int ret = -1, name_sz = 0, io_serv_count = 0, i = 0;
	int attr_mask, last_handle_created = 0;
	pinode *parent_ptr = NULL, *pinode_ptr = NULL;
	bmi_addr_t serv_addr1,serv_addr2,*bmi_addr_list = NULL;
	PVFS_handle *df_handle_array = NULL,new_bkt = 0,handle_mask = 0;
	pinode_reference entry;
	struct PINT_decoded_msg decoded;
	bmi_size_t max_msg_sz;

	enum {
	    NONE_FAILURE = 0,
	    PCACHE_LOOKUP_FAILURE,
	    DCACHE_LOOKUP_FAILURE,
	    DCACHE_INSERT_FAILURE,
	    LOOKUP_SERVER_FAILURE,
	    CREATE_MSG_FAILURE,
	    CRDIRENT_MSG_FAILURE,
	    PREIO1_CREATE_FAILURE,
	    PREIO2_CREATE_FAILURE,
	    PREIO3_CREATE_FAILURE,
	    IO_REQ_FAILURE,
	    SETATTR_FAILURE,
	    PCACHE_INSERT1_FAILURE,
	    PCACHE_INSERT2_FAILURE,
	} failure = NONE_FAILURE;

	gossip_ldebug(CLIENT_DEBUG,"creating file named %s\n", req->entry_name);
	gossip_ldebug(CLIENT_DEBUG,"parent handle = %lld\n", req->parent_refn.handle);
	gossip_ldebug(CLIENT_DEBUG,"parent fsid = %d\n", req->parent_refn.fs_id);

        /* get the pinode of the parent so we can check permissions */
        attr_mask = ATTR_BASIC | ATTR_META;
        ret = phelper_get_pinode(req->parent_refn, &parent_ptr, attr_mask, 
				    req->credentials);
        if(ret < 0)
        {
	    /* parent pinode doesn't exist ?!? */
	    gossip_ldebug(CLIENT_DEBUG,"unable to get pinode for parent\n");
	    failure = PCACHE_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* check permissions in parent directory */
	ret = check_perms(parent_ptr->attr,req->credentials.perms,
			    req->credentials.uid, req->credentials.gid);
	if (ret < 0)
	{
	    ret = (-EPERM);
	    gossip_ldebug(CLIENT_DEBUG,"--===PERMISSIONS===--\n");
	    failure = PCACHE_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* Lookup handle(if it exists) in dcache */
	ret = PINT_dcache_lookup(req->entry_name,req->parent_refn,&entry);
	if (ret < 0 )
	{
	    /* there was an error, bail*/
	    failure = DCACHE_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* the entry could still exist, it may be uncached though */
	if (entry.handle != PINT_DCACHE_HANDLE_INVALID)
	{
	    /* pinode already exists, should fail create with EXISTS*/
	    ret = (-EEXIST);
	    failure = DCACHE_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* how many data files do we need to create? */
	io_serv_count = req->attr.u.meta.nr_datafiles;

	/* if the user passed in -1, we're going to assume that's the default
	 * and create one datafile per server */
	if (io_serv_count == -1)
	{
	    PINT_bucket_get_num_io( req->parent_refn.fs_id, &io_serv_count);
	}

	gossip_ldebug(CLIENT_DEBUG,"number of data files to create = %d\n",io_serv_count);

	/* if the user passed in a NULL pointer for the distribution, we
	 * need to get the default distribution for them
	 */

	if (req->attr.u.meta.dist == NULL)
	{
	    req->attr.u.meta.dist = PVFS_Dist_create("default_dist");
	    //PINT_Dist_dump(req->attr.u.meta.dist);
	}

	/* Determine the initial metaserver for new file */
	ret = PINT_bucket_get_next_meta(req->parent_refn.fs_id,&serv_addr1,
					&new_bkt,&handle_mask);	
	if (ret < 0)
	{
	    failure = LOOKUP_SERVER_FAILURE;
	    goto return_error;
	}	
	
	/* send the create request for the meta file */
	req_p.op = PVFS_SERV_CREATE;
	req_p.rsize = sizeof(struct PVFS_server_req_s);
	req_p.credentials = req->credentials;
	req_p.u.create.bucket = new_bkt;
	req_p.u.create.handle_mask = handle_mask;
	req_p.u.create.fs_id = req->parent_refn.fs_id;

	/* Q: is this sane?  pretty sure we're creating meta files here, but do 
	 * we want to re-use this for other object types? symlinks, dirs, etc?*/

	req_p.u.create.object_type = ATTR_META;

	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* send the server request */
	ret = PINT_server_send_req(serv_addr1, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
	    failure = LOOKUP_SERVER_FAILURE;
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;
	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = CREATE_MSG_FAILURE;
	    goto return_error;
        }

	/* save the handle for the meta file so we can refer to it later */
	entry.handle = ack_p->u.create.handle;
	entry.fs_id = req->parent_refn.fs_id;

	/* these fields are the only thing we need to set for the response to
	 * the calling function
	 */

	resp->pinode_refn.handle = entry.handle;
	resp->pinode_refn.fs_id = req->parent_refn.fs_id;

	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	/* Create directory entry server request to parent */
	/* Query BTI to get initial meta server */
	ret = PINT_bucket_map_to_server(&serv_addr2,req->parent_refn.handle,
			req->parent_refn.fs_id);
	if (ret < 0)
	{
	    failure = CREATE_MSG_FAILURE;
	    goto return_error;
	}

	/* send crdirent to associate a name with the meta file we just made */

	name_sz = strlen(req->entry_name) + 1; /*include null terminator*/
	req_p.op = PVFS_SERV_CREATEDIRENT;
	req_p.rsize = sizeof(struct PVFS_server_req_s) + name_sz;

	/* credentials come from req->credentials and are set in the previous
	 * create request.  so we don't have to set those again.
	 */

	/* just update the pointer, it'll get malloc'ed when its sent on the 
	 * wire.
	 */
	req_p.u.crdirent.name = req->entry_name;
	req_p.u.crdirent.new_handle = entry.handle;
	req_p.u.crdirent.parent_handle = req->parent_refn.handle;
	req_p.u.crdirent.fs_id = req->parent_refn.fs_id;

	/* max response size is the same as the previous request */

	/* Make server request */
	ret = PINT_server_send_req(serv_addr2, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
	    failure = CREATE_MSG_FAILURE;
	    goto return_error;
	}

	if (ack_p->status < 0 )
        {
	    /* this could fail for many reasons, EEXISTS will probbably be the 
	     * most common.
	     */

            ret = ack_p->status;
            failure = CRDIRENT_MSG_FAILURE;
            goto return_error;
        }

	/* we need one BMI address for each data file */
	bmi_addr_list = (bmi_addr_t *)malloc(sizeof(bmi_addr_t)*io_serv_count);
	if (bmi_addr_list == NULL)
	{
		ret = (-ENOMEM);
		failure = PREIO1_CREATE_FAILURE;
		goto return_error;
	}

	/* I'm using df_handle_array to store the new bucket # before I 
	 * send the create request and then the newly created handle when I
	 * get that back from the server.
	 *
	 * this may be confusing:  since we don't care about the bucket after
	 * sending the create request, I'm reusing this space to store the new
	 * handle that we get back from the server. its cheaper since I only
	 * malloc once.  Both items are stored as PVFS_handle types.
	 * 
	 */

	df_handle_array = (PVFS_handle*)malloc(io_serv_count*sizeof(PVFS_handle));
	if (df_handle_array == NULL)
	{
	    failure = PREIO2_CREATE_FAILURE;
	    goto return_error;
	}
	
	ret = PINT_bucket_get_next_io(req->parent_refn.fs_id, io_serv_count,
			bmi_addr_list, df_handle_array, &handle_mask);
	if (ret < 0)
	{
	    failure = PREIO3_CREATE_FAILURE;
	    goto return_error;
	}

	/* send create requests to each I/O server for the data files */

	/* TODO: right now this is serialized, we should come back later and 
	 * make this asynchronous or something.
	 */

	/* these fields are the same for each server message so we only need to
	 * set them once.
	 */

	req_p.op = PVFS_SERV_CREATE;
	req_p.rsize = sizeof(struct PVFS_server_req_s);
	req_p.credentials = req->credentials;
	req_p.u.create.handle_mask = handle_mask;
	req_p.u.create.fs_id = req->parent_refn.fs_id;
	/* we're making data files on each server */
	req_p.u.create.object_type = ATTR_DATA;

	/* create requests get a generic response */
	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* NOTE: if we need to rollback data file creation, the first valid
	 * handle to remove would be i - 1 (as long as i < 0).
	 */

	for(i = 0;i < io_serv_count; i++)
	{
		/* Fill in the parameters */
		req_p.u.create.bucket = df_handle_array[i];

		/* Server request */
		ret = PINT_server_send_req(bmi_addr_list[i],&req_p,max_msg_sz,
					    &decoded);
		if (ret < 0)
		{
		    /* if we fail then we assume no data file has been created
		     * on the server
		     */
		    failure = IO_REQ_FAILURE;
		    goto return_error;
		}

		ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

		/* make sure the operation didn't fail*/
		if (ack_p->status < 0 )
		{
		    ret = ack_p->status;
		    failure = IO_REQ_FAILURE;
		    goto return_error;
		}

		/* store the new handle here */
		df_handle_array[i] = ack_p->u.create.handle;

		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	}

	/* store all the handles to the files we've created in the metafile
	 * on the server. (via setattr).
	 */
	req_p.op = PVFS_SERV_SETATTR;
	req_p.rsize = sizeof(struct PVFS_server_req_s) 
			+ io_serv_count*sizeof(PVFS_handle);
	req_p.u.setattr.handle = entry.handle;
	req_p.u.setattr.fs_id = req->parent_refn.fs_id;
	req_p.u.setattr.attrmask = req->attrmask;

	/* TODO: figure out how we're storing the distribution for the file
	 * does this go in the attributes, or the eattr?
	 */

	/* even though this says copy, we're just updating the pointer for the
	 * array of data files
	 */
	copy_attributes(&req_p.u.setattr.attr, req->attr, io_serv_count,
			df_handle_array);

gossip_ldebug(CLIENT_DEBUG,"\towner: %d\n\tgroup: %d\n\tperms: %d\n\tatime: %ld\n\tmtime: %ld\n\tctime: %ld\n\tobjtype: %d\n",req_p.u.setattr.attr.owner, req_p.u.setattr.attr.group, req_p.u.setattr.attr.perms, req_p.u.setattr.attr.atime, req_p.u.setattr.attr.mtime, req_p.u.setattr.attr.ctime, req_p.u.setattr.attr.objtype);
gossip_ldebug(CLIENT_DEBUG,"\t\tnr_datafiles: %d\n",req_p.u.setattr.attr.u.meta.nr_datafiles);
    for(i=0;i<req_p.u.setattr.attr.u.meta.nr_datafiles;i++)
	gossip_ldebug(CLIENT_DEBUG,"\t\tdatafile handle: %lld\n",req_p.u.setattr.attr.u.meta.dfh[i]);

	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* send the setattr request to the meta server */
	ret = PINT_server_send_req(serv_addr1, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
	    failure = SETATTR_FAILURE;
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	/* make sure the operation didn't fail*/
	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = SETATTR_FAILURE;
	    goto return_error;
	}

	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	/* don't need to hold on to the io server addresses anymore */
	free(bmi_addr_list);

	/* add entry to dcache */
	ret = PINT_dcache_insert(req->entry_name, entry, req->parent_refn);
	if (ret < 0)
	{
	    failure = DCACHE_INSERT_FAILURE;
	    goto return_error;
	}

	/* Allocate the pinode */
	ret = PINT_pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
	    failure = PCACHE_INSERT1_FAILURE;
	    goto return_error;
	}
	/* Fill up the pinode */
	pinode_ptr->pinode_ref.handle = entry.handle;
	pinode_ptr->pinode_ref.fs_id = req->parent_refn.fs_id;
	pinode_ptr->mask = req->attrmask;
	/* Allocate the handle array */
	pinode_ptr->attr = req_p.u.setattr.attr;
	/* Fill in the timestamps */

	ret = phelper_fill_timestamps(pinode_ptr);
	if (ret < 0)
	{
	    failure = PCACHE_INSERT2_FAILURE;
	    goto return_error;
	}
	/* Add pinode to the cache */
	ret = PINT_pcache_insert(pinode_ptr);
	if (ret < 0)
	{
	    failure = PCACHE_INSERT2_FAILURE;
	    goto return_error;
	}	

  	return(0); 

return_error:
	switch(failure)
	{
	    case PCACHE_INSERT2_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"PCACHE_INSERT2_FAILURE\n");
		PINT_pcache_pinode_dealloc(pinode_ptr);
	    case PCACHE_INSERT1_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"PCACHE_INSERT1_FAILURE\n");
	    case DCACHE_INSERT_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"DCACHE_INSERT_FAILURE\n");
		ret = 0;
		break;
	    case SETATTR_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"SETATTR_FAILURE\n");
	    case IO_REQ_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"IO_REQ_FAILURE\n");
		/* rollback each of the data files we created */
		last_handle_created = i;
		req_p.op = PVFS_SERV_REMOVE;
		req_p.rsize = sizeof(struct PVFS_server_req_s);
		req_p.credentials = req->credentials;
		req_p.u.remove.fs_id = req->parent_refn.fs_id;
		max_msg_sz = sizeof(struct PVFS_server_resp_s);

		for(i = 0;i < last_handle_created; i++)
		{
		    /*best effort rollback*/
		    req_p.u.remove.handle = df_handle_array[i];
		    ret = PINT_server_send_req(bmi_addr_list[i], &req_p, 
						max_msg_sz, &decoded);

		    PINT_decode_release(&decoded, PINT_DECODE_RESP,
					REQ_ENC_FORMAT);
		}

	    case PREIO3_CREATE_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"PREIO3_CREATE_FAILURE\n");
		free(df_handle_array);
	    case PREIO2_CREATE_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"PREIO2_CREATE_FAILURE\n");
		free(bmi_addr_list);
	    case PREIO1_CREATE_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"PREIO1_CREATE_FAILURE\n");
		/* rollback crdirent */
		req_p.op = PVFS_SERV_RMDIRENT;
		req_p.rsize = sizeof(struct PVFS_server_req_s) 
			+ strlen(req->entry_name)+1; /*include null terminator*/

		req_p.credentials = req->credentials;
		req_p.u.rmdirent.parent_handle = req->parent_refn.handle;
		req_p.u.rmdirent.fs_id = req->parent_refn.fs_id;
		req_p.u.rmdirent.entry = req->entry_name;
		max_msg_sz = sizeof(struct PVFS_server_resp_s);
		ret = PINT_server_send_req(serv_addr2, &req_p, max_msg_sz, 
					    &decoded);

		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	    case CRDIRENT_MSG_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"CRDIRENT_MSG_FAILURE\n");
		/* rollback create req*/
		req_p.op = PVFS_SERV_REMOVE;
		req_p.rsize = sizeof(struct PVFS_server_req_s);
		req_p.credentials = req->credentials;
		req_p.u.remove.handle = entry.handle;
		req_p.u.remove.fs_id = entry.fs_id;
		max_msg_sz = sizeof(struct PVFS_server_resp_s);
		ret = PINT_server_send_req(serv_addr1, &req_p, max_msg_sz, 
					    &decoded);

		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	    case CREATE_MSG_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"CREATE_MSG_FAILURE\n");
		if (decoded.buffer != NULL)
		    PINT_decode_release(&decoded, PINT_DECODE_RESP, 
					    REQ_ENC_FORMAT);

	    case LOOKUP_SERVER_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"LOOKUP_SERVER_FAILURE\n");
	    case PCACHE_LOOKUP_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"PCACHE_LOOKUP_FAILURE\n");
	    case DCACHE_LOOKUP_FAILURE:
		gossip_ldebug(CLIENT_DEBUG,"DCACHE_LOOKUP_FAILURE\n");
	    case NONE_FAILURE:

	    /* TODO: do we want to setup a #define for these invalid handle/fsid
	     * values? */
	    resp->pinode_refn.handle = -1;
	    resp->pinode_refn.fs_id = -1;
	}
	return (ret);
}

/* copy_attributes
 *
 * copies the attributes from an attribute to another attribute 
 * structure
 *
 */
static void copy_attributes(PVFS_object_attr *new,PVFS_object_attr old,
	int handle_count, PVFS_handle *handle_array)
{
	/* Copy the generic attributes */	

	new->owner = old.owner;
	new->group = old.group;
	new->perms = old.perms;
	new->objtype = ATTR_META;

	/* Fill in the metafile attributes */
	new->u.meta.dist = old.u.meta.dist;
	new->u.meta.dfh = handle_array;
	new->u.meta.nr_datafiles = handle_count;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

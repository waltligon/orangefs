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

#define REQ_ENC_FORMAT 0

static void copy_attributes(PVFS_object_attr new,PVFS_object_attr old,
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
	int ret = -1, ret1 = -1, cflags = 0, name_sz = 0, mask = 0;
	int item = 0;
	PVFS_size handle_size = 0;
	pinode *parent_ptr = NULL, *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr1,serv_addr2,*bmi_addr_list = NULL;
	char *server = NULL,**io_serv_list = NULL,*segment = NULL;
	PVFS_handle *df_handle_array = NULL,new_bkt = 0,handle_mask = 0,
		*bkt_array = NULL;
	pinode_reference entry;
	int io_serv_cnt = 0, i = 0, j = 0;
	struct PINT_decoded_msg decoded;
	bmi_size_t max_msg_sz;
	int attr_mask, start = 0, failed_after_send = 0;

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
	    IO_SEND_FAILURE,
	    IO_REQ_FAILURE,
	    SETATTR_SEND_FAILURE,
	    SETATTR_RECV_FAILURE,
	    PCACHE_INSERT1_FAILURE,
	    PCACHE_INSERT2_FAILURE,
	    PCACHE_INSERT3_FAILURE,
	} failure = NONE_FAILURE;

	attr_mask = ATTR_BASIC | ATTR_META;

	ret = phelper_get_pinode(req->parent_refn, &parent_ptr, attr_mask, req->credentials);
	{
	    /* parent pinode doesn't exist ?!? */
	    assert(0);
	}

	/* check permissions in parent directory */

	ret = check_perms(parent_ptr->attr,req->credentials.perms,
			    req->credentials.uid, req->credentials.gid);
	if (ret < 0)
	{
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

	if (entry.handle != PINT_DCACHE_HANDLE_INVALID)
	{
	    /* pinode already exists, should fail create with EXISTS*/
	    ret = (-EEXIST);
	    failure = DCACHE_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* Determine the initial metaserver for new file */
	ret = PINT_bucket_get_next_meta(req->parent_refn.fs_id,&serv_addr1,
			&new_bkt,&handle_mask);	
	if (ret < 0)
	{
	    failure = LOOKUP_SERVER_FAILURE;
	    goto return_error;
	}	
	
	/* Fill in the parameters */
	/* TODO: Fill in bucket ID */
	req_p.op = PVFS_SERV_CREATE;
	req_p.rsize = sizeof(struct PVFS_server_req_s);
	req_p.credentials = req->credentials;
	req_p.u.create.bucket = new_bkt;
	req_p.u.create.handle_mask = handle_mask;
	req_p.u.create.fs_id = req->parent_refn.fs_id;

	/* Q: is this sane?  pretty sure we're creating meta files here, but do 
	 * we wanna re-use this for other object types?  symlinks, dirs, etc?*/

	req_p.u.create.object_type = ATTR_META;

	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* Server request */
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

	/* New entry pinode reference */
	entry.handle = ack_p->u.create.handle;

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

	/* Fill in the parameters */

	name_sz = strlen(req->entry_name) + 1; /*include null terminator*/
	req_p.op = PVFS_SERV_CREATEDIRENT;
	req_p.rsize = sizeof(struct PVFS_server_req_s) + name_sz;
	/* credentials come from req->credentials and are set in the previous
	 * create request.
	 */

	/* just update the pointer, it'll get malloc'ed when its sent on the 
	 * wire 
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
	    /* Error - EXISTS, file present */
            ret = ack_p->status;
            failure = CRDIRENT_MSG_FAILURE;
            goto return_error;
        }

	/* Get I/O server list and bucket list */
	io_serv_cnt = req->attr.u.meta.nr_datafiles;

	/* Allocate BMI address array */
	bmi_addr_list = (bmi_addr_t *)malloc(sizeof(bmi_addr_t) * io_serv_cnt);
	if (bmi_addr_list == NULL)
	{
		ret = (-ENOMEM);
		failure = PREIO1_CREATE_FAILURE;
		goto return_error;
	}

	/* I'm using the df_handle array to store the new bucket # before I 
	 * send the create request and then the newly created handle.  when I
	 * get that back from the server.
	 *
	 * this may be confusing:  since we don't care about the bucket after
	 * sending the create request, I'm reusing this space to store the new
	 * handle that we get back from the server. its cheaper since I only
	 * malloc once.
	 * 
	 */

	df_handle_array = (PVFS_handle *)malloc(io_serv_cnt * sizeof(PVFS_handle));
	if (df_handle_array == NULL)
	{
	    failure = PREIO2_CREATE_FAILURE;
	    goto return_error;
	}
	
	ret = PINT_bucket_get_next_io(req->parent_refn.fs_id, io_serv_cnt,
			bmi_addr_list, df_handle_array, &handle_mask);
	if (ret < 0)
	{
	    failure = PREIO3_CREATE_FAILURE;
	    goto return_error;
	}

	/* Make create requests to each I/O server */
	/* right now this is serialized, we should come back later and make this
	 * asynchronous or something
	 */

	/* these fields are the same for each server message so we only need to
	 * set them once
	 */

	req_p.op = PVFS_SERV_CREATE;
	req_p.rsize = sizeof(struct PVFS_server_req_s);
	req_p.credentials = req->credentials;
	req_p.u.create.handle_mask = handle_mask;
	req_p.u.create.fs_id = req->parent_refn.fs_id;
	/* we're making data files on each server */
	req_p.u.create.object_type = ATTR_DATA;

	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	for(i = 0;i < io_serv_cnt; i++)
	{
		/* Fill in the parameters */
		req_p.u.create.bucket = df_handle_array[i];

		/* Server request */
		ret = PINT_server_send_req(bmi_addr_list[i], &req_p, max_msg_sz, &decoded);
		if (ret < 0)
		{
		    failure = IO_SEND_FAILURE;
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

	/* Aggregate all handles and make a setattr request */
	req_p.op = PVFS_SERV_SETATTR;
	req_p.rsize = sizeof(struct PVFS_server_req_s) + io_serv_cnt * sizeof(PVFS_handle);
	req_p.u.setattr.handle = entry.handle;
	req_p.u.setattr.fs_id = req->parent_refn.fs_id;
	req_p.u.setattr.attrmask = req->attrmask;
	/* Copy the attribute structure */
	copy_attributes(req_p.u.setattr.attr,req->attr,io_serv_cnt,df_handle_array);

	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* Make a setattr server request */
	ret = PINT_server_send_req(serv_addr1, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
		failure = SETATTR_SEND_FAILURE;
		goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	/* make sure the operation didn't fail*/
	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = SETATTR_RECV_FAILURE;
	    goto return_error;
	}


	/* Add entry to dcache */
	ret = PINT_dcache_insert(req->entry_name,entry,req->parent_refn);
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
	pinode_ptr->attr.u.meta.dfh = (PVFS_handle *)malloc(io_serv_cnt
			* sizeof(PVFS_handle));
	if (!(pinode_ptr->attr.u.meta.dfh))
	{
	    failure = PCACHE_INSERT2_FAILURE;
	    goto return_error;
	}
	memcpy(pinode_ptr->attr.u.meta.dfh,df_handle_array, io_serv_cnt * sizeof(PVFS_handle));
	/* Fill in the timestamps */
	ret = phelper_fill_timestamps(pinode_ptr);
	if (ret <0)
	{
	    failure = PCACHE_INSERT3_FAILURE;
	    goto return_error;
	}
	/* Add pinode to the cache */
	ret = PINT_pcache_insert(pinode_ptr);
	if (ret < 0)
	{
	    failure = PCACHE_INSERT3_FAILURE;
	    goto return_error;
	}	

  	return(0); 

return_error:
	switch(failure)
	{
	    case PCACHE_INSERT3_FAILURE:
		free(pinode_ptr->attr.u.meta.dfh);
	    case PCACHE_INSERT2_FAILURE:
		PINT_pcache_pinode_dealloc(pinode_ptr);
	    case PCACHE_INSERT1_FAILURE:
	    case DCACHE_INSERT_FAILURE:
		ret = 1;
		break;
	    case SETATTR_RECV_FAILURE:
	    case SETATTR_SEND_FAILURE:
	    case IO_REQ_FAILURE:
		failed_after_send = 1;
	    case IO_SEND_FAILURE:
		/* rollback each of the data files we created */
		start = i;
		if (failed_after_send != 1)
		    start--;
		req_p.op = PVFS_SERV_REMOVE;
		req_p.rsize = sizeof(struct PVFS_server_req_s);
		req_p.credentials = req->credentials;
		req_p.u.remove.fs_id = req->parent_refn.fs_id;
		max_msg_sz = sizeof(struct PVFS_server_resp_s);

		for(i = 0;i < start; i++)
		{
		    /*best effort rollback*/
		    req_p.u.remove.handle = df_handle_array[i];
		    ret = PINT_server_send_req(bmi_addr_list[i], &req_p, max_msg_sz, &decoded);
		    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
		}

	    case PREIO3_CREATE_FAILURE:
		free(df_handle_array);
	    case PREIO2_CREATE_FAILURE:
		free(bmi_addr_list);
	    case PREIO1_CREATE_FAILURE:
		/* rollback crdirent */
		req_p.op = PVFS_SERV_RMDIRENT;
		req_p.rsize = sizeof(struct PVFS_server_req_s) + strlen(req->entry_name) + 1; /* include null terminator */
		req_p.credentials = req->credentials;
		req_p.u.rmdirent.parent_handle = req->parent_refn.handle;
		req_p.u.rmdirent.fs_id = req->parent_refn.fs_id;
		req_p.u.rmdirent.entry = req->entry_name;
		max_msg_sz = sizeof(struct PVFS_server_resp_s);
		ret = PINT_server_send_req(serv_addr2, &req_p, max_msg_sz, &decoded);
		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	    case CRDIRENT_MSG_FAILURE:
		/* rollback create req*/
		req_p.op = PVFS_SERV_REMOVE;
		req_p.rsize = sizeof(struct PVFS_server_req_s);
		req_p.credentials = req->credentials;
		max_msg_sz = sizeof(struct PVFS_server_resp_s);
		ret = PINT_server_send_req(serv_addr1, &req_p, max_msg_sz, &decoded);
		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	    case CREATE_MSG_FAILURE:
		if (decoded.buffer != NULL)
		    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	    case LOOKUP_SERVER_FAILURE:
	    case PCACHE_LOOKUP_FAILURE:
	    case DCACHE_LOOKUP_FAILURE:
	    case NONE_FAILURE:
	}
	return (ret);
}

/* copy_attributes
 *
 * copies the attributes from an attribute to another attribute 
 * structure
 *
 */
static void copy_attributes(PVFS_object_attr new,PVFS_object_attr old,
	int handle_count, PVFS_handle *handle_array)
{
	/* Copy the generic attributes */	
	new.owner = old.owner;
	new.group = old.group;
	new.perms = old.perms;
	new.objtype = old.objtype;

	/* Fill in the metafile attributes */
	/* TODO: Is this going to work? */
#if 0
	/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
	new.u.meta.dist = old.u.meta.dist;
#endif
	new.u.meta.dfh = handle_array;
	new.u.meta.nr_datafiles = handle_count;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

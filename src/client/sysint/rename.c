/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Rename Function Implementation */

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


#if 0
static int get_bmi_address(bmi_addr_t *io_addr_array, int32_count num_io,\
		PVFS_handle *handle_array);
static int do_crdirent(char *name,PVFS_handle parent,PVFS_fs_id fsid,\
		PVFS_handle entry_handle,bmi_addr_t addr);

extern pcache pvfs_pcache; 
#endif

/* PVFS_sys_rename()
 *
 * Rename a file. the plan:
 *	- lookup the filename, get handle for meta file
 *	- get pinodes for the old/new parents
 *	- permissions check both
 *	- send crdirent msg to new parent
 *	- send rmdirent msg to old parent
 *	- in case of failure of crdirent, exit
 *	- in case of failure of rmdirent to old parent, rmdirent the new dirent
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_rename(PVFS_sysreq_rename *req)
{
    struct PVFS_server_req_s req_p;		/* server request */
    struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
    int ret = -1;
    pinode *new_parent_p = NULL, *old_entry_p = NULL;
    bmi_addr_t serv_addr;	/* PVFS address type structure */
    int name_sz = 0;
    PVFS_bitfield attr_mask;
    pinode_reference old_entry;
    struct PINT_decoded_msg decoded;
    bmi_size_t max_msg_sz;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag;

    attr_mask = ATTR_BASIC | ATTR_META;

    ret = PINT_do_lookup(req->old_entry, req->old_parent_refn, attr_mask,
			    req->credentials, &old_entry);
    if (ret < 0)
    {
	goto return_error;
    }

    /* get the pinode for the thing we're renaming */
    ret = phelper_get_pinode(old_entry, &old_entry_p, attr_mask, req->credentials);

    if (ret < 0)
    {
	goto return_error;
    }

    /* are we allowed to delete this file? */
    ret = check_perms(old_entry_p->attr, req->credentials.perms,
			    req->credentials.uid, req->credentials.gid);
    if (ret < 0)
    {
	ret = (-EPERM);
	goto return_error;
    }

    /* make sure the new parent exists */

    ret = phelper_get_pinode(req->new_parent_refn, &new_parent_p, 
				attr_mask, req->credentials);
    if(ret < 0)
    {
	/* parent pinode doesn't exist ?!? */
	gossip_ldebug(CLIENT_DEBUG,"unable to get pinode for parent\n");
	goto return_error;
    }

    /* check permissions in parent directory */
    ret = check_perms(new_parent_p->attr, req->credentials.perms,
				req->credentials.uid, req->credentials.gid);
    if (ret < 0)
    {
	ret = (-EPERM);
	gossip_ldebug(CLIENT_DEBUG,"error checking permissions for new parent\n");
	goto return_error;
    }

    ret = PINT_bucket_map_to_server(&serv_addr,req->new_parent_refn.handle,
					req->new_parent_refn.fs_id);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"unable to map a server to the new parent via the bucket table interface\n");
	goto return_error;
    }

    name_sz = strlen(req->new_entry) + 1; /*include null terminator*/
    req_p.op = PVFS_SERV_CREATEDIRENT;
    req_p.rsize = sizeof(struct PVFS_server_req_s) + name_sz;
    req_p.credentials = req->credentials;

    req_p.u.crdirent.name = req->new_entry;
    req_p.u.crdirent.new_handle = old_entry.handle;
    req_p.u.crdirent.parent_handle = req->new_parent_refn.handle;
    req_p.u.crdirent.fs_id = req->new_parent_refn.fs_id;

    /* create requests get a generic response */
    max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);

    op_tag = get_next_session_tag();

    /* Make server request */
    ret = PINT_send_req(serv_addr, &req_p, max_msg_sz, &decoded, &encoded_resp,
			op_tag);
    if (ret < 0)
    {
	goto return_error;
    }

    ack_p = (struct PVFS_server_resp_s *) decoded.buffer;
    if (ack_p->status < 0 )
    {
	/* this could fail for many reasons, EEXISTS will probbably be the
	 * most common.
	 */
	ret = ack_p->status;
	goto return_error;
    }

    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded, &encoded_resp, 
			    op_tag);

    /* now we have 2 dirents pointing to one meta file, we need to rmdirent the 
     * old one
     */

    ret = PINT_bucket_map_to_server(&serv_addr,req->new_parent_refn.handle,
					req->new_parent_refn.fs_id);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"unable to map a server to the old parent\n");
	goto return_error;
    }

    /* the following arguments are the same from the last server msg:
     * req_p.credentials
     */

    name_sz = strlen(req->old_entry) + 1; /*include null terminator*/
    req_p.op = PVFS_SERV_RMDIRENT;
    req_p.rsize = sizeof(struct PVFS_server_req_s) + name_sz;

    req_p.u.rmdirent.entry = req->old_entry;
    req_p.u.rmdirent.parent_handle = req->old_parent_refn.handle;
    req_p.u.rmdirent.fs_id = req->old_parent_refn.fs_id;

    op_tag = get_next_session_tag();

    /* dead man walking */
    ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
	&decoded, &encoded_resp, op_tag);
    if (ret < 0)
    {
	goto return_error;
    }

    ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

    if (ack_p->status < 0 )
    {
	ret = ack_p->status;
	goto return_error;
    }

    /* sanity check:
     * rmdirent returns a handle to the file for the dirent that was
     * removed. if this isn't equal to what we passed in, we need to figure
     * out what we deleted and figure out why the server had the wrong link.
     */

    assert(ack_p->u.rmdirent.entry_handle == req->old_parent_refn.handle);
    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
			&encoded_resp, op_tag);

    return (0);

return_error:

    return (ret);

#if 0
	
	/* Revalidate the old_parent handle */
	/* Get the parent pinode */
	cflags = HANDLE_VALIDATE;
	vflags = 0;
	attr_mask = ATTR_BASIC + ATTR_META;
	/* Get the pinode either from cache or from server */
	ret = phelper_get_pinode(req->old_parent_reference,pvfs_pcache,&pinode_ptr,\
			attr_mask, vflags, req->credentials);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	cflags = HANDLE_VALIDATE;
	mask = ATTR_BASIC + ATTR_META;
	ret = phelper_validate_pinode(pinode_ptr,cflags,mask);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}

	/* Revalidate the new_parent handle */
	/* Get the parent pinode */
	cflags = HANDLE_VALIDATE;
	vflags = 0;
	attr_mask = ATTR_BASIC + ATTR_META;
	/* Get the pinode either from cache or from server */
	ret = phelper_get_pinode(req->new_parent_reference,pvfs_pcache,&pinode_ptr,\
			attr_mask, vflags, req->credentials);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}

	/* Remove directory entry server request */

	/* Query the BTI to get initial meta server */
	/* TODO: Uncomment this after implementation !!! */
	/*ret = bt_map_bucket_to_server(&server1,old_parent_reference.handle,\
	 req->fs_id);
	if (ret < 0)
	{
		goto rmdirent_map_failure;
	}*/
	ret = BMI_addr_lookup(&serv_addr1,server1);
	if (ret < 0)
	{
		goto rmdirent_map_failure;
	}
	/* Deallocate allocated memory */
	free(server1);
	name_sz = strlen(req->old_entry);
	/* Fill in the parameters */
	req_rmdirent.entry = (PVFS_string)malloc(name_sz + 1);
	if (!req_rmdirent.entry)
	{
		ret = -ENOMEM;
		goto rmdirent_map_failure;	
	}
	strncpy(req_rmdir.entry,req->old_entry,name_sz);
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

	/* Free the allocated memory */
	if (req_rmdirent.entry)
		free(req_rmdirent.entry);
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

	/* Create directory entry server request */

	/* Query the BTI to get initial meta server */
	/* TODO: Uncomment this after implementation !!! */
	/*ret = bt_map_bucket_to_server(&server2,new_parent_reference.handle,\
	 req->fs_id);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}*/
	ret = BMI_addr_lookup(&serv_addr2,server2);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	name_sz = strlen(req->new_entry);
	/* Fill in the parameters */
	req_crdirent.name = (PVFS_string)malloc(name_sz + 1);
	if (!req_crdirent.name)
	{
		ret = -ENOMEM;
		goto pinode_get_failure;	
	}
	strncpy(req_crdirent.name,req->new_entry,name_sz);
	req_crdirent.name[name_sz] = '\0'; 
	req_crdirent.new_handle = entry.handle; 
	req_crdirent.parent_handle = req->parent_handle;
	req_crdirent.fs_id = req->fs_id;
	 
	/* server request */
	ret = pint_serv_crdirent(&req_job,&ack_job,&req_crdirent,&serv_addr2); 
	if (ret < 0)
	{
		goto crdirent_failure;
	}

	/* Free the jobs */
	sysjob_free(serv_addr2,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr2,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	
	/* Remove the dentry from the dcache */
	/* TODO: We need to note down that this failed but let the rename 
	 * complete successfully. The mechanism for that ain't in place yet!!
	 */
	ret = PINT_dcache_remove(req->old_entry,old_parent_reference,\
			&item_found);
	/*if (ret < 0)
	{
		goto remove_failure;
	}*/
	/* Insert the new entry into dcache */
	ret = PINT_dcache_insert(req->new_entry,entry,new_parent_reference);

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
	if (req_rmdirent.entry)
		free(req_rmdirent.entry);

rmdirent_map_failure:
	if (server1)
		free(server1);
pinode_get_failure:
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	
	/* Free the pinode */
	pcache_pinode_dealloc(pinode_ptr);

	return(ret);
#endif
}

#if 0

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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

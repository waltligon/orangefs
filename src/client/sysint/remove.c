/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Remove Implementation */

#include <assert.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "pint-dcache.h"
#include "pint-bucket.h"
#include "pcache.h"
#include "PINT-reqproto-encode.h"

#define REQ_ENC_FORMAT 0

/* PVFS_sys_remove()
 *
 * Remove a file with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_remove(char* entry_name, PVFS_pinode_reference parent_refn, 
                    PVFS_credentials credentials)
{
	struct PVFS_server_req_s req_p;		    /* server request */
	struct PVFS_server_resp_s *ack_p = NULL;    /* server response */
	int ret = -1, ioserv_count = 0;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr;	/* PVFS address type structure */
	int name_sz = 0, attr_mask = 0;
	PVFS_pinode_reference entry;
	int items_found = 0, i = 0;
	struct PINT_decoded_msg decoded;
	bmi_size_t max_msg_sz;
	void* encoded_resp;
	PVFS_msg_tag_t op_tag;

	enum {
	    NONE_FAILURE = 0,
	    GET_PINODE_FAILURE,
	    SERVER_LOOKUP_FAILURE,
	    SEND_REQ_FAILURE,
	    RECV_REQ_FAILURE,
	    REMOVE_CACHE_FAILURE,
	} failure = NONE_FAILURE;

	ret = PINT_do_lookup(entry_name, parent_refn,
				credentials, &entry);
	if (ret < 0)
	{
	    failure = GET_PINODE_FAILURE;
	    goto return_error;
	}

	/* get the pinode for the thing we're deleting */
	attr_mask = PVFS_ATTR_COMMON_ALL;
	ret = phelper_get_pinode(entry, &pinode_ptr,
			attr_mask, credentials );
	if (ret < 0)
	{
	    failure = GET_PINODE_FAILURE;
	    goto return_error;
	}

	/* if this is a regular file, then we now also need to
	 * make sure that we have distribution information in the
	 * metadata attributes
	 */
	if(pinode_ptr->attr.objtype == PVFS_TYPE_METAFILE)
	{
	    if((pinode_ptr->mask & PVFS_ATTR_META_ALL) !=
		PVFS_ATTR_META_ALL)
	    {
		phelper_release_pinode(pinode_ptr);
		/* meta attributes aren't there- try again */
		attr_mask |= PVFS_ATTR_META_ALL;
		ret = phelper_get_pinode(entry, &pinode_ptr,
		    attr_mask, credentials);
		if(ret < 0)
		{
		    failure = GET_PINODE_FAILURE;
		    goto return_error;
		}
	    }
	}

	/* are we allowed to delete this file? */
	ret = check_perms(pinode_ptr->attr, credentials.perms,
				credentials.uid, credentials.gid);
	if (ret < 0)
	{ 
	    phelper_release_pinode(pinode_ptr);
	    ret = (-EPERM);
	    failure = GET_PINODE_FAILURE;
	    goto return_error;
	}

	ret = PINT_bucket_map_to_server(&serv_addr, entry.handle, entry.fs_id);
	if (ret < 0)
	{
	    failure = SERVER_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* send remove message to the meta file */
	req_p.op = PVFS_SERV_REMOVE;
	req_p.rsize = sizeof(struct PVFS_server_req_s);
	req_p.credentials = credentials;
	req_p.u.remove.handle = pinode_ptr->pinode_ref.handle;
	req_p.u.remove.fs_id = parent_refn.fs_id;
	max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);

	op_tag = get_next_session_tag();

	/* dead man walking */
	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
            &decoded, &encoded_resp, op_tag);
	if (ret < 0)
	{
	    failure = SEND_REQ_FAILURE;
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = RECV_REQ_FAILURE;
	    goto return_error;
	}

	PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
            &encoded_resp, op_tag);

	/* rmdirent the dir entry */
	ret = PINT_bucket_map_to_server(&serv_addr, parent_refn.handle, parent_refn.fs_id);
	if (ret < 0)
	{
	    failure = SERVER_LOOKUP_FAILURE;
	    goto return_error;
	}

	name_sz = strlen(entry_name) + 1; /*include null terminator*/

	req_p.op = PVFS_SERV_RMDIRENT;
	req_p.rsize = sizeof(struct PVFS_server_req_s) + name_sz;
	req_p.credentials = credentials;
	req_p.u.rmdirent.entry = entry_name;
	req_p.u.rmdirent.parent_handle = parent_refn.handle;
	req_p.u.rmdirent.fs_id = parent_refn.fs_id;

	op_tag = get_next_session_tag();

	/* dead man walking */
	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
            &decoded, &encoded_resp, op_tag);
	if (ret < 0)
	{
	    failure = SEND_REQ_FAILURE;
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = RECV_REQ_FAILURE;
	    goto return_error;
	}

	/* sanity check:
	 * rmdirent returns a handle to the meta file for the dirent that was 
	 * removed. if this isn't equal to what we passed in, we need to figure 
	 * out what we deleted and figure out why the server had the wrong link.
	 *
	 * CORRECTION:
	 * rmdirent returns the handle in the directory (it may not be a
	 * meta file)
	 */

	assert(ack_p->u.rmdirent.entry_handle == pinode_ptr->pinode_ref.handle);

	PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
            &encoded_resp, op_tag);

	if(pinode_ptr->attr.objtype == PVFS_TYPE_METAFILE)
	{
	    /* send remove messages to each of the data file servers */

	    /* none of this stuff changes, so we don't need to set it in a loop */
	    req_p.op = PVFS_SERV_REMOVE;
	    req_p.rsize = sizeof(struct PVFS_server_req_s);
	    req_p.credentials = credentials;
	    req_p.u.remove.fs_id = parent_refn.fs_id;
	    max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);

	    ioserv_count = pinode_ptr->attr.u.meta.nr_datafiles;

	    /* TODO: come back and unserialize this */
	    for(i = 0; i < ioserv_count; i++)
	    {
		/* each of the data files could be on different servers, so we need
		 * to get the correct server from the bucket table interface
		 */
		req_p.u.remove.handle = pinode_ptr->attr.u.meta.dfh[i];
		ret = PINT_bucket_map_to_server(&serv_addr, req_p.u.remove.handle, parent_refn.fs_id);
		if (ret < 0)
		{
		    failure = SERVER_LOOKUP_FAILURE;
		    goto return_error;
		}

		op_tag = get_next_session_tag();

		ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
		    &decoded, &encoded_resp, op_tag);
		if (ret < 0)
		{
		    failure = SEND_REQ_FAILURE;
		    goto return_error;
		}

		ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

		if (ack_p->status < 0 )
		{
		    ret = ack_p->status;
		    failure = RECV_REQ_FAILURE;
		    goto return_error;
		}

		PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
		    &encoded_resp, op_tag);
	    }
	}

	phelper_release_pinode(pinode_ptr);

	/* Remove the dentry from the dcache */
	ret = PINT_dcache_remove(entry_name,parent_refn,&items_found);
	if (ret < 0)
	{
	    failure = REMOVE_CACHE_FAILURE;
	    goto return_error;
	}

	/* Remove from pinode cache */
	ret = PINT_pcache_remove(entry,&item_ptr);
	if (ret < 0)
	{
	    failure = REMOVE_CACHE_FAILURE;
	    goto return_error;
	}

	/* free the pinode that we removed from cache */
	PINT_pcache_pinode_dealloc(item_ptr);

	return(0);
return_error:

    /* TODO: what exactly (if anything) do we want to roll back in case
     * something gets fubar'ed while we're removing data/meta/dirent files?
     */

    switch(failure)
    {
	case RECV_REQ_FAILURE:
	    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
		&encoded_resp, op_tag);
	case SEND_REQ_FAILURE:
	case SERVER_LOOKUP_FAILURE:
	    phelper_release_pinode(pinode_ptr);
	case GET_PINODE_FAILURE:
	case REMOVE_CACHE_FAILURE:
	case NONE_FAILURE:
	    break;
    }
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Set Attribute Function Implementation */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "PINT-reqproto-encode.h"

#define REQ_ENC_FORMAT 0

/* PVFS_sys_setattr()
 *
 * sets attributes for a particular PVFS file 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_setattr(PVFS_sysreq_setattr *req)
{
	struct PVFS_server_req_s req_p;			/* server request */
	struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
	int ret = -1, flags = 0;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr;		/* PVFS address type structure */
	char *server = NULL;
	PVFS_bitfield mask = req->attrmask;
	pinode_reference entry;
	PVFS_size handlesize = 0;
	bmi_size_t max_msg_sz = sizeof(struct PVFS_server_resp_s);
	struct PINT_decoded_msg decoded;

	enum {
	    NONE_FAILURE = 0,
	    PCACHE_LOOKUP_FAILURE,
	    PINODE_REMOVE_FAILURE,
	    MAP_TO_SERVER_FAILURE,
	} failure = NONE_FAILURE;

	/* Validate the handle */
	/* If size to be fetched, distribution needs to be fetched
	 * along with other desired metadata
	 */

	/*Q: does being able to set the size make any sense at all?*/
	/*A: NO! */
	if ((mask & ATTR_SIZE) == ATTR_SIZE)
		return (-1);

	/* Fill in pinode reference */
	entry.handle = req->pinode_refn.handle;
	entry.fs_id = req->pinode_refn.fs_id;
	
	/* Lookup the entry...may or may not exist in the cache */
#if 0
	/* this is commented out until getattr works correctly */

	ret = PINT_pcache_lookup(entry,pinode_ptr);
	/* Check if pinode was returned */
	if (ret == PCACHE_LOOKUP_FAILURE)
	{
		mask = mask | ATTR_BASIC;	
		ret = phelper_refresh_pinode(mask, pinode_ptr, entry, req->credentials);
		if (ret < 0)
		{
		    failure = PCACHE_LOOKUP_FAILURE;
		    goto return_error;
		}
	}
#endif

	/* by this point we definately have a pinode cache entry */

	/* Free the previously allocated pinode */
	//pcache_pinode_dealloc(pinode_ptr);

	/* TODO: If no pinode, assume handle is valid.Is this ok? */
	

	/* Make a setattr request */

	/* Get the server thru the BTI using the handle */
	ret = PINT_bucket_map_to_server(&serv_addr,entry.handle,entry.fs_id);
	if (ret < 0)
	{
	    failure = MAP_TO_SERVER_FAILURE;
	    goto return_error;
	}

	/* Create the server request */
	req_p.op = PVFS_SERV_SETATTR;
	req_p.credentials = req->credentials;
	if ((req->attr.objtype & ATTR_META) == ATTR_META)
	{
	    handlesize = req->attr.u.meta.nr_datafiles * sizeof(PVFS_handle);
	}
	else
	{
	    handlesize = 0;
	}
	req_p.rsize = sizeof(struct PVFS_server_req_s) + handlesize;
	req_p.u.setattr.handle = entry.handle;
	req_p.u.setattr.fs_id = entry.fs_id;
	req_p.u.setattr.attrmask = mask;
	req_p.u.setattr.attr = req->attr;

	/* Make a server setattr request */	
	ret = PINT_server_send_req(serv_addr, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
	    failure = MAP_TO_SERVER_FAILURE;
	    goto return_error;
	}

	/* TODO: we get a generic response back from the server and its pretty
	 * much empty.  we're not looking at any of the fields in here to
	 * check for success/failure of the io operation, so we free the
	 * structure immediately.
	 */

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	/* make sure the actual IO suceeded */
	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = PINODE_REMOVE_FAILURE;
	    goto return_error;
	}

	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	/* Remove the pinode only if it is in the cache */
	/* Note: Until an error returning scheme is decided
	 * this will return a success even in case of error
	 * as setattr is complete and there is no way to know
	 * that some problem occurred
	 */
	if (pinode_ptr != NULL)
	{
		ret = PINT_pcache_remove(entry,&item_ptr);
		if (ret < 0)
		{
			failure = PINODE_REMOVE_FAILURE;
			goto return_error;
		}
		/* Free the pinode returned */
		PINT_pcache_pinode_dealloc(item_ptr);
	}
	else
	{
		PINT_pcache_pinode_alloc(&pinode_ptr);
		/* Fill in the pinode reference for the new pinode */
		pinode_ptr->pinode_ref = entry;
	}

	/* Modify pinode to reflect changed attributes */
	ret =	modify_pinode(pinode_ptr,req->attr,req->attrmask);
	if (ret < 0)
	{
		failure = PINODE_REMOVE_FAILURE;
		goto return_error;
	}
		
	/* Add the pinode and return */
	ret = PINT_pcache_insert(pinode_ptr);
	if (ret < 0)
	{
		failure = PINODE_REMOVE_FAILURE;
		goto return_error;
	}

	return(0);

return_error:

	switch( failure )
	{
	    case PINODE_REMOVE_FAILURE:
		if (ack_p != NULL)
		    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	    case MAP_TO_SERVER_FAILURE:
		if (server != NULL)
		    free(server);

	    case PCACHE_LOOKUP_FAILURE:
		PINT_pcache_pinode_dealloc(pinode_ptr);

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

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
#include "config-manage.h"
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
	struct PVFS_server_req_s *req_p = NULL;	/* server request */
	struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
	int ret = -1, flags = 0;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	bmi_addr_t serv_addr;		/* PVFS address type structure */
	char *server = NULL;
	PVFS_bitfield mask = req->attrmask;
	pinode_reference entry;
	PVFS_servreq_setattr req_args;
	PVFS_size handlesize = 0;
	bmi_size_t max_msg_sz = sizeof(struct PVFS_server_resp_s);
	struct PINT_decoded_msg decoded;

	req_p = (struct PVFS_server_req_s *) malloc(sizeof(struct PVFS_server_req_s));
	if (req_p == NULL) {
		assert(0);
	}

	/* Validate the handle */
	/* If size to be fetched, distribution needs to be fetched
	 * along with other desired metadata
	 */

	/*Q: does being able to set the size make any sense at all?*/
	if ((mask & ATTR_SIZE) == ATTR_SIZE)
		mask = mask | ATTR_META;

	/* Fill in pinode reference */
	entry.handle = req->pinode_refn.handle;
	entry.fs_id = req->pinode_refn.fs_id;
	
	/* Get the pinode */
	ret = PINT_pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto pinode_alloc_failure;
	}
	/* Lookup the entry...may or may not exist in the cache */
	ret = PINT_pcache_lookup(entry,pinode_ptr);
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

	/* Create the server request */
	req_p->op = PVFS_SERV_SETATTR;
	req_p->credentials = req->credentials;
	if ((req->attr.objtype & ATTR_META) == ATTR_META)
	{
		handlesize = req->attr.u.meta.nr_datafiles * sizeof(PVFS_handle);
	}
	else
	{
		handlesize = 0;
	}
	req_p->rsize = sizeof(struct PVFS_server_req_s) + handlesize;
	req_p->u.setattr.handle = entry.handle;
	req_p->u.setattr.fs_id = entry.fs_id;
	req_p->u.setattr.attrmask = mask;
	req_p->u.setattr.attr = req->attr;

	/* Make a server setattr request */	
	ret = PINT_server_send_req(serv_addr, req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
		goto map_to_server_failure;
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
	    goto pinode_remove_failure;
	    ret = ack_p->status;
	}

	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	/* the request isn't needed anymore, free it */
	free(req_p);

	/* Remove the pinode only if it is in the cache */
	/* Note: Until an error returning scheme is decided
	 * this will return a success even in case of error
	 * as setattr is complete and there is no way to know
	 * that some problem occurred
	 */
	if (pinode_ptr->pinode_ref.handle != -1)
	{
		ret = PINT_pcache_remove(entry,&item_ptr);
		if (ret < 0)
		{
			goto pinode_remove_failure;
		}
		/* Free the pinode returned */
		PINT_pcache_pinode_dealloc(item_ptr);
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
	ret = PINT_pcache_insert(pinode_ptr);
	if (ret < 0)
	{
		goto pinode_remove_failure;
	}

	return(0);

pinode_remove_failure:
printf("pinode_remove_failure\n");
	if (ack_p != NULL)
		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	if (req_p != NULL)
		free(req_p);

map_to_server_failure:
printf("map_to_server_failure\n");
	if (server != NULL)
		free(server);

pcache_lookup_failure:
printf("pcache_lookup_failure\n");
	PINT_pcache_pinode_dealloc(pinode_ptr);

pinode_alloc_failure:
printf("pinode_alloc_failure\n");

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

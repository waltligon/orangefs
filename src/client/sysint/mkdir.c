/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Make Directory Function Implementation */

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


/* PVFS_sys_mkdir()
 *
 * create a directory with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_mkdir(PVFS_sysreq_mkdir *req, PVFS_sysresp_mkdir *resp)
{
    struct PVFS_server_req_s req_p;		/* server request */
    struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
    int ret = -1;
    pinode *pinode_ptr = NULL, *parent_ptr = NULL;
    bmi_addr_t serv_addr1, serv_addr2;	/* PVFS address type structure */
    int name_sz = 0;
    PVFS_handle new_bucket = 0, handle_mask = 0;
    pinode_reference entry;
    int attr_mask;
    struct PINT_decoded_msg decoded;
    bmi_size_t max_msg_sz;

    enum {
	NONE_FAILURE = 0,
	PCACHE_LOOKUP_FAILURE,
	DCACHE_LOOKUP_FAILURE,
	LOOKUP_SERVER_FAILURE,
	MKDIR_MSG_FAILURE,
	CRDIRENT_MSG_FAILURE,
	DCACHE_INSERT_FAILURE,
	PCACHE_INSERT1_FAILURE,
	PCACHE_INSERT2_FAILURE,
    } failure = NONE_FAILURE;
	

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

    /* Determine the initial metaserver for new file */
    ret = PINT_bucket_get_next_meta(req->parent_refn.fs_id,&serv_addr1,
					&new_bucket,&handle_mask);
    if (ret < 0)
    {
	failure = LOOKUP_SERVER_FAILURE;
	goto return_error;
    }

    /* send the create request for the meta file */
    req_p.op = PVFS_SERV_MKDIR;
    req_p.rsize = sizeof(struct PVFS_server_req_s);
    req_p.credentials = req->credentials;
    req_p.u.mkdir.bucket = new_bucket;
    req_p.u.mkdir.handle_mask = handle_mask;
    req_p.u.mkdir.fs_id = req->parent_refn.fs_id;
    req_p.u.mkdir.attr = req->attr;
    req_p.u.mkdir.attrmask = req->attrmask;

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
	failure = MKDIR_MSG_FAILURE;
	goto return_error;
    }

    /* save the handle for the meta file so we can refer to it later */
    entry.handle = ack_p->u.mkdir.handle;
    entry.fs_id = req->parent_refn.fs_id;

    /* these fields are the only thing we need to set for the response to
     * the calling function
     */

    resp->pinode_refn.handle = entry.handle;
    resp->pinode_refn.fs_id = entry.fs_id;

    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

    /* the all the dirents for files/directories are stored on whatever server
     * holds the parent handle */
    ret = PINT_bucket_map_to_server(&serv_addr2,req->parent_refn.handle,
    req->parent_refn.fs_id);
    if (ret < 0)
    {
	failure = MKDIR_MSG_FAILURE;
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
    req_p.u.crdirent.fs_id = entry.fs_id;

    /* max response size is the same as the previous request */

    /* Make server request */
    ret = PINT_server_send_req(serv_addr2, &req_p, max_msg_sz, &decoded);
    if (ret < 0)
    {
	failure = MKDIR_MSG_FAILURE;
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

    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

    /* add the new directory to the dcache and pinode caches */
    ret = PINT_dcache_insert(req->entry_name, entry, req->parent_refn);
    if (ret < 0)
    {
	failure = DCACHE_INSERT_FAILURE;
	goto return_error;
    }

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
    pinode_ptr->attr = req->attr;

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

	case CRDIRENT_MSG_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"CRDIRENT_MSG_FAILURE\n");

	    /* rollback mkdir message */
	    req_p.op = PVFS_SERV_RMDIR;
	    req_p.rsize = sizeof(struct PVFS_server_req_s);
	    req_p.credentials = req->credentials;
	    req_p.u.rmdir.handle = entry.handle;
	    req_p.u.rmdir.fs_id = entry.fs_id;

	    max_msg_sz = sizeof(struct PVFS_server_resp_s);
	    ret = PINT_server_send_req(serv_addr1, &req_p, max_msg_sz,&decoded);
	    PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	case MKDIR_MSG_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"MKDIR_MSG_FAILURE\n");
	    if (decoded.buffer != NULL)
		PINT_decode_release(&decoded, PINT_DECODE_RESP,
						REQ_ENC_FORMAT);
	case LOOKUP_SERVER_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"LOOKUP_SERVER_FAILURE\n");

	case DCACHE_LOOKUP_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"PCACHE_LOOKUP_FAILURE\n");

	case PCACHE_LOOKUP_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"DCACHE_LOOKUP_FAILURE\n");

	case NONE_FAILURE:
    }

return_error:
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

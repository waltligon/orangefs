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

extern struct server_configuration_s g_server_config;

/* PVFS_sys_mkdir()
 *
 * create a directory with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_mkdir(char* entry_name, PVFS_pinode_reference parent_refn, 
                        PVFS_object_attr attr, 
                        PVFS_credentials credentials, PVFS_sysresp_mkdir *resp)
{
    struct PVFS_server_req req_p;		/* server request */
    struct PVFS_server_resp *ack_p = NULL;	/* server response */
    int ret = -1;
    pinode *pinode_ptr = NULL, *parent_ptr = NULL;
    bmi_addr_t serv_addr1, serv_addr2;	/* PVFS address type structure */
    int name_sz = 0;
    PVFS_pinode_reference entry;
    int attr_mask;
    struct PINT_decoded_msg decoded;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag;
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
    attr_mask = PVFS_ATTR_COMMON_ALL;
    ret = phelper_get_pinode(parent_refn, &parent_ptr, attr_mask,
                             credentials);
    if(ret < 0)
    {
	/* parent pinode doesn't exist ?!? */
	gossip_ldebug(CLIENT_DEBUG,"unable to get pinode for parent\n");
	failure = PCACHE_LOOKUP_FAILURE;
	goto return_error;
    }

    /* check permissions in parent directory */
    ret = check_perms(parent_ptr->attr,credentials.perms,
                      credentials.uid, credentials.gid);
    if (ret < 0)
    {
	phelper_release_pinode(parent_ptr);
	ret = (-EPERM);
	gossip_ldebug(CLIENT_DEBUG,"--===PERMISSIONS===--\n");
	failure = PCACHE_LOOKUP_FAILURE;
	goto return_error;
    }

    phelper_release_pinode(parent_ptr);

    /* Lookup handle(if it exists) in dcache */
    ret = PINT_dcache_lookup(entry_name,parent_refn,&entry);
    if (ret < 0 )
    {
	/* there was an error, bail*/
	gossip_ldebug(CLIENT_DEBUG,"dcache lookup failure\n");
	failure = DCACHE_LOOKUP_FAILURE;
	goto return_error;
    }

    /* the entry could still exist, it may be uncached though */

    if (entry.handle != PINT_DCACHE_HANDLE_INVALID)
    {
	/* pinode already exists, should fail create with EXISTS*/
	gossip_ldebug(CLIENT_DEBUG,"pinode already exists\n");
	ret = (-EEXIST);
	failure = DCACHE_LOOKUP_FAILURE;
	goto return_error;
    }

    /* Determine the initial metaserver for new file */
    ret = PINT_bucket_get_next_meta(&g_server_config,
                                    parent_refn.fs_id,&serv_addr1);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"failed to get meta server\n");
	failure = LOOKUP_SERVER_FAILURE;
	goto return_error;
    }

    /* send the create request for the meta file */
    req_p.op = PVFS_SERV_MKDIR;
    req_p.rsize = sizeof(struct PVFS_server_req);
    req_p.credentials = credentials;
    req_p.u.mkdir.requested_handle = 0;
    req_p.u.mkdir.fs_id = parent_refn.fs_id;
    req_p.u.mkdir.attr = attr;
    /* filter to make sure the caller passed in a reasonable attr mask */
    req_p.u.mkdir.attr.mask &= PVFS_ATTR_SYS_ALL_NOSIZE;

    /* set the object type */
    req_p.u.mkdir.attr.mask |= PVFS_ATTR_COMMON_TYPE;
    req_p.u.mkdir.attr.objtype = PVFS_TYPE_DIRECTORY;

    max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);

    /* send the server request */

    op_tag = get_next_session_tag();

    ret = PINT_send_req(serv_addr1, &req_p, max_msg_sz,
                        &decoded, &encoded_resp, op_tag);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"failed to send mkdir request\n");
	failure = LOOKUP_SERVER_FAILURE;
	goto return_error;
    }

    ack_p = (struct PVFS_server_resp *) decoded.buffer;
    if (ack_p->status < 0 )
    {
	gossip_ldebug(CLIENT_DEBUG,"mkdir response indicates failure\n");
	ret = ack_p->status;
	failure = MKDIR_MSG_FAILURE;
	goto return_error;
    }

    /* save the handle for the meta file so we can refer to it later */
    entry.handle = ack_p->u.mkdir.handle;
    entry.fs_id = parent_refn.fs_id;

    /* these fields are the only thing we need to set for the response to
     * the calling function
     */

    resp->pinode_refn.handle = entry.handle;
    resp->pinode_refn.fs_id = entry.fs_id;

    PINT_release_req(serv_addr1, &req_p, max_msg_sz, &decoded,
                     &encoded_resp, op_tag);

    /* the all the dirents for files/directories are stored on whatever server
     * holds the parent handle */
    ret = PINT_bucket_map_to_server(&serv_addr2,parent_refn.handle,
                                    parent_refn.fs_id);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"failed to map server\n");
	failure = MKDIR_MSG_FAILURE;
	goto return_error;
    }

    /* send crdirent to associate a name with the meta file we just made */

    /* remove leading slashes from name; this isn't a complete fix. */
    while (*entry_name == '/') entry_name++;

    name_sz = strlen(entry_name) + 1; /*include null terminator*/
    req_p.op = PVFS_SERV_CREATEDIRENT;
    req_p.rsize = sizeof(struct PVFS_server_req) + name_sz;

    /* credentials come from credentials and are set in the previous
     * create request.  so we don't have to set those again.
     */

    /* just update the pointer, it'll get malloc'ed when its sent on the
     * wire.
     */
    req_p.u.crdirent.name = entry_name;
    req_p.u.crdirent.new_handle = entry.handle;
    req_p.u.crdirent.parent_handle = parent_refn.handle;
    req_p.u.crdirent.fs_id = entry.fs_id;

    /* max response size is the same as the previous request */

    op_tag = get_next_session_tag();
    /* Make server request */
    ret = PINT_send_req(serv_addr2, &req_p, max_msg_sz,
                        &decoded, &encoded_resp, op_tag);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"failed to send crdirent request\n");
	failure = MKDIR_MSG_FAILURE;
	goto return_error;
    }

    ack_p = (struct PVFS_server_resp *) decoded.buffer;

    if (ack_p->status < 0 )
    {
	/* this could fail for many reasons, EEXISTS will probbably be the
	 * most common.
	 */
	gossip_ldebug(CLIENT_DEBUG,"crdirent response indicates failure\n");
	ret = ack_p->status;
	failure = CRDIRENT_MSG_FAILURE;
	goto return_error;
    }

    PINT_release_req(serv_addr2, &req_p, max_msg_sz, &decoded,
                     &encoded_resp, op_tag);

    /* add the new directory to the dcache and pinode caches */
    ret = PINT_dcache_insert(entry_name, entry, parent_refn);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"dcache insertion failure\n");
	failure = DCACHE_INSERT_FAILURE;
	goto return_error;
    }

    ret = PINT_pcache_pinode_alloc(&pinode_ptr);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"pcache pinode allocation failure\n");
	failure = PCACHE_INSERT1_FAILURE;
	goto return_error;
    }

    /* Fill up the pinode */
    pinode_ptr->pinode_ref.handle = entry.handle;
    pinode_ptr->pinode_ref.fs_id = parent_refn.fs_id;
    pinode_ptr->attr = attr;
    /* filter to make sure mask is reasonable */
    pinode_ptr->attr.mask &= PVFS_ATTR_COMMON_ALL;
    /* set the object type */
    pinode_ptr->attr.objtype = PVFS_TYPE_DIRECTORY;

    /* Fill in the timestamps */
    ret = phelper_fill_timestamps(pinode_ptr);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"pcache pinode timestamp failure\n");
	failure = PCACHE_INSERT2_FAILURE;
	goto return_error;
    }
    /* Add pinode to the cache */
    ret = PINT_pcache_insert(pinode_ptr);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"pcache pinode insertion failure\n");
	failure = PCACHE_INSERT2_FAILURE;
	goto return_error;
    }

    ret = PINT_pcache_insert_rls(pinode_ptr);

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

	case CRDIRENT_MSG_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"CRDIRENT_MSG_FAILURE\n");

	    /* rollback mkdir message */
	    req_p.op = PVFS_SERV_REMOVE;
	    req_p.rsize = sizeof(struct PVFS_server_req);
	    req_p.credentials = credentials;
	    req_p.u.remove.handle = entry.handle;
	    req_p.u.remove.fs_id = entry.fs_id;

	    max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);
	    op_tag = get_next_session_tag();
	    ret = PINT_send_req(serv_addr1, &req_p, max_msg_sz,
                                &decoded, &encoded_resp, op_tag);

            PINT_release_req(serv_addr1, &req_p, max_msg_sz, &decoded,
                             &encoded_resp, op_tag);
            decoded.buffer = NULL;

	case MKDIR_MSG_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"MKDIR_MSG_FAILURE\n");
	    /*op_tag should still be valid if the pointer is non-null*/
	    if (decoded.buffer != NULL)
            {
		PINT_release_req(serv_addr1, &req_p, max_msg_sz, &decoded,
                                 &encoded_resp, op_tag);
            }

	case LOOKUP_SERVER_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"LOOKUP_SERVER_FAILURE\n");

	case DCACHE_LOOKUP_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"PCACHE_LOOKUP_FAILURE\n");

	case PCACHE_LOOKUP_FAILURE:
	    gossip_ldebug(CLIENT_DEBUG,"DCACHE_LOOKUP_FAILURE\n");

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

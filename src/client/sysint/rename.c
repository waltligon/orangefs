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
int PVFS_sys_rename(char* old_entry, PVFS_pinode_reference old_parent_refn, 
                        char* new_entry, PVFS_pinode_reference new_parent_refn, 
                        PVFS_credentials credentials)
{
    struct PVFS_server_req req_p;		/* server request */
    struct PVFS_server_resp *ack_p = NULL;	/* server response */
    int ret = -1;
    pinode *new_parent_p = NULL, *old_entry_p = NULL;
    bmi_addr_t serv_addr;	/* PVFS address type structure */
    uint32_t attr_mask;
    PVFS_pinode_reference old_entry_refn;
    struct PINT_decoded_msg decoded;
    bmi_size_t max_msg_sz;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag;

    if((strlen(new_entry) + 1) > PVFS_REQ_LIMIT_PATH_NAME_BYTES) 
    {
	return -ENAMETOOLONG;
    }

    attr_mask = PVFS_ATTR_COMMON_ALL;

    ret = PINT_do_lookup(old_entry, old_parent_refn,
			    credentials, &old_entry_refn);
    if (ret < 0)
    {
	goto return_error;
    }

    /* get the pinode for the thing we're renaming */
    ret = phelper_get_pinode(old_entry_refn, &old_entry_p, attr_mask, credentials);

    if (ret < 0)
    {
	goto return_error;
    }

    /* are we allowed to delete this file? */
    ret = check_perms(old_entry_p->attr, credentials.perms,
			    credentials.uid, credentials.gid);
    if (ret < 0)
    {
	phelper_release_pinode(old_entry_p);
	ret = (-EPERM);
	goto return_error;
    }
    phelper_release_pinode(old_entry_p);

    /* make sure the new parent exists */

    ret = phelper_get_pinode(new_parent_refn, &new_parent_p, 
				attr_mask, credentials);
    if(ret < 0)
    {
	/* parent pinode doesn't exist ?!? */
	gossip_ldebug(CLIENT_DEBUG,"unable to get pinode for parent\n");
	goto return_error;
    }

    /* check permissions in parent directory */
    ret = check_perms(new_parent_p->attr, credentials.perms,
				credentials.uid, credentials.gid);
    if (ret < 0)
    {
	phelper_release_pinode(new_parent_p);
	ret = (-EPERM);
	gossip_ldebug(CLIENT_DEBUG,"error checking permissions for new parent\n");
	goto return_error;
    }
    phelper_release_pinode(new_parent_p);

    ret = PINT_bucket_map_to_server(&serv_addr,new_parent_refn.handle,
					new_parent_refn.fs_id);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"unable to map a server to the new parent via the bucket table interface\n");
	goto return_error;
    }

    req_p.op = PVFS_SERV_CREATEDIRENT;
    req_p.credentials = credentials;

    req_p.u.crdirent.name = new_entry;
    req_p.u.crdirent.new_handle = old_entry_refn.handle;
    req_p.u.crdirent.parent_handle = new_parent_refn.handle;
    req_p.u.crdirent.fs_id = new_parent_refn.fs_id;

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

    ack_p = (struct PVFS_server_resp *) decoded.buffer;
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

    ret = PINT_bucket_map_to_server(&serv_addr,new_parent_refn.handle,
					new_parent_refn.fs_id);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"unable to map a server to the old parent\n");
	goto return_error;
    }

    /* the following arguments are the same from the last server msg:
     * req_p.credentials
     */
    req_p.op = PVFS_SERV_RMDIRENT;

    req_p.u.rmdirent.entry = old_entry;
    req_p.u.rmdirent.parent_handle = old_parent_refn.handle;
    req_p.u.rmdirent.fs_id = old_parent_refn.fs_id;

    op_tag = get_next_session_tag();

    /* dead man walking */
    ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
	&decoded, &encoded_resp, op_tag);
    if (ret < 0)
    {
	goto return_error;
    }

    ack_p = (struct PVFS_server_resp *) decoded.buffer;

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

    assert(ack_p->u.rmdirent.entry_handle == old_parent_refn.handle);
    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
			&encoded_resp, op_tag);

    return (0);

return_error:

    return (ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

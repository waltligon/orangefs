/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Truncate Implementation */

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

/* PVFS_sys_truncate()
 *
 * 1). get the distribution info.
 * 2). send truncate messages to each server that has a datafile telling it to
 *	truncate to the specified physical size
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_truncate(PVFS_pinode_reference pinode_refn, PVFS_size size, 
                        PVFS_credentials credentials)
{
    struct PVFS_server_req_s req_p;			/* server request */
    struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
    int ret = -1;
    pinode *pinode_ptr = NULL;
    bmi_addr_t serv_addr;	/* PVFS address type structure */
    uint32_t attr_mask;
    struct PINT_decoded_msg decoded;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag;
    int max_msg_sz;
    PVFS_Dist *dist;
    int i;

    enum
    {
	NONE_FAILURE = 0,
	GET_PINODE_FAILURE,
	MAP_TO_SERVER_FAILURE,
	SEND_MSG_FAILURE,
	DECODE_MSG_FAILURE,
    } failure = NONE_FAILURE;

    /* Get the directory pinode -- don't retrieve the size */
    attr_mask = ATTR_BASIC;
    ret = phelper_get_pinode(pinode_refn,&pinode_ptr, attr_mask, 
				credentials);
    if (ret < 0)
    {
	failure = NONE_FAILURE;
	goto return_error;
    }

    /* make sure we're allowed to remove that request */

    ret = check_perms(pinode_ptr->attr, credentials.perms,
                                credentials.uid, credentials.gid);
    if (ret < 0)
    {
	ret = (-EPERM);
	failure = GET_PINODE_FAILURE;
	goto return_error;
    }

    /* ok from here on is tricky:
     * we need to look at the distribution info to see what servers have
     * data, and from there send truncate messages to them.
     *
     */

    dist = pinode_ptr->attr.u.meta.dist;
    req_p.op = PVFS_SERV_TRUNCATE;
    req_p.credentials = credentials;
    req_p.rsize = sizeof(struct PVFS_server_req_s);
    req_p.u.truncate.fs_id = pinode_refn.fs_id;

    /* we're sending the total logical filesize to the server and it will figure
     * out how much of the phyiscal file it needs to get rid of.
     */

    req_p.u.truncate.size = size;

    /* TODO: come back and unserialize this eventually */

    for(i = 0; i < pinode_ptr->attr.u.meta.nr_datafiles; i++)
    {
	req_p.u.truncate.handle = pinode_ptr->attr.u.meta.dfh[i];

	ret = PINT_bucket_map_to_server(&serv_addr, req_p.u.truncate.handle,
		    pinode_refn.fs_id);
	if (ret < 0)
	{
	    failure = MAP_TO_SERVER_FAILURE;
	    goto return_error;
	}

	max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);

	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz, &decoded, &encoded_resp, op_tag);
	if (ret < 0)
	{
	    failure = SEND_MSG_FAILURE;
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = DECODE_MSG_FAILURE;
	    goto return_error;
	}

	PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
		&encoded_resp, op_tag);
    }

    phelper_release_pinode(pinode_ptr);

    return(0);
return_error:

    switch(failure)
    {
	case DECODE_MSG_FAILURE:
	    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
		&encoded_resp, op_tag);
	case SEND_MSG_FAILURE:
	case MAP_TO_SERVER_FAILURE:
	case GET_PINODE_FAILURE:
	    phelper_release_pinode(pinode_ptr);
	case NONE_FAILURE:
	default:
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

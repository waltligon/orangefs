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
int PVFS_sys_truncate(PVFS_sysreq_truncate *req)
{
    struct PVFS_server_req_s req_p;			/* server request */
    struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
    int ret = -1;
    pinode *pinode_ptr = NULL;
    bmi_addr_t serv_addr;	/* PVFS address type structure */
    PVFS_bitfield attr_mask;
    struct PINT_decoded_msg decoded;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag;
    int max_msg_sz;
    PVFS_Dist *dist;
    int i;
    
    enum
    {
	NONE_FAILURE = 0,
    } failure = NONE_FAILURE;
	
    /* Get the directory pinode -- don't retrieve the size */
    attr_mask = ATTR_BASIC;
    ret = phelper_get_pinode(req->pinode_refn,&pinode_ptr, attr_mask, 
				req->credentials);
    if (ret < 0)
    {
	failure = NONE_FAILURE;
	goto return_error;
    }

    /* make sure we're allowed to remove that request */

    ret = check_perms(pinode_ptr->attr, req->credentials.perms,
                                req->credentials.uid, req->credentials.gid);
    if (ret < 0)
    {
	ret = (-EPERM);
	failure = NONE_FAILURE;
	goto return_error;
    }

    /* ok from here on is tricky:
     * we need to look at the distribution info to see what servers have
     * data, and from there send truncate messages to them.
     *
     */

    dist = pinode_ptr->attr.u.meta.dist;
    req_p.op = PVFS_SERV_TRUNCATE;
    req_p.credentials = req->credentials;
    req_p.rsize = sizeof(struct PVFS_server_req_s);
    req_p.u.truncate.fs_id = req->pinode_refn.fs_id;

    /* we're sending the total logical filesize to the server and it will figure
     * out how much of the phyiscal file it needs to get rid of.
     */

    req_p.u.truncate.size = req->size;

    /* TODO: come back and unserialize this eventually */

    for(i = 0; i < pinode_ptr->attr.u.meta.nr_datafiles; i++)
    {
	req_p.u.truncate.handle = pinode_ptr->attr.u.meta.dfh[i];

	ret = PINT_bucket_map_to_server(&serv_addr, req_p.u.truncate.handle,
		    req->pinode_refn.fs_id);
	if (ret < 0)
	{
	    failure = NONE_FAILURE;
	    goto return_error;
	}

	max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op);

	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz, &decoded, &encoded_resp, op_tag);
	if (ret < 0)
	{
	    failure = NONE_FAILURE;
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = NONE_FAILURE;
	    goto return_error;
	}
    }

    return(0);

return_error:

/* TODO: this error checking thing seems useless if there aren't any pointers.
 * I don't have to free anything, so I just took this part out.
 */

#if 0
    switch(failure)
    {
	case RECV_REQ_FAILURE:
	case SEND_REQ_FAILURE:
	case SERVER_LOOKUP_FAILURE:
	case GET_PINODE_FAILURE:
	case NONE_FAILURE:
    }

#endif

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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Read Directory Implementation */

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

/* PVFS_sys_readdir()
 *
 * read a directory with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_readdir(PVFS_pinode_reference pinode_refn, PVFS_ds_position token, 
                        int pvfs_dirent_incount, PVFS_credentials credentials,
			PVFS_sysresp_readdir *resp)
{
	struct PVFS_server_req req_p;			/* server request */
	struct PVFS_server_resp *ack_p = NULL;	/* server response */
	int ret = -1;
	pinode *pinode_ptr = NULL;
	bmi_addr_t serv_addr;	/* PVFS address type structure */
	struct PINT_decoded_msg decoded;
	void* encoded_resp;
	PVFS_msg_tag_t op_tag;
	int max_msg_sz;

	enum {
	    NONE_FAILURE = 0,
	    GET_PINODE_FAILURE,
	    SERVER_LOOKUP_FAILURE,
	    SEND_REQ_FAILURE,
	    RECV_REQ_FAILURE,
	    _FAILURE,
	} failure = NONE_FAILURE;
	
	/* Revalidate directory handle */
	/* Get the directory pinode */
	ret = phelper_get_pinode(pinode_refn,&pinode_ptr,
			PVFS_ATTR_COMMON_ALL, credentials);
	if (ret < 0)
	{
	    failure = GET_PINODE_FAILURE;
	    goto return_error;
	}

	phelper_release_pinode(pinode_ptr);

	/* Read directory server request */

	/* Query the BTI to get initial meta server */
	ret = PINT_bucket_map_to_server(&serv_addr, pinode_refn.handle,
	  		pinode_refn.fs_id);
	if (ret < 0)
	{
	    failure = SERVER_LOOKUP_FAILURE;
	    goto return_error;
	}

	/* send a readdir message to the server */
	req_p.op = PVFS_SERV_READDIR;
	req_p.credentials = credentials;
	req_p.rsize = sizeof(struct PVFS_server_req);
	req_p.u.readdir.handle = pinode_refn.handle;
	req_p.u.readdir.fs_id = pinode_refn.fs_id;
	req_p.u.readdir.token = token;
	req_p.u.readdir.dirent_count = pvfs_dirent_incount;

	max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op)
			+ pvfs_dirent_incount * sizeof(PVFS_dirent);

	/*send the readdir request to the server*/

	op_tag = get_next_session_tag();

	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
	    &decoded, &encoded_resp, op_tag);
        if (ret < 0)
        {
            failure = SEND_REQ_FAILURE;
            goto return_error;
        }

        ack_p = (struct PVFS_server_resp *) decoded.buffer;

        if (ack_p->status < 0 )
        {
            ret = ack_p->status;
            failure = RECV_REQ_FAILURE;
            goto return_error;
        }

	/*pass everything the server replied with back to the calling function*/
	resp->token = ack_p->u.readdir.token;
	resp->pvfs_dirent_outcount = ack_p->u.readdir.dirent_count;
	if ( 0 < ack_p->u.readdir.dirent_count)
	{
	    resp->dirent_array = malloc(sizeof(PVFS_dirent) * ack_p->u.readdir.dirent_count);
	    if (resp->dirent_array != NULL)
	    {
		memcpy(resp->dirent_array, ack_p->u.readdir.dirent_array,
		    sizeof(PVFS_dirent) * ack_p->u.readdir.dirent_count);
	    }
	    else
	    {
		ret = (-ENOMEM);
		failure = RECV_REQ_FAILURE;
		goto return_error;
	    }
	}

	PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
	    &encoded_resp, op_tag);

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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Get attribute Function Implementation */
#include <malloc.h>
#include <assert.h>
#include <string.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pvfs2-req-proto.h"
#include "pvfs-distribution.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "PINT-reqproto-encode.h"

/* PVFS_sys_getattr()
 *
 * retrieves attributes for a file system object
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_getattr_old(PVFS_pinode_reference pinode_refn,
			 uint32_t attrmask, 
			 PVFS_credentials credentials,
			 PVFS_sysresp_getattr *resp)
{
    int ret = -1;
    PVFS_object_attr tmp_attr;

    memset(&tmp_attr, 0, sizeof(PVFS_object_attr));

    /* make sure that the caller asked for valid fields */
    if((attrmask & ~PVFS_ATTR_SYS_ALL) != 0)
    {
	gossip_lerr("Error: PVFS_sys_getattr(): attempted to "
                    "get invalid attributes.\n");
	return(-EINVAL);
    }

    ret = PINT_sys_getattr(pinode_refn, attrmask, credentials, &tmp_attr);
    if(ret < 0)
    {
	return(ret);
    }

    PINT_CONVERT_ATTR(&(resp->attr), &tmp_attr, PVFS_ATTR_COMMON_ALL);

    if (attrmask & PVFS_ATTR_SYS_SIZE)
    {
        resp->attr.size = tmp_attr.u.data.size;
        resp->attr.mask |= PVFS_ATTR_SYS_SIZE;
        resp->attr.mask |= PVFS_ATTR_DATA_ALL;
    }
    else
    {
        resp->attr.size = 0;
        resp->attr.mask &= ~PVFS_ATTR_SYS_SIZE;
        resp->attr.mask &= ~PVFS_ATTR_DATA_ALL;
    }

    /*
      FIXME/NOTE: if this is ever used anymore, you may have
      to add appropriate symlink conversion as well.

      i.e. if (attrmask & PVFS_ATTR_SYS_LNKTARGET)
      resp->link_target = strdup(tmp_attr.u.sym.target_path);
      of something ...
    */

    return(0);
}


/* TODO: this function is a hack- will be removed later */
/* PINT_sys_getattr()
 *
 * internal function to obtain the attributes of a PVFS object
 *
 * TODO: Yuck.  We have a lot of assumptions in here about what
 * the attributes look like and what masks are set in the pinode
 * cache- needs to be updated eventually...
 * 
 * returns 0 on success, -errno on failure
 */
int PINT_sys_getattr(PVFS_pinode_reference pinode_refn, uint32_t attrmask, 
		    PVFS_credentials credentials, PVFS_object_attr* out_attr)
{
    struct PVFS_server_req req_p;	 	/* server request */
    struct PVFS_server_resp *ack_p = NULL; /* server response */
    int ret = -1;
    bmi_addr_t serv_addr;	            /* PVFS address type structure */ 
    char *server = NULL;
    PVFS_size *size_array = 0;
    PINT_pinode *entry_pinode = NULL;
    PVFS_pinode_reference entry;
    struct PINT_decoded_msg decoded;
    int max_msg_sz = 0;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag = get_next_session_tag();
    PVFS_handle *data_files = NULL;
    PVFS_Dist *dist = NULL;
    PVFS_size total_filesize;
    int num_data_servers;
    int i, can_compute_size = 0;

	enum {
	    NONE_FAILURE = 0,
	    MAP_SERVER_FAILURE,
	    SEND_REQ_FAILURE,
	    MALLOC_DFH_FAILURE,
	    PCACHE_INSERT_FAILURE,
	} failure = NONE_FAILURE;

	entry.handle = pinode_refn.handle;
	entry.fs_id = pinode_refn.fs_id;

	entry_pinode = PINT_pcache_lookup(entry);
        if (!entry_pinode)
        {
            entry_pinode = PINT_pcache_pinode_alloc();
            assert(entry_pinode);
        }
        entry_pinode->refn = entry;

	ret = PINT_bucket_map_to_server(&serv_addr,entry.handle,entry.fs_id);
        if (ret < 0)
        {
		failure = MAP_SERVER_FAILURE;
		goto return_error;
        }

	req_p.op = PVFS_SERV_GETATTR;
        req_p.credentials = credentials;
	req_p.u.getattr.handle = entry.handle;
	req_p.u.getattr.fs_id = entry.fs_id;
        req_p.u.getattr.attrmask = attrmask;
        /*
          append all meta info flags if size is requested since
          we'll need that information to compute the size
        */
        if (attrmask & PVFS_ATTR_SYS_SIZE)
        {
            req_p.u.getattr.attrmask |= PVFS_ATTR_META_ALL;
        }

	max_msg_sz = PINT_encode_calc_max_size(
            PINT_ENCODE_RESP, req_p.op, PINT_CLIENT_ENC_TYPE);

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
		failure = SEND_REQ_FAILURE;
		goto return_error;
        }

	*out_attr = ack_p->u.getattr.attr;
	if (out_attr->objtype == PVFS_TYPE_METAFILE)
	{
	    if ((out_attr->mask & PVFS_ATTR_META_DFILES) &&
                (out_attr->u.meta.dfile_count > 0))
	    {
		assert(ack_p->u.getattr.attr.u.meta.dfile_array != NULL);

		out_attr->u.meta.dfile_array = malloc(
                    out_attr->u.meta.dfile_count * sizeof(PVFS_handle));
		if (out_attr->u.meta.dfile_array ==  NULL)
		{
		    ret = (-ENOMEM);
		    failure = MALLOC_DFH_FAILURE;
		    goto return_error;
		}
		memcpy(out_attr->u.meta.dfile_array, 
			ack_p->u.getattr.attr.u.meta.dfile_array, 
			out_attr->u.meta.dfile_count * sizeof(PVFS_handle));
	    }

	    /* TODO: make this better */
	    if ((out_attr->mask & PVFS_ATTR_META_DIST) &&
                (out_attr->u.meta.dist_size > 0))
	    {
		gossip_lerr("KLUDGE: packing dist to memcpy it.\n");
		out_attr->u.meta.dist =
		    malloc(out_attr->u.meta.dist_size);
		if(out_attr->u.meta.dist == NULL)
		{
		    ret = -ENOMEM;
		    failure = MALLOC_DFH_FAILURE;
		    goto return_error;
		}
		PINT_Dist_encode(out_attr->u.meta.dist, 
		    ack_p->u.getattr.attr.u.meta.dist);
		PINT_Dist_decode(out_attr->u.meta.dist, NULL);
	    }
	}

        /*
          determine if the size was requested, and if we have the
          appropriate meta information to compute it
        */
        if ((attrmask & PVFS_ATTR_SYS_SIZE) &&
            (out_attr->objtype == PVFS_TYPE_METAFILE) &&
            (out_attr->mask & PVFS_ATTR_META_ALL))
        {
            can_compute_size = 1;
        }

	PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
                         &encoded_resp, op_tag);

	if (can_compute_size)
	{
	    /* TODO: things to do to get the size:
	     * 1). send a getattr message to each server that has a datafile
	     * 2). call the dist code to figure out if a server has sparse
	     *	data written
	     */
            num_data_servers = out_attr->u.meta.dfile_count;
            if (num_data_servers == 0)
            {
                /*
                  if there are no data servers, don't even try to
                  compute anything here
                */
                gossip_lerr("Number of dataservers is 0. "
                            "Skipping size computation!\n");
                ret = NONE_FAILURE;
                goto return_error;
            }

	    size_array = malloc(num_data_servers * sizeof(PVFS_size));
	    if (size_array == NULL)
	    {
		ret = -ENOMEM;
		goto return_error;
	    }

	    /* we need to send one getattr to each server for each datafile*/
	    data_files = out_attr->u.meta.dfile_array;
	    dist = out_attr->u.meta.dist;
	    req_p.op = PVFS_SERV_GETATTR;
	    req_p.credentials = credentials;
	    req_p.u.getattr.attrmask = PVFS_ATTR_DATA_SIZE;
	    req_p.u.getattr.fs_id = entry.fs_id;

	    for(i = 0; i < num_data_servers; i++)
	    {
		ret = PINT_bucket_map_to_server(
                    &serv_addr, data_files[i], entry.fs_id);
		if (ret < 0)
		{
		    failure = MAP_SERVER_FAILURE;
		    goto return_error;
		}

		req_p.u.getattr.handle = data_files[i];

		max_msg_sz = PINT_encode_calc_max_size(
                    PINT_ENCODE_RESP, req_p.op, PINT_CLIENT_ENC_TYPE);

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
		    failure = SEND_REQ_FAILURE;
		    goto return_error;
		}

		size_array[i] = ack_p->u.getattr.attr.u.data.size;
	    }

	    /* now call the distribution code for this data so we can figure
	     * out what the true filesize is.
	     */
	    ret = PINT_Dist_lookup(dist);
	    if (ret < 0)
	    {
		goto return_error;
	    }

	    total_filesize = (dist->methods->logical_file_size)
                (dist->params, num_data_servers, size_array);

	    out_attr->u.data.size = total_filesize;

	    entry_pinode->size = total_filesize;
	}
        PINT_pcache_set_valid(entry_pinode);

	/* Free memory allocated for name */
	if (size_array)
	    free(size_array);

	return(0);

return_error:

	switch( failure ) 
	{
		case PCACHE_INSERT_FAILURE:
		    free(out_attr->u.meta.dfile_array);
		case MALLOC_DFH_FAILURE:
		case SEND_REQ_FAILURE:
		case MAP_SERVER_FAILURE:
		    if (ack_p)
			PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
				&encoded_resp, op_tag);

		    if (server)
			free(server);
		    /* Free memory allocated for name */
		    if (size_array)
			free(size_array);
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "bmi.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

/* do_encode_resp()
 * 
 * encodes a response structure to be
 * sent over the network
 * 
 * returns 0 on success, -ERRNO on failure
 */

int do_encode_resp(
	struct PVFS_server_resp_s *response,
	struct PINT_encoded_msg *target_msg,
	int header_size
	)
{
    int total_path_count = 0;

    /* TODO: CHECK RETURN VALUES */

    switch(response->op)
    {
	/* these first response types have no trailing data */
	case PVFS_SERV_CREATE:
	case PVFS_SERV_MKDIR:
	case PVFS_SERV_RMDIRENT:
	case PVFS_SERV_IO:
	case PVFS_SERV_NOOP:
	case PVFS_SERV_SETATTR:
	case PVFS_SERV_REMOVE:
	case PVFS_SERV_CREATEDIRENT:
	case PVFS_SERV_WRITE_COMPLETION:
	    /* 
	     *	There is an int64_t here...
	     *	but handled correctly so far!
	     */
	    target_msg->size_list   = malloc(sizeof(PVFS_size));
	    target_msg->buffer_list = malloc(sizeof(void *));
	    target_msg->list_count  = 1;
	    target_msg->buffer_type = BMI_PRE_ALLOC;
	    target_msg->size_list[0] = target_msg->total_size 
		= sizeof(struct PVFS_server_resp_s) + header_size;
	    target_msg->buffer_list[0] 
		= BMI_memalloc(target_msg->dest, 
			sizeof(struct PVFS_server_resp_s) + header_size,
			BMI_SEND);
	    memcpy(
		    target_msg->buffer_list[0], 
		    response, 
		    sizeof(struct PVFS_server_resp_s)
		  );
	    /* set accurate rsize for encoded version */
	    ((struct PVFS_server_resp_s*)(target_msg->buffer_list[0]))->rsize
		= target_msg->total_size - header_size;
	    return(0);

	case PVFS_SERV_GETATTR:
	    /* 
	     *	Ok... here is the trick... 
	     *	We need to pack the struct,
	     *	then based on object type,
	     *	store the associated specific
	     *	attribs.
	     *	7.1.03 This is only true for 
	     *	PVFS_metafile_attr.  DW
	     */
	    // if (resp->u.getattr.objtype != METAFILE) ?? DW

	    /* This code is only good when there are no trailing pointers */
	    target_msg->size_list   = malloc(sizeof(PVFS_size));
	    target_msg->buffer_list = malloc(sizeof(void *));
	    target_msg->list_count  = 1;
	    target_msg->buffer_type = BMI_PRE_ALLOC;

	    /* we may need to pack trailing data for metafiles */
	    if(response->u.getattr.attr.objtype ==
		PVFS_TYPE_METAFILE)
	    {
		char* pack_dest = NULL;
		/* make it big enough to hold datafiles and dist */
		target_msg->size_list[0] = 
		    target_msg->total_size = 
		    sizeof(struct PVFS_server_resp_s) + header_size
		    + response->u.getattr.attr.u.meta.dist_size
		    + (response->u.getattr.attr.u.meta.dfile_count
		    * sizeof(PVFS_handle));
		target_msg->buffer_list[0] = 
		    BMI_memalloc(target_msg->dest, 
		    target_msg->total_size,
		    BMI_SEND);
		pack_dest = (char*)target_msg->buffer_list[0];
		/* copy in the datafiles */
		if(response->u.getattr.attr.u.meta.dfile_count > 0)
		{
		    pack_dest += sizeof(struct PVFS_server_resp_s);
		    memcpy(
			pack_dest,
			response->u.getattr.attr.u.meta.dfile_array,
			(response->u.getattr.attr.u.meta.dfile_count
			* sizeof(PVFS_handle)));
		}
		/* copy in the distribution */
		if(response->u.getattr.attr.u.meta.dist_size > 0)
		{
		    pack_dest +=
			(response->u.getattr.attr.u.meta.dfile_count
			* sizeof(PVFS_handle));
		    PINT_Dist_encode(pack_dest,
			response->u.getattr.attr.u.meta.dist);
		}
	    }
	    /* not a metafile */
	    else
	    {
		target_msg->size_list[0] = 
		    target_msg->total_size = sizeof(struct PVFS_server_resp_s)+header_size;
		target_msg->buffer_list[0]
		    = BMI_memalloc(target_msg->dest, 
			    sizeof(struct PVFS_server_resp_s) + header_size,
			    BMI_SEND);
	    }

	    /* in either case, pack the basic ack in */
	    memcpy(
		    target_msg->buffer_list[0], 
		    response, 
		    sizeof(struct PVFS_server_resp_s)
		  );

	    /* set rsize for the "on-the-wire" version */
	    ((struct PVFS_server_resp_s*)target_msg->buffer_list[0])->rsize =
		target_msg->total_size - header_size;
	    return(0);

	case PVFS_SERV_GETCONFIG:
	    /* assert on all strings (at least for now) */
	    if(response->status == 0)
	    {
                assert(response->u.getconfig.fs_config_buf != NULL);
                assert(response->u.getconfig.server_config_buf != NULL);

		target_msg->size_list   = malloc(3*sizeof(PVFS_size));
		target_msg->buffer_list = malloc(3*sizeof(void *));
		target_msg->list_count  = 3;
		target_msg->buffer_type = BMI_EXT_ALLOC;

		target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s);
		target_msg->size_list[1] = response->u.getconfig.fs_config_buflen;
		target_msg->size_list[2] = response->u.getconfig.server_config_buflen;
		target_msg->total_size = target_msg->size_list[0] +
                    response->u.getconfig.fs_config_buflen +
                    response->u.getconfig.server_config_buflen;

		target_msg->buffer_list[0] = response;
		target_msg->buffer_list[1] = response->u.getconfig.fs_config_buf;
		target_msg->buffer_list[2] = response->u.getconfig.server_config_buf;
	    }
	    else
	    {
		target_msg->size_list   = malloc(sizeof(PVFS_size));
		target_msg->buffer_list = malloc(sizeof(void *));
		target_msg->list_count  = 1;
		target_msg->buffer_type = BMI_EXT_ALLOC;
		target_msg->buffer_list[0] = response;
		target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s);
		target_msg->total_size = target_msg->size_list[0];
	    }

	    /* TODO: this one is special; it doesn't have
	     * header_size tacked onto it (!?)
	     */

	    /* set accurate rsize for encoded version */
	    ((struct PVFS_server_resp_s*)(target_msg->buffer_list[0]))->rsize
		= target_msg->total_size;
	    return(0);

	case PVFS_SERV_LOOKUP_PATH:
	    assert(response->u.lookup_path.handle_array != NULL);

	    /* this one doesn't have header_size tacked onto it (!?) */
	    response->rsize = sizeof(struct PVFS_server_resp_s) +
		response->u.lookup_path.count * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));

	    /* We need to lookout for the fact that we may not have the attributes for
	       a single segment within the path. In this case we will only have a handle
	       which most of this is dealt with in the state machine.  dw
	     */
	    if((response->rsize - sizeof(struct PVFS_server_resp_s) 
			- response->u.lookup_path.count * (sizeof(PVFS_handle) +
			    sizeof(PVFS_object_attr))) < 0)
		total_path_count = response->u.lookup_path.count -1;
	    else
		total_path_count = response->u.lookup_path.count;

	    target_msg->size_list   = malloc(3*sizeof(PVFS_size));
	    target_msg->buffer_list = malloc(3*sizeof(void *));
	    target_msg->list_count  = 3;
	    target_msg->buffer_type = BMI_EXT_ALLOC;

	    if(response->status != 0)
		target_msg->list_count = 1;

	    target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s);
	    target_msg->size_list[1] = response->u.lookup_path.count*sizeof(PVFS_handle);
	    target_msg->size_list[2] = total_path_count*sizeof(PVFS_object_attr);
	    target_msg->total_size = target_msg->size_list[0]
		+ target_msg->size_list[1] + target_msg->size_list[2];

	    target_msg->buffer_list[0] = response;
	    target_msg->buffer_list[1] = response->u.lookup_path.handle_array;
	    target_msg->buffer_list[2] = response->u.lookup_path.attr_array;


#if 0
	    target_msg->total_size = target_msg->size_list[0] = response->rsize;
	    respbuf = target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest,
		    target_msg->total_size + header_size,
		    BMI_SEND);

	    memcpy(respbuf, response, sizeof(struct PVFS_server_resp_s));
	    respbuf += sizeof(struct PVFS_server_resp_s);

	    /* make two passes, first to copy handles, second for attribs */
	    for (i=0; i < response->u.lookup_path.count; i++) {
		memcpy(respbuf, &(response->u.lookup_path.handle_array[i]), sizeof(PVFS_handle));
		respbuf += sizeof(PVFS_handle);
	    }
	    for (i=0; i < total_path_count; i++) {
		memcpy(respbuf, &(response->u.lookup_path.attr_array[i]), sizeof(PVFS_object_attr));
		respbuf += sizeof(PVFS_object_attr);
	    }
#endif
	    return(0);

	case PVFS_SERV_READDIR:

	    target_msg->size_list   = malloc(2*sizeof(PVFS_size));
	    target_msg->buffer_list = malloc(2*sizeof(void *));
	    target_msg->list_count  = 2;
	    target_msg->buffer_type = BMI_EXT_ALLOC;

	    /* If there was an error, we need to not send any dirents */
	    if(response->status != 0)
		target_msg->list_count = 1;

	    target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s);
	    target_msg->size_list[1] = response->u.readdir.pvfs_dirent_count 
		* sizeof(PVFS_dirent);
	    target_msg->total_size = target_msg->size_list[0] + target_msg->size_list[1];

	    target_msg->buffer_list[0] = response;
	    target_msg->buffer_list[1] = response->u.readdir.pvfs_dirent_array;

#if 0
	    target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest,
		    target_msg->total_size + header_size,
		    BMI_SEND);

	    memcpy(respbuf, response, sizeof(struct PVFS_server_resp_s));
	    respbuf += sizeof(struct PVFS_server_resp_s);

	    for (i=0; i < response->u.readdir.pvfs_dirent_count; i++)
	    {
		memcpy(respbuf, &(response->u.readdir.pvfs_dirent_array[i]), sizeof(PVFS_dirent));
		respbuf += sizeof(PVFS_dirent);
	    }
#endif
	    /* TODO: this one is special; it doesn't have
	     * header_size tacked onto it (!?)
	     */

	    /* set accurate rsize for encoded version */
	    ((struct PVFS_server_resp_s*)(target_msg->buffer_list[0]))->rsize
		= target_msg->total_size;

	    return 0;

	default:
	    return -1;
    }
    return -EINVAL;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

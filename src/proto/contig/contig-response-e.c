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
    int i=0;
    size_t strlen1, strlen2;
    char *respbuf;

    /* TODO: CHECK RETURN VALUES */
    target_msg->size_list   = malloc(sizeof(PVFS_size));
    target_msg->buffer_list = malloc(sizeof(void *));
    target_msg->list_count  = 1;

    switch(response->op)
    {
	case PVFS_SERV_CREATE:
	case PVFS_SERV_MKDIR:
	case PVFS_SERV_RMDIRENT:

	    /* 
	     *	There is no response struct for
	     *	these requests below.  Therefore
	     *	we should not call it.
	     * TODO: Are we still using Generic ACK? dw 07.01.03
	     */
				//free(target_msg->buffer_list);
	case PVFS_SERV_NOOP:
	case PVFS_SERV_SETATTR:
	case PVFS_SERV_REMOVE:
	case PVFS_SERV_RMDIR:
	case PVFS_SERV_CREATEDIRENT:
	    /* 
	     *	There is an int64_t here...
	     *	but handled correctly so far!
	     */
	    target_msg->size_list[0] = target_msg->total_size = sizeof(struct PVFS_server_resp_s);
	    target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest, 
						      sizeof(struct PVFS_server_resp_s) + header_size,
						      BMI_SEND_BUFFER);
	    memcpy(target_msg->buffer_list[0], response, sizeof(struct PVFS_server_resp_s));
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
	    target_msg->size_list[0] = 
		target_msg->total_size = sizeof(struct PVFS_server_resp_s);
	    target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest, 
						      sizeof(struct PVFS_server_resp_s) + header_size,
						      BMI_SEND_BUFFER);
		 memcpy(target_msg->buffer_list[0], response, sizeof(struct PVFS_server_resp_s));
				// else pack it!
	    return(0);

	case PVFS_SERV_GETCONFIG:
	    /* assert on all strings (at least for now) */
	    assert(response->u.getconfig.meta_server_mapping != NULL);
	    assert(response->u.getconfig.io_server_mapping != NULL);

	    strlen1 = strlen(response->u.getconfig.meta_server_mapping) + 1; /* include NULL terminator */
	    strlen2 = strlen(response->u.getconfig.io_server_mapping) + 1;

	    target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s) + strlen1 + strlen2;
	    target_msg->total_size = target_msg->size_list[0];

	    respbuf = target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest,
								target_msg->total_size + header_size,
								BMI_SEND_BUFFER);

	    /* use respbuf as a temporary pointer to clean this up */
	    memcpy(respbuf, response, sizeof(struct PVFS_server_resp_s));
	    respbuf += sizeof(struct PVFS_server_resp_s);
	    memcpy(respbuf, response->u.getconfig.meta_server_mapping, strlen1);
	    respbuf += strlen1;
	    memcpy(respbuf, response->u.getconfig.io_server_mapping, strlen2);
	    return(0);
				
	case PVFS_SERV_LOOKUP_PATH:
	    assert(response->u.lookup_path.handle_array != NULL);

	    target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s) +
		response->u.lookup_path.count * sizeof(PVFS_handle) +
		response->u.lookup_path.count * sizeof(PVFS_object_attr);
	    target_msg->total_size = target_msg->size_list[0];
	    respbuf = target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest,
								target_msg->total_size + header_size,
								BMI_SEND_BUFFER);

	    memcpy(respbuf, response, sizeof(struct PVFS_server_resp_s));
	    respbuf += sizeof(struct PVFS_server_resp_s);

	    /* make two passes, first to copy handles, second for attribs */
	    for (i=0; i < response->u.lookup_path.count; i++) {
		memcpy(respbuf, &(response->u.lookup_path.handle_array[i]), sizeof(PVFS_handle));
		respbuf += sizeof(PVFS_handle);
	    }
	    for (i=0; i < response->u.lookup_path.count; i++) {
		memcpy(respbuf, &(response->u.lookup_path.attr_array[i]), sizeof(PVFS_object_attr));
		respbuf += sizeof(PVFS_object_attr);
	    }
	    return(0);

	case PVFS_SERV_READDIR:
	    target_msg->size_list[0] = sizeof(struct PVFS_server_resp_s) + 
		response->u.readdir.pvfs_dirent_count * sizeof(PVFS_dirent);
	    target_msg->total_size = target_msg->size_list[0];
	    respbuf = target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest,
							        target_msg->total_size + header_size,
								BMI_SEND_BUFFER);

	    memcpy(respbuf, response, sizeof(struct PVFS_server_resp_s));
	    respbuf += sizeof(struct PVFS_server_resp_s);

	    for (i=0; i < response->u.readdir.pvfs_dirent_count; i++)
	    {
		memcpy(respbuf, &(response->u.readdir.pvfs_dirent_array[i]), sizeof(PVFS_dirent));
		respbuf += sizeof(PVFS_dirent);
	    }
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

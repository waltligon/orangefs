/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "bmi.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

int do_decode_req(
		  void *input_buffer,
		  struct PINT_decoded_msg *target_msg,
		  bmi_addr_t target_addr
		  )
{
    struct PVFS_server_req_s * dec_msg = NULL;
    char* char_ptr = (char *) input_buffer;
    int size = 0, i = 0;

    size = ((struct PVFS_server_req_s *)input_buffer)->rsize;

    if (size <= 0)
    {
	/*we called decode on a null buffer??*/
	return(-EINVAL);
    }
    dec_msg = malloc(size);
    if (dec_msg == NULL)
    {
	return (-ENOMEM);
    }
    memcpy(dec_msg, char_ptr, size);
    target_msg->buffer = dec_msg;

    switch(((struct PVFS_server_req_s *)char_ptr)->op)
    {
	case PVFS_SERV_GETCONFIG:
	    /* update the pointer to fs_name string */
	    char_ptr += sizeof( struct PVFS_server_req_s );
	    dec_msg->u.getconfig.fs_name = char_ptr;
	    return(0);

	case PVFS_SERV_LOOKUP_PATH:
	    char_ptr += sizeof( struct PVFS_server_req_s );
	    dec_msg->u.lookup_path.path = char_ptr;
	    return(0);

	case PVFS_SERV_CREATEDIRENT:
	    char_ptr += sizeof( struct PVFS_server_req_s );
	    dec_msg->u.crdirent.name = char_ptr;
	    return(0);

	case PVFS_SERV_RMDIRENT:
	    char_ptr += sizeof( struct PVFS_server_req_s );
	    dec_msg->u.rmdirent.entry = char_ptr;
	    return(0);

	case PVFS_SERV_MKDIR:
	    char_ptr += sizeof( struct PVFS_server_req_s );
	    if ( dec_msg->u.mkdir.attr.objtype == ATTR_META )
	    {
		dec_msg->u.mkdir.attr.u.meta.dfh = (PVFS_handle *)char_ptr;
	    }
	    return(0);

	case PVFS_SERV_SETATTR:
	    char_ptr += sizeof(struct PVFS_server_req_s);

	    if ( dec_msg->u.setattr.attr.objtype == ATTR_META )
	    {
		dec_msg->u.setattr.attr.u.meta.dfh = (PVFS_handle *)char_ptr;
	    }
	    target_msg->buffer = dec_msg;

	    return(0);
	case PVFS_SERV_RMDIR: /*these structures are all self contained (no pointers that need to be packed) */
	case PVFS_SERV_CREATE:
	case PVFS_SERV_READDIR:
	case PVFS_SERV_GETATTR:
	case PVFS_SERV_STATFS:
	case PVFS_SERV_REMOVE:
	case PVFS_SERV_TRUNCATE:
	case PVFS_SERV_ALLOCATE:
	    return(0);

	case PVFS_SERV_IO: /*haven't been implemented yet*/
	case PVFS_SERV_IOSTATFS:
	case PVFS_SERV_GETDIST:
	case PVFS_SERV_REVLOOKUP:
	default:
	    printf("Unpacking Req Op: %d Not Supported\n", ((struct PVFS_server_req_s *)char_ptr)->op);
	    return -1;
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

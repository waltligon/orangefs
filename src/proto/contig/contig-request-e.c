/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "bmi.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

int do_encode_req(
		  struct PVFS_server_req_s *request,
		  struct PINT_encoded_msg *target_msg,
		  int header_size
		  )
{
    void* enc_msg;
    bmi_size_t size = 0, name_sz = 0;

    /* all the messages that we build in this function are one contig. block */
    /* TODO: USE ONE MALLOC() INSTEAD OF TWO */
    target_msg->size_list   = malloc(sizeof(PVFS_size));
    target_msg->buffer_list = (void *) malloc(sizeof(void *));
    if (!target_msg)
    {
	return -EINVAL;
    }
    target_msg->list_count = 1;
    target_msg->buffer_flag = 0;

    switch( request->op )
    {
	case PVFS_SERV_GETCONFIG:

	    /* just assert on the fs_name being non-NULL; returning EINVAL is
	     * the right thing to do for cases where the user could have given
	     * us a bad value or a syscall returned something, but at this point
	     * if we get a bad string it's just a bug in our code.
	     */
	    assert(request->u.getconfig.fs_name != NULL);

	    name_sz = strlen(request->u.getconfig.fs_name) + 1; /* include NULL terminator in size */
	    size    = sizeof(struct PVFS_server_req_s) + name_sz;
	    enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) (size + header_size), BMI_SEND_BUFFER);

	    /* here the right thing to do is to return the error code. */
	    if (enc_msg == NULL)
	    {
		return -ENOMEM;
	    }
	    /* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0]   = size;
	    target_msg->total_size     = size;

	    memcpy(enc_msg, request, sizeof(struct PVFS_server_req_s));
	    /* note: we know the size of the string from above, so we can just memcpy. */
	    memcpy(enc_msg + sizeof(struct PVFS_server_req_s), request->u.getconfig.fs_name, name_sz);

	    /* put NULLs in all pointers going across wire */
	    ((struct PVFS_server_req_s *)enc_msg)->u.getconfig.fs_name = NULL;

	    return 0;

	    /* END NEW VERSION */

	case PVFS_SERV_LOOKUP_PATH:
	    assert(request->u.lookup_path.path != NULL);

	    name_sz = strlen( request->u.lookup_path.path ) + 1; /* include NULL terminator in size */
	    size = sizeof( struct PVFS_server_req_s ) + name_sz;
	    enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );

	    if (enc_msg == NULL)
	    {
		return (-ENOMEM);
	    }
	    /* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;

	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

	    /* copy a null terminated string to another */
	    memcpy(enc_msg + sizeof( struct PVFS_server_req_s ), request->u.lookup_path.path, name_sz);

	    /* make pointer since we're sending it over the wire and don't want 
	     * random memory referenced on the other side */

	    ((struct PVFS_server_req_s *)enc_msg)->u.lookup_path.path = NULL;
	    return (0);

	case PVFS_SERV_CREATEDIRENT:
	    assert(request->u.crdirent.name != NULL);

	    name_sz = strlen( request->u.crdirent.name ) + 1; /* include NULL terminator in size */
	    size = sizeof( struct PVFS_server_req_s ) + name_sz;
	    enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );

	    if (enc_msg == NULL)
	    {
		return (-ENOMEM);
	    }
	    /* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;

	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

	    /* copy a null terminated string to another */
	    memcpy(enc_msg + sizeof( struct PVFS_server_req_s ), request->u.crdirent.name, name_sz);

	    /* make pointer since we're sending it over the wire and don't want 
	     * random memory referenced on the other side */

	    ((struct PVFS_server_req_s *)enc_msg)->u.crdirent.name = NULL;
	    return (0);

	case PVFS_SERV_RMDIRENT:
	    assert(request->u.rmdirent.entry != NULL);

	    name_sz = strlen( request->u.rmdirent.entry ) + 1; /* include NULL terminator in size */
	    size = sizeof( struct PVFS_server_req_s ) + name_sz;
	    enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );

	    if (enc_msg == NULL)
	    {
		return (-ENOMEM);
	    }
	    /* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;

	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

	    /* copy a null terminated string to another */
	    memcpy( enc_msg + sizeof( struct PVFS_server_req_s ), request->u.rmdirent.entry, name_sz);

	    /* make pointer since we're sending it over the wire and don't want 
	     * random memory referenced on the other side */

	    ((struct PVFS_server_req_s *)enc_msg)->u.rmdirent.entry = NULL;
	    return (0);

	case PVFS_SERV_MKDIR:
	    size = sizeof( struct PVFS_server_req_s ) + sizeof( struct PVFS_object_attr );

	    /* if we're mkdir'ing a meta file, we need to alloc space for the attributes */
	    if ( request->u.mkdir.attr.objtype == ATTR_META )
	    {
		size += request->u.mkdir.attr.u.meta.nr_datafiles * sizeof( PVFS_handle );
	    }

	    /* TODO: come back and alloc the right spaces for 
	     * distributions cause they're going to change */

	    enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );
	    if (!enc_msg)
	    {
		return (-ENOMEM);
	    }
	    /* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;

	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

	    /* throw handles at the end for metadata files */
	    if ( request->u.mkdir.attr.objtype == ATTR_META )
	    {
				/* handles */
		memcpy( enc_msg + sizeof( struct PVFS_server_req_s ),
			request->u.mkdir.attr.u.meta.dfh, 
			request->u.mkdir.attr.u.meta.nr_datafiles * sizeof( PVFS_handle ) );

	        /* make pointer since we're sending it over the wire and don't want 
		 * random memory referenced on the other side */

		((struct PVFS_server_req_s *)enc_msg)->u.mkdir.attr.u.meta.dfh = NULL;
	    }
	    return (0);

	case PVFS_SERV_SETATTR:
	    size = sizeof( struct PVFS_server_req_s ) + sizeof( struct PVFS_object_attr );

	    if(request->u.setattr.attr.objtype == ATTR_META)
	    {
		/* negative datafiles? wtf ... */
		if(request->u.setattr.attr.u.meta.nr_datafiles >= 0)
		{
		    assert(request->u.setattr.attr.u.meta.dfh != NULL);
		    size += request->u.setattr.attr.u.meta.nr_datafiles * sizeof( PVFS_handle );
		}
	    }

	    /* TODO: come back and alloc the right spaces for 
	     * distributions and eattribs cause they're going to change */

	    enc_msg = BMI_memalloc( target_msg->dest, size + header_size, BMI_SEND_BUFFER );
	    if (enc_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;

	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );
	    enc_msg += sizeof( struct PVFS_server_req_s );

	    /* throw handles at the end for metadata files */
	    if ( request->u.setattr.attr.objtype == ATTR_META )
	    {
		memcpy( enc_msg, 
			request->u.setattr.attr.u.meta.dfh, 
			request->u.setattr.attr.u.meta.nr_datafiles * sizeof( PVFS_handle ) );

		/* make pointer NULL since we're sending it over the wire and don't want 
		 * random memory referenced on the other side */

		/*((struct PVFS_server_req_s *)enc_msg)->u.setattr.attr.u.meta.dfh = NULL;*/
	    }
	    return (0);
	case PVFS_SERV_IO: 
	    /* TODO: this one is a work in progress.
	     * for now it works like the self contained operations
	     * listed below, but it will get more complex later
	     */
	    size = sizeof( struct PVFS_server_req_s );
	    enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER ) ;
	    if (enc_msg == NULL)
	    {
		return (-ENOMEM);
	    }
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;
	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

	    return (0);
	case PVFS_SERV_RMDIR: /*these structures are all self contained (no pointers that need to be packed) */
	case PVFS_SERV_CREATE:
	case PVFS_SERV_READDIR:
	case PVFS_SERV_GETATTR:
	case PVFS_SERV_STATFS:
	case PVFS_SERV_REMOVE:
	case PVFS_SERV_TRUNCATE:
	case PVFS_SERV_ALLOCATE:
	    size = sizeof( struct PVFS_server_req_s );
	    enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER ) ;
	    if (enc_msg == NULL)
	    {
		return (-ENOMEM);
	    }
	    target_msg->buffer_list[0] = enc_msg;
	    target_msg->size_list[0] = size;
	    target_msg->total_size = size;
	    memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

	    return (0);

	/*haven't been implemented yet*/
	case PVFS_SERV_IOSTATFS:
	case PVFS_SERV_GETDIST:
	case PVFS_SERV_REVLOOKUP:
	    printf("op: %d not implemented\n", request->op);
	    target_msg = NULL;
	    return -1;
	default:
	    printf("op: %d not defined\n", request->op);
	    target_msg = NULL;
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

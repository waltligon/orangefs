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
    //struct PVFS_server_req_s * request = input_buffer;
    struct PVFS_server_req_s * dec_msg = NULL;
    char* char_ptr = (char *) input_buffer;
    int size = 0, i = 0;
    switch(((struct PVFS_server_req_s *)char_ptr)->op)
    {
	case PVFS_SERV_GETCONFIG:
	    /*print_request( request );*/
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    memcpy( dec_msg, char_ptr, sizeof( struct PVFS_server_req_s ) );
	    char_ptr += sizeof( struct PVFS_server_req_s );
	    size = strlen( char_ptr ) + 1; /* include NULL terminator */

	    /* copy in fs_name string */
	    dec_msg->u.getconfig.fs_name = (char *) BMI_memalloc(target_addr, size, BMI_RECV_BUFFER);
	    if (dec_msg->u.getconfig.fs_name == NULL)
	    {
		BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
		return (-ENOMEM);
	    }
	    memcpy(dec_msg->u.getconfig.fs_name, char_ptr, size);

	    target_msg->buffer = (void *) dec_msg;
	    return(0);

	case PVFS_SERV_LOOKUP_PATH:
	    /*print_request( request );*/
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    memcpy( dec_msg, char_ptr, sizeof( struct PVFS_server_req_s ) );
	    char_ptr += sizeof(struct PVFS_server_req_s);
	    size = strlen( char_ptr ) + 1;

	    dec_msg->u.lookup_path.path = (char *) BMI_memalloc(target_addr, size, BMI_RECV_BUFFER);
	    if (dec_msg->u.lookup_path.path == NULL)
	    {
		BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
		return (-ENOMEM);
	    }

	    memcpy(dec_msg->u.lookup_path.path, char_ptr, size);
	    target_msg->buffer = (void *) dec_msg;

	    return(0);

	case PVFS_SERV_CREATEDIRENT:
	    /*print_request( request );*/
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    memcpy( dec_msg, char_ptr, sizeof( struct PVFS_server_req_s ) );
	    char_ptr += sizeof(struct PVFS_server_req_s);
	    size = strlen( char_ptr ) + 1;

	    dec_msg->u.crdirent.name = (char *)BMI_memalloc(target_addr, size, BMI_RECV_BUFFER);
	    if(dec_msg->u.crdirent.name == NULL)
	    {
		BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
		return (-ENOMEM);
	    }

	    memcpy(dec_msg->u.crdirent.name, char_ptr, size);
	    target_msg->buffer = dec_msg;

	    return(0);

	case PVFS_SERV_RMDIRENT:
	    /*print_request( request );*/
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    memcpy( dec_msg, char_ptr, sizeof( struct PVFS_server_req_s ) );
	    char_ptr += sizeof(struct PVFS_server_req_s);
	    size = strlen( char_ptr ) + 1;

	    dec_msg->u.rmdirent.entry = (char *)BMI_memalloc(target_addr, size, BMI_RECV_BUFFER);
	    if(dec_msg->u.rmdirent.entry == NULL)
	    {
		BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
		return (-ENOMEM);
	    }

	    memcpy(dec_msg->u.rmdirent.entry, char_ptr, size);
	    target_msg->buffer = dec_msg;
	    return(0);

	case PVFS_SERV_MKDIR:
	    /*print_request( request );*/
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    memcpy( dec_msg, char_ptr, sizeof( struct PVFS_server_req_s ) );
	    char_ptr += sizeof( struct PVFS_server_req_s );

	    if ( dec_msg->u.mkdir.attr.objtype == ATTR_META )
	    {
		size = dec_msg->u.mkdir.attr.u.meta.nr_datafiles * sizeof(PVFS_handle);
		dec_msg->u.mkdir.attr.u.meta.dfh = (PVFS_handle *)BMI_memalloc(target_addr, size, BMI_RECV_BUFFER);
		if(dec_msg->u.mkdir.attr.u.meta.dfh == NULL)
		{
		    BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
		    return (-ENOMEM);
		}
				/* copy handles at the end of the struct into the new memory */
		memcpy( dec_msg->u.mkdir.attr.u.meta.dfh,
			char_ptr ,
			dec_msg->u.mkdir.attr.u.meta.nr_datafiles * sizeof( PVFS_handle ) );
	    }

	    target_msg->buffer = dec_msg;
	    return(0);

	case PVFS_SERV_SETATTR:
	    /*print_request( request );*/
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }

	    memcpy( dec_msg, char_ptr, sizeof( struct PVFS_server_req_s ) );
	    char_ptr += sizeof(struct PVFS_server_req_s);

	    if ( ((struct PVFS_server_req_s *)dec_msg)->u.setattr.attr.objtype == ATTR_META )
	    {
		PVFS_handle *new_handle = NULL;
		/* probbably shouldn't call this 'size' since I'm putting the # of elements in here */
		size = dec_msg->u.setattr.attr.u.meta.nr_datafiles;

		new_handle = (PVFS_handle *)BMI_memalloc(target_addr, size * sizeof(PVFS_handle), BMI_RECV_BUFFER);
		if(new_handle == NULL)
		{
		    BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
		    return (-ENOMEM);
		}

		/* copy handles at the end of the struct into the new memory */
		memcpy( new_handle, char_ptr, size * sizeof( PVFS_handle ) );
		dec_msg->u.setattr.attr.u.meta.dfh = new_handle;

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
	    dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
	    if (dec_msg == NULL)
	    {
		return (-ENOMEM);
	    }
	    memcpy(dec_msg, char_ptr, sizeof(struct PVFS_server_req_s));
	    target_msg->buffer = dec_msg;
			
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

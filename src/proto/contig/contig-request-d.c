/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>
#include <string.h>
#include <stdlib.h>

DECODE_REQ_HEAD(do_decode_req)
{
	struct PVFS_server_req_s* request = input_buffer;
	void* dec_msg = NULL;
	char* char_ptr = NULL; /* use this to step through buffers and find out where strings are */
	int size = 0, i = 0;
	switch(request->op)
	{
		case PVFS_SERV_GETCONFIG:
			/*print_request( request );*/
			dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
			if (!dec_msg)
			{
				return (-ENOMEM);
			}
			memcpy( dec_msg, (struct PVFS_server_req_s*) request, sizeof( struct PVFS_server_req_s ) );
			char_ptr = ((char *)request + sizeof(struct PVFS_server_req_s));
			size = strlen( char_ptr );
			((struct PVFS_server_req_s *)dec_msg)->u.getconfig.fs_name = (char *)BMI_memalloc(target_addr, size + 1, BMI_RECV_BUFFER);
			if(!((struct PVFS_server_req_s *)dec_msg)->u.getconfig.fs_name)
			{
				BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
				return (-ENOMEM);
			}
			strncpy(((struct PVFS_server_req_s *)dec_msg)->u.getconfig.fs_name, char_ptr, size);
			strncpy(((char*)(((struct PVFS_server_req_s *)dec_msg)->u.getconfig.fs_name) + size), "\0", 1);
			target_msg->buffer = dec_msg;

			return(0);
		case PVFS_SERV_LOOKUP_PATH:
			/*print_request( request );*/
			dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
			if (!dec_msg)
			{
				return (-ENOMEM);
			}
			memcpy( dec_msg, (struct PVFS_server_req_s*) request, sizeof( struct PVFS_server_req_s ) );
			char_ptr = ((char *)request + sizeof(struct PVFS_server_req_s));
			size = strlen( char_ptr );
			((struct PVFS_server_req_s *)dec_msg)->u.lookup_path.path = (char *)BMI_memalloc(target_addr, size + 1, BMI_RECV_BUFFER);
			if(!((struct PVFS_server_req_s *)dec_msg)->u.lookup_path.path)
			{
				BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
				return (-ENOMEM);
			}
			strncpy(((struct PVFS_server_req_s *)dec_msg)->u.lookup_path.path, char_ptr, size);
			strncpy(((char*)(((struct PVFS_server_req_s *)dec_msg)->u.lookup_path.path) + size), "\0", 1);
			target_msg->buffer = dec_msg;

			return(0);
		case PVFS_SERV_CREATEDIRENT:
			/*print_request( request );*/
			dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
			if (!dec_msg)
			{
				return (-ENOMEM);
			}
			memcpy( dec_msg, (struct PVFS_server_req_s*) request, sizeof( struct PVFS_server_req_s ) );
			char_ptr = ((char *)request + sizeof(struct PVFS_server_req_s));
			size = strlen( char_ptr );
			((struct PVFS_server_req_s *)dec_msg)->u.crdirent.name = (char *)BMI_memalloc(target_addr, size + 1, BMI_RECV_BUFFER);
			if(!((struct PVFS_server_req_s *)dec_msg)->u.crdirent.name)
			{
				BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
				return (-ENOMEM);
			}
			strncpy(((struct PVFS_server_req_s *)dec_msg)->u.crdirent.name, char_ptr, size);
			strncpy(((char*)(((struct PVFS_server_req_s *)dec_msg)->u.crdirent.name) + size), "\0", 1);
			target_msg->buffer = dec_msg;

			return(0);
		case PVFS_SERV_RMDIRENT:
			/*print_request( request );*/
			dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
			if (!dec_msg)
			{
				return (-ENOMEM);
			}
			memcpy( dec_msg, (struct PVFS_server_req_s*) request, sizeof( struct PVFS_server_req_s ) );
			char_ptr = ((char *)request + sizeof(struct PVFS_server_req_s));
			size = strlen( char_ptr );
			((struct PVFS_server_req_s *)dec_msg)->u.rmdirent.entry = (char *)BMI_memalloc(target_addr, size + 1, BMI_RECV_BUFFER);
			if(!((struct PVFS_server_req_s *)dec_msg)->u.rmdirent.entry)
			{
				BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
				return (-ENOMEM);
			}
			strncpy(((struct PVFS_server_req_s *)dec_msg)->u.rmdirent.entry, char_ptr, size);
			strncpy(((char*)(((struct PVFS_server_req_s *)dec_msg)->u.rmdirent.entry) + size), "\0", 1);
			target_msg->buffer = dec_msg;

			return(0);
		case PVFS_SERV_MKDIR:
			/*print_request( request );*/
			dec_msg = BMI_memalloc( target_addr, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
			if (!dec_msg)
			{
				return (-ENOMEM);
			}
			memcpy( dec_msg, (struct PVFS_server_req_s*) request, sizeof( struct PVFS_server_req_s ) );

			if ( ((struct PVFS_server_req_s *)dec_msg)->u.mkdir.attr.objtype == ATTR_META )
                        {
                                size = ( ((struct PVFS_server_req_s *)dec_msg)->u.mkdir.attr.u.meta.nr_datafiles) * sizeof(PVFS_handle);
				((struct PVFS_server_req_s *)dec_msg)->u.mkdir.attr.u.meta.dfh = (PVFS_handle *)BMI_memalloc(target_addr, size, BMI_RECV_BUFFER);
				if(!((struct PVFS_server_req_s *)dec_msg)->u.mkdir.attr.u.meta.dfh)
				{
					BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
					return (-ENOMEM);
				}
				/* copy handles at the end of the struct into the new memory */
				memcpy( ((struct PVFS_server_req_s *)dec_msg)->u.mkdir.attr.u.meta.dfh,
					((char*)dec_msg + sizeof( struct PVFS_server_req_s ) ),
                                        ((struct PVFS_server_req_s *)dec_msg)->u.mkdir.attr.u.meta.nr_datafiles * sizeof( PVFS_handle ) );
                        }
			target_msg->buffer = dec_msg;

			return(0);
		case PVFS_SERV_SETATTR:
			/*print_request( request );*/
			dec_msg = BMI_memalloc( target_addr,
					sizeof( struct PVFS_server_req_s ), 
					BMI_RECV_BUFFER );
			if (!dec_msg)
			{
				return (-ENOMEM);
			}
			memcpy( dec_msg, 
				(struct PVFS_server_req_s*) request, 
				sizeof( struct PVFS_server_req_s ) );

			if ( ((struct PVFS_server_req_s *)dec_msg)->u.setattr.attr.objtype == ATTR_META )
                        {
				PVFS_handle *myhandle = NULL;
				PVFS_handle *handle_ptr = NULL;
				/* probbably shouldn't call this 'size' since I'm putting the # of elements in here */
                                size = ( ((struct PVFS_server_req_s *)dec_msg)->u.setattr.attr.u.meta.nr_datafiles);
				myhandle = (PVFS_handle *)BMI_memalloc(target_addr, size * sizeof(PVFS_handle), BMI_RECV_BUFFER);
				if(!myhandle)
				{
					BMI_memfree( target_addr, dec_msg, sizeof( struct PVFS_server_req_s ), BMI_RECV_BUFFER );
					return (-ENOMEM);
				}
				/* copy handles at the end of the struct into the new memory */
				handle_ptr = (PVFS_handle*)((char*)dec_msg + sizeof( struct PVFS_server_req_s ));
				for(i = 0; i < size; i++)
				{
					myhandle[i] = handle_ptr[i];
				}
				/*memcpy( myhandle,
					((char*)dec_msg + sizeof( struct PVFS_server_req_s ) ),
                                        size * sizeof( PVFS_handle ) );*/

				((struct PVFS_server_req_s *)dec_msg)->u.setattr.attr.u.meta.dfh = myhandle;
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
			if (!dec_msg)
                        {
                                return (-ENOMEM);
                        }
			memcpy( dec_msg, (struct PVFS_server_req_s*) request, sizeof( struct PVFS_server_req_s ) );
			target_msg->buffer = dec_msg;
			
			return(0);
		case PVFS_SERV_IO: /*haven't been implemented yet*/
                case PVFS_SERV_IOSTATFS:
                case PVFS_SERV_GETDIST:
                case PVFS_SERV_REVLOOKUP:
		default:
			printf("Unpacking Req Op: %d Not Supported\n",request->op);
			return -1;
	}
	/* should never get to this point */
	return -1;
}

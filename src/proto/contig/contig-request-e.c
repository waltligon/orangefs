/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>
#include <string.h>
#include <stdio.h>

ENCODE_REQ_HEAD(do_encode_req)
{
	void* enc_msg;
	int size = 0, name_sz = 0, i = 0;
	/* we're expecting target_msg to already be a valid pointer*/
	target_msg->size_list = malloc(sizeof(PVFS_size));
	if (!target_msg)
	{
		return (-EINVAL);
	}
	target_msg->list_count = 1;
	target_msg->buffer_flag = 0;
	switch( request->op )
	{
		case PVFS_SERV_GETCONFIG:
			if (!request->u.getconfig.fs_name)
			{
				printf("invalid string passed\n");
				return (-EINVAL);
			}
			printf("geting string len\n");
			name_sz = strlen( request->u.getconfig.fs_name );
			size = sizeof( struct PVFS_server_req_s ) + name_sz + 1;
			printf("total space: %d str len: %d header: %d struct: %d\n", size, name_sz, header_size, sizeof(struct PVFS_server_req_s));
			enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t) (size + header_size), BMI_SEND_BUFFER );
			if (!enc_msg)
			{
				printf("unable to malloc = %d bytes\n", size);
				return (-ENOMEM);
			}
			target_msg->buffer_list = (void*) malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

			printf("copying strings now\n");
        		/* copy a null terminated string to another */
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ), request->u.getconfig.fs_name, (size_t) name_sz);
			printf("copying terminator\n");
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ) + name_sz , "\0", 1 );

			/* make pointer since we're sending it over the wire and don't want 
			 * random memory referenced on the other side */

			printf("updating pointer\n");
			((struct PVFS_server_req_s *)enc_msg)->u.getconfig.fs_name = NULL;
			printf("done\n");
			return (0);

		case PVFS_SERV_LOOKUP_PATH:
			if (!request->u.lookup_path.path)
			{
				return (-EINVAL);
			}
			name_sz = strlen( request->u.lookup_path.path );
			size = sizeof( struct PVFS_server_req_s ) + name_sz + 1;
                	enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );
			if (!enc_msg)
			{
				return (-ENOMEM);
			}
			target_msg->buffer_list = (void*) malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

        		/* copy a null terminated string to another */
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ), request->u.lookup_path.path, (size_t)name_sz);
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ) + name_sz , "\0", 1 );

			/* make pointer since we're sending it over the wire and don't want 
			 * random memory referenced on the other side */

			((struct PVFS_server_req_s *)enc_msg)->u.lookup_path.path = NULL;
			return (0);

		case PVFS_SERV_CREATEDIRENT:
			if (!request->u.crdirent.name)
			{
				return (-EINVAL);
			}
			name_sz = strlen( request->u.crdirent.name );
			size = sizeof( struct PVFS_server_req_s ) + name_sz + 1;
                	enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );
			if (!enc_msg)
			{
				return (-ENOMEM);
			}
			target_msg->buffer_list = (void*) malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

        		/* copy a null terminated string to another */
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ), request->u.crdirent.name, (size_t)name_sz);
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ) + name_sz , "\0", 1 );

			/* make pointer since we're sending it over the wire and don't want 
			 * random memory referenced on the other side */

			((struct PVFS_server_req_s *)enc_msg)->u.crdirent.name = NULL;
			return (0);

		case PVFS_SERV_RMDIRENT:
			if (!request->u.rmdirent.entry)
			{
				return (-EINVAL);
			}
			name_sz = strlen( request->u.rmdirent.entry );
			size = sizeof( struct PVFS_server_req_s ) + name_sz + 1;
                	enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );
			if (!enc_msg)
			{
				return (-ENOMEM);
			}
			target_msg->buffer_list = (void*) malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

        		/* copy a null terminated string to another */
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ), request->u.rmdirent.entry, (size_t)name_sz);
        		strncpy((char *)enc_msg + sizeof( struct PVFS_server_req_s ) + name_sz , "\0", 1 );

			/* make pointer since we're sending it over the wire and don't want 
			 * random memory referenced on the other side */

			((struct PVFS_server_req_s *)enc_msg)->u.rmdirent.entry = NULL;
			return (0);

		case PVFS_SERV_MKDIR:
			size = sizeof( struct PVFS_server_req_s ) + sizeof( struct PVFS_object_attr );

			/* if we're mkdir'ing a meta file, we need to alloc space for the attributes */
			if ( request->u.mkdir.attr.objtype == ATTR_META )
			{
				size += ((request->u.mkdir.attr.u.meta.nr_datafiles) * sizeof( PVFS_handle ));
			}

			/* TODO: come back and alloc the right spaces for 
			 * distributions cause they're going to change */

                	enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );
			if (!enc_msg)
			{
				return (-ENOMEM);
			}
			target_msg->buffer_list = (void*) malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

			/* throw handles at the end for metadata files */
			if ( request->u.mkdir.attr.objtype == ATTR_META )
			{
				/* handles */
				memcpy( ((char*)enc_msg + sizeof( struct PVFS_server_req_s )),
					request->u.mkdir.attr.u.meta.dfh, 
					request->u.mkdir.attr.u.meta.nr_datafiles * sizeof( PVFS_handle ) );

			        /* make pointer since we're sending it over the wire and don't want 
			         * random memory referenced on the other side */

				((struct PVFS_server_req_s *)enc_msg)->u.mkdir.attr.u.meta.dfh = NULL;
			}
			return (0);

		case PVFS_SERV_SETATTR:
			size = sizeof( struct PVFS_server_req_s ) + sizeof( struct PVFS_object_attr );

			/* if we're mkdir'ing a meta file, we need to alloc space for the attributes */
			if ( request->u.setattr.attr.objtype == ATTR_META )
			{
				/* negative datafiles? wtf ... */
				if (request->u.setattr.attr.u.meta.nr_datafiles < 0)
				{
					return (-EINVAL);
				}
				size += ((request->u.setattr.attr.u.meta.nr_datafiles) * sizeof( PVFS_handle ));
			}

			/* TODO: come back and alloc the right spaces for 
			 * distributions and eattribs cause they're going to change */

                	enc_msg = BMI_memalloc( target_msg->dest, (bmi_size_t)(size + header_size), BMI_SEND_BUFFER );
			if (!enc_msg)
			{
				return (-ENOMEM);
			}
			target_msg->buffer_list = (void*) malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );

			/* throw handles at the end for metadata files */
			if ( request->u.setattr.attr.objtype == ATTR_META )
			{
				PVFS_handle *h_ptr = (PVFS_handle*)((char*)enc_msg + sizeof(struct PVFS_server_req_s));
				/* handles */
				for(i = 0; i < request->u.setattr.attr.u.meta.nr_datafiles; i++)
				{
					h_ptr[i] = request->u.setattr.attr.u.meta.dfh[i];
				}
				/*memcpy( ((char*)enc_msg + sizeof( struct PVFS_server_req_s ) ),
					request->u.setattr.attr.u.meta.dfh, 
					request->u.setattr.attr.u.meta.nr_datafiles * sizeof( PVFS_handle ) );*/

				/* make pointer NULL since we're sending it over the wire and don't want 
				 * random memory referenced on the other side */

				((struct PVFS_server_req_s *)enc_msg)->u.setattr.attr.u.meta.dfh = NULL;
			}
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
			if (!enc_msg)
			{
				return (-ENOMEM);
			}
			memcpy( enc_msg, request, sizeof( struct PVFS_server_req_s ) );
			target_msg->buffer_list = (void*)malloc(sizeof(void *));
			target_msg->buffer_list[0] = enc_msg;
			target_msg->size_list[0] = size;
			target_msg->total_size = size;
			return (0);

		case PVFS_SERV_IO: /*haven't been implemented yet*/
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

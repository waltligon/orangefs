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
#include <string.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>

/* do_encode_resp()
 * 
 * encodes a response structure to be
 * sent over the network
 * 
 * returns 0 on success, -ERRNO on failure
 */

ENCODE_RESP_HEAD(do_encode_resp)
	{
		int i=0;
		target_msg->size_list = malloc(sizeof(PVFS_size));
		target_msg->list_count=1;
		target_msg->buffer_list = malloc(sizeof(void *));
		switch(response->op)
		{
			case PVFS_SERV_CREATE:
			case PVFS_SERV_MKDIR:
			case PVFS_SERV_RMDIRENT:
				/* 
				 *	There is an int64_t here...
				 *	but handled correctly so far!
				 */
				target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest, 
																		sizeof(struct PVFS_server_resp_s)+
																		header_size,
																		BMI_SEND_BUFFER);
				memcpy(target_msg->buffer_list[0],response,sizeof(struct PVFS_server_resp_s));
				target_msg->size_list[0] = 
						target_msg->total_size = sizeof(struct PVFS_server_resp_s);
				return(0);

			case PVFS_SERV_SETATTR:
			case PVFS_SERV_REMOVE:
			case PVFS_SERV_RMDIR:
			case PVFS_SERV_CREATEDIRENT:
				/* 
				 *	There is no response struct for
				 *	these requests.  Therefore
				 *	we should not call it.
				 * TODO: Are we still using Generic ACK? dw 07.01.03
				 */
				//free(target_msg->buffer_list);
				return -1;

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
				target_msg->buffer_list[0] = BMI_memalloc(target_msg->dest, 
																		sizeof(struct PVFS_server_resp_s)+
																		header_size,
																		BMI_SEND_BUFFER);
				target_msg->size_list[0] = 
						target_msg->total_size = sizeof(struct PVFS_server_resp_s);
				// else pack it!
				return(0);

			case PVFS_SERV_GETCONFIG:

				target_msg->buffer_list[0] = 
					BMI_memalloc(target_msg->dest,
				  					 (target_msg->size_list[0] = 
									  		sizeof(struct PVFS_server_resp_s)+
									 		strlen(response->u.getconfig.meta_server_mapping)+
									 		strlen(response->u.getconfig.io_server_mapping)+2)+
											header_size,
									 BMI_SEND_BUFFER);

				memcpy(target_msg->buffer_list[0],
						 response,
						 sizeof(struct PVFS_server_resp_s));

				strcpy(((char *)target_msg->buffer_list[0])+
							sizeof(struct PVFS_server_resp_s),
						 response->u.getconfig.meta_server_mapping);

				strcpy(((char *)target_msg->buffer_list[0])+
						    sizeof(struct PVFS_server_resp_s)+
							 strlen(response->u.getconfig.meta_server_mapping)+1,
							 response->u.getconfig.io_server_mapping);

				strcpy(((char *)target_msg->buffer_list[0])+
							 target_msg->size_list[0]-1,
							 "\0");
				target_msg->total_size=target_msg->size_list[0];
				return(0);
				
			case PVFS_SERV_LOOKUP_PATH:

				target_msg->buffer_list[0] = 
					BMI_memalloc(target_msg->dest,
									 (target_msg->size_list[0] = 
									  	sizeof(struct PVFS_server_resp_s)+
									 	response->u.lookup_path.count*sizeof(PVFS_handle)+
									 	response->u.lookup_path.count*sizeof(PVFS_object_attr))+
										header_size,
									 BMI_SEND_BUFFER);

				memcpy(target_msg->buffer_list[0],
						 response,
						 sizeof(struct PVFS_server_resp_s));

				while(i++ < response->u.lookup_path.count)
				{
					// Wonder if Memcpy is correct here.	
					memcpy((target_msg->buffer_list[0]+
							 	sizeof(struct PVFS_server_resp_s)+
							 	(i-1)*sizeof(PVFS_handle)),
							 &(response->u.lookup_path.handle_array[i-1]),
							 sizeof(PVFS_handle));

					memcpy((target_msg->buffer_list[0]+
							 		sizeof(struct PVFS_server_resp_s)+
								 	response->u.lookup_path.count*sizeof(PVFS_handle)+
						 			(i-1)*sizeof(PVFS_object_attr)),
							 &(response->u.lookup_path.attr_array[i-1]),
							 sizeof(PVFS_object_attr));
				}
				target_msg->total_size=target_msg->size_list[0];
				return(0);

			case PVFS_SERV_READDIR:
				target_msg->buffer_list[0] = 
					BMI_memalloc(target_msg->dest,
								 (target_msg->size_list[0] = 
								  	sizeof(struct PVFS_server_resp_s)+
								 	response->u.readdir.pvfs_dirent_count*sizeof(PVFS_dirent))+
									header_size,
								 BMI_SEND_BUFFER);
				memcpy(target_msg->buffer_list[0],
						 response,
						 sizeof(struct PVFS_server_resp_s));
				i=0;  // just in case
				while(i++ < response->u.lookup_path.count)
				{
						
					memcpy(((char*)target_msg->buffer_list[0])+
							 	sizeof(struct PVFS_server_resp_s)+
							 	(i-1)*sizeof(PVFS_dirent),
							 &(response->u.readdir.pvfs_dirent_array[i-1]),
							 sizeof(PVFS_dirent));
				}
				target_msg->total_size = target_msg->size_list[0];
				return 0;

			default:
				return -1;
		}
	return -EINVAL;
}

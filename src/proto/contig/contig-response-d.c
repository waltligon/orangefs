/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>

DECODE_RESP_HEAD(do_decode_resp)
{

	struct PVFS_server_resp_s *response = input_buffer;

	target_msg->buffer = input_buffer;
	switch(response->op)
	{
		case PVFS_SERV_CREATE:
			//memcpy(target_msg->buffer,response,sizeof(struct PVFS_server_resp_s));
			return 0;
		case PVFS_SERV_GETCONFIG:
			response->u.getconfig.meta_server_mapping = (char *)input_buffer 
											+ sizeof(struct PVFS_server_resp_s);
			response->u.getconfig.io_server_mapping = (char *)input_buffer 
											+ sizeof(struct PVFS_server_resp_s)
											+ strlen(response->u.getconfig.meta_server_mapping)+1;
			return 0;
		case PVFS_SERV_LOOKUP_PATH:
			response->u.lookup_path.handle_array = 
										(PVFS_handle *) ((char*)input_buffer + 
									  sizeof(struct PVFS_server_resp_s));
			response->u.lookup_path.attr_array = 
						(PVFS_object_attr *) ((char*)input_buffer 
						+ sizeof(struct PVFS_server_resp_s) + 
						sizeof(PVFS_handle)*response->u.lookup_path.count);
			return 0;

		default:
	}
	return -1;
}

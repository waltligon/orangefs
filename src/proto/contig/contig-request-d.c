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
#include "pint-request.h"
#include "pint-distribution.h"

int do_decode_req(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr)
{
    struct PVFS_server_req *dec_msg = NULL;
    char *char_ptr = (char *) input_buffer;
    int size = 0;
    int tmp_count;
    int ret = -1;

    size = input_size;

    if (size <= 0)
    {
	/*we called decode on a null buffer?? */
	return (-EINVAL);
    }
    dec_msg = malloc(size);
    if (dec_msg == NULL)
    {
	return (-ENOMEM);
    }
    memcpy(dec_msg, char_ptr, size);
    target_msg->buffer = dec_msg;

    switch (((struct PVFS_server_req *) char_ptr)->op)
    {
    case PVFS_SERV_LOOKUP_PATH:
	char_ptr += sizeof(struct PVFS_server_req);
	dec_msg->u.lookup_path.path = char_ptr;
	return (0);

    case PVFS_SERV_CREATEDIRENT:
	char_ptr += sizeof(struct PVFS_server_req);
	dec_msg->u.crdirent.name = char_ptr;
	return (0);

    case PVFS_SERV_RMDIRENT:
	char_ptr += sizeof(struct PVFS_server_req);
	dec_msg->u.rmdirent.entry = char_ptr;
	return (0);

    case PVFS_SERV_CREATE:
	char_ptr += sizeof(struct PVFS_server_req);
        dec_msg->u.create.handle_extent_array.extent_count =
            *((uint32_t *)char_ptr);
        dec_msg->u.create.handle_extent_array.extent_array =
            (PVFS_handle_extent *)((char *)char_ptr + sizeof(uint32_t));
        return (0);

    case PVFS_SERV_MKDIR:
	char_ptr += sizeof(struct PVFS_server_req);

        dec_msg->u.mkdir.handle_extent_array.extent_count =
            *((uint32_t *)char_ptr);
        char_ptr += sizeof(uint32_t);

        dec_msg->u.mkdir.handle_extent_array.extent_array =
            (PVFS_handle_extent *)char_ptr;
        char_ptr += (dec_msg->u.mkdir.handle_extent_array.extent_count *
                     sizeof(PVFS_handle_extent));

	if (dec_msg->u.mkdir.attr.objtype == PVFS_TYPE_METAFILE)
	{
	    dec_msg->u.mkdir.attr.u.meta.dfile_array = (PVFS_handle *) char_ptr;
	}
	return (0);

    case PVFS_SERV_SETATTR:
	char_ptr += sizeof(struct PVFS_server_req);

	if (dec_msg->u.setattr.attr.objtype == PVFS_TYPE_METAFILE)
	{
            if (dec_msg->u.setattr.attr.mask & PVFS_ATTR_META_DFILES)
            {
                dec_msg->u.setattr.attr.u.meta.dfile_array =
                    (PVFS_handle *) char_ptr;

                /* move the pointer past the data files, we could have
                 * distribution information, check the size to be sure
                 */
                char_ptr += dec_msg->u.setattr.attr.u.meta.dfile_count
                    * sizeof(PVFS_handle);
            }

            if (dec_msg->u.setattr.attr.mask & PVFS_ATTR_META_DIST)
            {
                dec_msg->u.setattr.attr.u.meta.dist = (PVFS_Dist *) char_ptr;
                PINT_Dist_decode(dec_msg->u.setattr.attr.u.meta.dist, NULL);
            }
	}
	return (0);

    case PVFS_SERV_IO:
	/* set pointers to the request and dist information */
	tmp_count = *(int *) (char_ptr + sizeof(struct PVFS_server_req));
	char_ptr += sizeof(struct PVFS_server_req) + 2 * sizeof(int);
	dec_msg->u.io.io_req = (PVFS_Request) char_ptr;
	char_ptr += tmp_count;
	dec_msg->u.io.io_dist = (PVFS_Dist *) char_ptr;
	/* decode the request and dist */
	ret = PINT_Request_decode(dec_msg->u.io.io_req);
	if (ret < 0)
	{
	    free(dec_msg);
	    return (ret);
	}
	PINT_Dist_decode(dec_msg->u.io.io_dist, NULL);
	ret = PINT_Dist_lookup(dec_msg->u.io.io_dist);
	if (ret < 0)
	{
	    free(dec_msg);
	    return (ret);
	}
	return (0);
	/*these structures are all self contained (no pointers that need to be packed) */
    case PVFS_SERV_READDIR:
    case PVFS_SERV_GETATTR:
    case PVFS_SERV_REMOVE:
    case PVFS_SERV_TRUNCATE:
    case PVFS_SERV_GETCONFIG:
	return (0);
    default:
	printf("Unpacking Req Op: %d Not Supported\n",
	       ((struct PVFS_server_req *) char_ptr)->op);
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

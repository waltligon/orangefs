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
#include "pint-distribution.h"

extern PINT_encoding_table_values contig_buffer_table;

/* do_encode_resp()
 * 
 * encodes a response structure to be
 * sent over the network
 * 
 * returns 0 on success, -ERRNO on failure
 */

int do_encode_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg)
{
    char* tmp_ptr = NULL;

    /* TODO: CHECK RETURN VALUES */

    switch (response->op)
    {
	/* these first response types have no trailing data */
    case PVFS_SERV_FLUSH:
    case PVFS_SERV_CREATE:
    case PVFS_SERV_MKDIR:
    case PVFS_SERV_RMDIRENT:
    case PVFS_SERV_IO:
    case PVFS_SERV_SETATTR:
    case PVFS_SERV_REMOVE:
    case PVFS_SERV_CRDIRENT:
    case PVFS_SERV_WRITE_COMPLETION:
    case PVFS_SERV_MGMT_SETPARAM:
    case PVFS_SERV_TRUNCATE:
    case PVFS_SERV_MGMT_NOOP:
    case PVFS_SERV_STATFS:
	/* 
	 *  There is an int64_t here...
	 *  but handled correctly so far!
	 */
	target_msg->size_list = malloc(sizeof(PVFS_size));
	target_msg->buffer_list = malloc(sizeof(void *));
	target_msg->list_count = 1;
	target_msg->buffer_type = BMI_PRE_ALLOC;
	target_msg->size_list[0] = target_msg->total_size
	    = sizeof(struct PVFS_server_resp) + PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->buffer_list[0]
	    = BMI_memalloc(target_msg->dest,
			   sizeof(struct PVFS_server_resp) + PINT_ENC_GENERIC_HEADER_SIZE,
			   BMI_SEND);
	tmp_ptr = target_msg->buffer_list[0];
	memcpy(tmp_ptr, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	tmp_ptr += PINT_ENC_GENERIC_HEADER_SIZE;
	memcpy(tmp_ptr,
	       response, sizeof(struct PVFS_server_resp));
	return (0);

    case PVFS_SERV_GETATTR:
	/* 
	 *  Ok... here is the trick... 
	 *  We need to pack the struct,
	 *  then based on object type,
	 *  store the associated specific
	 *  attribs.
	 *  7.1.03 This is only true for 
	 *  PVFS_metafile_attr.  DW
	 */

	/* This code is only good when there are no trailing pointers */
	target_msg->size_list = malloc(sizeof(PVFS_size));
	target_msg->buffer_list = malloc(sizeof(void *));
	target_msg->list_count = 1;
	target_msg->buffer_type = BMI_PRE_ALLOC;

	/* we may need to pack trailing data for metafiles */
	if (response->u.getattr.attr.objtype == PVFS_TYPE_METAFILE)
	{
            char *pack_dest = NULL;

            target_msg->total_size = sizeof(struct PVFS_server_resp) +
                PINT_ENC_GENERIC_HEADER_SIZE;

            if (response->u.getattr.attr.mask & PVFS_ATTR_META_DFILES)
            {
                target_msg->total_size +=
                    (response->u.getattr.attr.u.meta.dfile_count
                     * sizeof(PVFS_handle));
            }

            if (response->u.getattr.attr.mask & PVFS_ATTR_META_DIST)
            {
                target_msg->total_size +=
                    response->u.getattr.attr.u.meta.dist_size;
            }

            target_msg->size_list[0] = target_msg->total_size;
            target_msg->buffer_list[0] =
                BMI_memalloc(target_msg->dest,
                             target_msg->total_size, BMI_SEND);

            pack_dest = (char *) target_msg->buffer_list[0]
                + PINT_ENC_GENERIC_HEADER_SIZE;

            if (response->u.getattr.attr.mask & PVFS_ATTR_META_DFILES)
            {
                /* copy in the datafiles */
                if (response->u.getattr.attr.u.meta.dfile_count > 0)
                {
                    pack_dest += sizeof(struct PVFS_server_resp);
                    memcpy(pack_dest,
                           response->u.getattr.attr.u.meta.dfile_array,
                           (response->u.getattr.attr.u.meta.dfile_count
                            * sizeof(PVFS_handle)));
                }
            }
            if (response->u.getattr.attr.mask & PVFS_ATTR_META_DIST)
            {
                /* copy in the distribution */
                if (response->u.getattr.attr.u.meta.dist_size > 0)
                {
                    pack_dest +=
                        (response->u.getattr.attr.u.meta.dfile_count
                         * sizeof(PVFS_handle));
                    PINT_Dist_encode(pack_dest,
                                     response->u.getattr.attr.u.meta.dist);
                }
            }
	}
        else if (response->u.getattr.attr.objtype == PVFS_TYPE_SYMLINK)
        {
            char *pack_dest = NULL;

            target_msg->total_size = sizeof(struct PVFS_server_resp) +
                PINT_ENC_GENERIC_HEADER_SIZE;

            if (response->u.getattr.attr.mask & PVFS_ATTR_SYMLNK_TARGET)
            {
                target_msg->total_size +=
                    sizeof(response->u.getattr.attr.u.sym.target_path_len);
                target_msg->total_size +=
                    response->u.getattr.attr.u.sym.target_path_len;
            }

            target_msg->size_list[0] = target_msg->total_size;
            target_msg->buffer_list[0] =
                BMI_memalloc(target_msg->dest,
                             target_msg->total_size, BMI_SEND);

            pack_dest = (char *) target_msg->buffer_list[0]
                + PINT_ENC_GENERIC_HEADER_SIZE;

            if (response->u.getattr.attr.mask & PVFS_ATTR_SYMLNK_TARGET)
            {
                /* copy in the symlink target path len and path */
                assert(response->u.getattr.attr.u.sym.target_path_len > 0);

                pack_dest += sizeof(struct PVFS_server_resp);
                *((uint32_t *)pack_dest) =
                    response->u.getattr.attr.u.sym.target_path_len;
                pack_dest += sizeof(uint32_t);
                memcpy(pack_dest,
                       response->u.getattr.attr.u.sym.target_path,
                       response->u.getattr.attr.u.sym.target_path_len);
                pack_dest +=
                    response->u.getattr.attr.u.sym.target_path_len;
            }
        }
	else
	{
	    target_msg->size_list[0] =
		target_msg->total_size =
		sizeof(struct PVFS_server_resp) + PINT_ENC_GENERIC_HEADER_SIZE;
	    target_msg->buffer_list[0] =
		BMI_memalloc(target_msg->dest,
			     sizeof(struct PVFS_server_resp) + 
			     PINT_ENC_GENERIC_HEADER_SIZE,
			     BMI_SEND);
	}

	/* in either case, pack the basic ack in */
	tmp_ptr = target_msg->buffer_list[0];
	memcpy(tmp_ptr, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	tmp_ptr += PINT_ENC_GENERIC_HEADER_SIZE;
	memcpy(tmp_ptr,
	       response, sizeof(struct PVFS_server_resp));

	return (0);

    case PVFS_SERV_GETCONFIG:
	/* assert on all strings (at least for now) */
	if (response->status == 0)
	{
	    assert(response->u.getconfig.fs_config_buf != NULL);
	    assert(response->u.getconfig.server_config_buf != NULL);

	    target_msg->size_list = malloc(4 * sizeof(PVFS_size));
	    target_msg->buffer_list = malloc(4 * sizeof(void *));
	    target_msg->list_count = 4;
	    target_msg->buffer_type = BMI_EXT_ALLOC;

	    target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	    target_msg->size_list[2] = response->u.getconfig.fs_config_buf_size;
	    target_msg->size_list[3] =
		response->u.getconfig.server_config_buf_size;
	    target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	    target_msg->total_size = target_msg->size_list[0] +
		target_msg->size_list[1] +
		target_msg->size_list[2] +
		target_msg->size_list[3];

	    target_msg->buffer_list[1] = response;
	    target_msg->buffer_list[2] = response->u.getconfig.fs_config_buf;
	    target_msg->buffer_list[3] =
		response->u.getconfig.server_config_buf;
	    target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	}
	else
	{
	    target_msg->size_list = malloc(2*sizeof(PVFS_size));
	    target_msg->buffer_list = malloc(2*sizeof(void *));
	    target_msg->list_count = 2;
	    target_msg->buffer_type = BMI_EXT_ALLOC;
	    target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	    target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	    target_msg->buffer_list[1] = response;
	    target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	    target_msg->total_size = target_msg->size_list[0] +
		target_msg->size_list[1];
	}
	return (0);

    case PVFS_SERV_LOOKUP_PATH:
	assert(response->u.lookup_path.handle_array != NULL);

	target_msg->size_list = malloc(4 * sizeof(PVFS_size));
	target_msg->buffer_list = malloc(4 * sizeof(void *));
	target_msg->list_count = 4;
	target_msg->buffer_type = BMI_EXT_ALLOC;

	if (response->status != 0)
	    target_msg->list_count = 2;

	target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	target_msg->size_list[2] =
	    response->u.lookup_path.handle_count * sizeof(PVFS_handle);
	target_msg->size_list[3] =
	    response->u.lookup_path.attr_count * sizeof(PVFS_object_attr);
	target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->total_size = target_msg->size_list[0]
	    + target_msg->size_list[1] + target_msg->size_list[2] + 
	    target_msg->size_list[3];

	target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	target_msg->buffer_list[1] = response;
	target_msg->buffer_list[2] = response->u.lookup_path.handle_array;
	target_msg->buffer_list[3] = response->u.lookup_path.attr_array;
	return (0);

    case PVFS_SERV_READDIR:

	target_msg->size_list = malloc(3 * sizeof(PVFS_size));
	target_msg->buffer_list = malloc(3 * sizeof(void *));
	target_msg->list_count = 3;
	target_msg->buffer_type = BMI_EXT_ALLOC;

	/* If there was an error, we need to not send any dirents */
	if (response->status != 0)
	    target_msg->list_count = 2;

	target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	target_msg->size_list[2] = response->u.readdir.dirent_count
	    * sizeof(PVFS_dirent);
	target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->total_size =
	    target_msg->size_list[0] + target_msg->size_list[1] + 
	    target_msg->size_list[2];

	target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	target_msg->buffer_list[1] = response;
	target_msg->buffer_list[2] = response->u.readdir.dirent_array;

	return 0;
    case PVFS_SERV_MGMT_PERF_MON:

	target_msg->size_list = malloc(3 * sizeof(PVFS_size));
	target_msg->buffer_list = malloc(3 * sizeof(void *));
	target_msg->list_count = 3;
	target_msg->buffer_type = BMI_EXT_ALLOC;

	/* If there was an error, we need to not send any trailing data */
	if (response->status != 0)
	    target_msg->list_count = 2;

	target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	target_msg->size_list[2] = response->u.mgmt_perf_mon.perf_array_count
	    * sizeof(struct PVFS_mgmt_perf_stat);
	target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->total_size =
	    target_msg->size_list[0] + target_msg->size_list[1] + 
	    target_msg->size_list[2];

	target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	target_msg->buffer_list[1] = response;
	target_msg->buffer_list[2] = response->u.mgmt_perf_mon.perf_array;
	return 0;
    case PVFS_SERV_MGMT_EVENT_MON:

	target_msg->size_list = malloc(3 * sizeof(PVFS_size));
	target_msg->buffer_list = malloc(3 * sizeof(void *));
	target_msg->list_count = 3;
	target_msg->buffer_type = BMI_EXT_ALLOC;

	/* If there was an error, we need to not send any trailing data */
	if (response->status != 0)
	    target_msg->list_count = 2;

	target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	target_msg->size_list[2] = response->u.mgmt_event_mon.event_count
	    * sizeof(struct PVFS_mgmt_event);
	target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->total_size =
	    target_msg->size_list[0] + target_msg->size_list[1] + 
	    target_msg->size_list[2];

	target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	target_msg->buffer_list[1] = response;
	target_msg->buffer_list[2] = response->u.mgmt_event_mon.event_array;
	return 0;

    case PVFS_SERV_MGMT_ITERATE_HANDLES:

	target_msg->size_list = malloc(3 * sizeof(PVFS_size));
	target_msg->buffer_list = malloc(3 * sizeof(void *));
	target_msg->list_count = 3;
	target_msg->buffer_type = BMI_EXT_ALLOC;

	/* If there was an error, we need to not send any trailing data */
	if (response->status != 0)
	    target_msg->list_count = 2;

	target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	target_msg->size_list[2] = response->u.mgmt_iterate_handles.handle_count
	    * sizeof(PVFS_handle);
	target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->total_size =
	    target_msg->size_list[0] + target_msg->size_list[1] + 
	    target_msg->size_list[2];

	target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	target_msg->buffer_list[1] = response;
	target_msg->buffer_list[2] = response->u.mgmt_iterate_handles.handle_array;
	return 0;
    case PVFS_SERV_MGMT_DSPACE_INFO_LIST:

	target_msg->size_list = malloc(3 * sizeof(PVFS_size));
	target_msg->buffer_list = malloc(3 * sizeof(void *));
	target_msg->list_count = 3;
	target_msg->buffer_type = BMI_EXT_ALLOC;

	/* If there was an error, we need to not send any trailing data */
	if (response->status != 0)
	    target_msg->list_count = 2;

	target_msg->size_list[1] = sizeof(struct PVFS_server_resp);
	target_msg->size_list[2] = response->u.mgmt_dspace_info_list.dspace_info_count
	    * sizeof(struct PVFS_mgmt_dspace_info);
	target_msg->size_list[0] = PINT_ENC_GENERIC_HEADER_SIZE;
	target_msg->total_size =
	    target_msg->size_list[0] + target_msg->size_list[1] + 
	    target_msg->size_list[2];

	target_msg->buffer_list[0] = contig_buffer_table.generic_header;
	target_msg->buffer_list[1] = response;
	target_msg->buffer_list[2] = response->u.mgmt_dspace_info_list.dspace_info_array;
	return 0;

    /* invalid responses: */
    case PVFS_SERV_INVALID:
    case PVFS_SERV_PERF_UPDATE:
    case PVFS_SERV_JOB_TIMER:
	assert(0);
	gossip_lerr("Error: response type %d is invalid.\n",
	    (int)response->op);
	return(-ENOSYS);
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

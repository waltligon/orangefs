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
#include "pint-distribution.h"
#include "pint-request.h"

extern PINT_encoding_table_values contig_buffer_table;

int do_encode_req(
    struct PVFS_server_req *request,
    struct PINT_encoded_msg *target_msg)
{
    void *enc_msg;
    bmi_size_t size = 0, name_sz = 0;
    PINT_Request *encode_file_req = NULL;
    PVFS_Dist *encode_io_dist = NULL;
    int ret = -1;

    /* all the messages that we build in this function are one contig. block */
    /* TODO: USE ONE MALLOC() INSTEAD OF TWO */
    target_msg->size_list = malloc(sizeof(PVFS_size));
    target_msg->buffer_list = (void *) malloc(sizeof(void *));
    if (!target_msg)
    {
	return -EINVAL;
    }
    target_msg->list_count = 1;
    target_msg->buffer_type = BMI_EXT_ALLOC;

    if(request->op == PVFS_SERV_MGMT_ITERATE_HANDLES)
	assert(request->u.mgmt_iterate_handles.handle_count
	    <= PVFS_REQ_LIMIT_MGMT_ITERATE_HANDLES_COUNT);
    if(request->op == PVFS_SERV_MGMT_PERF_MON)
	assert(request->u.mgmt_perf_mon.count
	    <= PVFS_REQ_LIMIT_MGMT_PERF_MON_COUNT);
    if(request->op == PVFS_SERV_MGMT_EVENT_MON)
	assert(request->u.mgmt_event_mon.event_count
	    <= PVFS_REQ_LIMIT_MGMT_EVENT_MON_COUNT);

    switch (request->op)
    {
    case PVFS_SERV_GETCONFIG:
	size = sizeof(struct PVFS_server_req) + PINT_ENC_GENERIC_HEADER_SIZE;
	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);

	/* here the right thing to do is to return the error code. */
	if (enc_msg == NULL)
	{
	    return -ENOMEM;
	}
	/* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));
	return 0;

	/* END NEW VERSION */

    case PVFS_SERV_LOOKUP_PATH:
	assert(request->u.lookup_path.path != NULL);

	name_sz = strlen(request->u.lookup_path.path) + 1;	/* include NULL terminator in size */
	size = sizeof(struct PVFS_server_req) + PINT_ENC_GENERIC_HEADER_SIZE
	    + name_sz;
	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);

	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}
	/* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));

	/* copy a null terminated string to another */
	memcpy(enc_msg + sizeof(struct PVFS_server_req),
	       request->u.lookup_path.path, name_sz);

	/* make pointer since we're sending it over the wire and don't want 
	 * random memory referenced on the other side */

	((struct PVFS_server_req *) enc_msg)->u.lookup_path.path = NULL;
	return (0);

    case PVFS_SERV_CRDIRENT:
	assert(request->u.crdirent.name != NULL);

	name_sz = strlen(request->u.crdirent.name) + 1;	/* include NULL terminator in size */
	size = sizeof(struct PVFS_server_req) + PINT_ENC_GENERIC_HEADER_SIZE 
	    + name_sz;
	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);

	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}
	/* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));

	/* copy a null terminated string to another */
	memcpy(enc_msg + sizeof(struct PVFS_server_req),
	       request->u.crdirent.name, name_sz);

	/* make pointer since we're sending it over the wire and don't want 
	 * random memory referenced on the other side */

	((struct PVFS_server_req *) enc_msg)->u.crdirent.name = NULL;
	return (0);

    case PVFS_SERV_MGMT_DSPACE_INFO_LIST:

	size = sizeof(struct PVFS_server_req) + PINT_ENC_GENERIC_HEADER_SIZE 
	    + (request->u.mgmt_dspace_info_list.handle_count*sizeof(PVFS_handle));
	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);

	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}
	/* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));

	/* copy array of handles */
	memcpy(enc_msg + sizeof(struct PVFS_server_req),
	       request->u.mgmt_dspace_info_list.handle_array, 
	       request->u.mgmt_dspace_info_list.handle_count*sizeof(PVFS_handle));

	return (0);


    case PVFS_SERV_RMDIRENT:
	assert(request->u.rmdirent.entry != NULL);

	name_sz = strlen(request->u.rmdirent.entry) + 1;	/* include NULL terminator in size */
	size = sizeof(struct PVFS_server_req) + PINT_ENC_GENERIC_HEADER_SIZE
	    + name_sz;
	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);

	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}
	/* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));

	/* copy a null terminated string to another */
	memcpy(enc_msg + sizeof(struct PVFS_server_req),
	       request->u.rmdirent.entry, name_sz);

	/* make pointer since we're sending it over the wire and don't want 
	 * random memory referenced on the other side */

	((struct PVFS_server_req *) enc_msg)->u.rmdirent.entry = NULL;
	return (0);

    case PVFS_SERV_CREATE:
        size = sizeof(struct PVFS_server_req) + sizeof(uint32_t)
            + PINT_ENC_GENERIC_HEADER_SIZE;

        /*
          we need to alloc space for the variably sized handle
          extent array.
        */
        size += (request->u.create.handle_extent_array.extent_count *
                 sizeof(PVFS_handle_extent));

        enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);
        if (!enc_msg)
        {
            return (-ENOMEM);
        }

        target_msg->buffer_list[0] = enc_msg;
        target_msg->size_list[0] = size;
        target_msg->total_size = size;

        memcpy(enc_msg, contig_buffer_table.generic_header,
               PINT_ENC_GENERIC_HEADER_SIZE);
        enc_msg = (void *)((char *)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
        memcpy(enc_msg, request, sizeof(struct PVFS_server_req));
        *((uint32_t *)((char *)enc_msg + sizeof(struct PVFS_server_req))) =
            request->u.create.handle_extent_array.extent_count;
        memcpy(enc_msg + sizeof(struct PVFS_server_req) + sizeof(uint32_t),
               request->u.create.handle_extent_array.extent_array,
               (request->u.create.handle_extent_array.extent_count *
                sizeof(PVFS_handle_extent)));

        return (0);

    case PVFS_SERV_MKDIR:
	size = sizeof(struct PVFS_server_req) +
            sizeof(struct PVFS_object_attr) + sizeof(uint32_t) +
	    PINT_ENC_GENERIC_HEADER_SIZE;


        /*
          we need to alloc space for the variably sized handle
          extent array.
        */
        size += (request->u.mkdir.handle_extent_array.extent_count *
                 sizeof(PVFS_handle_extent));

	/*
          if we're mkdir'ing a meta file, we need to alloc
          space for the attributes
        */
	if (request->u.mkdir.attr.objtype == PVFS_TYPE_METAFILE)
	{
	    size +=
		request->u.mkdir.attr.u.meta.dfile_count * sizeof(PVFS_handle);
	}

	/* TODO: come back and alloc the right spaces for 
	 * distributions cause they're going to change */

	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);
	if (!enc_msg)
	{
	    return (-ENOMEM);
	}
	/* TODO: CAN WE JUST TACK THE BUFFER LIST ONTO THE END OF THE BMI_memalloc? */
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));
        *((uint32_t *)((char *)enc_msg + sizeof(struct PVFS_server_req))) =
            request->u.mkdir.handle_extent_array.extent_count;
        memcpy(enc_msg + sizeof(struct PVFS_server_req) + sizeof(uint32_t),
               request->u.mkdir.handle_extent_array.extent_array,
               (request->u.mkdir.handle_extent_array.extent_count *
                sizeof(PVFS_handle_extent)));

	/* throw handles at the end for metadata files */
	if (request->u.mkdir.attr.objtype == PVFS_TYPE_METAFILE)
	{
	    /* handles */
	    memcpy((enc_msg + sizeof(struct PVFS_server_req) + sizeof(uint32_t) +
                   (request->u.mkdir.handle_extent_array.extent_count *
                    sizeof(PVFS_handle_extent))),
		   request->u.mkdir.attr.u.meta.dfile_array,
		   request->u.mkdir.attr.u.meta.dfile_count *
		   sizeof(PVFS_handle));

	    /* make pointer since we're sending it over the wire and don't want 
	     * random memory referenced on the other side */

	    ((struct PVFS_server_req *) enc_msg)->u.mkdir.attr.u.meta.
		dfile_array = NULL;
	}
	return (0);

    case PVFS_SERV_SETATTR:
	size =
	    sizeof(struct PVFS_server_req) + sizeof(struct PVFS_object_attr) +
	    PINT_ENC_GENERIC_HEADER_SIZE;

	if (request->u.setattr.attr.objtype == PVFS_TYPE_METAFILE)
	{
            if (request->u.setattr.attr.mask & PVFS_ATTR_META_DFILES)
	    {
                assert(request->u.setattr.attr.u.meta.dfile_count > 0);
                assert(request->u.setattr.attr.u.meta.dfile_count <
                       PVFS_REQ_LIMIT_DFILE_COUNT + 1);
                assert(request->u.setattr.attr.u.meta.dfile_array != NULL);
                
		size +=
		    request->u.setattr.attr.u.meta.dfile_count *
		    sizeof(PVFS_handle);
	    }

            if (request->u.setattr.attr.mask & PVFS_ATTR_META_DIST)
            {
                assert(request->u.setattr.attr.u.meta.dist != NULL);
                
		request->u.setattr.attr.u.meta.dist_size =
		    PINT_DIST_PACK_SIZE(request->u.setattr.attr.u.meta.dist);
		size += request->u.setattr.attr.u.meta.dist_size;
	    }
	}
        else if (request->u.setattr.attr.objtype == PVFS_TYPE_SYMLINK)
        {
            if (request->u.setattr.attr.mask & PVFS_ATTR_SYMLNK_ALL)
            {
                assert(request->u.setattr.attr.u.sym.target_path_len > 0);
                assert(request->u.setattr.attr.u.sym.target_path);

                size +=
                    sizeof(request->u.setattr.attr.u.sym.target_path_len);
                size += request->u.setattr.attr.u.sym.target_path_len;
            }
        }

	/* TODO: come back and alloc the right spaces for 
	 * distributions and attribs cause they're going to change */

	enc_msg = BMI_memalloc(target_msg->dest, size, BMI_SEND);
	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}

	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;

	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));
	enc_msg += sizeof(struct PVFS_server_req);

	if (request->u.setattr.attr.objtype == PVFS_TYPE_METAFILE)
	{
            if (request->u.setattr.attr.mask & PVFS_ATTR_META_DFILES)
            {
                assert(request->u.setattr.attr.u.meta.dfile_count > 0);
                assert(request->u.setattr.attr.u.meta.dfile_count <
                       PVFS_REQ_LIMIT_DFILE_COUNT + 1);
                assert(request->u.setattr.attr.u.meta.dfile_array != NULL);

                memcpy(enc_msg,
                       request->u.setattr.attr.u.meta.dfile_array,
                       request->u.setattr.attr.u.meta.dfile_count *
                       sizeof(PVFS_handle));

                enc_msg +=
                    request->u.setattr.attr.u.meta.dfile_count *
                    sizeof(PVFS_handle);
            }

	    /* pack distribution information now */
            if (request->u.setattr.attr.mask & PVFS_ATTR_META_DIST)
            {
                assert(request->u.setattr.attr.u.meta.dist != NULL);
                
                PINT_Dist_encode(enc_msg, request->u.setattr.attr.u.meta.dist);
            }
	}
        else if (request->u.setattr.attr.objtype == PVFS_TYPE_SYMLINK)
        {
            if (request->u.setattr.attr.mask & PVFS_ATTR_SYMLNK_ALL)
            {
                *((uint32_t *)enc_msg) =
                    request->u.setattr.attr.u.sym.target_path_len;
                enc_msg +=
                    sizeof(request->u.setattr.attr.u.sym.target_path_len);
                memcpy(enc_msg,
                       request->u.setattr.attr.u.sym.target_path,
                       request->u.setattr.attr.u.sym.target_path_len);
                enc_msg += request->u.setattr.attr.u.sym.target_path_len;
            }
        }
	return (0);
    case PVFS_SERV_IO:

	/* make it large enough for the req structure, the dist, the
	 * io description, and two integers (used to indicate the
	 * size of the dist and description)
	 */
	size = sizeof(struct PVFS_server_req) + 2 * sizeof(int) +
	    PINT_REQUEST_PACK_SIZE(request->u.io.file_req) +
	    PINT_DIST_PACK_SIZE(request->u.io.io_dist) + 
	    PINT_ENC_GENERIC_HEADER_SIZE;

	/* create buffer for encoded message */
	enc_msg =
	    BMI_memalloc(target_msg->dest, (bmi_size_t)size, BMI_SEND);
	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;
	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	/* copy the basic request structure in */
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));
	/* store the size of the io description */
	*(int *) ((char *) (enc_msg) + sizeof(struct PVFS_server_req))
	    = PINT_REQUEST_PACK_SIZE(request->u.io.file_req);
	/* store the size of the distribution */
	*(int *) ((char *) (enc_msg) + sizeof(struct PVFS_server_req)
		  + sizeof(int)) = PINT_DIST_PACK_SIZE(request->u.io.io_dist);
	/* find pointers to where the req and dist will be packed */
	encode_file_req = (PINT_Request *) ((char *) (enc_msg) +
					  sizeof(struct PVFS_server_req) +
					  2 * sizeof(int));
	encode_io_dist =
	    (PVFS_Dist *) ((char *) (encode_file_req) +
			   PINT_REQUEST_PACK_SIZE(request->u.io.file_req));
	/* pack the I/O description */
	ret = PINT_Request_commit(encode_file_req, request->u.io.file_req);
	if (ret < 0)
	{
	    BMI_memfree(target_msg->dest, enc_msg, size, BMI_SEND);
	    return (ret);
	}
	ret = PINT_Request_encode(encode_file_req);
	if (ret < 0)
	{
	    BMI_memfree(target_msg->dest, enc_msg, size, BMI_SEND);
	    return (ret);
	}
	/* pack the distribution */
	PINT_Dist_encode(encode_io_dist, request->u.io.io_dist);
	return (0);

	/*these structures are all self contained (no pointers that need to be packed) */
    case PVFS_SERV_MGMT_ITERATE_HANDLES:
    case PVFS_SERV_MGMT_PERF_MON:
    case PVFS_SERV_MGMT_EVENT_MON:
    case PVFS_SERV_READDIR:
    case PVFS_SERV_GETATTR:
    case PVFS_SERV_REMOVE:
    case PVFS_SERV_TRUNCATE:
    case PVFS_SERV_FLUSH:
    case PVFS_SERV_MGMT_SETPARAM:
    case PVFS_SERV_MGMT_NOOP:
    case PVFS_SERV_STATFS:
	size = sizeof(struct PVFS_server_req) + PINT_ENC_GENERIC_HEADER_SIZE;
	enc_msg = BMI_memalloc(target_msg->dest, (bmi_size_t) size, BMI_SEND);
	if (enc_msg == NULL)
	{
	    return (-ENOMEM);
	}
	target_msg->buffer_list[0] = enc_msg;
	target_msg->size_list[0] = size;
	target_msg->total_size = size;
	memcpy(enc_msg, contig_buffer_table.generic_header,
	    PINT_ENC_GENERIC_HEADER_SIZE);
	enc_msg = (void*)((char*)enc_msg + PINT_ENC_GENERIC_HEADER_SIZE);
	memcpy(enc_msg, request, sizeof(struct PVFS_server_req));
	return (0);

    /* invalid request types */
    case PVFS_SERV_INVALID:
    case PVFS_SERV_PERF_UPDATE:
    case PVFS_SERV_JOB_TIMER:
    case PVFS_SERV_PROTO_ERROR:
    case PVFS_SERV_WRITE_COMPLETION:
	assert(0);
	gossip_lerr("Error: request type %d is invalid.\n",
	    (int)request->op);
	return(-ENOSYS);
    }

    gossip_lerr("Error: system attempted to encode an unkown request structure.\n");
    assert(0);
    return(-ENOSYS);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* I/O Function Implementation */
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pvfs2-req-proto.h>
#include <pvfs-distribution.h>
#include <pint-servreq.h>
#include <pint-bucket.h>
#include <PINT-reqproto-encode.h>

/* TODO: where does this define really belong? */
#define REQ_ENC_FORMAT 0

/* TODO: remove anything with the word "HACK" in it later on; we
 * are just kludging some stuff for now to be able to test I/O
 * functionality 
 * TODO: try to do something to avoid so many mallocs
 * TODO: figure out if we have to do anything special for short
 * reads or writes
 * TODO: figure out what should be passed out in the system
 * interface response (more info on what completed, info on which
 * servers failed, etc.)
 */

/* PVFS_sys_io()
 *
 * performs a read or write operation.  PVFS_sys_read() and
 * PVFS_sys_write() are aliases to this function.
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_io(PVFS_sysreq_io *req, PVFS_sysresp_io *resp,
    enum PVFS_sys_io_type type)
{
    pinode* pinode_ptr = NULL;
    PVFS_bitfield attr_mask = 0;
    int ret = -1;
    bmi_addr_t* addr_array = NULL;
    struct PVFS_server_req_s* req_array = NULL;
    struct PVFS_server_resp_s* tmp_resp = NULL;
    void** resp_encoded_array = NULL;
    struct PINT_decoded_msg* resp_decoded_array = NULL;
    PINT_Request_file_data* file_data_array = NULL;
    int* error_code_array = NULL;
    flow_descriptor** flow_array = NULL;
    int i;
    PVFS_msg_tag_t op_tag = get_next_session_tag();
    int target_handle_count = 0;
    PVFS_handle* target_handle_array = NULL;
    int total_errors = 0;

    struct PINT_Request_state* req_state = NULL;
    PINT_Request_file_data tmp_file_data;
    PVFS_count32 segmax = 1;
    PVFS_size bytemax = 1;
    PVFS_offset offset = 0;
    PVFS_boolean eof_flag = 0;

    PVFS_Dist* HACK_io_dist = NULL;

    if((type != PVFS_SYS_IO_READ) && (type != PVFS_SYS_IO_WRITE))
    {
	return(-EINVAL);
    }

    /* find a pinode for the target file */
    attr_mask = ATTR_BASIC|ATTR_META;
    ret = phelper_get_pinode(req->pinode_refn, &pinode_ptr,
	attr_mask, req->credentials);
    if(ret < 0)
    {
	return(ret);
    }

    /* check permissions */
    ret = check_perms(pinode_ptr->attr, req->credentials.perms,
	req->credentials.uid, req->credentials.gid);
    if(ret < 0)
    {
	return(-EACCES);
    }

    gossip_err("WARNING: kludging distribution in PINT_sys_io().\n");

    /* for now, build our own dist */
    HACK_io_dist = PVFS_Dist_create("default_dist");
    if(!HACK_io_dist)
    {
	gossip_lerr("Error: PVFS_Dist_create() failure.\n");
	return(-EINVAL);
    }
    ret = PINT_Dist_lookup(HACK_io_dist);
    if(ret < 0)
    {
	goto out;
    }

    target_handle_array =
	(PVFS_handle*)malloc(pinode_ptr->attr.u.meta.nr_datafiles
	* sizeof(PVFS_handle));
    if(!target_handle_array)
    {
	ret = -errno;
	goto out;
    }

    /* find out which handles must be included to service this
     * particular I/O request; hopefully we don't really have to
     * contact everyone, just the servers that hold the parts of
     * the file that we are interested in.
     */
    req_state = PINT_New_request_state(req->io_req);
    if(!req_state)
    {
	ret = -ENOMEM;
	goto out;
    }
    for(i=0; i<pinode_ptr->attr.u.meta.nr_datafiles; i++)
    {
	/* NOTE: we don't have to give an accurate file size here,
	 * as long as we set the extend flag to tell the I/O req
	 * processor to continue past eof if needed
	 */
	tmp_file_data.fsize = 0;  
	tmp_file_data.dist = HACK_io_dist;
	tmp_file_data.iod_num = i;
	tmp_file_data.iod_count = pinode_ptr->attr.u.meta.nr_datafiles;
	tmp_file_data.extend_flag = 1;

	ret = PINT_Process_request(req_state, &tmp_file_data,
	    &segmax, NULL, NULL, &offset, &bytemax, &eof_flag,
	    PINT_CKSIZE);
	if(ret < 0)
	{
	    goto out;
	}

	/* did we find that any data belongs to this handle? */
	/* TODO: change this to support multiple handles */
#if 0
	if(bytemax)
	{
	    target_handle_array[target_handle_count] =
		pinode_ptr->attr.u.meta.dfh[i]; 
	    target_handle_count++;
	}
#else
	if(bytemax && i == 0)
	{
	    target_handle_array[target_handle_count] =
		pinode_ptr->attr.u.meta.dfh[i]; 
	    target_handle_count++;
	}
	else
	{
	    gossip_lerr("KLUDGE: only allowing I/O to first data handle.\n");
	}
#endif
    }
    PINT_Free_request_state(req_state);
    req_state = NULL;

    /* stuff for request array */
    addr_array = (bmi_addr_t*)malloc(target_handle_count *
	sizeof(bmi_addr_t));
    req_array = (struct PVFS_server_req_s*)
	malloc(target_handle_count * 
	sizeof(struct PVFS_server_req_s));
    resp_encoded_array = (void**)malloc(target_handle_count *
	sizeof(void*));
    resp_decoded_array = (struct PINT_decoded_msg*)
	malloc(target_handle_count * 
	sizeof(struct PINT_decoded_msg));

    /* stuff for both request array and flow array */
    error_code_array = (int*)malloc(target_handle_count *
	sizeof(int));
    memset(error_code_array, 0, (target_handle_count*sizeof(int)));

    /* stuff for running flows */
    file_data_array = (PINT_Request_file_data*)malloc(
	target_handle_count*sizeof(PINT_Request_file_data));
    flow_array = (flow_descriptor**)malloc(target_handle_count *
	sizeof(flow_descriptor*));
    if(flow_array)
	memset(flow_array, 0, (target_handle_count *
	    sizeof(flow_descriptor*)));
	
    if(!addr_array || !req_array || !resp_encoded_array ||
	!resp_decoded_array || !error_code_array || 
	!file_data_array || !flow_array)
    {
	ret = -ENOMEM;
	goto out;
    }

    /* setup the I/O request to each data server */
    for(i=0; i<target_handle_count; i++)
    {
	/* resolve the address of the server */
	ret = PINT_bucket_map_to_server(
	    &(addr_array[i]),
	    target_handle_array[i],
	    req->pinode_refn.fs_id);
	if(ret < 0)
	{
	    goto out;
	}

	/* fill in the I/O request */
	req_array[i].op = PVFS_SERV_IO;
	req_array[i].rsize = sizeof(struct PVFS_server_req_s);
	req_array[i].credentials = req->credentials;
	req_array[i].u.io.handle = target_handle_array[i];
	req_array[i].u.io.fs_id = req->pinode_refn.fs_id;
	req_array[i].u.io.iod_num = i;
	/* TODO: change this to support multiple handles */
#if 0
	req_array[i].u.io.iod_count =
	    pinode_ptr->attr.u.meta.nr_datafiles;
#else
	req_array[i].u.io.iod_count = 1;
#endif
	req_array[i].u.io.io_req = req->io_req;
	req_array[i].u.io.io_dist = HACK_io_dist;
	if(type == PVFS_SYS_IO_READ)
	    req_array[i].u.io.io_type = PVFS_IO_READ;
	if(type == PVFS_SYS_IO_WRITE)
	    req_array[i].u.io.io_type = PVFS_IO_WRITE;
    }
   
    /* send the I/O requests on their way */
    ret = PINT_send_req_array(
	addr_array,
	req_array,
	PINT_get_encoded_generic_ack_sz(0, PVFS_SERV_IO),
	resp_encoded_array,
	resp_decoded_array,
	error_code_array,
	target_handle_count,
	op_tag);
    if(ret < 0)
    {
	goto out;
    }

    /* set an error code for each negative ack that we received */
    for(i=0; i<target_handle_count; i++)
    {
	tmp_resp = (struct
	    PVFS_server_resp_s*)resp_decoded_array[i].buffer;
	if(!(error_code_array[i]) && tmp_resp->status)
	    error_code_array[i] = tmp_resp->status;
    }

    /* setup a flow for each I/O server that gave us a positive
     * response
     */
    for(i=0; i<target_handle_count; i++)
    {
	tmp_resp = (struct
	    PVFS_server_resp_s*)resp_decoded_array[i].buffer;
	if(!(error_code_array[i]))
	{
	    flow_array[i] = PINT_flow_alloc();
	    if(!flow_array[i])
	    {
		error_code_array[i] = -ENOMEM;
		continue;
	    }
	    flow_array[i]->file_data = &(file_data_array[i]);
	    flow_array[i]->file_data->fsize =
		tmp_resp->u.io.bstream_size;
	    flow_array[i]->file_data->dist = HACK_io_dist;
	    flow_array[i]->file_data->iod_num = i;
	    /* TODO: change this to support multiple handles */
#if 0
	    flow_array[i]->file_data->iod_count =
		pinode_ptr->attr.u.meta.nr_datafiles;
#else
	    flow_array[i]->file_data->iod_count = 1;
#endif
	    flow_array[i]->request = req->io_req;
	    flow_array[i]->flags = 0;
	    flow_array[i]->tag = op_tag;
	    flow_array[i]->user_ptr = NULL;

	    if(type == PVFS_SYS_IO_READ)
	    {
		flow_array[i]->file_data->extend_flag = 0;
		flow_array[i]->src.endpoint_id = BMI_ENDPOINT;
		flow_array[i]->src.u.bmi.address = addr_array[i];
		flow_array[i]->dest.endpoint_id = MEM_ENDPOINT;
		flow_array[i]->dest.u.mem.size = req->buffer_size;
		flow_array[i]->dest.u.mem.buffer = req->buffer;
	    }
	    else
	    {
		flow_array[i]->file_data->extend_flag = 1;
		flow_array[i]->src.endpoint_id = MEM_ENDPOINT;
		flow_array[i]->src.u.mem.size = req->buffer_size;
		flow_array[i]->src.u.mem.buffer = req->buffer;
		flow_array[i]->dest.endpoint_id = BMI_ENDPOINT;
		flow_array[i]->dest.u.bmi.address = addr_array[i];
	    }
	}
    }

    /* actually run any flows that are needed */
    /* NOTE: we don't have to check if any flows are necessary or
     * not; this function will exit without doing anything if no
     * flows are runnable
     */
    ret = PINT_flow_array(flow_array, error_code_array,
	target_handle_count);
    if(ret < 0)
    {
	goto out;
    }

    /* if this was a write operation, then we need to wait for a
     * final ack from the data servers indicating the status of
     * the operation
     */
    if(type == PVFS_SYS_IO_WRITE)
    {
	/* wait for all of the final acks */
	ret = PINT_recv_ack_array(
	    addr_array,
	    PINT_get_encoded_generic_ack_sz(0,
		PVFS_SERV_WRITE_COMPLETION),
	    resp_encoded_array,
	    resp_decoded_array,
	    error_code_array,
	    target_handle_count,
	    op_tag);
	if(ret < 0)
	{
	    goto out;
	}

	/* set an error code for each negative ack that we received */
	for(i=0; i<target_handle_count; i++)
	{
	    tmp_resp = (struct
		PVFS_server_resp_s*)resp_decoded_array[i].buffer;
	    if(!(error_code_array[i]) && tmp_resp->status)
		error_code_array[i] = tmp_resp->status;
	}
    }

    /* default to reporting no errors, until we check our error
     * code array
     */
    ret = 0;
    resp->total_completed = 0;

    /* find out how many errors we hit */
    for(i=0; i<target_handle_count; i++)
    {
	tmp_resp = (struct
	    PVFS_server_resp_s*)resp_decoded_array[i].buffer;
	if(error_code_array[i])
	{
	    total_errors ++;
	    ret = -EIO;
	}
	else
	{
	    if(type == PVFS_SYS_IO_WRITE)
	    {
		resp->total_completed +=
		    tmp_resp->u.write_completion.total_completed;
	    }
	    else
	    {
		resp->total_completed +=
		    flow_array[i]->total_transfered;
	    }
	}
    }
    gossip_ldebug(CLIENT_DEBUG, 
	"%d servers contacted.\n", target_handle_count);
    gossip_ldebug(CLIENT_DEBUG,
	"%d servers experienced errors.\n", total_errors);
       
    /* drop through and pass out return value, successful cases go
     * through here also
     */
out:
    
    if(addr_array)
	free(addr_array);
    if(req_array)
	free(req_array);
    if(resp_encoded_array)
	free(resp_encoded_array);
    if(resp_decoded_array)
	free(resp_decoded_array);
    if(error_code_array)
	free(error_code_array);
    if(file_data_array)
	free(file_data_array);
    if(target_handle_array)
	free(target_handle_array);
    if(flow_array)
    {
	for(i=0; i<target_handle_count; i++)
	{
	    if(flow_array[i])
		PINT_flow_free(flow_array[i]);
	}
	free(flow_array);
    }

    if(HACK_io_dist)
    {
	free(HACK_io_dist);
    }

    if(req_state)
	PINT_Free_request_state(req_state);

    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

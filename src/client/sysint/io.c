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

static int HACK_create(PVFS_handle* handle, PVFS_fs_id fsid);
static int HACK_remove(PVFS_handle handle, PVFS_fs_id fsid);

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
    int partial_flag = 0;
    PVFS_msg_tag_t op_tag = get_next_session_tag();
    int target_handle_count = 0;
    PVFS_handle* target_handle_array = NULL;
    struct PINT_Request_state* req_state = NULL;
    PINT_Request_file_data tmp_file_data;
    PVFS_count32 segmax = 1;
    PVFS_size bytemax = 1;
    PVFS_offset offset = 0;
    PVFS_boolean eof_flag = 0;

    PVFS_handle HACK_foo;
    PVFS_Dist* HACK_io_dist = NULL;
    PVFS_handle* HACK_datafile_handles = &HACK_foo;
    int HACK_num_datafiles = 1;

    if((type != PVFS_SYS_IO_READ) && (type != PVFS_SYS_IO_WRITE))
    {
	return(-EINVAL);
    }

    /* find a pinode for the target file */
    attr_mask = ATTR_BASIC;
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

    gossip_err(
	"WARNING: kludging distribution and datafile in PINT_sys_io().\n");

    /* for now, create our own data file */
    ret = HACK_create(&HACK_foo, req->pinode_refn.fs_id);
    if(ret < 0)
    {
	/* don't bother cleaning up here; this isn't "real" code */
	gossip_lerr("Error: HACK_create() failure, exiting.\n");
	exit(-1);
    }

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

    target_handle_array = (PVFS_handle*)malloc(HACK_num_datafiles
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
    for(i=0; i<HACK_num_datafiles; i++)
    {
	/* NOTE: we don't have to give an accurate file size here,
	 * as long as we set the extend flag to tell the I/O req
	 * processor to continue past eof if needed
	 */
	tmp_file_data.fsize = 0;  
	tmp_file_data.dist = HACK_io_dist;
	tmp_file_data.iod_num = i;
	tmp_file_data.iod_count = HACK_num_datafiles;
	tmp_file_data.extend_flag = 1;

	ret = PINT_Process_request(req_state, &tmp_file_data,
	    &segmax, NULL, NULL, &offset, &bytemax, &eof_flag,
	    PINT_CKSIZE);
	if(ret < 0)
	{
	    goto out;
	}

	/* did we find that any data belongs to this handle? */
	if(bytemax)
	{
	    target_handle_array[target_handle_count] =
		HACK_datafile_handles[i]; 
	    target_handle_count++;
	}
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
	req_array[i].u.io.iod_count = HACK_num_datafiles;
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
    if(ret < 0 && ret != -EIO)
    {
	goto out;
    }

    /* If EIO, then at least one of the I/O req/resp exchanges
     * failed.  We will continue and carry I/O out with whoever we
     * can, though.
     */
    if(ret == -EIO)
    {
	partial_flag = 1;
	gossip_ldebug(CLIENT_DEBUG, "Warning: one or more I/O requests failed.\n");
    }

    /* setup a flow for each I/O server that gave us a positive
     * response
     */
    for(i=0; i<target_handle_count; i++)
    {
	tmp_resp = (struct
	    PVFS_server_resp_s*)resp_decoded_array[i].buffer;
	if(!(error_code_array[i]) && !(tmp_resp->status))
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
	    flow_array[i]->file_data->iod_count =
		HACK_num_datafiles;

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

    /* now we need to take stock of how many operations (if any)
     * failed, and report how much total data was transferred
     */
    resp->total_completed = 0;
    for(i=0; i<target_handle_count; i++)
    {
	if(error_code_array[i] || flow_array[i]->error_code)
	{
	    /* we suffered at least one failure that is specific
	     * to a particular server
	     */
	    ret = -EIO;
	}
	else
	{
	    resp->total_completed +=
		flow_array[i]->total_transfered;
	}
    }

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

    HACK_remove(HACK_foo, req->pinode_refn.fs_id);
    
    return(ret);
}

/* TODO: remove these later */
static int HACK_create(PVFS_handle* handle, PVFS_fs_id fsid)
{

    int ret = -1;
    bmi_addr_t server_addr;
    bmi_op_id_t client_ops[2];
    int outcount = 0;
    bmi_error_code_t error_code;
    bmi_size_t actual_size;
    struct PINT_encoded_msg encoded1;
    struct PINT_decoded_msg decoded1;
    struct PVFS_server_req_s my_req;
    struct PVFS_server_resp_s* create_dec_ack;
    int create_ack_size;
    void *create_ack;

    /* figure out how big of an ack to post */
    create_ack_size = PINT_get_encoded_generic_ack_sz(0, PVFS_SERV_CREATE);

    /* create storage for all of the acks */
    create_ack = malloc(create_ack_size);
    if(!create_ack)
	return(-errno);

    ret = PINT_bucket_map_to_server(
	&server_addr,
	4095,
	fsid);
    if(ret < 0)
    {
	fprintf(stderr, "PINT_bucket_map_to_server() failure.\n");
	return(-1);
    }

    /**************************************************
    * create request (create a data file to operate on) 
    */
    my_req.op = PVFS_SERV_CREATE;
    my_req.rsize = sizeof(struct PVFS_server_req_s);
    my_req.credentials.uid = 100;
    my_req.credentials.gid = 100;
    my_req.credentials.perms = U_WRITE | U_READ;  

    /* create specific fields */
    my_req.u.create.bucket = 4095;
    my_req.u.create.handle_mask = 0;
    my_req.u.create.fs_id = fsid;
    my_req.u.create.object_type = ATTR_DATA;

    ret = PINT_encode(&my_req,PINT_ENCODE_REQ,&encoded1,server_addr,0);
    if(ret < 0)
    {
	fprintf(stderr, "Error: PINT_encode failure.\n");
	return(-1);
    }

    /* send the request on its way */
    ret = BMI_post_sendunexpected_list(
	&(client_ops[1]), 
	encoded1.dest,
	encoded1.buffer_list, 
	encoded1.size_list,
	encoded1.list_count,
	encoded1.total_size, 
	encoded1.buffer_flag, 
	0, 
	NULL);
    if(ret < 0)
    {
	errno = -ret;
	perror("BMI_post_send");
	return(-1);
    }
    if(ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of request */
	do
	{
	    ret = BMI_test(client_ops[1], &outcount, &error_code, &actual_size,
	    NULL, 10);
	} while(ret == 0 && outcount == 0);

	if(ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Error: request send failed.\n");
	    if(ret<0)
	    {
		errno = -ret;
		perror("BMI_test");
	    }
	    return(-1);
	}
    }

    /* release the encoded message */
    PINT_encode_release(&encoded1, PINT_ENCODE_REQ, 0);

    /* post a recv for the server acknowledgement */
    ret = BMI_post_recv(&(client_ops[0]), server_addr, create_ack, 
	create_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
	NULL);
    if(ret < 0)
    {
	errno = -ret;
	perror("BMI_post_recv");
	return(-1);
    }
    if(ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of ack recv */
	do
	{
	    ret = BMI_test(client_ops[0], &outcount, &error_code,
	    &actual_size, NULL, 10);
	} while(ret == 0 && outcount == 0);

	if(ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Error: ack recv.\n");
	    fprintf(stderr, "   ret: %d, error code: %d\n",ret,error_code);
	    return(-1);
	}
    }
    else
    {
	if(actual_size != create_ack_size)
	{
	    printf("Error: short recv.\n");
	    return(-1);
	}
    }

    /* look at the ack */
    ret = PINT_decode(
	create_ack,
	PINT_ENCODE_RESP,
	&decoded1,
	server_addr,
	actual_size,
	NULL);
    if(ret < 0)
    {
	fprintf(stderr, "Error: PINT_decode() failure.\n");
	return(-1);
    }

    create_dec_ack = decoded1.buffer;
    if(create_dec_ack->op != PVFS_SERV_CREATE)
    {
	fprintf(stderr, "ERROR: received ack of wrong type (%d)\n", 
	    (int)create_dec_ack->op);
	return(-1);
    }
    if(create_dec_ack->status != 0)
    {
	fprintf(stderr, "ERROR: server returned status: %d\n",
	    (int)create_dec_ack->status);
	return(-1);
    }

    *handle = create_dec_ack->u.create.handle;

    /* release the decoded buffers */
    PINT_decode_release(&decoded1, PINT_ENCODE_RESP, 0);

    free(create_ack);

    return(0);
}

/* TODO: remove these later */
static int HACK_remove(PVFS_handle handle, PVFS_fs_id fsid)
{
    int ret = -1;
    bmi_addr_t server_addr;
    bmi_op_id_t client_ops[2];
    int outcount = 0;
    bmi_error_code_t error_code;
    bmi_size_t actual_size;
    struct PVFS_server_req_s my_req;
    void* remove_ack;
    int remove_ack_size;
    struct PVFS_server_resp_s* remove_dec_ack;
    struct PINT_encoded_msg encoded3;
    struct PINT_decoded_msg decoded3;

    remove_ack_size = PINT_get_encoded_generic_ack_sz(0,
	PVFS_SERV_REMOVE);

    remove_ack = malloc(remove_ack_size);
    if(!remove_ack)
	return(-errno);

    ret = PINT_bucket_map_to_server(
	&server_addr,
	4095,
	fsid);
    if(ret < 0)
    {
	fprintf(stderr, "PINT_bucket_map_to_server() failure.\n");
	return(-1);
    }

    my_req.op = PVFS_SERV_REMOVE;
    my_req.rsize = sizeof(struct PVFS_server_req_s);
    my_req.credentials.uid = 100;
    my_req.credentials.gid = 100;
    my_req.credentials.perms = U_WRITE | U_READ;  

    /* remove specific fields */
    my_req.u.remove.fs_id = fsid;
    my_req.u.remove.handle = handle;

    ret = PINT_encode(&my_req,PINT_ENCODE_REQ,&encoded3,server_addr,0);
    if(ret < 0)
    {
	fprintf(stderr, "Error: PINT_encode failure.\n");
	return(-1);
    }

    /* send the request on its way */
    ret = BMI_post_sendunexpected_list(
	&(client_ops[1]), 
	encoded3.dest,
	encoded3.buffer_list, 
	encoded3.size_list,
	encoded3.list_count,
	encoded3.total_size, 
	encoded3.buffer_flag, 
	0, 
	NULL);
    if(ret < 0)
    {
	errno = -ret;
	perror("BMI_post_send");
	return(-1);
    }
    if(ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of request */
	do
	{
	    ret = BMI_test(client_ops[1], &outcount, &error_code, &actual_size,
		NULL, 10);
	} while(ret == 0 && outcount == 0);

	if(ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Error: request send failed.\n");
	    if(ret<0)
	    {
		errno = -ret;
		perror("BMI_test");
	    }
	    return(-1);
	}
    }

    /* release the encoded message */
    PINT_encode_release(&encoded3, PINT_ENCODE_REQ, 0);

    /* post a recv for the server acknowledgement */
    ret = BMI_post_recv(&(client_ops[0]), server_addr, remove_ack, 
	remove_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
	NULL);
    if(ret < 0)
    {
	errno = -ret;
	perror("BMI_post_recv");
	return(-1);
    }
    if(ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of ack recv */
	do
	{
	    ret = BMI_test(client_ops[0], &outcount, &error_code,
		&actual_size, NULL, 10);
	} while(ret == 0 && outcount == 0);

	if(ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Error: ack recv.\n");
	    fprintf(stderr, "   ret: %d, error code: %d\n",ret,error_code);
	    return(-1);
	}
    }
    else
    {
	if(actual_size != remove_ack_size)
	{
	    printf("Error: short recv.\n");
	    return(-1);
	}
    }

    /* look at the ack */
    ret = PINT_decode(
	remove_ack,
	PINT_ENCODE_RESP,
	&decoded3,
	server_addr,
	actual_size,
	NULL);
    if(ret < 0)
    {
	fprintf(stderr, "Error: PINT_decode() failure.\n");
	return(-1);
    }

    remove_dec_ack = decoded3.buffer;
    if(remove_dec_ack->op != PVFS_SERV_REMOVE)
    {
	fprintf(stderr, "ERROR: received ack of wrong type (%d)\n", 
	    (int)remove_dec_ack->op);
	return(-1);
    }
    if(remove_dec_ack->status != 0)
    {
	fprintf(stderr, "ERROR: server returned status: %d\n",
	    (int)remove_dec_ack->status);
	return(-1);
    }

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

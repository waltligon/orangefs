/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <assert.h>

#if 0
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "gen-locks.h"
#endif
#include "pint-servreq.h"
#include "job.h"
#include "pvfs2-req-proto.h"
#include "bmi.h"
#include "PINT-reqproto-encode.h"

#define REQ_ENC_FORMAT 0

#ifndef HEADER_SIZE
#define HEADER_SIZE sizeof(int)
#endif

/* server_send_req()
 *
 * TODO: PREPOST RECV
 *
 * Fills in PINT_decoded_msg pointed to by decoded_p; decoded_p->buffer will 
 * hold the decoded acknowledgement.  The caller must call PINT_decode_release()
 * on decoded_p in order to free the memory allocated during the decoding
 * process.
 */
int PINT_server_send_req(bmi_addr_t addr,
			 struct PVFS_server_req_s *req_p,
			 bmi_size_t max_resp_size,
			 struct PINT_decoded_msg *decoded_p)
{
    int ret;
    struct PINT_encoded_msg encoded;
    char *encoded_resp;
    job_status_s s_status, r_status;
    PVFS_msg_tag_t op_tag = get_next_session_tag();

    /* the request protocol adds a small header (at the end--so its really a footer, but..)
     * so since this parameter only counts the structure size we need to add it here
     */
    max_resp_size += HEADER_SIZE;

    /* convert into something we can send across the wire.
     *
     * PINT_encode returns an encoded buffer in encoded. We have to free it
     * later.
     */
    ret = PINT_encode(req_p, PINT_ENCODE_REQ, &encoded, addr, REQ_ENC_FORMAT);

    /* allocate space for response, prepost receive */
    encoded_resp = BMI_memalloc(addr, max_resp_size, BMI_RECV_BUFFER);
    if (encoded_resp == NULL)
    {
	ret = -ENOMEM;
	goto return_error;
    }

    /* post a blocking send job (this is a helper function) */
    ret = job_bmi_send_blocking(addr,
				encoded.buffer_list[0],
				encoded.size_list[0],
				op_tag,
				BMI_PRE_ALLOC,
				1, /* # of items in lists */
				&s_status);
    if (ret < 0)
    {
	goto return_error;
    }
    else if (ret == 0 && s_status.error_code != 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"send failed\n");
	ret = -EINVAL;
	goto return_error;
    }

    debug_print_type(req_p, 0);

    /* post a blocking receive job */
    ret = job_bmi_recv_blocking(addr, encoded_resp, max_resp_size, op_tag, BMI_PRE_ALLOC, &r_status);
    gossip_ldebug(CLIENT_DEBUG,"message recieved: r_status.actual_size = %lld\n",r_status.actual_size);
    gossip_ldebug(CLIENT_DEBUG,"r_status.error_code = %d\nreturn value = %d\n",r_status.error_code, ret);

    gossip_ldebug(CLIENT_DEBUG,"status(insided encoded struct) = %d\n",((struct PVFS_server_resp_s *)encoded_resp)->status);
    gossip_ldebug(CLIENT_DEBUG,"rsize(insided encoded struct) = %lld\n",((struct PVFS_server_resp_s *)encoded_resp)->rsize);
    if (((struct PVFS_server_resp_s *)encoded_resp)->rsize == 0)
	((struct PVFS_server_resp_s *)encoded_resp)->rsize = sizeof(struct PVFS_server_resp_s);
    if (ret < 0)
    {
	goto return_error;
    }
    else 
    {
	if (((ret == 0) && (r_status.error_code != 0)) || (r_status.actual_size > max_resp_size))
	{
	    gossip_ldebug(CLIENT_DEBUG,"status code error failed\n");
	    ret = -EINVAL;
	    goto return_error;
	}
#if 0
	else
	{
	    gossip_ldebug(CLIENT_DEBUG,"%d != 1, r_status.error_code == %d\n", ret, r_status.error_code );
	    gossip_ldebug(CLIENT_DEBUG,"r_status.actual_size == %d\n", r_status.actual_size );
	    gossip_ldebug(CLIENT_DEBUG,"max_resp_size == %d\n", max_resp_size );
	}
#endif
    }
    gossip_ldebug(CLIENT_DEBUG,"job_bmi call was successfull\n");

    /* decode msg from wire format here; function allocates space for decoded response.
     * PINT_decode_release() must be used to free this later.
     */
    ret = PINT_decode(encoded_resp,
		      PINT_DECODE_RESP,
		      decoded_p,
		      addr,
		      r_status.actual_size,
		      NULL);
    if (ret < 0)
    {
	gossip_ldebug(CLIENT_DEBUG,"decode failed\n");
	ret = (-EINVAL);
	goto return_error;
    }

    debug_print_type(decoded_p->buffer, 1);

    /* free encoded_resp buffer */
    ret = BMI_memfree(addr, encoded_resp, max_resp_size, BMI_RECV_BUFFER);
    assert(ret == 0);

    return 0;
 return_error:
    return ret;
}


/* PINT_send_req()
 *
 * TODO: prepost recv
 * TODO: use a non-infinite timeout
 *
 * sends a request and receives an acknowledgement, all in one
 * blocking fuction.  It does encoding, decoding, and error
 * checking along the way.  NOTE: PINT_release_req() should be
 * called to clean up after the ack buffer is no longer needed.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_send_req(bmi_addr_t addr,
    struct PVFS_server_req_s *req_p,
    bmi_size_t max_resp_size,
    struct PINT_decoded_msg *decoded_resp,
    void** encoded_resp,
    PVFS_msg_tag_t op_tag)
{
    int ret;
    struct PINT_encoded_msg encoded_req;
    job_status_s tmp_status;
    job_id_t tmp_id;
    int count = 0;

    *encoded_resp = NULL;

    /* encode the request */
    ret = PINT_encode(
	req_p, 
	PINT_ENCODE_REQ, 
	&encoded_req, 
	addr, 
	REQ_ENC_FORMAT);
    if(ret < 0)
    {
	return(ret);
    }

    /* send the encoded request */
    ret = job_bmi_send_list(
	encoded_req.dest,
	encoded_req.buffer_list,
	encoded_req.size_list,
	encoded_req.list_count,
	encoded_req.total_size,
	op_tag,
	encoded_req.buffer_flag,
	1,
	NULL,
	&tmp_status,
	&tmp_id);
    if(ret < 0)
    {
	goto send_req_out;
    }
    else if(ret == 1)
    {
	/* immediate completion; continue on unless an immediate
	 * job error is reported
	 */
	if(tmp_status.error_code != 0)
	{
	    ret = tmp_status.error_code;
	    goto send_req_out;
	}
    }
    else
    {
	/* we need to test for completion */
	ret = job_test(tmp_id, &count, NULL, &tmp_status, -1);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr("Error: PINT_send_req() critical failure.\n");
	    exit(-1);
	}
	if(tmp_status.error_code != 0)
	{
	    ret = tmp_status.error_code;
	    goto send_req_out;
	}
    }

    /* allocate space for response */
    *encoded_resp = BMI_memalloc(addr, max_resp_size, BMI_RECV_BUFFER);
    if (encoded_resp == NULL)
    {
	ret = -ENOMEM;
	goto send_req_out;
    }

    /* recv the response */
    ret = job_bmi_recv(
	addr, 
	*encoded_resp,
	max_resp_size,
	op_tag,
	BMI_PRE_ALLOC,
	NULL,
	&tmp_status,
	&tmp_id);
    if(ret < 0)
    {
	goto send_req_out;
    }
    else if(ret == 1)
    {
	/* immediate completion */
	if(tmp_status.error_code != 0)
	{
	    ret = tmp_status.error_code;
	    goto send_req_out;
	}
    }
    else
    {
	/* we need to test for completion */
	ret = job_test(tmp_id, &count, NULL, &tmp_status, -1);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr("Error: PINT_send_req() critical failure.\n");
	    exit(-1);
	}
	if(tmp_status.error_code != 0)
	{
	    ret = tmp_status.error_code;
	    goto send_req_out;
	}
    }

    /* decode the message that we received */
    ret = PINT_decode(encoded_resp,
	PINT_DECODE_RESP,
	decoded_resp,
	addr,
	tmp_status.actual_size,
	NULL);
    if (ret < 0)
    {
	goto send_req_out;
    }

    ret = 0;

send_req_out:

    PINT_encode_release(
	&encoded_req, 
	PINT_ENCODE_REQ,
	REQ_ENC_FORMAT);

    if(*encoded_resp)
	BMI_memfree(addr, *encoded_resp, max_resp_size, BMI_RECV_BUFFER);

    return(ret);
}

/* PINT_release_req()
 *
 * Companion function to PINT_send_req(); it releases any
 * resources created earlier.  The caller should not use this
 * buffer until it is completely done with the response buffers
 * (decoded or encoded)
 *
 * no return value
 */
void PINT_release_req(bmi_addr_t addr,
    struct PVFS_server_req_s *req_p,
    bmi_size_t max_resp_size,
    struct PINT_decoded_msg *decoded_resp,
    void** encoded_resp,
    PVFS_msg_tag_t op_tag)
{
    /* the only resources we need to get rid of are the decoded
     * response and the encoded response.
     */

    PINT_decode_release(
	decoded_resp,
	PINT_DECODE_RESP,
	REQ_ENC_FORMAT);

    BMI_memfree(addr, *encoded_resp, max_resp_size,
	BMI_RECV_BUFFER);

    return;
}

/* PINT_send_req_array()
 *
 * TODO: prepost receives 
 * TODO: use timeouts rather than infinite blocking
 * TODO: try to avoid mallocing so much 
 *
 * Sends an array of requests and waits for an acknowledgement for each.
 *
 * returns 0 on success, -EIO on partial failure, -errno on total
 * failure
 */
int PINT_send_req_array(bmi_addr_t* addr_array,
    struct PVFS_server_req_s* req_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size,
    PVFS_msg_tag_t op_tag)
{
    int i;
    int ret = -1;
    struct PINT_encoded_msg* req_encoded_array = NULL;
    job_id_t* id_array = NULL;
    job_status_s* status_array = NULL;
    int* index_array = NULL;
    int total_completed = 0;
    int count;
    int total_errors = 0;

    /* allocate some bookkeeping fields */
    req_encoded_array = (struct PINT_encoded_msg*)malloc(array_size *
	sizeof(struct PINT_encoded_msg));
    status_array = (job_status_s*)malloc(array_size *
	sizeof(job_status_s));
    index_array = (int*)malloc(array_size * sizeof(int));
    id_array = (job_id_t*)malloc(array_size * sizeof(job_id_t));

    if(!req_encoded_array || !status_array || !id_array ||
	!index_array)
    {
	ret = -ENOMEM;
	goto out;
    }

    /* clear some of the arrays for safety */
    memset(error_code_array, 0, (array_size*sizeof(int)));
    memset(resp_encoded_array, 0, (array_size*sizeof(void*)));
    memset(req_encoded_array, 0, (array_size*sizeof(struct
	PINT_encoded_msg)));
    memset(resp_decoded_array, 0, (array_size*sizeof(struct
	PINT_decoded_msg)));

    /* encode all of the requests */
    for(i=0; i<array_size; i++)
    {
	ret = PINT_encode(&(req_array[i]), PINT_ENCODE_REQ,
	    &(req_encoded_array[i]), addr_array[i], REQ_ENC_FORMAT);
	if(ret < 0)
	{
	    total_errors++;
	    error_code_array[i] = ret;
	}
    }

    /* post a bunch of sends */
    /* keep up with job ids, and the number of immediate completions */
    total_completed = 0;
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    ret = job_bmi_send_list(
		req_encoded_array[i].dest, 
		req_encoded_array[i].buffer_list,
		req_encoded_array[i].size_list,
		req_encoded_array[i].list_count,
		req_encoded_array[i].total_size,
		op_tag,
		req_encoded_array[i].buffer_flag,
		1,
		NULL,
		&(status_array[i]),
		&(id_array[i]));
	    if(ret < 0)
	    {
		/* immediate error */
		total_errors++;
		error_code_array[i] = ret;
		id_array[i] = 0;
	    }
	    else if (ret == 1)
	    {
		/* immediate completion */
		error_code_array[i] = status_array[i].error_code;
		if(error_code_array[i])
		{
		    total_errors++;
		}
		else
		{
		    total_completed++;
		}
		id_array[i] = 0;
	    }
	}
    }

    /* see if anything needs to be tested for completion */
    if((total_completed+total_errors) < array_size)
    {
	count = array_size;
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr(
		"Error: PINT_server_send_req_array() critical failure.\n");
	    exit(-1);
	}
	    
	/* all sends are complete now, fill in error codes */
	for(i=0; i<count; i++)
	{
	    error_code_array[index_array[i]] =
		status_array[i].error_code;
	    if(status_array[i].error_code)
	    {
		total_errors++;
	    }
	}
    }

    /* release request encodings */
    for(i=0; i<array_size; i++)
    {
	if(req_encoded_array[i].total_size)
	{
	    PINT_encode_release(&(req_encoded_array[i]),
		PINT_ENCODE_REQ, REQ_ENC_FORMAT);
	}
    }

    /* allocate room for responses */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    resp_encoded_array[i] = BMI_memalloc(addr_array[i], max_resp_size,
		BMI_RECV_BUFFER);
	    if(!resp_encoded_array[i])
	    {
		error_code_array[i] = -ENOMEM;
		total_errors++;
	    }
	}
    }

    /* post a bunch of receives */
    /* keep up with job ids and the number of immediate completions */
    memset(id_array, 0, (array_size*sizeof(job_id_t)));
    total_completed = 0;
    for(i=0; i<array_size; i++)
    {
	/* skip servers that have already experienced communication
	 * failure 
	 */
	if(!(error_code_array[i]))
	{
	    ret = job_bmi_recv(
		addr_array[i],
		resp_encoded_array[i], 
		max_resp_size, 
		op_tag,
		BMI_PRE_ALLOC,
		NULL, 
		&(status_array[i]), 
		&(id_array[i]));
	    if(ret < 0)
	    {
		/* immediate error */
		total_errors++;
		error_code_array[i] = ret;
		id_array[i] = 0;
	    }
	    else if (ret == 1)
	    {
		/* immediate completion */
		error_code_array[i] = status_array[i].error_code;
		if(error_code_array[i])
		{
		    total_errors++;
		}
		else
		{
		    total_completed++;
		}
		id_array[i] = 0;
	    }
	}
    }

    /* see if anything needs to be tested for completion */
    if((total_completed + total_errors) < array_size)
    {
	count = array_size;
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr("Error: PINT_server_send_req_array() critical failure.\n");
	    exit(-1);
	}

	/* all receives are completed now, fill in error codes */
	for(i=0; i<count; i++)
	{
	    error_code_array[index_array[i]] =
		status_array[i].error_code;
	    if(status_array[i].error_code)
	    {
		total_errors++;
	    }
	}
    }

    /* decode any responses that we successfully received */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    ret = PINT_decode(resp_encoded_array[i], PINT_DECODE_RESP,
		&(resp_decoded_array[i]), addr_array[i],
		status_array[i].actual_size, NULL);
	    if(ret < 0)
	    {
		error_code_array[i] = ret;
		total_errors++;
	    }
	}
    }

    gossip_ldebug(CLIENT_DEBUG, 
	"PINT_server_send_req_array() called for %d requests, %d failures.\n", 
        array_size, total_errors);

    if(total_errors)
    {
	ret = -EIO;
    }
    else
    {
	ret = 0;
    }

out:

    if(req_encoded_array)
	free(req_encoded_array);
    if(status_array)
	free(status_array);
    if(id_array)
	free(id_array);
    if(index_array)
	free(index_array);

    return(ret);
}

/* PINT_release_req_array()
 *
 * partner function to PINT_send_req_array(); should be called to
 * release resources allocated in earlier call
 *
 * no return value
 */
void PINT_release_req_array(bmi_addr_t* addr_array,
    struct PVFS_server_req_s* req_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size)
{
    int i;

    /* this really just means getting rid of the receive buffer and
     * decoded response
     */
    for(i=0; i<array_size; i++)
    {
	if(resp_decoded_array[i].buffer)
	{
	    PINT_decode_release(&(resp_decoded_array[i]),
		PINT_DECODE_RESP, REQ_ENC_FORMAT);
	}
	if(resp_encoded_array[i])
	{
	    BMI_memfree(addr_array[i], resp_encoded_array[i],
		max_resp_size, BMI_RECV_BUFFER);
	}
    }

    return;
}


/* PINT_flow_array()
 *
 * Posts and completes an array of flows.  Note that some of the entries
 * in the flow array may be NULL; these will be skipped
 *
 * returns 0 on success, -EIO on partial failure, -errno on total
 * failure
 */
int PINT_flow_array(
    flow_descriptor** flow_array,
    int* error_code_array,
    int array_size)
{
    int ret = -1;
    job_status_s* status_array = NULL;
    job_id_t* id_array = NULL;
    int* index_array = NULL;
    job_status_s tmp_status;
    int i;
    int count = 0;

    status_array = (job_status_s*)malloc(array_size * sizeof(job_status_s));
    index_array = (int*)malloc(array_size * sizeof(int));
    id_array = (job_id_t*)malloc(array_size * sizeof(job_id_t));

    if(!status_array || !index_array || !id_array)
    {
	ret = -ENOMEM;
	goto flow_array_out;
    }

    /* clear the error code array for safety */
    memset(error_code_array, 0, array_size*sizeof(int));

    for(i=0; i<array_size; i++)
    {
	if(flow_array[i])
	{
	    ret = job_flow(
		flow_array[i], 
		NULL,
		&tmp_status,
		&(id_array[i]));
	    if(ret < 0)
	    {
		error_code_array[i] = ret;
		id_array[i] = 0;
	    }
	    else if(ret == 1)
	    {
		error_code_array[i] = tmp_status.error_code;
		id_array[i] = 0;
	    }
	    else
	    {
		count++;
	    }
	}
    }

    /* test for completion if any flows are pending */
    if(count)
    {
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr(
		"Error: PINT_flow_array() critical failure.\n");
	    exit(-1);
	}

	/* all flows are complete now, fill in error codes */	
	for(i=0; i<count; i++)
	{
	    error_code_array[index_array[i]] =
		status_array[i].error_code;
	}
    }

    ret = 0;

flow_array_out:

    if(id_array)
	free(id_array);
    if(index_array)
	free(index_array);
    if(status_array)
	free(status_array);

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

/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* TODO: cleanup and reorganize some of this code */

#include <errno.h>
#include <assert.h>

#include "pint-servreq.h"
#include "job.h"
#include "pvfs2-req-proto.h"
#include "bmi.h"
#include "PINT-reqproto-encode.h"

extern job_context_id PVFS_sys_job_context;

/* PINT_release_req()
 *
 * Companion function to PINT_send_req(); it releases any
 * resources created earlier.  The caller should not use this
 * buffer until it is completely done with the response buffers
 * (decoded or encoded)
 *
 * no return value
 */
void PINT_release_req(PVFS_BMI_addr_t addr,
    struct PVFS_server_req *req_p,
    enum PVFS_encoding_type encoding,
    struct PINT_decoded_msg *decoded_resp,
    void** encoded_resp,
    PVFS_msg_tag_t op_tag)
{
    /* the only resources we need to get rid of are the decoded
     * response and the encoded response.
     */
    bmi_size_t max_resp_size = PINT_encode_calc_max_size(PINT_ENCODE_RESP,
      req_p->op, encoding);

    PINT_decode_release(
	decoded_resp,
	PINT_DECODE_RESP);

    BMI_memfree(addr, *encoded_resp, max_resp_size,
	BMI_RECV);

    return;
}

#if 0  /* unused */
/* PINT_send_req_array()
 *
 * TODO: prepost receives 
 * TODO: use timeouts rather than infinite blocking
 * TODO: try to avoid mallocing so much 
 *
 * Sends an array of requests and waits for an acknowledgement for each.
 * NOTE: this function will skip an indices with a nonzero
 * error_code_array entry; so remember to zero it out before
 * calling if needed.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_send_req_array(PVFS_BMI_addr_t* addr_array,
    struct PVFS_server_req* req_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size,
    PVFS_msg_tag_t* op_tag_array)
{
    int i;
    int ret = -1;
    struct PINT_encoded_msg* req_encoded_array = NULL;
    job_id_t* id_array = NULL;
    job_status_s* status_array = NULL;
    int* index_array = NULL;
    int count;
    int need_to_test = 0;

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
    memset(resp_encoded_array, 0, (array_size*sizeof(void*)));
    memset(req_encoded_array, 0, (array_size*sizeof(struct
	PINT_encoded_msg)));
    memset(resp_decoded_array, 0, (array_size*sizeof(struct
	PINT_decoded_msg)));

    /* encode all of the requests */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    ret = PINT_encode(&(req_array[i]), PINT_ENCODE_REQ,
		&(req_encoded_array[i]), addr_array[i], PINT_CLIENT_ENC_TYPE);
	    if(ret < 0)
	    {
		error_code_array[i] = ret;
	    }
	}
    }

    /* post a bunch of sends */
    /* keep up with job ids, and the number of immediate completions */
    memset(id_array, 0, (array_size*sizeof(job_id_t)));
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
		op_tag_array[i],
		req_encoded_array[i].buffer_type,
		1,
		NULL,
		0,
		&(status_array[i]),
		&(id_array[i]),
		PVFS_sys_job_context,
		JOB_TIMEOUT_INF);
	    gossip_err("SEND_REQ_ARRAY(): send %d returned %d.\n",
		i, ret);
	    if(ret < 0)
	    {
		/* immediate error */
		error_code_array[i] = ret;
		id_array[i] = 0;
	    }
	    else if (ret == 1)
	    {
		/* immediate completion */
		error_code_array[i] = status_array[i].error_code;
		id_array[i] = 0;
	    }
	    else
	    {
		need_to_test++;
	    }
	}
    }

    /* see if anything needs to be tested for completion */
    if(need_to_test)
    {
	gossip_err("SEND_REQ_ARRAY(): testing for %d jobs.\n",
	    need_to_test);
	count = array_size;
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1, PVFS_sys_job_context);
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
	}
    }

    /* release request encodings */
    for(i=0; i<array_size; i++)
    {
	if(req_encoded_array[i].total_size)
	{
	    PINT_encode_release(&(req_encoded_array[i]),
		PINT_ENCODE_REQ);
	}
    }

    /* allocate room for responses */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    resp_encoded_array[i] = BMI_memalloc(addr_array[i], max_resp_size,
		BMI_RECV);
	    if(!resp_encoded_array[i])
	    {
		error_code_array[i] = -ENOMEM;
	    }
	}
    }

    /* post a bunch of receives */
    /* keep up with job ids and the number of immediate completions */
    memset(id_array, 0, (array_size*sizeof(job_id_t)));
    need_to_test = 0;
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
		op_tag_array[i],
		BMI_PRE_ALLOC,
		NULL, 
		0,
		&(status_array[i]), 
		&(id_array[i]),
		PVFS_sys_job_context,
		JOB_TIMEOUT_INF);
	    gossip_err("SEND_REQ_ARRAY(): recv %d returned %d.\n",
		i, ret);
	    if(ret < 0)
	    {
		/* immediate error */
		error_code_array[i] = ret;
		id_array[i] = 0;
	    }
	    else if (ret == 1)
	    {
		/* immediate completion */
		error_code_array[i] = status_array[i].error_code;
		id_array[i] = 0;
		if(status_array[i].error_code == 0)
		    assert(status_array[i].actual_size == max_resp_size);
	    }
	    else
	    {
		need_to_test++;
	    }
	}
    }

    /* see if anything needs to be tested for completion */
    if(need_to_test)
    {
	gossip_err("SEND_REQ_ARRAY(): testing for %d jobs.\n",
	    need_to_test);
	count = array_size;
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1, PVFS_sys_job_context);
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
	    if(status_array[i].error_code == 0)
		assert(status_array[i].actual_size == max_resp_size);
	}
    }

    /* decode any responses that we successfully received */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    ret = PINT_decode(resp_encoded_array[i], PINT_DECODE_RESP,
		&(resp_decoded_array[i]), addr_array[i],
		max_resp_size);
	    if(ret < 0)
	    {
		error_code_array[i] = ret;
	    }
	}
    }

    ret = 0;

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
void PINT_release_req_array(PVFS_BMI_addr_t* addr_array,
    struct PVFS_server_req* req_array,
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
		PINT_DECODE_RESP);
	}
	if(resp_encoded_array[i])
	{
	    BMI_memfree(addr_array[i], resp_encoded_array[i],
		max_resp_size, BMI_RECV);
	}
    }

    return;
}
#endif  /* unused */

/* PINT_recv_ack_array()
 *
 * TODO: use timeouts rather than infinite blocking
 * TODO: try to avoid mallocing so much 
 *
 * Receives an array of acknowledgements.
 *
 * NOTE: this function will skip an indices with a nonzero
 * error_code_array entry; so remember to zero it out before
 * calling if needed.  
 *
 * returns 0 on success, -errno on failure
 */
int PINT_recv_ack_array(PVFS_BMI_addr_t* addr_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size,
    PVFS_msg_tag_t* op_tag_array)
{
    int i;
    int ret = -1;
    job_id_t* id_array = NULL;
    job_status_s* status_array = NULL;
    int* index_array = NULL;
    int count;
    int need_to_test = 0;

    /* allocate some bookkeeping fields */
    status_array = (job_status_s*)malloc(array_size *
	sizeof(job_status_s));
    index_array = (int*)malloc(array_size * sizeof(int));
    id_array = (job_id_t*)malloc(array_size * sizeof(job_id_t));

    if(!status_array || !id_array || !index_array)
    {
	ret = -ENOMEM;
	goto out;
    }

    /* clear some of the arrays for safety */
    memset(resp_encoded_array, 0, (array_size*sizeof(void*)));
    memset(resp_decoded_array, 0, (array_size*sizeof(struct
	PINT_decoded_msg)));

    /* allocate room for responses */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    resp_encoded_array[i] = BMI_memalloc(addr_array[i], max_resp_size,
		BMI_RECV);
	    if(!resp_encoded_array[i])
	    {
		error_code_array[i] = -ENOMEM;
	    }
	}
    }

    /* post a bunch of receives */
    /* keep up with job ids and the number of immediate completions */
    memset(id_array, 0, (array_size*sizeof(job_id_t)));
    need_to_test = 0;
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
		op_tag_array[i],
		BMI_PRE_ALLOC,
		NULL, 
		0,
		&(status_array[i]), 
		&(id_array[i]),
		PVFS_sys_job_context,
		JOB_TIMEOUT_INF);
	    if(ret < 0)
	    {
		/* immediate error */
		error_code_array[i] = ret;
		id_array[i] = 0;
	    }
	    else if (ret == 1)
	    {
		/* immediate completion */
		error_code_array[i] = status_array[i].error_code;
		id_array[i] = 0;
		if(status_array[i].error_code == 0)
		    assert(status_array[i].actual_size == max_resp_size);
	    }
	    else
	    {
		need_to_test++;
	    }
	}
    }

    /* see if anything needs to be tested for completion */
    if(need_to_test)
    {
	count = array_size;
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1, PVFS_sys_job_context);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr("Error: PINT_recv_ack_array() critical failure.\n");
	    exit(-1);
	}

	/* all receives are completed now, fill in error codes */
	for(i=0; i<count; i++)
	{
	    error_code_array[index_array[i]] =
		status_array[i].error_code;
	    if(status_array[i].error_code == 0)
		assert(status_array[i].actual_size == max_resp_size);
	}
    }

    /* decode any responses that we successfully received */
    for(i=0; i<array_size; i++)
    {
	if(!(error_code_array[i]))
	{
	    ret = PINT_decode(resp_encoded_array[i], PINT_DECODE_RESP,
		&(resp_decoded_array[i]), addr_array[i],
		max_resp_size);
	    if(ret < 0)
	    {
		error_code_array[i] = ret;
	    }
	}
    }

    ret = 0;

out:

    if(status_array)
	free(status_array);
    if(id_array)
	free(id_array);
    if(index_array)
	free(index_array);

    return(ret);
}


/* PINT_release_ack_array()
 *
 * partner function to PINT_recv_ack_array(); should be called to
 * release resources allocated in earlier call
 *
 * no return value
 */
void PINT_release_ack_array(PVFS_BMI_addr_t* addr_array,
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
		PINT_DECODE_RESP);
	}
	if(resp_encoded_array[i])
	{
	    BMI_memfree(addr_array[i], resp_encoded_array[i],
		max_resp_size, BMI_RECV);
	}
    }

    return;
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

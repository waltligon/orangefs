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

/* server_send_req_array()
 *
 * TODO: prepost receives 
 * TODO: add more gossip output
 *
 * Sends an array of requests and waits for an acknowledgement for each.
 * Fills in PINT_decoded_msg array pointed to by decoded_array.  The
 * caller must call PINT_decode_release() for each array element later
 * on.
 *
 */
int PINT_server_send_req_array(bmi_addr_t* addr_array,
			 struct PVFS_server_req_s* req_array,
			 bmi_size_t max_resp_size,
			 void** resp_encoded_array,
			 struct PINT_decoded_msg* resp_decoded_array,
			 int* error_code_array,
			 int array_size)
{
    int i, j;
    int ret = -1;
    struct PINT_encoded_msg* req_encoded_array = NULL;
    PVFS_msg_tag_t op_tag = get_next_session_tag();
    job_id_t* id_array = NULL;
    job_status_s* status_array = NULL;
    int* index_array = NULL;
    int total_completed = 0;
    int count;

    enum
    {
	NONE_FAIL,
	MEM_FAIL
    } failure_state = NONE_FAIL;

    /* clear some of the arrays for safety */
    memset(error_code_array, 0, (array_size*sizeof(int)));
    memset(resp_encoded_array, 0, (array_size*sizeof(void*)));

    /* allocate some bookkeeping fields */
    req_encoded_array = (struct PINT_encoded_msg*)malloc(array_size *
	sizeof(struct PINT_encoded_msg));
    status_array = (job_status_s*)malloc(array_size *
	sizeof(job_status_s));
    index_array = (int*)malloc(array_size * sizeof(int));
    id_array = (job_id_t*)malloc(array_size * sizeof(job_id_t));

    if(!req_encoded_array || !status_array || !id_array)
    {
	ret = -ENOMEM;
	failure_state = MEM_FAIL;
	goto out;
    }

    /* encode all of the requests */
    for(i=0; i<array_size; i++)
    {
	ret = PINT_encode(&(req_array[i]), PINT_ENCODE_REQ,
	    &(req_encoded_array[i]), addr_array[i], REQ_ENC_FORMAT);
	if(ret < 0)
	{
	    /* go back and release earlier encodings */
	    for(j=(i-1); j>-1; j++)
	    {
		PINT_encode_release(&(req_encoded_array[i]),
		    PINT_ENCODE_REQ, REQ_ENC_FORMAT);
	    }
	    failure_state = MEM_FAIL;
	    goto out;
	}
    }

    /* post a bunch of sends */
    /* keep up with job ids, and the number of immediate completions */
    total_completed = 0;
    for(i=0; i<array_size; i++)
    {
	ret = job_bmi_send_list(
	    req_encoded_array[i].dest, 
	    req_encoded_array[i].buffer_list,
	    req_encoded_array[i].size_list,
	    req_encoded_array[i].list_count,
	    req_encoded_array[i].total_size,
	    op_tag,
	    req_encoded_array[i].buffer_flag,
	    0,
	    NULL,
	    &(status_array[i]),
	    &(id_array[i]));
	if(ret < 0)
	{
	    /* set the error code array entry; as a side effect, this
	     * will keep us from looking for a response to this
	     * particular request later 
	     */
	    total_completed++;
	    error_code_array[i] = ret;
	    id_array[i] = 0;
	}
	else if (ret == 1)
	{
	    /* immediate completion */
	    /* increment our completed count, make sure this id_array
	     * entry is cleared
	     */
	    total_completed++;
	    error_code_array[i] = status_array[i].error_code;
	    id_array[i] = 0;
	}
    }

    /* see if anything needs to be tested for completion */
    if(total_completed < array_size)
    {
	count = array_size;
	/* TODO: do a timeout here later */
	ret = job_testsome(id_array, &count, index_array, NULL,
	    status_array, -1);
	if(ret < 0)
	{
	    /* TODO: there is no real way cleanup from this right now */
	    gossip_lerr("Error: PINT_server_send_req_array() critical failure.\n");
	    exit(-1);
	}
	    
	/* all sends are complete now, fill in error codes */
	total_completed = array_size;
	for(i=0; i<count; i++)
	{
	    error_code_array[index_array[i]] =
		status_array[i].error_code;
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
	    }
	}
    }

    /* TODO: add cleanup of resp_encoded_array at bottom */
    /* TODO: this function isn't completed yet */
    ret = -ENOSYS;

out:

    switch(failure_state)
    {
	case NONE_FAIL:
	case MEM_FAIL:
	    if(req_encoded_array)
		free(req_encoded_array);
	    if(status_array)
		free(status_array);
	    if(id_array)
		free(id_array);
	    if(index_array)
		free(index_array);
	    break;
    }
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

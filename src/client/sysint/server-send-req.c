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
    printf(" sent\n");

    /* post a blocking receive job */
    ret = job_bmi_recv_blocking(addr, encoded_resp, max_resp_size, op_tag, BMI_PRE_ALLOC, &r_status);
#if 0
    printf("message recieved: r_status.actual_size = %d\n",r_status.actual_size);
    printf("r_status.error_code = %d\nreturn value = %d\n",r_status.error_code, ret);
#endif
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
	    printf("%d > 0\n", ret);
	    printf("%d != 1, r_status.error_code == %d\n", ret, r_status.error_code );
	    printf("r_status.actual_size == %d\n", r_status.actual_size );
	    printf("max_resp_size == %d\n", max_resp_size );
	}
#endif
    }
    printf("job_bmi call was successfull\n");

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
    printf(" recv'ed\n");

    /* free encoded_resp buffer */
    ret = BMI_memfree(addr, encoded_resp, max_resp_size, BMI_RECV_BUFFER);
    assert(ret == 0);

    return 0;
 return_error:
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

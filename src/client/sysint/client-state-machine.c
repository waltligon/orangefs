/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "pvfs2-client-state-machine.h"
#include "state-machine-fns.h"
#include "pvfs2-debug.h"
#include "job.h"
#include "gossip.h"

/* from original remove.c */
#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "pint-dcache.h"
#include "pint-bucket.h"
#include "pcache.h"
#include "PINT-reqproto-encode.h"

job_context_id pint_client_sm_context;

/* PINT_serv_prepare_msgpair()
 *
 * TODO: cache some values locally and assign at the end.
 */
int PINT_serv_prepare_msgpair(PVFS_pinode_reference object_ref,
			      struct PVFS_server_req *req_p,
			      struct PINT_encoded_msg *encoded_req_out_p,
			      void **encoded_resp_out_pp,
			      bmi_addr_t *svr_addr_p,
			      int *max_resp_sz_out_p,
			      PVFS_msg_tag_t *session_tag_out_p)
{
    int ret;

    /* must determine destination server before we can encode;
     * this fills in sm_p->svr_addr.
     */
    ret = PINT_bucket_map_to_server(svr_addr_p,
				    object_ref.handle,
				    object_ref.fs_id);
    if (ret < 0) {
	assert(0);
    }

    /* encode request */
    ret = PINT_encode(req_p,
		      PINT_ENCODE_REQ,
		      encoded_req_out_p,
		      *svr_addr_p,
		      PINT_CLIENT_ENC_TYPE);
    if (ret < 0) {
	assert(0);
    }

    /* calculate maximum response message size and allocate space */
    *max_resp_sz_out_p = PINT_encode_calc_max_size(PINT_ENCODE_RESP,
						  req_p->op,
						  PINT_CLIENT_ENC_TYPE);

    *encoded_resp_out_pp = BMI_memalloc(*svr_addr_p,
					*max_resp_sz_out_p,
					BMI_RECV);
    if (*encoded_resp_out_pp == NULL) {
	assert(0);
    }

    /* get session tag to associate with send and receive */
    *session_tag_out_p = get_next_session_tag();

    return 0;
}

int PINT_serv_decode_resp(void *encoded_resp_p,
			  struct PINT_decoded_msg *decoded_resp_p,
			  bmi_addr_t *svr_addr_p,
			  int actual_resp_sz,
			  struct PVFS_server_resp **resp_out_pp)
{
    int ret;

    /* decode response */
    ret = PINT_decode(encoded_resp_p,
		      PINT_DECODE_RESP,
		      decoded_resp_p, /* holds data on decoded resp */
		      *svr_addr_p,
		      actual_resp_sz);
    if (ret < 0) {
	assert(0);
    }

    /* point a reasonably typed pointer at the response data */
    *resp_out_pp = (struct PVFS_server_resp *) decoded_resp_p->buffer;

    return 0;
}

int PINT_serv_free_msgpair_resources(struct PINT_encoded_msg *encoded_req_p,
				     void *encoded_resp_p,
				     struct PINT_decoded_msg *decoded_resp_p,
				     bmi_addr_t *svr_addr_p,
				     int max_resp_sz)
{
    PINT_encode_release(encoded_req_p,
			PINT_ENCODE_REQ);

    /* sm_p->req doesn't go anywhere; we'll use it again. */

    PINT_decode_release(decoded_resp_p,
			PINT_DECODE_RESP);

    BMI_memfree(*svr_addr_p,
		encoded_resp_p,
		max_resp_sz,
		BMI_RECV);

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

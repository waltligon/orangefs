/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __MSGPAIRARRAY_H
#define __MSGPAIRARRAY_H

#include "pvfs2-types.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "job.h"

/*
 * This structure holds everything that we need for the state of a
 * message pair.  We need arrays of these in some cases, so it's
 * convenient to group it like this.
 *
 */
typedef struct PINT_sm_msgpair_state_s
{
    /* NOTE: fs_id, handle, retry flag, and comp_fn, should be filled
     * in prior to going into the msgpair code path.
     */
    PVFS_fs_id fs_id;
    PVFS_handle handle;

    /* should be either PVFS_MSGPAIR_RETRY, or PVFS_MSGPAIR_NO_RETRY*/
    int retry_flag;

    /* don't use this -- internal msgpairarray use only */
    int retry_count;

    /* comp_fn called after successful reception and decode of
     * respone, if the msgpair state machine is used for processing.
     */
    int (* comp_fn)(void *sm_p, struct PVFS_server_resp *resp_p, int i);

    /* comp_ct used to keep up with number of operations remaining */
    int comp_ct;

    /* server address */
    PVFS_BMI_addr_t svr_addr;

    /* req and encoded_req are used to send a request */
    struct PVFS_server_req req;
    struct PINT_encoded_msg encoded_req;

    /* max_resp_sz, svr_addr, and encoded_resp_p used to recv a response */
    int max_resp_sz;
    void *encoded_resp_p;

    /* send_id, recv_id used to track completion of operations */
    job_id_t send_id, recv_id;
    /* send_status, recv_status used for error handling etc. */
    job_status_s send_status, recv_status;

    /* op_status is the code returned from the server, if the
     * operation was actually processed (recv_status.error_code == 0)
     */
    PVFS_error op_status;

    /*
      used in the retry code path to know if we've already completed
      or not (to avoid re-doing the work we've already done)
    */
    int complete;

} PINT_sm_msgpair_state;

#endif /* __MSGPAIRARRAY_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */


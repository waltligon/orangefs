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


extern struct PINT_state_machine_s pvfs2_msgpairarray_sm;

/*
  the following values are to be used by the struct
  PINT_sm_msgpair_state_s message's retry_flag variable
*/
#define PVFS_MSGPAIR_RETRY          0xFE
#define PVFS_MSGPAIR_NO_RETRY       0xFF

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
    
    int (* comp_fn)(void *sm_p, struct PVFS_server_resp *resp_p, int i);

    /* server address */
    PVFS_BMI_addr_t svr_addr;

    /* req and encoded_req are used to send a request */
    struct PVFS_server_req req;
    struct PINT_encoded_msg encoded_req;

    PVFS_msg_tag_t session_tag; /*can be prefilled to reuse the same session tag*/
    
    /* the encoding type to use for the req */
    enum PVFS_encoding_type enc_type;

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

/* used to pass in parameters that apply to every entry in a msgpair array */
typedef struct PINT_sm_msgpair_params_s
{   
    int job_timeout;
    int retry_delay;
    int retry_limit;
    job_context_id job_context;

    /* comp_ct used to keep up with number of operations remaining */
    int comp_ct;

} PINT_sm_msgpair_params;


/* helper functions */

int PINT_msgpairarray_init(
    PINT_sm_msgpair_state ** msgpairarray,
    int count);

void PINT_msgpairarray_destroy(PINT_sm_msgpair_state * msgpairarray);

int PINT_serv_decode_resp(
    PVFS_fs_id fs_id,
    void *encoded_resp_p,
    struct PINT_decoded_msg *decoded_resp_p,
    PVFS_BMI_addr_t *svr_addr_p,
    int actual_resp_sz,
    struct PVFS_server_resp **resp_out_pp);

int PINT_serv_free_msgpair_resources(
    struct PINT_encoded_msg *encoded_req_p,
    void *encoded_resp_p,
    struct PINT_decoded_msg *decoded_resp_p,
    PVFS_BMI_addr_t *svr_addr_p,
    int max_resp_sz);

int PINT_serv_msgpairarray_resolve_addrs(
    int count, 
    PINT_sm_msgpair_state* msgarray);

#define PRINT_ENCODING_ERROR(type_str, type)                        \
do {                                                                \
    gossip_err("***********************************************"    \
               "********************\n");                           \
    gossip_err("Encoding type (%d) is not %s. Trying all "          \
               "supported encoders.\n\n", type, type_str);          \
    gossip_err("Please check that your pvfs2tab file has a "        \
               "%s encoding entry,\n  or use the default "          \
               "by using \"encoding=default\"\n", type_str);        \
    gossip_err("***********************************************"    \
               "********************\n");                           \
} while(0)


#define PINT_init_msgpair(sm_p, msg_p)                         \
do {                                                           \
    msg_p = &sm_p->msgpair;                                    \
    memset(msg_p, 0, sizeof(PINT_sm_msgpair_state));           \
    if (sm_p->msgarray && (sm_p->msgarray != &(sm_p->msgpair)))\
    {                                                          \
        free(sm_p->msgarray);                                  \
        sm_p->msgarray = NULL;                                 \
    }                                                          \
    sm_p->msgarray = msg_p;                                    \
    sm_p->msgarray_count = 1;                                  \
} while(0)


#define PINT_init_msgarray_params(msgarray_params_ptr, __fsid)     \
do {                                                               \
    PINT_sm_msgpair_params *mpp = msgarray_params_ptr;             \
    struct server_configuration_s *server_config =                 \
        PINT_get_server_config_struct(__fsid);                     \
    mpp->job_context = pint_client_sm_context;                     \
    if (server_config)                                             \
    {                                                              \
        mpp->job_timeout = server_config->client_job_bmi_timeout;  \
        mpp->retry_limit = server_config->client_retry_limit;      \
        mpp->retry_delay = server_config->client_retry_delay_ms;   \
    }                                                              \
    else                                                           \
    {                                                              \
        mpp->job_timeout = PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT;   \
        mpp->retry_limit = PVFS2_CLIENT_RETRY_LIMIT_DEFAULT;       \
        mpp->retry_delay = PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT;    \
    }                                                              \
    PINT_put_server_config_struct(server_config);                  \
} while(0)

#define PINT_init_msgarray_params_server(msgarray_params_ptr)      \
do {                                                               \
    PINT_sm_msgpair_params *mpp = msgarray_params_ptr;             \
    struct server_configuration_s *server_config =                 \
        get_server_config_struct();                                \
    mpp->job_context = server_job_context;                         \
    if (server_config)                                             \
    {                                                              \
        mpp->job_timeout = server_config->client_job_bmi_timeout;  \
        mpp->retry_limit = server_config->client_retry_limit;      \
        mpp->retry_delay = server_config->client_retry_delay_ms;   \
    }                                                              \
    else                                                           \
    {                                                              \
        mpp->job_timeout = PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT;   \
        mpp->retry_limit = PVFS2_CLIENT_RETRY_LIMIT_DEFAULT;       \
        mpp->retry_delay = PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT;    \
    }                                                              \
} while(0)


#endif /* __MSGPAIRARRAY_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


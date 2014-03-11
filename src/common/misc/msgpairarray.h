/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* mps.h
 *
 * This modified version is for PVFS v3/OrangeFS Next it combines the
 * old msgpairarray functionality with io and smallio and adds mirrored
 * objects capability.
 */


#ifndef __MSGPAIRARRAY_H
#define __MSGPAIRARRAY_H

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "job.h"

/* these are used as the status_user_tag for bmi jobs */
typedef enum msgphase_t {
    PVFS_MPA_SEND,
    PVFS_MPA_RECV,
    PVFS_MPA_FLOW,
    PVFS_MPA_ACK
} msgphase_t;

typedef enum msgaction_t {
    PVFS_MPA_OK,
    PVFS_MPA_RETRY,
    PVFS_MPA_FAIL
} msgaction_t;

extern struct PINT_state_machine_s pvfs2_msgpairarray_sm;

/*
  the following values are to be used by the struct
  PINT_sm_msgpair_state_s message's retry_flag variable
*/
#define PVFS_MSGPAIR_RETRY          0xFE
#define PVFS_MSGPAIR_NO_RETRY       0xFF

#define PINT_MSGPAIR_PARENT_SM -1

/*
 * This structure holds everything that we need for the state of a
 * message pair.  We need arrays of these in some cases, so it's
 * convenient to group it like this.
 *
 */
typedef struct PINT_sm_msgpair_state_s
{
    /* These should be set before starting MPA */
    PVFS_io_class msgclass; /* is this regular IO, SMALL_IO, or METADATA */
    PVFS_io_type  msgdir;   /* READ or WRITE */

    /* NOTE: fs_id, handle, retry flag, and comp_fn, should be filled
     * in prior to going into the msgpair code path.
     */
    int         server_nr; /* for IO and SMALL_IO */
    PVFS_fs_id  fs_id;
    PVFS_handle handle;

    /* should be either PVFS_MSGPAIR_RETRY, or PVFS_MSGPAIR_NO_RETRY*/
    int retry_flag;

    /* don't use this -- internal msgpairarray use only */
    int retry_count;

    int (* comp_fn)(void *sm_p, struct PVFS_server_resp *resp_p, int i);

    /* server address - corresponds to sid_array[sid_index] */
    /* initialize to address of sid_array[0] */
    PVFS_BMI_addr_t svr_addr;
    /* number of sids in the array */
    /* if this is 1 should never try to access sid_array */
    int sid_count;
    /* which SID we are using - always initialize to 0 */
    int sid_index;
    /* pointer to alternate sids for the target object */
    /* if this is NULL there are no alternates, sid_count should be 1 */
    PVFS_SID *sid_array;

    /* session identifier between send and rcvs*/
    /* note:  server-side i/o machine uses the session_tag as the flow */
    /* descriptor tag.                                                 */
    bmi_msg_tag_t session_tag;

    /* req and encoded_req are used to send a request */
    struct PVFS_server_req  req;
    struct PINT_encoded_msg encoded_req;

    /* the encoding type to use for the req */
    enum PVFS_encoding_type enc_type;

    /* max_resp_sz, svr_addr, and encoded_resp_p used to recv a response */
    int  max_resp_sz;
    void *encoded_resp_p;

    /* send_id, recv_id used to track completion of operations */
    job_id_t send_id, recv_id, flow_id, ack_id;

    /* send_status, recv_status used for error handling etc. */
    job_status_s send_status, recv_status, flow_status, ack_status;

    /* descriptor for flows */
    flow_descriptor flow_desc;

    /* op_status is the code returned from the server, if the
     * operation was actually processed (recv_status.error_code == 0)
     */
    PVFS_error  op_status;
    msgaction_t op_action;

    /* used in the retry code path to know if we've already completed
     * or not (to avoid re-doing the work we've already done)
     */
    int complete;

    /* variables used to be in sm_p */
    int dfile_size;

} PINT_sm_msgpair_state;

/* used to pass in parameters that apply to every entry in a msgpair array */
typedef struct
{   
    int job_timeout;
    int retry_delay;
    int retry_limit;
    job_context_id job_context;
    int quiet_flag;   /* if set, cuts down on error messages during retry */

    /* comp_ct used to keep up with number of operations remaining */
    int send_ct;
    int recv_ct;
    int flow_ct;
    int ack_ct;

} PINT_sm_msgpair_params;

/* flow parameters - not needed for SMALL_IO or METADATA */
typedef struct
{
    enum PVFS_flowproto_type flowproto_type;
    enum PVFS_io_type io_type;
    PVFS_Request mem_req;
    PVFS_Request file_req;
    PVFS_offset file_req_offset;
    void *buffer;
    PVFS_size *dfile_size_array;
    PVFS_object_attr *attr;
    enum PVFS_encoding_type encoding;
    PVFS_hint hints;
} PINT_sm_msgpair_flow;

typedef struct PINT_sm_msgarray_op
{
    PINT_sm_msgpair_params params;   /* these are shared */
    int count;                       /* the same for all message pairs */
    int index;                       /* this is unique for each message pair */
    PINT_sm_msgpair_state *msgarray; /* these are shared */
    PINT_sm_msgpair_state msgpair;   /* for single message pair jobs */
    int total_size;
    PINT_sm_msgpair_flow *flow_params;
} PINT_sm_msgarray_op;

#define PINT_msgpair_init(op)                                     \
    do {                                                          \
        memset(&(op)->msgpair, 0, sizeof(PINT_sm_msgpair_state)); \
        if((op)->msgarray != &(op)->msgpair)                      \
        {                                                         \
            free((op)->msgarray);                                 \
            (op)->msgarray = NULL;                                \
        }                                                         \
        (op)->count = 1;                                          \
        (op)->msgarray = &(op)->msgpair;                          \
    } while(0)

#define foreach_msgpair(__msgarray_op, __msg_p, __i)          \
    for(__i = 0, __msg_p = &((__msgarray_op)->msgarray[__i]); \
        __i < (__msgarray_op)->count;                         \
        ++__i, __msg_p = &((__msgarray_op)->msgarray[__i]))

/* helper functions */

int PINT_msgpairarray_init(
    PINT_sm_msgarray_op *op,
    int count);

void PINT_msgpairarray_destroy(PINT_sm_msgarray_op *op);

int PINT_msgarray_status(PINT_sm_msgarray_op *op);

int PINT_serv_decode_resp(PVFS_fs_id fs_id,
                          void *encoded_resp_p,
                          struct PINT_decoded_msg *decoded_resp_p,
                          PVFS_BMI_addr_t *svr_addr_p,
                          int actual_resp_sz,
                          struct PVFS_server_resp **resp_out_pp);

int PINT_serv_free_msgpair_resources(struct PINT_encoded_msg *encoded_req_p,
                                     void *encoded_resp_p,
                                     struct PINT_decoded_msg *decoded_resp_p,
                                     PVFS_BMI_addr_t *svr_addr_p,
                                     int max_resp_sz);

int PINT_serv_msgpairarray_resolve_addrs(PINT_sm_msgarray_op *op);

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

#define PINT_init_msgpair(__sm_p, __msg_p)                         \
do {                                                           \
    __msg_p = &__sm_p->msgpair;                                    \
    memset(__msg_p, 0, sizeof(PINT_sm_msgpair_state));           \
    if (__sm_p->msgarray && (__sm_p->msgarray != &(__sm_p->msgpair)))\
    {                                                          \
        free(__sm_p->msgarray);                                  \
        __sm_p->msgarray = NULL;                                 \
    }                                                          \
    __sm_p->msgarray = __msg_p;                                    \
    __sm_p->msgarray_count = 1;                                  \
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


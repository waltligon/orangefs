/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _PINT_SERVREQ_H
#define _PINT_SERVREQ_H

#include "PINT-reqproto-encode.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pvfs2-req-proto.h"

int PINT_send_req_array(bmi_addr_t* addr_array,
    struct PVFS_server_req* req_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size,
    PVFS_msg_tag_t* op_tag_array);

void PINT_release_req_array(bmi_addr_t* addr_array,
    struct PVFS_server_req* req_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size);

int PINT_send_req(bmi_addr_t addr,
    struct PVFS_server_req *req_p,
    bmi_size_t max_resp_size,
    struct PINT_decoded_msg *decoded_resp,
    void** encoded_resp,
    PVFS_msg_tag_t op_tag);

void PINT_release_req(bmi_addr_t addr,
    struct PVFS_server_req *req_p,
    bmi_size_t max_resp_size,
    struct PINT_decoded_msg *decoded_resp,
    void** encoded_resp,
    PVFS_msg_tag_t op_tag);

int PINT_recv_ack_array(bmi_addr_t* addr_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size,
    PVFS_msg_tag_t* op_tag_array);

void PINT_release_ack_array(bmi_addr_t* addr_array,
    bmi_size_t max_resp_size,
    void** resp_encoded_array,
    struct PINT_decoded_msg* resp_decoded_array,
    int* error_code_array,
    int array_size);

/* dunno where these belong, but here is better than nowhere. -- rob */
void debug_print_type(void* thing, int type);
PVFS_msg_tag_t get_next_session_tag(void);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
#endif

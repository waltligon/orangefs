/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "bmi.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include <stdlib.h>
#include <errno.h>
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

int do_encode_req(
    struct PVFS_server_req *request,
    struct PINT_encoded_msg *target_msg,
    int header_size);
int do_encode_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg,
    int header_size);
int do_decode_resp(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr);
int do_decode_req(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr);
void do_decode_rel(
    struct PINT_decoded_msg *msg,
    enum PINT_encode_msg_type input_type);
void do_encode_rel(
    struct PINT_encoded_msg *msg,
    enum PINT_encode_msg_type input_type);

int do_encode_gen_ack_sz(
    int);
void init_contig(
    void);

PINT_encoding_functions_s contig_buffer_functions = {
    do_encode_req,
    do_encode_resp,
    do_decode_req,
    do_decode_resp,
    do_encode_rel,
    do_decode_rel,
    do_encode_gen_ack_sz
};

PINT_encoding_table_values_s contig_buffer_table = {
    &contig_buffer_functions,
    "Contiguous",
    init_contig
};

int do_encode_gen_ack_sz(
    int op)
{
    return sizeof(struct PVFS_server_resp) + ENCODED_HEADER_SIZE;
}

void init_contig(
    void)
{
    contig_buffer_table.op = &contig_buffer_functions;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

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
    struct PINT_encoded_msg *target_msg);
int do_encode_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg);
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
int do_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type);
void init_contig(
    void);

PINT_encoding_functions_s contig_buffer_functions = {
    do_encode_req,
    do_encode_resp,
    do_decode_req,
    do_decode_resp,
    do_encode_rel,
    do_decode_rel,
    do_encode_calc_max_size
};

PINT_encoding_table_values_s contig_buffer_table = {
    &contig_buffer_functions,
    "Contiguous",
    init_contig
};

void init_contig(
    void)
{
    contig_buffer_table.op = &contig_buffer_functions;
}

int do_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type)
{
    int size = sizeof(struct PVFS_server_resp) + PINT_ENC_GENERIC_HEADER_SIZE;

    if(input_type != PINT_ENCODE_RESP)
    {
	gossip_lerr("Not supported yet.\n");
	return(-ENOSYS);
    }

    switch(op_type)
    {
	case PVFS_SERV_GETATTR:
	    size += PVFS_REQ_LIMIT_DIST_BYTES;
	    size += (PVFS_REQ_LIMIT_DFILE_COUNT*sizeof(PVFS_handle));
	    break;
	case PVFS_SERV_GETCONFIG:
	    size += (PVFS_REQ_LIMIT_CONFIG_FILE_BYTES*2);
	    break;
	case PVFS_SERV_LOOKUP_PATH:
	    size += (PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT*sizeof(PVFS_object_attr));
	    size += (PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT*sizeof(PVFS_handle));
	    break;
	case PVFS_SERV_READDIR:
	    size += (PVFS_REQ_LIMIT_DIRENT_COUNT*sizeof(PVFS_dirent));
	    break;
	case PVFS_SERV_MGMT_PERF_MON:
	    size += (PVFS_REQ_LIMIT_MGMT_PERF_MON_COUNT
		* sizeof(struct PVFS_mgmt_perf_stat));
	    break;
	default:
	    break;
    }

    return(size);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

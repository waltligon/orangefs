/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>

ENCODE_REQ_HEAD(do_encode_req);
ENCODE_RESP_HEAD(do_encode_resp);
DECODE_RESP_HEAD(do_decode_resp);
DECODE_REQ_HEAD(do_decode_req);
DECODE_REL_HEAD(do_decode_rel);
ENCODE_REL_HEAD(do_encode_rel);
int do_encode_gen_ack_sz(int);
void init_contig(void);

PINT_encoding_functions_s contig_buffer_functions =
{
	do_encode_req,
	do_encode_resp,
	do_decode_req,
	do_decode_resp,
	do_encode_rel,
	do_decode_rel,
	do_encode_gen_ack_sz
};

PINT_encoding_table_values_s contig_buffer_table =
{
	&contig_buffer_functions,
	"Contiguous",
	init_contig
};

int do_encode_gen_ack_sz(int op)
{
	return sizeof(struct PVFS_server_resp) + ENCODED_HEADER_SIZE;
}

void init_contig(void)
{
	contig_buffer_table.op = &contig_buffer_functions;
}

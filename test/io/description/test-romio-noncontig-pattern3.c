/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "pint-distribution.h"
#include "pint-dist-utils.h"
#include "pvfs2-request.h"
#include "pint-request.h"

#define SEGMAX 16
#define BYTEMAX (256*1024)

int main(
    int argc,
    char **argv)
{
    int i;
    PINT_Request *file_req;
    PINT_Request *mem_req;
    PINT_Request_state *file_state;
    PINT_request_file_data rf1;
    PINT_Request_result seg1;
    int32_t *len_array = NULL;
    PVFS_offset *off_array = NULL;
    PVFS_size total_bytes_client = 0;

    /* PVFS_Process_request arguments */
    int retval;

    len_array = (int32_t *) malloc(17 * sizeof(int32_t));
    off_array = (PVFS_offset *) malloc(17 * sizeof(PVFS_offset));
    assert(len_array != NULL && off_array != NULL);

    /* setup file datatype */
    len_array[0] = 4;
    off_array[0] = 327552;
    for (i = 1; i < 17; i++)
    {
	len_array[i] = 4;
	off_array[i] = off_array[0] + i * 8;
    }
    PVFS_Request_hindexed(17, len_array, off_array, PVFS_BYTE, &file_req);

    /* setup mem datatype */
    len_array[0] = 4;
    off_array[0] = 135295720;
    for (i = 1; i < 17; i++)
    {
	len_array[i] = 4;
	off_array[i] = off_array[0] + i * 8;
    }
    PVFS_Request_hindexed(17, len_array, off_array, PVFS_BYTE, &mem_req);

    file_state = PINT_new_request_state(file_req);

    /* set up file data for request */
    PINT_dist_initialize(NULL);
    rf1.server_nr = 0;
    rf1.server_ct = 4;
    rf1.fsize = 0;
    rf1.dist = PINT_dist_create("simple_stripe");
    rf1.extend_flag = 1;
    PINT_dist_lookup(rf1.dist);

    /* set up result struct */
    seg1.offset_array = (int64_t *) malloc(SEGMAX * sizeof(int64_t));
    seg1.size_array = (int64_t *) malloc(SEGMAX * sizeof(int64_t));
    seg1.bytemax = BYTEMAX;
    seg1.segmax = SEGMAX;
    seg1.bytes = 0;
    seg1.segs = 0;

    PINT_REQUEST_STATE_RESET(file_state);


    PINT_REQUEST_STATE_SET_TARGET(file_state, 0);
    PINT_REQUEST_STATE_SET_FINAL(file_state, PINT_REQUEST_TOTAL_BYTES(mem_req));


    /* Turn on debugging */
    // gossip_enable_stderr();
    // gossip_set_debug_mask(1,REQUEST_DEBUG); 

    /* process request */
    retval = PINT_process_request(file_state, NULL, &rf1, &seg1, PINT_SERVER);

    assert(retval >= 0);

    printf("results of PINT_process_request():\n");
    printf("%d segments with %lld bytes\n", seg1.segs, lld(seg1.bytes));
    total_bytes_client += seg1.bytes;
    for (i = 0; i < seg1.segs; i++)
    {
	printf("  segment %d: offset: %d size: %d\n",
	       i, (int) seg1.offset_array[i], (int) seg1.size_array[i]);
    }

    printf("PINT_REQUEST_DONE: %d\n", PINT_REQUEST_DONE(file_state));

    if (!PINT_REQUEST_DONE(file_state))
    {
	fprintf(stderr,
		"NEXT call to PINT_REQUEST_DONE should return 0.\n");

	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;

	/* process request */
	retval = PINT_process_request(file_state, NULL, &rf1,
				      &seg1, PINT_SERVER);

	assert(retval >= 0);

	printf("results of PINT_process_request():\n");
	printf("%d segments with %lld bytes\n", seg1.segs, lld(seg1.bytes));
	total_bytes_client += seg1.bytes;
	for (i = 0; i < seg1.segs; i++)
	{
	    printf("  segment %d: offset: %d size: %d\n",
		   i, (int) seg1.offset_array[i], (int) seg1.size_array[i]);
	}

	printf("PINT_REQUEST_DONE: %d\n", PINT_REQUEST_DONE(file_state));
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

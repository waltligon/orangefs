/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>

#include <pint-distribution.h>
#include <pvfs-distribution.h>
#include <pint-request.h>
#include <pvfs-request.h>

extern PVFS_Distribution default_dist;

#define SEGMAX 32
#define BYTEMAX 250

main(int argc, char **argv)
{
	int i;
	PINT_Request req1;
	PINT_Request req2;
	PINT_Request req3;

	// PVFS_Process_request arguments
	PINT_Request_state *reqs;
	PVFS_Dist_parm *dparm;
	PINT_Request_file_data rfdata;
	int32_t segmax;
	PVFS_offset *offset_array;
	PVFS_size *size_array;
	PVFS_offset offset;
	PVFS_size bytemax;
	PVFS_boolean eof_flag;

	// Turn on debugging
	gossip_enable_stderr();
	gossip_set_debug_mask(1,REQUEST_DEBUG);

	req2.offset = 0; // This is a byte type
	req2.num_ereqs = 1;
	req2.stride = 0;
	req2.num_blocks = 1;
	req2.ub = 1;
	req2.lb = 0;
	req2.aggregate_size = 1;
	req2.depth = 0;
	req2.num_contig_chunks = 1;
	req2.ereq = NULL;
	req2.sreq = NULL;

	req3.offset = 0;
	req3.num_ereqs = 8;
	req3.stride = 20;
	req3.num_blocks = 20;
	req3.ub = 400;
	req3.lb = 0;
	req3.aggregate_size = 160;
	req3.depth = 1;
	req3.num_contig_chunks = 20;
	req3.ereq = &req2;
	req3.sreq = NULL;

	req1.offset = 0;
	req1.num_ereqs = 4;
	req1.stride = 2000;
	req1.num_blocks = 10;
	req1.ub = 19600;
	req1.lb = 0;
	req1.aggregate_size = 6400;
	req1.depth = 2;
	req1.num_contig_chunks = 800;
	req1.ereq = &req3;
	req1.sreq = NULL;

	reqs = PINT_New_request_state(&req1);
	rfdata.server_nr = 0;
	rfdata.server_ct = 1;
	rfdata.fsize = 10000000;
	rfdata.dist = &default_dist;
	rfdata.dparm = NULL;
	rfdata.extend_flag = 0;
	offset_array = (PVFS_offset *)malloc(SEGMAX * sizeof(PVFS_offset));
	size_array = (PVFS_size *)malloc(SEGMAX * sizeof(PVFS_size));
	eof_flag = 0;
	offset = 0;
	do {
		segmax = SEGMAX;
		bytemax = BYTEMAX;
		PINT_Process_request(reqs, &rfdata, &segmax, offset_array,
				size_array, &offset, &bytemax, &eof_flag, PINT_SERVER);
		printf("processed %lld bytes in %d segments\n", bytemax, segmax);
		for (i = 0; i < segmax; i++)
		{
			printf("segment %d: offset=%lld size=%lld\n", i,
					offset_array[i], size_array[i]);
		}
	} while (offset != -1);
}

update_fsize(int64_t fsize)
{
	return (0);
}

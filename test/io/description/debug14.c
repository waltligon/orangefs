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
#include <pint-dist-utils.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#include "debug.h"

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

PVFS_offset exp1_offset[] =
{
	0,
	720,
	970
};

PVFS_size exp1_size[] =
{
	220,
	180,
	40
};

PINT_Request_result expected[] =
{{
	offset_array : &exp1_offset[0],
	size_array : &exp1_size[0],
	segmax : SEGMAX,
	segs : 1,
	bytes : 220
}, {
	offset_array : &exp1_offset[1],
	size_array : &exp1_size[1],
	segmax : SEGMAX,
	segs : 2,
	bytes : 220
}};

int request_debug(void)
{
	int i;
	PINT_Request *r1;
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_request_file_data rf1;
	PINT_Request_result seg1;
	static int32_t blen[2] = {20, 200};
	static PVFS_offset disp[2] = {10, 100};

	/* PVFS_Process_request arguments */
	int retval;

	/* set up request state */
	PVFS_Request_hindexed(2, blen, disp, PVFS_BYTE, &r1);
	rs1 = PINT_new_request_state(r1);

	/* set up memory request */
	PVFS_Request_contiguous(220, PVFS_BYTE, &r2);
	rs2 = PINT_new_request_state(r2);

	/* set up file data for request */
	PINT_dist_initialize(NULL);
	rf1.server_nr = 0;
	rf1.server_ct = 4;
	rf1.fsize = 6000;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_dist_lookup(rf1.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;
	
	/* skip into the file datatype */
	PINT_REQUEST_STATE_SET_TARGET(rs1, 500);
	PINT_REQUEST_STATE_SET_FINAL(rs1,720);

   /* Turn on debugging */
	if (gossipflag)
	{
		gossip_enable_stderr();
		gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG); 
	}

	i = 0;

	/* skipping logical bytes */
	// PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
	// PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
	
	printf("\n************************************\n");
	printf("1 request in CLIENT mode server 0 of 4\n");
	printf("indexed request type, contig memory type\n");
	printf("offset 500 size 220\n");
	printf("************************************\n");
	do
	{
		PINT_REQUEST_STATE_RESET(rs1);
		PINT_REQUEST_STATE_RESET(rs2);
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_process_request(rs1, rs2, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&expected[i],"Results expected");
			cmpseg(&seg1,&expected[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** request done.\n");
	}
	
	printf("\n************************************\n");
	do
	{
		PINT_REQUEST_STATE_RESET(rs1);
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&expected[i],"Results expected");
			cmpseg(&seg1,&expected[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** request done.\n");
	}

	return 0;
}

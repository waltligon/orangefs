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

#include <debug.h>

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

PVFS_offset exp1_offset [] = {
   31232 ,
   40960 ,
   51200 ,
   61440 
};

PVFS_size exp1_size [] = {
	512,
	1024,
	1024,
	512
};

PINT_Request_result expected[] =
{{
   offset_array : &exp1_offset[0],
   size_array : &exp1_size[0],
   segmax : SEGMAX,
   segs : 4,
   bytes : (2*512)+(2*1024)
}};

int request_debug(void)
{
	int i;
	PINT_Request *r1;
	PINT_Request_state *rs1;
	PINT_request_file_data rf1;
	PINT_Request_result seg1;

	/* PVFS_Process_request arguments */
	int retval;

	/* set up request */
	PVFS_Request_vector(20, 1024, 10*1024, PVFS_BYTE, &r1);

	/* set up request state */
	rs1 = PINT_new_request_state(r1);

	/* set up file data for request */
	PINT_dist_initialize(NULL);
	rf1.server_nr = 0;
	rf1.server_ct = 8;
	rf1.fsize = 10000000;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_dist_lookup(rf1.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;

   /* Turn on debugging */
	if (gossipflag)
	{
		gossip_enable_stderr();
		gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG);
	}

	i = 0;

	/* skipping logical bytes */
	PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
	PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
	
	printf("\n************************************\n");
	printf("One request in SERVER mode size 20*1K stride 10K server 0 of 8\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Offset 0, file size 10000000, extend flag\n");
	printf("Start 3K+512 end 6K+512\n");
	printf("\n************************************\n");
	do
	{
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

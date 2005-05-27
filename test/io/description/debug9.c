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
	0 ,
	384 ,
	768 ,
	1152 ,
	1536 ,
	1920 ,
	2304 ,
	2688 ,
	3072 ,
	3456 ,
	3840 ,
	4224 ,
	4608 ,
	4992 ,
	5376 ,
	5760 
};

PVFS_offset exp2_offset [] = {
	6144 ,
	6528 ,
	6912 ,
	7296 ,
	7680 ,
	8064 ,
	8448 ,
	8832 ,
	9216 ,
	9600 ,
	9984 ,
	10368 ,
	10752 ,
	11136 ,
	11520 ,
	11904 
};

PVFS_offset exp3_offset [] = {
	12288 ,
	12672 ,
	13056 ,
	13440 ,
	13824 ,
	14208 ,
	14592 ,
	14976 ,
	15360 ,
	15744 ,
	16128 ,
	16512 ,
	16896 ,
	17280 ,
	17664 ,
	18048 
};

PVFS_offset exp4_offset [] = {
	18432 ,
	18816 ,
	19200 ,
	19584 ,
	19968 ,
	20352 ,
	20736 ,
	21120 
};

PVFS_size exp1_size [] = {
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128,
	128
};

PINT_Request_result exp[] =
{{
   offset_array : &exp1_offset[0],
   size_array : &exp1_size[0],
   segmax : SEGMAX,
   segs : 16,
   bytes : 16*128
}, {
   offset_array : &exp2_offset[0],
   size_array : &exp1_size[0],
   segmax : SEGMAX,
   segs : 16,
   bytes : 16*128
}, {
   offset_array : &exp3_offset[0],
   size_array : &exp1_size[0],
   segmax : SEGMAX,
   segs : 16,
   bytes : 16*128
}, {
   offset_array : &exp4_offset[0],
   size_array : &exp1_size[0],
   segmax : SEGMAX,
   segs : 8,
   bytes : 8*128
}};

int request_debug()
{
	int i;
	PINT_Request *r1;
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_file_data rf1;
	PINT_Request_result seg1;

	/* PVFS_Process_request arguments */
	int retval;

	/* set up request */
	PVFS_Request_vector(20, 1024, 20*1024, PVFS_BYTE, &r1);

	/* set up request state */
	rs1 = PINT_New_request_state(r1);

	/* set up memory request */
	PVFS_Request_vector(160, 128, 3*128, PVFS_BYTE, &r2);
	rs2 = PINT_New_request_state(r2);

	/* set up file data for request */
	PINT_dist_initialize();
	rf1.server_nr = 0;
	rf1.server_ct = 4;
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
	// PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
	// PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
	
	printf("\n************************************\n");
	printf("One request in CLIENT mode size 20*1K strided 20K server 0 of 4\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Offset 0M, file size 10000000, extend flag\n");
	printf("MemReq size 160*128 strided 3*128\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, rs2, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exp[i],"Results expected");
			cmpseg(&seg1,&exp[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** request done.\n");
	}

	return 0;
}

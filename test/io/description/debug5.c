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
	7012352
};
	
PVFS_size exp1_size [] = {
	1441792
};

PVFS_offset exp2_offset [] = {
	131072,
	327680,
	524288,
	720896,
	917504,
	1114112,
	1310720,
	1507328,
	1703936,
	1900544,
	2097152,
	2293760,
	2490368,
	2686976,
	2883584,
	3080192
};

PVFS_offset exp3_offset [] = {
	3276800,
	3473408,
	3670016,
	3866624,
	4063232,
	4259840
};

PVFS_size exp2_size [] = {
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536,
	65536
};

PINT_Request_result exp[] =
{{
	  offset_array : &exp1_offset[0],
	  size_array : &exp1_size[0],
	  segmax : SEGMAX,
	  segs : 1,
	  bytes : 1441792
}, {
	  offset_array : &exp2_offset[0],
	  size_array : &exp2_size[0],
	  segmax : SEGMAX,
	  segs : 16,
	  bytes : 16*65536
}, {
	  offset_array : &exp3_offset[0],
	  size_array : &exp2_size[0],
	  segmax : SEGMAX,
	  segs : 6,
	  bytes : 6*65536
}};


int request_debug()
{
	int i;
	PINT_Request *r1;
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_file_data rf1;
	PINT_Request_file_data rf2;
	PINT_Request_result seg1;

	int retval;

	int32_t blocklength = 10*1024*1024;
	PVFS_size displacement = 20*1024*1024;  

	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r1);
	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r2);

	rs1 = PINT_New_request_state(r1);
	rs2 = PINT_New_request_state(r2);

	PINT_dist_initialize();
	rf1.server_nr = 0;
	rf1.server_ct = 3;
	rf1.fsize = 8454144;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_dist_lookup(rf1.dist);

	rf2.server_nr = 1;
	rf2.server_ct = 3;
	rf2.fsize = 8454144;
	rf2.dist = PINT_dist_create("simple_stripe");
	rf2.extend_flag = 0;
	PINT_dist_lookup(rf2.dist);

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

// server stuff below this line
/****************************************************************/

	fprintf(stderr, "\n************************************\n");
	printf("One request in SERVER mode size 10M contiguous server 0 of 3\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Offset 20M, file size 8454144, no extend flag\n");
	printf("\n************************************\n");

	/* process request */
	retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

	if(retval >= 0)
	{
		prtseg(&seg1,"Results obtained");
		prtseg(&exp[i],"Results expected");
		cmpseg(&seg1,&exp[i]);
	}

	i++;
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		fprintf(stderr, "**** server done.\n");
	}

// client stuff below this line
/*******************************************************************/

	fprintf(stderr, "\n************************************\n");
	printf("One request in CLIENT mode size 10M contiguous server 1 of 3\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Offset 20M, file size 8454144, no extend flag\n");
	printf("\n************************************\n");
	seg1.bytes = 0;
	seg1.segs = 0;

	/* process request */
	retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_CLIENT);

	if(retval >= 0)
	{
		prtseg(&seg1,"Results obtained");
		prtseg(&exp[i],"Results expected");
		cmpseg(&seg1,&exp[i]);
	}

	i++;
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs2))
	{
		fprintf(stderr, "**** client request done - SHOULD NOT BE!.\n");
	}

	fprintf(stderr, "\n************************************\n");
	printf("One request in CLIENT mode size 10M contiguous server 1 of 3\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Continue where left off, file size 8454144, no extend flag\n");
	printf("Byte limit 393216\n");
	printf("\n************************************\n");
	seg1.bytemax = 393216;
	seg1.bytes = 0;
	seg1.segs = 0;

	retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_CLIENT);

	if(retval >= 0)
	{
		prtseg(&seg1,"Results obtained");
		prtseg(&exp[i],"Results expected");
		cmpseg(&seg1,&exp[i]);
	}

	i++;
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(!PINT_REQUEST_DONE(rs2))
	{
		fprintf(stderr, "\nAIEEEeee!  Why doesn't the client side set req processing offset to -1?.\n");
		fprintf(stderr, "... the server stopped correctly after this many bytes\n");
	}
	return 0;
}

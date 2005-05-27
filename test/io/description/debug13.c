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
	0
};

PVFS_size exp1_size[] =
{
	748
};

PVFS_offset exp2_offset[] =
{
	20,
	128,
	256,
 	384,
	544,
	672,
	800,
	960,
	1088,
	1216,
	3744,
	3872,
	4000,
	4128,
	4288,
	4416,

	4544,
	4704,
	4832,
	4960
};

PVFS_size exp2_size[] =
{
	12,
	32,
	32,
	64,
	32,
	32,
	64,
	32,
	32,
	32,
	32,
	32,
	32,
	64,
	32,
	32,

	64,
	32,
	32,
	32
};

PINT_Request_result exp[] =
{{
	offset_array : &exp1_offset[0],
	size_array : &exp1_size[0],
	segmax : SEGMAX,
	segs : 1,
	bytes : 748
}, {
	offset_array : &exp1_offset[0],
	size_array : &exp1_size[0],
	segmax : SEGMAX,
	segs : 1,
	bytes : 748
}, {
	offset_array : &exp2_offset[0],
	size_array : &exp2_size[0],
	segmax : SEGMAX,
	segs : 16,
	bytes : 588
}, {
	offset_array : &exp2_offset[16],
	size_array : &exp2_size[16],
	segmax : SEGMAX,
	segs : 4,
	bytes : 160
}, {
	offset_array : &exp2_offset[0],
	size_array : &exp2_size[0],
	segmax : SEGMAX,
	segs : 16,
	bytes : 588
}, {
	offset_array : &exp2_offset[16],
	size_array : &exp2_size[16],
	segmax : SEGMAX,
	segs : 4,
	bytes : 160
}};

int request_debug()
{
	int i, r_size;
	PINT_Request *r1, *r1a, *r1b, *r_packed;
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs1p;
	PINT_Request_state *rs2;
	PINT_Request_file_data rf1;
	PINT_Request_result seg1;

	/* PVFS_Process_request arguments */
	int retval;

	/* set up request state */
	PVFS_Request_vector(4, 4, 16, PVFS_DOUBLE, &r1a);
	PVFS_Request_vector(3, 3, 9, r1a, &r1b);
	rs1 = PINT_New_request_state(r1b);

	/* set up memory request */
	PVFS_Request_contiguous(4076, PVFS_BYTE, &r2);
	rs2 = PINT_New_request_state(r2);

	/* pack the request */
	r_size = PINT_REQUEST_PACK_SIZE(r1b);
	r_packed = (struct PINT_Request *)malloc(r_size);
	PINT_Request_commit(r_packed, r1b);

	PINT_Request_encode(r_packed);

	r_size = PINT_REQUEST_PACK_SIZE(r_packed);
	r1 = (struct PINT_Request *)malloc(r_size);
	memcpy(r1, r_packed, r_size);
	PINT_Request_decode(r1);

	rs1p = PINT_New_request_state(r1);

	/* set up file data for request */
	PINT_dist_initialize();
	rf1.server_nr = 0;
	rf1.server_ct = 2;
	rf1.fsize = 6000;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 0;

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;
	
	/* skip into the file datatype */
	PINT_REQUEST_STATE_SET_TARGET(rs1, 20);

	PINT_REQUEST_STATE_SET_TARGET(rs1p, 20);

   /* Turn on debugging */
	if (gossipflag)
	{
		gossip_enable_stderr();
		gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG); 
	}

	i = 0;

	PINT_Dump_packed_request(r_packed);

	/* skipping logical bytes */
	// PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
	// PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
	
	printf("\n************************************\n");
	printf("1 request in CLIENT mode server 0 of 2\n");
	printf("vector 3, 3, 9 of vector 4, 4, 16 of double\n");
	printf("contig memory request of 4076 bytes\n");
	printf("offset of 20 bytes, original version\n");
	printf("************************************\n");
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

	PINT_REQUEST_STATE_RESET(rs2);
	
	printf("\n************************************\n");
	printf("1 request in CLIENT mode server 0 of 2\n");
	printf("vector 3, 3, 9 of vector 4, 4, 16 of double\n");
	printf("contig memory request of 4076 bytes\n");
	printf("offset of 20 bytes, packed then unpacked\n");
	printf("************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1p, rs2, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exp[i],"Results expected");
			cmpseg(&seg1,&exp[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1p) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1p))
	{
		printf("**** request done.\n");
	}

	PINT_REQUEST_STATE_RESET(rs1);
	PINT_REQUEST_STATE_RESET(rs2);
	
	printf("\n************************************\n");
	printf("1 request in SERVER mode server 0 of 2\n");
	printf("vector 3, 3, 9 of vector 4, 4, 16 of double\n");
	printf("contig memory request of 4076 bytes\n");
	printf("offset of 20 bytes, original version\n");
	printf("************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, rs2, &rf1, &seg1, PINT_SERVER);

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

	PINT_REQUEST_STATE_RESET(rs1p);
	PINT_REQUEST_STATE_RESET(rs2);
	
	printf("\n************************************\n");
	printf("1 request in SERVER mode server 0 of 2\n");
	printf("vector 3, 3, 9 of vector 4, 4, 16 of double\n");
	printf("contig memory request of 4076 bytes\n");
	printf("offset of 20 bytes, packed then unpacked\n");
	printf("************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1p, rs2, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exp[i],"Results expected");
			cmpseg(&seg1,&exp[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1p) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1p))
	{
		printf("**** request done.\n");
	}

	PVFS_Request_free(&r1);
	PVFS_Request_free(&r1a);
	PVFS_Request_free(&r1b);
	PVFS_Request_free(&r2);
	PVFS_Request_free(&r_packed);

	return 0;
}

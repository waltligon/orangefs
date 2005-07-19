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
	0
};

PVFS_offset exp2_offset [] = {
	0,
	196608,
	393216,
	589824,
	786432,
	983040,
	1179648,
	1376256,
	1572864,
	1769472,
	1966080,
	2162688,
	2359296,
	2555904,
	2752512,
	2949120,

	3145728,
	3342336,
	3538944,
	3735552,
	3932160,
	4128768,
	4325376,
	4521984,
	4718592,
	4915200,
	5111808,
	5308416,
	5505024,
	5701632,
	5898240,
	6094848,

	6291456,
	6488064,
	6684672,
	6881280,
	7077888,
	7274496,
	7471104,
	7667712,
	7864320,
	8060928,
	8257536,
	8454144,
	8650752,
	8847360,
	9043968,
	9240576,

	9437184,
	9633792,
	9830400,
	10027008,
	10223616,
	10420224
};

PVFS_size exp1_size [] =
{
	3538944
};

PVFS_size exp2_size [] =
{
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
	65536,
	65536
};

PINT_Request_result exptd[] =
{{
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 3538944
 }, {
	   offset_array : &exp2_offset[0],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 16,
	   bytes : 65536*16
 }, {
	   offset_array : &exp2_offset[16],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 16,
	   bytes : 65536*16
 }, {
	   offset_array : &exp2_offset[32],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 16,
	   bytes : 65536*16
 }, {
	   offset_array : &exp2_offset[48],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 6,
	   bytes : 65536*6
}};


int request_debug(void)
{
	int i;
	PINT_Request *r1;
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_file_data rf1;
	PINT_Request_file_data rf2;
	PINT_Request_result seg1;

	/* PVFS_Process_request arguments */
	int retval;

	int32_t blocklength = 10*1024*1024; /* 10M */

	/* set up two requests, both at offset 0 */
	PVFS_size displacement = 0;  /* first at offset zero */
	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r1);
	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r2);

	/* set up two request states */
	rs1 = PINT_New_request_state(r1);
	rs2 = PINT_New_request_state(r2);

	/* set up file data for first request */
	PINT_dist_initialize();
	rf1.server_nr = 0;
	rf1.server_ct = 3;
	rf1.fsize = 8454144;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_dist_lookup(rf1.dist);

	/* file data for second request is the same, except the file
	 * will have grown by 10M 
	 */
	rf2.server_nr = 0;
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

	printf("\n************************************\n");
	printf("Two requests 10M each contiguous from offset 0 server 0 of 3\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("First in SERVER mode, file size 8454144, no extend flag\n");
	printf("\n************************************\n");

	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exptd[i],"Results expected");
			cmpseg(&seg1,&exptd[i]);
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
		printf("**** first request done.\n");
	}

	printf("\n************************************\n");
	printf("Second in CLIENT mode, file size 8454144, no extend flag\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exptd[i],"Results expected");
			cmpseg(&seg1,&exptd[i]);
	   }

      i++;

	} while(!PINT_REQUEST_DONE(rs2) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs2))
	{
		printf("**** second request done.\n");
	}

	return 0;
}

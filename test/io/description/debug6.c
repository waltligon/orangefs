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

PVFS_size exp1_size [] = {
	4194304
};

PINT_Request_result expected[] =
{{
   offset_array : &exp1_offset[0],
   size_array : &exp1_size[0],
   segmax : SEGMAX,
   segs : 1,
   bytes : 4194304
}};

int request_debug(void)
{
	int i;
	PINT_Request *r1;
	PINT_Request_state *rs1;
	PINT_request_file_data rf1;
	PINT_Request_result seg1;

	int retval;

	int32_t blocklength = 10*1024*1024;
	PVFS_size displacement = 0;  

	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r1);

	rs1 = PINT_new_request_state(r1);

	PINT_dist_initialize(NULL);
	rf1.server_nr = 0;
	rf1.server_ct = 1;
	rf1.fsize = 0;
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

// client stuff below this line
/*******************************************************************/

	fprintf(stderr, "\n************************************\n");
	printf("One request in CLIENT mode size 10M contiguous server 0 of 1\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Offset 0, file size 0, extend flag\n");
	printf("testing process limits 4M and return code\n");
	printf("\n************************************\n");

	/* process request */
	retval = PINT_process_request(rs1, NULL, &rf1, &seg1, PINT_CLIENT);

	if(retval >= 0)
	{
		prtseg(&seg1,"Results obtained");
		prtseg(&expected[i],"Results expected");
		cmpseg(&seg1,&expected[i]);
	}

	i++;

	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		fprintf(stderr, "AAAIIIEEEEE!  Why am I done?\n");
	}

	return 0;
}

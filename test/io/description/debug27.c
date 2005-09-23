/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>

#include <pint-distribution.h>
#include <pint-dist-utils.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#include <debug.h>

#define SEGMAX 16
#define BYTEMAX (1024*1024)

PVFS_offset exp1_offset[] =
{
		100, 25100
};
PVFS_offset exp1_size[] =
{
		24578, 24574
};
PINT_Request_result expected[] =
{{
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 2,
	   bytes : 49152
}};


int request_debug(void)
{
	int i;
	PINT_Request *r, *r2, *r3;
	PINT_Request *r_enc;
	PINT_Request_state *rs1;
	PINT_request_file_data rf1;
	PINT_Request_result seg1;
	int ret = -1;
	int pack_size = 0;

	/* PVFS_Process_request arguments */
	int retval;

   /* Turn on debugging */
	if (gossipflag)
	{
		gossip_enable_stderr();
		gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG);
	}

	PVFS_Request_contiguous(32, PVFS_BYTE, &r);
	PVFS_Request_vector(32, 32, 64, r, &r2);
	PVFS_Request_free(&r);
	PVFS_Request_vector(32, 32, 64, r2, &r3);
	PVFS_Request_free(&r2);
	pack_size = PINT_REQUEST_PACK_SIZE(r3);
	r_enc = (PINT_Request*)malloc(pack_size);
	ret = PINT_request_commit(r_enc, r3);
	PVFS_Request_free(&r3);

	/* set up request states */
	rs1 = PINT_new_request_state(r_enc);

	/* set up file data for each server */
	PINT_dist_initialize(NULL);
	rf1.server_nr = 0;
	rf1.server_ct = 4;
	rf1.fsize = 0;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_dist_lookup(rf1.dist);

	/* set up response for each server */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.segmax = SEGMAX;
	seg1.bytemax = BYTEMAX;
	seg1.segs = 0;
	seg1.bytes = 0;

	i = 0;

	printf("\n************************************\n");
	printf("Four requests in CLIENT mode 4 each contiguous servers 0-3 of 4\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Hindexed request, 3K block 4K stride, 64 blocks\n");
	printf("Memtype 8 blocks 24576 each, 25000 stride\n");
	printf("Each from offset 0, file size 0, extend flag\n");
	printf("Server 0\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_process_request(rs1, NULL, &rf1, &seg1, PINT_CLIENT);

		if(!PINT_REQUEST_DONE(rs1))
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&expected[i],"Results expected");
			cmpseg(&seg1,&expected[i]);
		}

		i++;

	} while(0); 
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** first request done.\n");
	}

	return 0;
}

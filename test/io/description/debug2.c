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
		0
};
PVFS_offset exp1_size[] =
{
		1048576
};
PINT_Request_result exp[] =
{{
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 1048576
}, {
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 1048576
}, {
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 1048576
}, {
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 1048576
}};


int request_debug()
{
	int i;
	PINT_Request *r;
	PINT_Request *r_enc;
	PINT_Request *r_dec;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_state *rs3;
	PINT_Request_state *rs4;
	PINT_Request_file_data rf1;
	PINT_Request_file_data rf2;
	PINT_Request_file_data rf3;
	PINT_Request_file_data rf4;
	PINT_Request_result seg1;
	int ret = -1;
	int pack_size = 0;

	/* PVFS_Process_request arguments */
	int retval;

	/* the case that we want to test is a write, with 4 servers holding
	 * parts of the file data.  We will setup 4 different request states,
	 * one per server.  The request will be large enough that all 4
	 * servers will be involved
	 */

	/* set up one request, we will reuse it for each server scenario */
	/* we want to read 4M of data, resulting from 1M from each server */
	PVFS_Request_contiguous((4*1024*1024), PVFS_BYTE, &r);

	/* allocate a new request and pack the original one into it */
	pack_size = PINT_REQUEST_PACK_SIZE(r);
	r_enc = (PINT_Request*)malloc(pack_size);
	ret = PINT_Request_commit(r_enc, r);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_commit() failure.\n");
		return(-1);
	}
	ret = PINT_Request_encode(r_enc);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_encode() failure.\n");
		return(-1);
	}


	/* decode the encoded request (hopefully ending up with something
	 * equivalent to the original request)
	 */
	r_dec = (PINT_Request*)malloc(pack_size);
	memcpy(r_dec, r_enc, pack_size);
	free(r_enc);
	free(r);
	ret = PINT_Request_decode(r_dec);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_decode() failure.\n");
		return(-1);
	}

	/* set up four request states */
	rs1 = PINT_New_request_state(r_dec);
	rs2 = PINT_New_request_state(r_dec);
	rs3 = PINT_New_request_state(r_dec);
	rs4 = PINT_New_request_state(r_dec);

	/* set up file data for each server */
	PINT_dist_initialize();
	rf1.server_nr = 0;
	rf1.server_ct = 4;
	rf1.fsize = 0;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_dist_lookup(rf1.dist);

	rf2.server_nr = 1;
	rf2.server_ct = 4;
	rf2.fsize = 0;
	rf2.dist = PINT_dist_create("simple_stripe");
	rf2.extend_flag = 1;
	PINT_dist_lookup(rf2.dist);

	rf3.server_nr = 2;
	rf3.server_ct = 4;
	rf3.fsize = 0;
	rf3.dist = PINT_dist_create("simple_stripe");
	rf3.extend_flag = 1;
	PINT_dist_lookup(rf3.dist);

	rf4.server_nr = 3;
	rf4.server_ct = 4;
	rf4.fsize = 0;
	rf4.dist = PINT_dist_create("simple_stripe");
	rf4.extend_flag = 1;
	PINT_dist_lookup(rf4.dist);

	/* set up response for each server */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.segmax = SEGMAX;
	seg1.bytemax = BYTEMAX;
	seg1.segs = 0;
	seg1.bytes = 0;

   /* Turn on debugging */
	if (gossipflag)
	{
		gossip_enable_stderr();
		gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG);
	}

	i = 0;

	printf("\n************************************\n");
	printf("Four requests in SERVER mode 4 each contiguous servers 0-3 of 4\n");
	printf("Simple stripe, default stripe size (64K)\n");
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
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

		if(!PINT_REQUEST_DONE(rs1))
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

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
		printf("**** first request done.\n");
	}

	printf("\n************************************\n");
	printf("Server 1\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_SERVER);

		if(!PINT_REQUEST_DONE(rs2))
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exp[i],"Results expected");
			cmpseg(&seg1,&exp[i]);
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

	printf("\n************************************\n");
	printf("Server 2\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs3, NULL, &rf3, &seg1, PINT_SERVER);

		if(!PINT_REQUEST_DONE(rs3))
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exp[i],"Results expected");
			cmpseg(&seg1,&exp[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs3) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs3))
	{
		printf("**** third request done.\n");
	}


	printf("\n************************************\n");
	printf("Server 3\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs4, NULL, &rf4, &seg1, PINT_SERVER);

		if(!PINT_REQUEST_DONE(rs4))
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&exp[i],"Results expected");
			cmpseg(&seg1,&exp[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs4) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs4))
	{
		printf("**** fourth request done.\n");
	}


	return 0;
}

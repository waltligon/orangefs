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
PVFS_offset exp2_offset[] =
{
		50000
};
PVFS_offset exp3_offset[] =
{
		100000
};
PVFS_offset exp4_offset[] =
{
		150000
};
PVFS_offset exp1_size[] =
{
		49152
};
PVFS_offset exp2_size[] =
{
		49152
};
PINT_Request_result expected[] =
{{
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 49152
}, {
	   offset_array : &exp2_offset[0],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 49152
}, {
	   offset_array : &exp3_offset[0],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 49152
}, {
	   offset_array : &exp4_offset[0],
	   size_array : &exp2_size[0],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 49152
}};


int request_debug(void)
{
	int i;
	int32_t fsizes[64];
	PVFS_size fdisps[64];
	int32_t msizes[4] = {49152, 49152, 49152, 49152};
	PVFS_size mdisps[4] = {0, 50000, 100000, 150000};
	PINT_Request *r;
	PINT_Request *r_enc;
	PINT_Request *m;
	PINT_Request *m_enc;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_state *rs3;
	PINT_Request_state *rs4;
	PINT_Request_state *ms1;
	PINT_Request_state *ms2;
	PINT_Request_state *ms3;
	PINT_Request_state *ms4;
	PINT_request_file_data rf1;
	PINT_request_file_data rf2;
	PINT_request_file_data rf3;
	PINT_request_file_data rf4;
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
	for (i = 0; i < 64; i++)
	{
		fsizes[i] = 3 * 1024;
		fdisps[i] = i * 4 * 1024;
	}
	PVFS_Request_hindexed(64,fsizes, fdisps, PVFS_BYTE, &r);

	/* allocate a new request and commit the original one into it */
	pack_size = PINT_REQUEST_PACK_SIZE(r);
	r_enc = (PINT_Request*)malloc(pack_size);
	ret = PINT_request_commit(r_enc, r);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_commit() failure.\n");
		return(-1);
	}
	free(r);


	/* decode the encoded request (hopefully ending up with something
	 * equivalent to the original request)
	 */

	/* set up four request states */
	rs1 = PINT_new_request_state(r_enc);
	rs2 = PINT_new_request_state(r_enc);
	rs3 = PINT_new_request_state(r_enc);
	rs4 = PINT_new_request_state(r_enc);

	/*
	PINT_REQUEST_STATE_SET_TARGET(rs1, 1024);
	PINT_REQUEST_STATE_SET_TARGET(rs2, 1024);
	PINT_REQUEST_STATE_SET_TARGET(rs3, 1024);
	PINT_REQUEST_STATE_SET_TARGET(rs4, 1024);
	*/

	/* set up one request, we will reuse it for each server scenario */
	/* we want to read 4M of data, resulting from 1M from each server */
	PVFS_Request_hindexed(4, msizes, mdisps, PVFS_BYTE, &m);

	/* allocate a new request and commit the original one into it */
	pack_size = PINT_REQUEST_PACK_SIZE(m);
	m_enc = (PINT_Request*)malloc(pack_size);
	ret = PINT_request_commit(m_enc, m);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_commit() failure.\n");
		return(-1);
	}
	free(m);

	/* set up four request states */
	ms1 = PINT_new_request_state(m_enc);
	ms2 = PINT_new_request_state(m_enc);
	ms3 = PINT_new_request_state(m_enc);
	ms4 = PINT_new_request_state(m_enc);

	/* set up file data for each server */
	PINT_dist_initialize(NULL);
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
	printf("Four requests in CLIENT mode 4 each contiguous servers 0-3 of 4\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Hindexed request, 3K block 4K stride, 64 blocks\n");
	printf("Memtype 4 blocks 49152 each, 50000 stride\n");
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
		retval = PINT_process_request(rs1, ms1, &rf1, &seg1, PINT_CLIENT);

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
	//} while(!PINT_REQUEST_DONE(rs1) && retval >= 0); 
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
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
		retval = PINT_process_request(rs2, ms2, &rf2, &seg1, PINT_CLIENT);

		if(!PINT_REQUEST_DONE(rs2))
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
	//} while(!PINT_REQUEST_DONE(rs2) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
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
		retval = PINT_process_request(rs3, ms3, &rf3, &seg1, PINT_CLIENT);

		if(!PINT_REQUEST_DONE(rs3))
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
	//} while(!PINT_REQUEST_DONE(rs3) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
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
		retval = PINT_process_request(rs4, ms4, &rf4, &seg1, PINT_CLIENT);

		if(!PINT_REQUEST_DONE(rs4))
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
	//} while(!PINT_REQUEST_DONE(rs4) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs4))
	{
		printf("**** fourth request done.\n");
	}


	return 0;
}

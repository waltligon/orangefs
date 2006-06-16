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

#define SEGMAX 2
#define BYTEMAX (1024*1024)

PVFS_offset exp1_offset[] =
{
		0, 49152, 98304, 147456, 163328, 196608,
		245760, 294912, 344064, 359936
};
PVFS_offset exp1_size[] =
{
		16384, 16384, 16384, 15872, 512, 16384,
		16384, 16384, 15872, 512
};
PINT_Request_result expected[] =
{{
	   offset_array : &exp1_offset[0],
	   size_array : &exp1_size[0],
	   segmax : SEGMAX,
	   segs : 2,
	   bytes : 32768
},{
	   offset_array : &exp1_offset[2],
	   size_array : &exp1_size[2],
	   segmax : SEGMAX,
	   segs : 2,
	   bytes : 32256
},{
	   offset_array : &exp1_offset[4],
	   size_array : &exp1_size[4],
	   segmax : SEGMAX,
	   segs : 2,
	   bytes : 16896
},{
	   offset_array : &exp1_offset[6],
	   size_array : &exp1_size[6],
	   segmax : SEGMAX,
	   segs : 2,
	   bytes : 32768
},{
	   offset_array : &exp1_offset[8],
	   size_array : &exp1_size[8],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 15872
},{
	   offset_array : &exp1_offset[9],
	   size_array : &exp1_size[9],
	   segmax : SEGMAX,
	   segs : 1,
	   bytes : 512
}};

int request_debug(void)
{
	int i;
	PINT_Request *r, *m;
	PINT_Request *r_enc, *m_enc;
	PINT_Request_state *rs1, *ms1;
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

	PVFS_Request_contiguous(1, PVFS_BYTE, &r);
	pack_size = PINT_REQUEST_PACK_SIZE(r);
	r_enc = (PINT_Request*)malloc(pack_size);
	ret = PINT_request_commit(r_enc, r);
	PVFS_Request_free(&r);

	/* set up request state */
	rs1 = PINT_new_request_state(r_enc);

	PINT_REQUEST_STATE_SET_TARGET(rs1, 512);
	PINT_REQUEST_STATE_SET_FINAL(rs1, 0);

	/* set up mem type */
	PVFS_Request_vector(8, 16*1024, 48*1024, PVFS_BYTE, &m);
	pack_size = PINT_REQUEST_PACK_SIZE(m);
	m_enc = (PINT_Request*)malloc(pack_size);
	ret = PINT_request_commit(m_enc, m);
	PVFS_Request_free(&m);

	/* set up mem type state */
	ms1 = PINT_new_request_state(m_enc);

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
	printf("one request in CLIENT mode \n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("Contig request\n");
	printf("Contig Memtype\n");
	printf("Each from offset 0, file size 0, extend flag\n");
	printf("Server 0\n");
	printf("************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_process_request(rs1, ms1, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&expected[i],"Results expected");
			cmpseg(&seg1,&expected[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1)); 

	printf("\n************************************\n");
	printf("Server 1\n");
	printf("************************************\n");
	PINT_REQUEST_STATE_RESET(rs1);
	PINT_REQUEST_STATE_RESET(ms1);
	rf1.server_nr = 1;
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_process_request(rs1, ms1, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&expected[i],"Results expected");
			cmpseg(&seg1,&expected[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1)); 

	printf("\n************************************\n");
	printf("Server 2\n");
	printf("************************************\n");
	PINT_REQUEST_STATE_RESET(rs1);
	PINT_REQUEST_STATE_RESET(ms1);
	rf1.server_nr = 2;
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_process_request(rs1, ms1, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			prtseg(&seg1,"Results obtained");
			prtseg(&expected[i],"Results expected");
			cmpseg(&seg1,&expected[i]);
		}

		i++;

	} while(!PINT_REQUEST_DONE(rs1)); 
	
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

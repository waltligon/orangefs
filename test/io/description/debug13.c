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

#include <pvfs-distribution.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#include <simple-stripe.h>

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

int main(int argc, char **argv)
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
	rf1.iod_num = 0;
	rf1.iod_count = 2;
	rf1.fsize = 6000;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_Dist_lookup(rf1.dist);

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
	// gossip_enable_stderr();
	// gossip_set_debug_mask(1,REQUEST_DEBUG); 

	PINT_Dump_packed_request(r_packed);

	/* skipping logical bytes */
	// PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
	// PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
	
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, rs2, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

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
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1p, rs2, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

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
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, rs2, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

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
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1p, rs2, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

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

	 gossip_enable_stderr();
	 gossip_set_debug_mask(1,REQUEST_DEBUG); 

	PVFS_Request_free(&r1);
	PVFS_Request_free(&r1a);
	PVFS_Request_free(&r1b);
	PVFS_Request_free(&r2);
	PVFS_Request_free(&r_packed);

	return 0;
}

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
	rf1.server_nr = 0;
	rf1.server_ct = 3;
	rf1.fsize = 8454144;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_Dist_lookup(rf1.dist);

	/* file data for second request is the same, except the file
	 * will have grown by 10M 
	 */
	rf2.server_nr = 0;
	rf2.server_ct = 3;
	rf2.fsize = 8454144;
	rf2.dist = PVFS_Dist_create("simple_stripe");
	rf2.extend_flag = 0;
	PINT_Dist_lookup(rf2.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;

   /* Turn on debugging */
	// gossip_enable_stderr(); 
	// gossip_set_debug_mask(1,REQUEST_DEBUG); 

	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request(PINT_SERVER):\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
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
		printf("**** first request done.\n");
	}

	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request(PINT_CLIENT):\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

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

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
#include <pvfs-request.h>
#include <pint-request.h>

#include <simple-stripe.h>

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

int main(int argc, char **argv)
{
	int i;
	PINT_Request *r1;
	PINT_Request_state *rs1;
	PINT_Request_file_data rf1;
	PINT_Request_result seg1;

	/* PVFS_Process_request arguments */
	int retval;

	/* set up request */
	PVFS_Request_vector(10, 1024, 10*1024, PVFS_BYTE, &r1);

	/* set up request state */
	rs1 = PINT_New_request_state(r1);

	/* set up file data for request */
	rf1.iod_num = 1;
	rf1.iod_count = 8;
	rf1.fsize = 10000000;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.eof_flag = 0;
	seg1.bytes = 0;
	seg1.segs = 0;

   /* Turn on debugging */
	/* gossip_enable_stderr(); /**/
	/* gossip_set_debug_mask(1,REQUEST_DEBUG); /**/

	/* skipping logical bytes */
	seg1.bytemax = (3 * 1024) + 512;
	
	retval = PINT_Process_request(rs1, NULL, &rf1, &seg1,
		PINT_CKSIZE_LOGICAL_SKIP); /**/

	/* need to reset bytemax before we contrinue */
	seg1.bytemax = BYTEMAX;

	printf("\n************************************\n");
	do
	{
		seg1.eof_flag = 0;
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

	} while(PINT_REQUEST_STATE_OFFSET(rs1) != -1 && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_STATE_OFFSET(rs1) == -1)
	{
		printf("**** request done.\n");
	}

	return 0;
}

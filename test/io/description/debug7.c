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

	/* PVFS_Process_request arguments */
	int retval;
	int32_t segmax;
	PVFS_offset *offset_array;
	PVFS_size *size_array;
	PVFS_offset offset;
	PVFS_size bytemax;
	PVFS_boolean eof_flag;

	/* set up two requests */
	PVFS_Request_vector(10, 1024, 10*1024, PVFS_BYTE, &r1);

	/* set up two request states */
	rs1 = PINT_New_request_state(r1);

	/* set up file data for first request */
	rf1.iod_num = 1;
	rf1.iod_count = 8;
	rf1.fsize = 10000000;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

   /* Turn on debugging */
	gossip_enable_stderr(); /**/
	gossip_set_debug_mask(1,REQUEST_DEBUG); /**/

	offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));

	offset = 0;

	/* skipping logical bytes */
	bytemax = (3 * 1024) + 512;
	
	retval = PINT_Process_request(rs1, &rf1, &segmax,
		offset_array, size_array, &offset, &bytemax, 
		&eof_flag, PINT_CKSIZE_LOGICAL_SKIP); /**/

	printf("\n************************************\n");
	do
	{
		eof_flag = 0;
		segmax = SEGMAX;
		bytemax = BYTEMAX;

		/* process request */
		retval = PINT_Process_request(rs1, &rf1, &segmax,
			offset_array, size_array, &offset, &bytemax, 
			&eof_flag, PINT_SERVER);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			for(i=0; i<segmax; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)offset_array[i], (int)size_array[i]);
			}
		}

	} while(offset != -1 && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(offset == -1)
	{
		printf("**** request done.\n");
	}

	return 0;
}

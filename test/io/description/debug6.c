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
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_file_data rf1;
	PINT_Request_file_data rf2;

	int retval;
	int32_t segmax;
	PVFS_offset *offset_array;
	PVFS_size *size_array;
	PVFS_offset offset;
	PVFS_size bytemax;
	PVFS_boolean eof_flag;

	int32_t blocklength = 10*1024*1024;
	PVFS_size displacement = 0;  

	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r1);
	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r2);

	rs1 = PINT_New_request_state(r1);
	rs2 = PINT_New_request_state(r2);

	rf1.iod_num = 0;
	rf1.iod_count = 1;
	rf1.fsize = 0;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

	rf2.iod_num = 0;
	rf2.iod_count = 1;
	rf2.fsize = 0;
	rf2.dist = PVFS_Dist_create("simple_stripe");
	rf2.extend_flag = 1;
	PINT_Dist_lookup(rf2.dist);

   /* Turn on debugging */
	//gossip_enable_stderr(); 
	//gossip_set_debug_mask(1,REQUEST_DEBUG);

	offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));

// client stuff below this line
/*******************************************************************/

	offset = 0;
	fprintf(stderr, "\n************************************\n");
	eof_flag = 0;
	segmax = SEGMAX;
	bytemax = BYTEMAX;

	/* process request */
	retval = PINT_Process_request(rs2, &rf2, &segmax,
		offset_array, size_array, &offset, &bytemax, 
		&eof_flag, PINT_CLIENT);

	if(retval >= 0)
	{
		fprintf(stderr, "results of PINT_Process_request(PINT_CLIENT):\n");
		fprintf(stderr, "req proc offset: %d\n", (int)offset);
		fprintf(stderr, "total size: %d\n", (int)bytemax);
		for(i=0; i<segmax; i++)
		{
			fprintf(stderr, "  segment %d: offset: %d size: %d\n",
				i, (int)offset_array[i], (int)size_array[i]);
		}
	}

	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(offset == -1)
	{
		fprintf(stderr, "AAAIIIEEEEE!  Why am I done?\n");
	}

	return 0;
}

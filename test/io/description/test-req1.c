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

#define SEGMAX 32
#define BYTEMAX 250

int main(int argc, char **argv)
{
	int i;
	PINT_Request *r1;
	PINT_Request_state *rs;
	PINT_Request_file_data rf;

	/* PVFS_Process_request arguments */
	int retval;
	int32_t segmax;
	PVFS_offset *offset_array;
	PVFS_size *size_array;
	PVFS_offset offset;
	PVFS_size bytemax;
	PVFS_boolean eof_flag;

	/* set up a request */
	PVFS_Request_vector(16, 4, 64, PVFS_DOUBLE, &r1);

	/* set up a request state */
	rs = PINT_New_request_state(r1);

	/* set up file data */
	rf.iod_num = 0;
	rf.iod_count = 2;
	rf.fsize = 10000000;

	/* grab a distribution */
	rf.dist = PVFS_Dist_create("simple_stripe");

	/* get the methods for the distribution */
	PINT_Dist_lookup(rf.dist);

   /* Turn on debugging */
	/* gossip_enable_stderr(); */
	/* gossip_set_debug_mask(1,REQUEST_DEBUG); */

	offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	eof_flag = 0;
	offset = 0;
	segmax = 1;
	bytemax = BYTEMAX;

	/* process request */
	retval = PINT_Process_request(rs, &rf, &segmax, NULL, NULL,
			&offset, &bytemax, &eof_flag, PINT_CKSIZE);

	/* print return value */
	printf("\nreturn %d, %d segments: offset=%lld bytemax=%lld\n",
			retval, segmax, offset, bytemax);
	offset = 0;
	do {
		segmax = SEGMAX;
		bytemax = BYTEMAX;
		/* process request */
		PINT_Process_request(rs, &rf, &segmax, offset_array,
				size_array, &offset, &bytemax, &eof_flag, PINT_CLIENT);
		printf("processed %lld bytes in %d segments\n", bytemax, segmax);
		for (i = 0; i < segmax; i++)
		{
			printf("segment %d: offset=%lld size=%lld\n", i,
					offset_array[i], size_array[i]);
		}
	} while (offset != -1);
	printf("finished processing request\n");
	segmax = 1;
	bytemax = BYTEMAX;
	/* process request */
	retval = PINT_Process_request(rs, &rf, &segmax, NULL, NULL,
			&offset, &bytemax, &eof_flag, PINT_CKSIZE);
	printf("\nreturn %d, %d segments: offset=%lld bytemax=%lld\n",
			retval, segmax, offset, bytemax);
	offset = 0;
	segmax = 1;
	bytemax = BYTEMAX;
	/* process request */
	retval = PINT_Process_request(rs, &rf, &segmax, NULL, NULL,
			&offset, &bytemax, &eof_flag, PINT_CKSIZE);
	printf("\nreturn %d, %d segments: offset=%lld bytemax=%lld\n",
			retval, segmax, offset, bytemax);
	offset = 0;
	do {
		segmax = SEGMAX;
		bytemax = BYTEMAX;
		/* process request */
		PINT_Process_request(rs, &rf, &segmax, offset_array,
				size_array, &offset, &bytemax, &eof_flag, PINT_SERVER);
		printf("processed %lld bytes in %d segments\n", bytemax, segmax);
		for (i = 0; i < segmax; i++)
		{
			printf("segment %d: offset=%lld size=%lld\n", i,
					offset_array[i], size_array[i]);
		}
	} while (offset != -1);
	printf("finished processing request\n");

	return 0;
}

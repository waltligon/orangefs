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
#define BYTEMAX (1024*1024)

int main(int argc, char **argv)
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
	int commit_index = 0;
	int ret = -1;
	int pack_size = 0;

	/* PVFS_Process_request arguments */
	int retval;
	PVFS_count32 segmax;
	PVFS_offset *offset_array;
	PVFS_size *size_array;
	PVFS_offset offset;
	PVFS_size bytemax;
	PVFS_boolean eof_flag;

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
	commit_index = 0;
	ret = PINT_Request_commit(r_enc, r, &commit_index);
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
	rf1.iod_num = 0;
	rf1.iod_count = 4;
	rf1.fsize = 0;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

	rf2.iod_num = 1;
	rf2.iod_count = 4;
	rf2.fsize = 0;
	rf2.dist = PVFS_Dist_create("simple_stripe");
	rf2.extend_flag = 1;
	PINT_Dist_lookup(rf2.dist);

	rf3.iod_num = 2;
	rf3.iod_count = 4;
	rf3.fsize = 0;
	rf3.dist = PVFS_Dist_create("simple_stripe");
	rf3.extend_flag = 1;
	PINT_Dist_lookup(rf3.dist);

	rf4.iod_num = 3;
	rf4.iod_count = 4;
	rf4.fsize = 0;
	rf4.dist = PVFS_Dist_create("simple_stripe");
	rf4.extend_flag = 1;
	PINT_Dist_lookup(rf4.dist);

   /* Turn on debugging */
	/* gossip_enable_stderr(); */
	/* gossip_set_debug_mask(1,REQUEST_DEBUG); */

	offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	offset = 0;

	printf("\n************************************\n");
	do
	{
		eof_flag = 0;
		segmax = SEGMAX;
		bytemax = BYTEMAX;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs1, &rf1, &segmax,
			offset_array, size_array, &offset, &bytemax, 
			&eof_flag, PINT_SERVER);

		if(offset != -1)
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			if(segmax == 0)
			{
				fprintf(stderr, "  IEEE! no results to report.\n");
			}
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
		printf("**** first request done.\n");
	}

	offset = 0;
	printf("\n************************************\n");
	do
	{
		eof_flag = 0;
		segmax = SEGMAX;
		bytemax = BYTEMAX;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs2, &rf2, &segmax,
			offset_array, size_array, &offset, &bytemax, 
			&eof_flag, PINT_SERVER);

		if(offset != -1)
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			if(segmax == 0)
			{
				fprintf(stderr, "  IEEE! no results to report.\n");
			}
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
		printf("**** second request done.\n");
	}

	offset = 0;
	printf("\n************************************\n");
	do
	{
		eof_flag = 0;
		segmax = SEGMAX;
		bytemax = BYTEMAX;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs3, &rf3, &segmax,
			offset_array, size_array, &offset, &bytemax, 
			&eof_flag, PINT_SERVER);

		if(offset != -1)
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			if(segmax == 0)
			{
				fprintf(stderr, "  IEEE! no results to report.\n");
			}
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
		printf("**** third request done.\n");
	}


	offset = 0;
	printf("\n************************************\n");
	do
	{
		eof_flag = 0;
		segmax = SEGMAX;
		bytemax = BYTEMAX;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs4, &rf4, &segmax,
			offset_array, size_array, &offset, &bytemax, 
			&eof_flag, PINT_SERVER);

		if(offset != -1)
		{
			fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
		}

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			if(segmax == 0)
			{
				fprintf(stderr, "  IEEE! no results to report.\n");
			}
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
		printf("**** fourth request done.\n");
	}


	return 0;
}

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

#include <pvfs-distribution.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#include <simple-stripe.h>

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

int main(int argc, char **argv)
{
	int i;
	PINT_Request *r;
	PINT_Request *r_enc;
	PINT_Request *r_dec;
	PINT_Request_state *rs1;
	PINT_Request_file_data rf1;
	PINT_Request_result seg1;
	int ret = -1;
	int pack_size = 0;

	/* DESCRIPTION: 
	 * in this case, we are doing a single write, of size 4390228,
	 * at offset 20M.  There are two servers.  We are looking at
	 * the output of the request processing code on the client side
	 */

	/* PVFS_Process_request arguments */
	int retval;
	int32_t blocklength = 4390228;
	PVFS_size displacement = 20*1024*1024;

	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r);

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

	rs1 = PINT_New_request_state(r_dec);

	/* set up file data for each server */
	rf1.server_nr = 0;
	rf1.server_ct = 2;
	rf1.fsize = 0;
	rf1.dist = PVFS_dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

	/* set up response struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.segmax = SEGMAX;
	seg1.bytemax = BYTEMAX;
	seg1.bytes = 0;
	seg1.segs = 0;

   /* Turn on debugging */
	/* gossip_enable_stderr(); */
	/* gossip_set_debug_mask(1,REQUEST_DEBUG); */

	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		/* note that bytemax is exactly large enough to hold all of the
		 * data that I should find here
		 */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
			if(seg1.segs == 0)
			{
				fprintf(stderr, "  AAIIEEE! no results to report.\n");
			}
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

		if(PINT_REQUEST_DONE(rs1))
		{
			fprintf(stderr, "  AAIIEEE! Why am I done?\n");
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

	return 0;
}

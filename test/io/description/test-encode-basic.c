/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>

#include <pvfs-distribution.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#include <simple-stripe.h>

#define SEGMAX 16
#define BYTEMAX (1024*1024)

int main(int argc, char **argv)
{
	PINT_Request *r;
	PINT_Request *r_enc;
	PINT_Request *r_dec;
	int ret = -1;
	int pack_size = 0;
	int commit_index = 0;

	r = PVFS_BYTE;

	/* allocate a new request and pack the original one into it */
	pack_size = PINT_REQUEST_PACK_SIZE(r);
	r_enc = (PINT_Request*)malloc(pack_size);
	assert(r_enc != NULL);

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

	return 0;
}

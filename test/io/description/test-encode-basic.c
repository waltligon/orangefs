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

#include <pint-distribution.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#define SEGMAX 16
#define BYTEMAX (1024*1024)

void Dump_request(PVFS_Request req);

int main(int argc, char **argv)
{
	PINT_Request *r;
	PINT_Request *r_enc;
	PINT_Request *r_dec;
	int ret = -1;
	int pack_size = 0;

	r = PVFS_BYTE;
	Dump_request(r); 

	/* allocate a new request and pack the original one into it */
	pack_size = PINT_REQUEST_PACK_SIZE(r);
	fprintf(stderr, "pack size is %d\n",pack_size);
	r_enc = (PINT_Request*)malloc(pack_size);
	assert(r_enc != NULL);

	ret = PINT_Request_commit(r_enc, r);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_commit() failure.\n");
		return(-1);
	}
	fprintf(stderr, "commit returns %d\n", ret);
	Dump_request(r_enc);
	ret = PINT_Request_encode(r_enc);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_encode() failure.\n");
		return(-1);
	}
	fprintf(stderr, "encode returns %d\n", ret);
	Dump_request(r_enc);


	/* decode the encoded request (hopefully ending up with something
	 * equivalent to the original request)
	 */
	r_dec = (PINT_Request*)malloc(pack_size);
	memcpy(r_dec, r_enc, pack_size);
	free(r_enc);
	// free(r);
	ret = PINT_Request_decode(r_dec);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_decode() failure.\n");
		return(-1);
	}
	fprintf(stderr, "decode returns %d\n", ret);

	return 0;
}

void Dump_request(PVFS_Request req)
{
	fprintf(stderr,"**********************\n");
	fprintf(stderr,"address:\t%p\n",req);
	fprintf(stderr,"offset:\t\t%d\n",(int)req->offset);
	fprintf(stderr,"num_ereqs:\t%d\n",(int)req->num_ereqs);
	fprintf(stderr,"num_blocks:\t%d\n",(int)req->num_blocks);
	fprintf(stderr,"stride:\t\t%d\n",(int)req->stride);
	fprintf(stderr,"ub:\t\t%d\n",(int)req->ub);
	fprintf(stderr,"lb:\t\t%d\n",(int)req->lb);
	fprintf(stderr,"agg_size:\t%d\n",(int)req->aggregate_size);
	fprintf(stderr,"num_chunk:\t%d\n",(int)req->num_contig_chunks);
	fprintf(stderr,"depth:\t\t%d\n",(int)req->depth);
	fprintf(stderr,"num_nest:\t%d\n",(int)req->num_nested_req);
	fprintf(stderr,"commit:\t\t%d\n",(int)req->committed);
	fprintf(stderr,"refcount:\t\t%d\n",(int)req->refcount);
	fprintf(stderr,"ereq:\t\t%p\n",req->ereq);
	fprintf(stderr,"sreq:\t\t%p\n",req->sreq);
	fprintf(stderr,"**********************\n");
}


/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Simulates Network Encoding of a trivial datatypes
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 8/22/2003
 * Last Updated: 8/22/2003
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include<assert.h>

#include <pint-request.h>
#include <pvfs-distribution.h>
#include <simple-stripe.h>
#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-encode-basic.h"
#define SEGMAX 16
#define BYTEMAX (1024*1024)

/* PROTO-TYPES */
/* static void Dump_request(PVFS_Request); */

/*
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
static int test_encode(void){
    /* Used for calculating correct offset values */

   PINT_Request *r;
   PINT_Request *r_enc;
   PINT_Request *r_dec;
   int ret = -1;
   int pack_size = 0;
                                                                                
   r = PVFS_BYTE;
/*
   Dump_request(r);
*/
                                                                                
   /* allocate a new request and pack the original one into it */
   pack_size = PINT_REQUEST_PACK_SIZE(r);
/*
   fprintf(stderr, "pack size is %d\n",pack_size);
*/
   r_enc = (PINT_Request*)malloc(pack_size);
   assert(r_enc != NULL);
                                                                                
   ret = PINT_Request_commit(r_enc, r);
   if(ret < 0)
   {
      fprintf(stderr, "PINT_Request_commit() failure.\n");
      return(-1);
   }
/*
   fprintf(stderr, "commit returns %d\n", ret);
   Dump_request(r_enc);
*/
   ret = PINT_Request_encode(r_enc);
   if(ret < 0)
   {
      fprintf(stderr, "PINT_Request_encode() failure.\n");
      return(-1);
   }
/*
   fprintf(stderr, "encode returns %d\n", ret);
   Dump_request(r_enc); */
                                                                                
                                                                                
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
/*
   fprintf(stderr, "decode returns %d\n", ret);
*/
                                                                                
   return 0;
}
                                                                                
#if 0
void Dump_request(PVFS_Request req)
{
   fprintf(stderr,"**********************\n");
   fprintf(stderr,"address:\t%x\n",(unsigned int)req);
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
   fprintf(stderr,"ereq:\t\t%x\n",(int)req->ereq);
   fprintf(stderr,"sreq:\t\t%x\n",(int)req->sreq);
   fprintf(stderr,"**********************\n");
}
#endif

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_encode_basic(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams __unused)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_encode();
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this simulates both the client and server side of a single server I/O
 * operation.  It simulates one phase of a particular romio test program
 * called "noncontig", which happens to use hindexed() calls currently.
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 8/25/2003
 * Last Updated: 8/25/2003
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include<assert.h>

#include <pint-request.h>
#include <pint-distribution.h>
#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-noncontig-pattern.h"
#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

/*
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
static int test_noncontig(void){
   int i;
   PINT_Request *file_req;
   PINT_Request *mem_req;
   PINT_Request_state *mem_state;
   PINT_Request_state *file_state;
   PINT_Request_state *file_state_server;
   PINT_Request_file_data rf1;
   PINT_Request_result seg1;
   int32_t* len_arrayS = NULL;
   int32_t* len_arrayC = NULL;
   PVFS_offset* off_arrayS = NULL;
   PVFS_offset* off_arrayC = NULL;
   PVFS_size total_bytes_client = 0;
   PVFS_size total_bytes_server = 0;
                                                                               
    /* Used for calculating correct offset values */
    int32_t tmpOff = 0;

   /* PVFS_Process_request arguments */
   int retval;
   len_arrayS = (int32_t*)malloc(64*sizeof(int32_t));
   len_arrayC = (int32_t*)malloc(64*sizeof(int32_t));
   off_arrayS = (PVFS_offset*)malloc(64*sizeof(PVFS_offset));
   off_arrayC = (PVFS_offset*)malloc(64*sizeof(PVFS_offset));
   assert(len_arrayS != NULL && len_arrayC != NULL && off_arrayS != NULL && off_arrayC != NULL);
                                                                               
   /* setup file datatype */
   for(i=0; i<63; i++)
   {
       len_arrayS[i] = 4;
       off_arrayS[i] = 4 + (8*i);
   }
   PVFS_Request_hindexed(63, len_arrayS, off_arrayS, PVFS_BYTE, &file_req);
                                                                               
   /* setup mem datatype */
   len_arrayC[0] = 0;
   off_arrayC[0] = 135295720;
   for(i=1; i<64; i++)
   {
       len_arrayC[i] = 4;
       off_arrayC[i] = off_arrayC[0] + 4 + ((i-1)*8);
   }
   PVFS_Request_hindexed(64, len_arrayC, off_arrayC, PVFS_BYTE, &mem_req);
                                                                               
   mem_state = PINT_New_request_state(mem_req);
   file_state = PINT_New_request_state(file_req);
   file_state_server = PINT_New_request_state(file_req);
                                                                               
   /* set up file data for request */
   rf1.server_nr = 0;
   rf1.server_ct = 1;
   rf1.fsize = 0;
   rf1.dist = PINT_dist_create("simple_stripe");
   rf1.extend_flag = 1;
   PINT_dist_lookup(rf1.dist);
                                                                               
   /* set up result struct */
   seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
   seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
   seg1.bytemax = BYTEMAX;
   seg1.segmax = SEGMAX;
   seg1.bytes = 0;
   seg1.segs = 0;
                                                                               
                                                                               
   PINT_REQUEST_STATE_SET_TARGET(file_state, 0);
   PINT_REQUEST_STATE_SET_FINAL(file_state, PINT_REQUEST_TOTAL_BYTES(mem_req));   PINT_REQUEST_STATE_SET_TARGET(file_state_server, 0);
   PINT_REQUEST_STATE_SET_FINAL(file_state_server, PINT_REQUEST_TOTAL_BYTES(mem_req));
                                                                               
                                                                               
   /* Turn on debugging */
/*
    gossip_enable_stderr();
    gossip_set_debug_mask(1,REQUEST_DEBUG);
                                                                               
   printf("\nCLIENT ************************************\n");
*/
   do
   {
      seg1.bytes = 0;
      seg1.segs = 0;
                                                                               
      /* process request */
      retval = PINT_Process_request(file_state, mem_state, &rf1, &seg1, PINT_CLIENT);
                                                                               
      if(retval >= 0)
      {
/*
         printf("results of PINT_Process_request():\n");
         printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
*/
         total_bytes_client += seg1.bytes;
         for(i=0; i<seg1.segs; i++, tmpOff++)
         {
	    if((int)off_arrayC[tmpOff] != (int)seg1.offset_array[i]){
		printf("segment %d's offset is %d but should be %d\n",
		       i,(int)seg1.offset_array[i],(int)off_arrayC[tmpOff]);
		return -1;
	    }
	    else if((int)len_arrayC[tmpOff] != (int)seg1.size_array[i]){
		printf("segment %d's size is %d but should be %d\n",
		       i,(int)seg1.size_array[i],(int)len_arrayC[tmpOff]);
		return -1;
	    }
         }
      }
                                                                               
   } while(!PINT_REQUEST_DONE(file_state) && retval >= 0);
                                                                               
   if(retval < 0)
   {
      fprintf(stderr, "Error: PINT_Process_request() failure.\n");
      return(-1);
   }
   if(PINT_REQUEST_DONE(file_state))
   {
/*
      printf("**** request done.\n");
*/
   }
                                                                               
/*
   printf("\nSERVER ************************************\n");
*/
    tmpOff = 0;
   do
   {
      seg1.bytes = 0;
      seg1.segs = 0;
                                                                               
      /* process request */
      retval = PINT_Process_request(file_state_server, NULL, &rf1, &seg1, PINT_SERVER);
                                                                               
      if(retval >= 0)
      {
/*
         printf("results of PINT_Process_request():\n");
         printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
*/
         total_bytes_server += seg1.bytes;
         for(i=0; i<seg1.segs; i++,tmpOff++)
         {
	    if((int)off_arrayS[tmpOff] != (int)seg1.offset_array[i]){
		printf("segment %d's offset is %d but should be %d\n",
		       i,(int)seg1.offset_array[i],(int)off_arrayS[tmpOff]);
		return -1;
	    }
	    else if((int)len_arrayS[tmpOff] != (int)seg1.size_array[i]){
		printf("segment %d's size is %d but should be %d\n",
		       i,(int)seg1.size_array[i],(int)len_arrayS[tmpOff]);
		return -1;
	    }
         }
      }
                                                                               
   } while(!PINT_REQUEST_DONE(file_state_server) && retval >= 0);
                                                                               
   if(retval < 0)
   {
      fprintf(stderr, "Error: PINT_Process_request() failure.\n");
      return(-1);
   }
   if(PINT_REQUEST_DONE(file_state_server))
   {
/*
      printf("**** request done.\n");
*/
   }
                                                                               
/*
   printf("total bytes processed on client side: %Ld\n", (long long)total_bytes_client);
   printf("total bytes processed on server side: %Ld\n", (long long)total_bytes_server);
*/
                                                                               
   if(total_bytes_client == total_bytes_server)
   {
/*
       printf("SUCCESS.\n");
*/
   }
   else
   {
       printf("FAILURE!!!\n");
	return -1;
   }
                                                                               
   return 0;
}
                                                                                

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_noncontig_pattern(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams __unused)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_noncontig();
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

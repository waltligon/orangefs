/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 9/3/2003
 * Last Updated: 9/3/2003
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include<assert.h>

#include <pint-request.h>
#include <pvfs-distribution.h>
#include <simple-stripe.h>
#include <pvfs2-debug.h>
#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-romio-noncontig-pattern2.h"
#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

/*
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
static int test_romio_noncontig2(void){
   int i;
   PINT_Request *file_req;
   PINT_Request *mem_req;
   PINT_Request_state *mem_state;
   PINT_Request_state *file_state;
   PINT_Request_state *file_state_server;
   PINT_Request_file_data rf1;
   PINT_Request_result seg1;
   int32_t* len_array = NULL;
   PVFS_offset* off_array = NULL;
   PVFS_size total_bytes_client = 0;
   PVFS_size total_bytes_server = 0;
    int totalsize = 0;
    int j;
                                                                                                                                                       
   /* PVFS_Process_request arguments */
   int retval;
                                                                                                                                                       
   len_array = (int32_t*)malloc(64*sizeof(int32_t));
   off_array = (PVFS_offset*)malloc(64*sizeof(PVFS_offset));
   assert(len_array != NULL && off_array != NULL);
                                                                                                                                                       
   /* setup file datatype */
   PVFS_Request_contiguous(256, PVFS_BYTE, &file_req);
                                                                                                                                                       
   /* setup mem datatype */
   len_array[0] = 0;
   off_array[0] = 135295720;
   for(i=0; i<64; i++)
   {
       len_array[i] = 4;
       off_array[i] = 135313976 + i*8;
	totalsize += 4;
   }
   PVFS_Request_hindexed(64, len_array, off_array, PVFS_BYTE, &mem_req);
                                                                                                                                                       
   mem_state = PINT_New_request_state(mem_req);
   file_state = PINT_New_request_state(file_req);
   file_state_server = PINT_New_request_state(file_req);
                                                                                                                                                       
   /* set up file data for request */
   rf1.server_nr = 0;
   rf1.server_ct = 1;
   rf1.fsize = 0;
   rf1.dist = PVFS_dist_create("simple_stripe");
   rf1.extend_flag = 1;
   PINT_Dist_lookup(rf1.dist);
                                                                                                                                                       
   /* set up result struct */
   seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
   seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
   seg1.bytemax = BYTEMAX;
   seg1.segmax = SEGMAX;
   seg1.bytes = 0;
   seg1.segs = 0;
                                                                                                                                                       
                                                                                                                                                       
   PINT_REQUEST_STATE_SET_TARGET(file_state, 0);
   PINT_REQUEST_STATE_SET_FINAL(file_state, PINT_REQUEST_TOTAL_BYTES(mem_req));
   PINT_REQUEST_STATE_SET_TARGET(file_state_server, 0);
   PINT_REQUEST_STATE_SET_FINAL(file_state_server, PINT_REQUEST_TOTAL_BYTES(mem_req));
                                                                                                                                                       
                                                                                                                                                       
   /* Turn on debugging */
   // gossip_enable_stderr();
   // gossip_set_debug_mask(1,REQUEST_DEBUG);
                                                                                                                                                       
    j = 0;
   do
   {
      seg1.bytes = 0;
      seg1.segs = 0;
                                                                                                                                                       
      /* process request */
      retval = PINT_Process_request(file_state, mem_state, &rf1, &seg1, PINT_CLIENT);
                                                                                                                                                       
      if(retval >= 0)
      {
         total_bytes_client += seg1.bytes;
         for(i=0; i<seg1.segs; i++,j++)
         {
	    if((int)seg1.offset_array[i] != off_array[j]){
		printf("Error: segment %d offset is %d but should be %d\n",i,(int)seg1.offset_array[i],(int)off_array[j]);
		return -1;
	    }
	    else if((int)seg1.size_array[i] != len_array[j]){
		printf("Error: segment %d size is %d but should be %d\n",i,(int)seg1.size_array[i],len_array[j]);
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
/*      printf("**** request done.\n");
*/
   }
                                                                                                                                                       
/*   printf("\nSERVER ************************************\n");
*/
   do
   {
      seg1.bytes = 0;
      seg1.segs = 0;
                                                                                                                                                       
      /* process request */
      retval = PINT_Process_request(file_state_server, NULL, &rf1, &seg1, PINT_SERVER);
                                                                                                                                                       
      if(retval >= 0)
      {
         total_bytes_server += seg1.bytes;
         for(i=0; i<seg1.segs; i++)
         {
	    if(totalsize != (int)seg1.size_array[i]){
		printf("Error: total size for server %d is %d but should be %d\n",i,(int)seg1.size_array[i],totalsize);
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
	return 0;
   }
   else
   {
/*
       printf("FAILURE!!!\n");
*/
	return -1;
   }
}

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_romio_noncontig_pattern2(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams __unused)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_romio_noncontig2();
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

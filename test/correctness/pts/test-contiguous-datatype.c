/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 8/29/2003
 * Last Updated: 8/29/2003
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include<assert.h>

#include <pint-request.h>
#include <pint-distribution.h>
#include <pvfs2-debug.h>
#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-contiguous-datatype.h"
#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

/*
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
static int test_cont_datatype(void){
   int i;
   PINT_Request *r1;
   PINT_Request *r2;
   PINT_Request_state *rs1;
   PINT_Request_state *rs2;
   PINT_Request_file_data rf1;
   PINT_Request_result seg1;
                                                                                
   /* PVFS_Process_request arguments */
   int retval;
                                                                                
   /* set up request */
   PVFS_Request_contiguous(266240, PVFS_BYTE, &r1);
                                                                                
   /* set up request state */
   rs1 = PINT_New_request_state(r1);
                                                                                
   /* set up memory request */
   PVFS_Request_contiguous(266240, PVFS_BYTE, &r2);
   rs2 = PINT_New_request_state(r2);
                                                                                
   /* set up file data for request */
   rf1.server_nr = 0;
   rf1.server_ct = 4;
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
                                                                                
   /* Turn on debugging */
/*
    gossip_enable_stderr();
    gossip_set_debug_mask(1,REQUEST_DEBUG);
*/
                                                                                
   /* skipping logical bytes */
/*
    PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
    PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
*/
                                                                                
   do
   {
      seg1.bytes = 0;
      seg1.segs = 0;
                                                                                
      /* process request */
      retval = PINT_Process_request(rs1, rs2, &rf1, &seg1, PINT_CLIENT);
                                                                                
      if(retval >= 0)
      {
         printf("results of PINT_Process_request():\n");
         printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
         for(i=0; i<seg1.segs; i++)
         {
	    if(i == 0 && (int)seg1.offset_array[i] != 0){
		printf("segment %d's offset is %d but should be %d\n",
			i,(int)seg1.offset_array[i],0);
		return -1;
	    }
	    else if(i == 1 && (int)seg1.offset_array[i] != 262144){
		printf("segment %d's offset is %d but should be %d\n",
			i,(int)seg1.offset_array[i],262144);
		return -1;
	    }
	    /* Note, we should be checking the size but I'm not sure what the size values should be */
         }
      }
                                                                                
   } while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
   if(retval < 0)
   {
      fprintf(stderr, "Error: PINT_Process_request() failure.\n");
      return(-1);
   }
   if(PINT_REQUEST_DONE(rs1))
   {
/*
      printf("**** request done.\n");
*/
   }
                                                                                
   return 0;
}
                                                                                

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_contiguous_datatype(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams __unused)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_cont_datatype();
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

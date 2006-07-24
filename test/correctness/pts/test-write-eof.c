/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Simulates request processing on both the client and server side for two
 * I/O operations to the same server that write beyond EOF.
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 8/26/2003
 * Last Updated: 8/26/2003
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
#include "test-write-eof.h"
#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

/*
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
static int test_write(void){
   int i;
   PINT_Request *r1;
   PINT_Request *r2;
   PINT_Request_state *rs1;
   PINT_Request_state *rs2;
   PINT_request_file_data rf1;
   PINT_request_file_data rf2;
   PINT_Request_result seg1;
                                                                                
   /* PVFS_Process_request arguments */
   int retval;
                                                                                
   int32_t blocklength = 10*1024*1024; /* 10M */

    /* Used for calculating correct offset values */
    int32_t tmpOff = 0;
    int32_t stripesize = 65536;
 
   /* set up two requests, both at offset 0 */
   PVFS_size displacement = 0;  /* first at offset zero */
   PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r1);
   PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r2);
                                                                                
   /* set up two request states */
   rs1 = PINT_new_request_state(r1);
   rs2 = PINT_new_request_state(r2);
   /* set up file data for first request */
   rf1.server_nr = 0;
   rf1.server_ct = 3;
   rf1.fsize = 8454144;
   rf1.dist = PINT_dist_create("simple_stripe");
   rf1.extend_flag = 0;
   PINT_dist_lookup(rf1.dist);
                                                                                
   /* file data for second request is the same, except the file
    * will have grown by 10M
    */
   rf2.server_nr = 0;
   rf2.server_ct = 3;
   rf2.fsize = 8454144;
   rf2.dist = PINT_dist_create("simple_stripe");
   rf2.extend_flag = 0;
   PINT_dist_lookup(rf2.dist);
                                                                                
   /* set up result struct */
   seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
   seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
   seg1.bytemax = BYTEMAX;
   seg1.segmax = SEGMAX;
   seg1.bytes = 0;
   seg1.segs = 0;
                                                                                
   /* Turn on debugging 
    gossip_enable_stderr();
    gossip_set_debug_mask(1,REQUEST_DEBUG);
*/
                                                                                
   do
   {
      seg1.bytes = 0;
      seg1.segs = 0;
                                                                                
      /* process request */
      retval = PINT_process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);
                                                                                
      if(retval >= 0)
      {
/*
         printf("results of PINT_Process_request(PINT_SERVER):\n");
         printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
*/
         for(i=0; i<seg1.segs; i++)
         {
	    if( (blocklength/3) != (int)seg1.size_array[i]){
		printf("segment %d size is %d but should be %d\n",
			i, (int)seg1.size_array[i],blocklength/3);
	    }
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
      printf("**** first request done.\n");
*/
   }
                                                                                
    tmpOff = 0;
    do
    {
	seg1.bytes = 0;
	seg1.segs = 0;
                                                                                
	/* process request */
	retval = PINT_process_request(rs2, NULL, &rf2, &seg1, PINT_CLIENT);
                                                                                
	if(retval >= 0)
	{
	    for(i=0; i<seg1.segs; i++, (tmpOff += stripesize*3))
	    {
		if(stripesize != (int)seg1.size_array[i]){
		    printf("segment %d's size is %d but should be %d\n",
			    i,(int)seg1.size_array[i],stripesize);
		    return -1;
		}
		else if(tmpOff != (int)seg1.offset_array[i]){
		    printf("segment %d's offset is %d but should be %d\n",
			    i,(int)seg1.offset_array[i],tmpOff);
		    return -1;
		}
	    }
	}
                                                                                
   } while(!PINT_REQUEST_DONE(rs2) && retval >= 0);
                                                                                
   if(retval < 0)
   {
      fprintf(stderr, "Error: PINT_Process_request() failure.\n");
      return(-1);
   }
   if(PINT_REQUEST_DONE(rs2))
   {
/*
      printf("**** second request done.\n");
*/
    }
   return 0;
}
                                                                                

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_write_eof(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams __unused)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_write();
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

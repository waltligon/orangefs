/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * test-request_indexed: Checks PVFS_Request_indexed for correct segmentation 
 * of the request.  For instance, If the segment size is to be 4M and there 
 * are 3 segments that are allocated to a 10M chunk, then the offsets for
 * those segments should be 
 * 0 (first segment with 4M of space) 
 * 4M (second segment with 4M of space)
 * 8M (third segment with 2M of space)
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 8/14/2003
 * Last Updated: 8/21/2003
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

#include <pint-request.h>
#include <pint-distribution.h>
#include "pint-dist-utils.h"
#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-request-indexed.h"
#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

/* Checks for valid segmentation on 2 request types for PVFS_Request_indexed
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
static int test_request(void){
    int i;
    PINT_Request *r1;
    PINT_Request *r2;
    PINT_Request_state *rs1;
    PINT_Request_state *rs2;
    PINT_Request_file_data rf1;
    PINT_Request_file_data rf2;
    PINT_Request_result seg1;
    PINT_Request_result seg2;
                                                                                
                                                                                
    /* PVFS_Process_request arguments */
    int retval;
                                                                                
    int32_t blocklength = 10*1024*1024; /* 10M */

    /* Used for calculating correct offset values */
    int32_t tmpSize;
    PVFS_size tmpOff;
    int32_t segSize;
                                                                                
    /* set up two requests */
    PVFS_size displacement1 = 0;  /* first at offset zero */
    PVFS_size displacement2 = 10*1024*1024;  /* next at 10M offset */
    PVFS_Request_indexed(1, &blocklength, &displacement1, PVFS_BYTE, &r1);
                                                                                
    PVFS_Request_indexed(1, &blocklength, &displacement2, PVFS_BYTE, &r2);
    /* set up two request states */
    rs1 = PINT_New_request_state(r1);
    rs2 = PINT_New_request_state(r2);
                                                                                
    /* set up file data for first request */
    PINT_dist_initialize();
    rf1.server_nr = 0;
    rf1.server_ct = 1;
    rf1.fsize = 0;
    rf1.dist = PINT_dist_create("simple_stripe");
    rf1.extend_flag = 1;
    PINT_dist_lookup(rf1.dist);
                                                                                
    /* file data for second request is the same, except the file
     * will have grown by 10M
     */
    rf2.server_nr = 0;
    rf2.server_ct = 1;
    rf2.fsize = 10*1024*1024;
    rf2.dist = PINT_dist_create("simple_stripe");
    rf2.extend_flag = 1;
    PINT_dist_lookup(rf2.dist);
                                                                                 
    /* set up result structures */
    seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg1.segmax = SEGMAX;
    seg1.bytemax = BYTEMAX;
    seg1.segs = 0;
    seg1.bytes = 0;
                                                                                 
    seg2.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg2.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg2.segmax = SEGMAX;
    seg2.bytemax = BYTEMAX;
    seg2.segs = 0;
    seg2.bytes = 0;
                                                                                 
    /* Turn on debugging */
    /* gossip_enable_stderr();
    gossip_set_debug_mask(1,REQUEST_DEBUG); */
                                                                                
    tmpSize = blocklength;
    tmpOff = displacement1;
    segSize = BYTEMAX;
    
    do
    {
	seg1.bytes = 0;
	seg1.segs = 0;
	/* process request */
	retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);
                                                                                
	if(retval >= 0)
	{
	    for(i = 0; i < seg1.segs; i++)
	    {
		if(tmpOff != ((int)seg1.offset_array[i])){
		    printf("Error:  segment %d offset is %d but should be %d\n",i,(int)seg1.offset_array[i],(int)tmpOff);
		    return -1;
		}
		if(segSize != ((int)seg1.size_array[i])){
		    printf("Error:  segment %d size is %d but should be %d\n",i,(int)seg1.size_array[i],segSize);
		    return -1;
		}

		if( (tmpSize - BYTEMAX) < BYTEMAX){
		    segSize = tmpSize - BYTEMAX;
		}
		else{   
		    segSize = BYTEMAX;
		}
		tmpSize -= BYTEMAX;
		tmpOff += BYTEMAX;
	    }
	}
    } while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
    if(retval < 0)
    {
      fprintf(stderr, "Error: PINT_Process_request() failure.\n");      return(-1);
   }
   if(PINT_REQUEST_DONE(rs1))
   {
/*      printf("**** first request done.\n");
*/
   }
    tmpOff = displacement2;
    tmpSize = blocklength;
    segSize = BYTEMAX;
   do
   {
      seg2.bytes = 0;
      seg2.segs = 0;
      /* process request */
      retval = PINT_Process_request(rs2, NULL, &rf2, &seg2, PINT_SERVER);
                                                                                
      if(retval >= 0)
      {
         for(i=0; i < seg2.segs; i++)
         {
		if(tmpOff != ((int)seg2.offset_array[i])){
		    printf("Error:  segment %d offset is %d but should be %d\n",i,(int)seg2.offset_array[i],(int)tmpOff);
		    return -1;
		}
		if(segSize != ((int)seg2.size_array[i])){
		    printf("Error:  segment %d size is %d but should be %d\n",i,(int)seg2.size_array[i],segSize);
		    return -1;
		}

		if( (tmpSize - BYTEMAX) < BYTEMAX){
		    segSize = tmpSize - BYTEMAX;
		}
		else{   
		    segSize = BYTEMAX;
		}
		tmpSize -= BYTEMAX;
		tmpOff += BYTEMAX;
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
 /*     printf("**** second request done.\n"); */
   }
                                                                                
   return 0;
}

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_request_indexed(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams __unused)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_request();
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

/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Simulates Accessing a data type with a specific starting location and ending location
 * Author: Michael Speth, Testing code written & designed by Phil C.
 * Date: 9/1/2003
 * Last Updated: 9/1/2003
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
#define SEGMAX 16
#define BYTEMAX (4*1024*1024)
extern pvfs_helper_t pvfs_helper;

/*
 * Parameters: none
 * Returns 0 on success and -1 on failure (ie - the segment offsets
 * were not calcuated correctly by Request_indexed
 */
int test_vec_start_final(void){
    int i;
    PINT_Request *r1;
    PINT_Request_state *rs1;
    PINT_Request_file_data rf1;
    PINT_Request_result seg1;
                                                                                
    /* PVFS_Process_request arguments */
    int retval;
                                                                                
    int32_t tmpOff, tmpSize;
    int segNum;

    /* set up request */
    PVFS_Request_vector(10, 1024, 10*1024, PVFS_BYTE, &r1);


    /* set up request state */
    rs1 = PINT_New_request_state(r1);
                                                                                
    /* set up file data for request */
    rf1.server_nr = 0;
    rf1.server_ct = 8;
    rf1.fsize = 10000000;
    rf1.dist = PVFS_Dist_create("simple_stripe");
    rf1.extend_flag = 1;
    PINT_Dist_lookup(rf1.dist);
                                                                                
    /* set up result struct */
    seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg1.bytemax = BYTEMAX;
    seg1.segmax = SEGMAX;
    seg1.bytes = 0;
    seg1.segs = 0;
                                                                                
    /* Turn on debugging */
    /* gossip_enable_stderr();
     gossip_set_debug_mask(1,REQUEST_DEBUG); */
                                                                                
    /* skipping logical bytes */
    /*seg1.bytemax = (3 * 1024) + 512;*/
                                                                                
    PINT_REQUEST_STATE_SET_TARGET(rs1,(3 * 1024) + 512);
    PINT_REQUEST_STATE_SET_FINAL(rs1,(6 * 1024) + 512);
                                                                                
    /* need to reset bytemax before we contrinue */
    /*seg1.bytemax = BYTEMAX;*/
                                                                                
    do
    {
       seg1.bytes = 0;
       seg1.segs = 0;
                                                                                
       /* process request */
       retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);
                                                                                 
	if(retval >= 0)
	{
	   tmpOff = (3 * 1024)*10 + 512;
	   tmpSize = 512;
	    segNum = 3;
	    for(i=0; i<seg1.segs; i++, tmpOff = segNum*(10*1024))
	    {
		if(tmpOff != (int)seg1.offset_array[i]){
		    printf("segment %d's offset is %d but should be %d\n",
			    i,(int)seg1.offset_array[i],tmpOff);
		    return -1;
		}
		else if(tmpSize != (int)seg1.size_array[i]){
		    printf("segment %d's size is %d but should be %d\n",
			i,(int)seg1.size_array[i],tmpSize);
		    return -1;
		}
		if(seg1.segs  == (i+2)){
		    tmpSize = 512;
		}
		else{
		    tmpSize = 1024;	
		}
		segNum++;
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
int test_vector_start_final_offset(MPI_Comm * comm,
		     int rank,
		     char *buf,
		     void *rawparams)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_vec_start_final();
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

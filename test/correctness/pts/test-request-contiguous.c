/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * test-request_contiguous: simulates request processing on 4 seperate servers 
 * that each  hold a portion of the file being accessed, for a contiguous read 
 * or write.
 * Author: Michael Speth - testing code written by Phil C.
 * Date: 8/21/2003
 * Last Updated: 8/21/2003
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

#include <pint-request.h>
#include <pvfs-distribution.h>
#include <pvfs2-types.h>
#include <pvfs2-request.h>
#include <simple-stripe.h>
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
int test_request_cont(void){
    int i;
    PINT_Request *r;
    PINT_Request *r_enc;
    PINT_Request *r_dec;
    PINT_Request_state *rs1;
    PINT_Request_state *rs2;
    PINT_Request_state *rs3;
    PINT_Request_state *rs4;
    PINT_Request_file_data rf1;
    PINT_Request_file_data rf2;
    PINT_Request_file_data rf3;
    PINT_Request_file_data rf4;
    PINT_Request_result seg1;
    int ret = -1;
    int pack_size = 0;
    int32_t segSize;

                                                                                
    /* PVFS_Process_request arguments */
    int retval;

                                                                                
    /* the case that we want to test is a write, with 4 servers holding
     * parts of the file data.  We will setup 4 different request states,
     * one per server.  The request will be large enough that all 4
     * servers will be involved
     */
                                                                                
    /* set up one request, we will reuse it for each server scenario */
    /* we want to read 4M of data, resulting from 1M from each server */
    PVFS_Request_contiguous((4*1024*1024), PVFS_BYTE, &r);

    /* Used for calculating correct offset values */
    segSize = 1024*1024;
                                                                                
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
                                                                                 
    /* set up four request states */
    rs1 = PINT_New_request_state(r_dec);
    rs2 = PINT_New_request_state(r_dec);
    rs3 = PINT_New_request_state(r_dec);
    rs4 = PINT_New_request_state(r_dec);
                                                                                 
    /* set up file data for each server */
    rf1.server_nr = 0;
    rf1.server_ct = 4;
    rf1.fsize = 0;
    rf1.dist = PVFS_Dist_create("simple_stripe");
    rf1.extend_flag = 1;
    PINT_Dist_lookup(rf1.dist);
                                                                                 
    rf2.server_nr = 1;
    rf2.server_ct = 4;
    rf2.fsize = 0;
    rf2.dist = PVFS_Dist_create("simple_stripe");
    rf2.extend_flag = 1;
    PINT_Dist_lookup(rf2.dist);
                                                                                 
    rf3.server_nr = 2;
    rf3.server_ct = 4;
    rf3.fsize = 0;
    rf3.dist = PVFS_Dist_create("simple_stripe");
    rf3.extend_flag = 1;
    PINT_Dist_lookup(rf3.dist);
                                                                                 
    rf4.server_nr = 3;
    rf4.server_ct = 4;
    rf4.fsize = 0;
    rf4.dist = PVFS_Dist_create("simple_stripe");
    rf4.extend_flag = 1;
    PINT_Dist_lookup(rf4.dist);
                                                                                 
    /* set up response for each server */
    seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
    seg1.segmax = SEGMAX;
    seg1.bytemax = BYTEMAX;
    seg1.segs = 0;
    seg1.bytes = 0;
                                                                                 
    /* Turn on debugging */
    /* gossip_enable_stderr(); */
    /* gossip_set_debug_mask(1,REQUEST_DEBUG); */
                                                                                 
    do
    {
       seg1.bytes = 0;
       seg1.segs = 0;
                                                                                 
	/* process request */
	/* note that bytemax is exactly large enough to hold all of the
	 * data that I should find here
         */
	retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);
	                                                                                 
	if(!PINT_REQUEST_DONE(rs1))
	{
          fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
	   return -1;
	}
	
	if(retval >= 0)
	{
	    if(seg1.segs == 0)
	    {
		fprintf(stderr, "  IEEE! no results to report.\n");
		return -1;
	    }
	    for(i=0; i<seg1.segs; i++)
	    {
		if( (int)seg1.size_array[i] != segSize){
		    printf("Error: segment %d size is %d but should be %d\n",i,(int)seg1.size_array[i],segSize);
		    return -1;
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
	/*printf("**** first request done.\n");
	*/
    }
                                                                                
    do
    {
	seg1.bytes = 0;
	seg1.segs = 0;
                                                                                
	/* process request */
	/* note that bytemax is exactly large enough to hold all of the
	 * data that I should find here
	 */
	retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_SERVER);
                                                                                
	if(!PINT_REQUEST_DONE(rs2))
	{
	    fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
	    return -1;
	}
                                                                                
	if(retval >= 0)
	{
	    if(seg1.segs == 0)
	    {
		fprintf(stderr, "  IEEE! no results to report.\n");
		return -1;
	    }
	    for(i=0; i<seg1.segs; i++)
	    {
		if( (int)seg1.size_array[i] != segSize){
		    printf("Error: segment %d size is %d but should be %d\n",i,(int)seg1.size_array[i],segSize);
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
/*	printf("**** second request done.\n");

*/
    }
                                                                                
    do
    {
	seg1.bytes = 0;
	seg1.segs = 0;
                                                                                
	/* process request */
	/* note that bytemax is exactly large enough to hold all of the
	* data that I should find here
	*/
	retval = PINT_Process_request(rs3, NULL, &rf3, &seg1, PINT_SERVER);
                                                                                
	if(!PINT_REQUEST_DONE(rs3))
	{
	    fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
	    return -1;
	}
                                                                                
	if(retval >= 0)
	{
	    if(seg1.segs == 0)
	    {
		fprintf(stderr, "  IEEE! no results to report.\n");
		return -1;
	    }
	    for(i=0; i<seg1.segs; i++)
	    {
		if( (int)seg1.size_array[i] != segSize){
		    printf("Error: segment %d size is %d but should be %d\n",i,(int)seg1.size_array[i],segSize);
		    return -1;
		}
	    }
	}
                                                                                
    } while(!PINT_REQUEST_DONE(rs3) && retval >= 0);
                                                                                
    if(retval < 0)
    {
	fprintf(stderr, "Error: PINT_Process_request() failure.\n");
	return(-1);
    }
    if(PINT_REQUEST_DONE(rs3))
    {
/*      printf("**** third request done.\n");
*/
    }
                                                                                
    do
    {
	seg1.bytes = 0;
	seg1.segs = 0;
                                                                                
	/* process request */
	/* note that bytemax is exactly large enough to hold all of the
	 * data that I should find here
	 */
	retval = PINT_Process_request(rs4, NULL, &rf4, &seg1, PINT_SERVER);
                                                                                
	if(!PINT_REQUEST_DONE(rs4))
	{
	    fprintf(stderr, "IEEE! reporting more work to do when I should really be done...\n");
	    return -1;
	}
                                                                                
	if(retval >= 0)
	{
	    if(seg1.segs == 0)
	    {
		fprintf(stderr, "  IEEE! no results to report.\n");
		return -1;
	    }
	    for(i=0; i<seg1.segs; i++)
	    {
		if( (int)seg1.size_array[i] != segSize){
		    printf("Error: segment %d size is %d but should be %d\n",i,(int)seg1.size_array[i],segSize);
		    return -1;
		}
	    }
	}
                                                                                
    } while(!PINT_REQUEST_DONE(rs4) && retval >= 0);
                                                                                
    if(retval < 0)
    {
	fprintf(stderr, "Error: PINT_Process_request() failure.\n");
	return(-1);
    }
    if(PINT_REQUEST_DONE(rs4))
    {
/*	printf("**** fourth request done.\n");
*/
    }
                                                                                
    return 0;
}

/* Preconditions: None
 * Parameters: comm - special pts communicator, rank - the rank of the process,
 * buf - not used
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_request_contiguous(MPI_Comm * comm,
		     int rank,
		     char *buf,
		     void *rawparams)
{
    int ret = -1;

    if (rank == 0)
    {
	ret = test_request_cont();
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

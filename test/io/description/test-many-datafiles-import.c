/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>

#include <pvfs-distribution.h>
#include <pvfs2-request.h>
#include <pint-request.h>
#include <assert.h>

#include <simple-stripe.h>

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

int main(int argc, char **argv)
{
	int i,j;
	PINT_Request *file_req;
	PINT_Request *mem_req;
	PINT_Request_state *mem_state;
	PINT_Request_state *file_state;
	PINT_Request_state *file_state_server;
	PINT_Request_file_data rf1;
	PINT_Request_result seg1;
	PVFS_size total_bytes_client = 0;
	PVFS_size total_bytes_server = 0;

	/* PVFS_Process_request arguments */
	int retval;

	/* setup file datatype */
	file_req = PVFS_BYTE;

	/* setup mem datatype */
	PVFS_Request_contiguous(2251664, PVFS_BYTE, &mem_req);

	for(j=0; j<67; j++)
	{
	    mem_state = PINT_New_request_state(mem_req);
	    file_state = PINT_New_request_state(file_req);
	    file_state_server = PINT_New_request_state(file_req);

	    /* set up file data for request */
	    rf1.iod_num = j;
	    rf1.iod_count = 67;
	    rf1.fsize = 41943040;
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
	    

	    PINT_REQUEST_STATE_SET_TARGET(file_state, 41943040);
	    PINT_REQUEST_STATE_SET_FINAL(file_state, 41943040+PINT_REQUEST_TOTAL_BYTES(mem_req));
	    PINT_REQUEST_STATE_SET_TARGET(file_state_server, 41943040);
	    PINT_REQUEST_STATE_SET_FINAL(file_state_server, 41943040+PINT_REQUEST_TOTAL_BYTES(mem_req));


       /* Turn on debugging */
	    // gossip_enable_stderr();
	    // gossip_set_debug_mask(1,REQUEST_DEBUG); 

	    printf("\nCLIENT ************************************\n");
	    do
	    {
		    seg1.bytes = 0;
		    seg1.segs = 0;
		    total_bytes_client = 0;
		    total_bytes_server = 0;

		    /* process request */
		    retval = PINT_Process_request(file_state, mem_state, &rf1, &seg1, PINT_CLIENT);

		    if(retval >= 0)
		    {
			    printf("results of PINT_Process_request():\n");
			    printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
			    total_bytes_client += seg1.bytes;
			    for(i=0; i<seg1.segs; i++)
			    {
				    printf("  segment %d: offset: %d size: %d\n",
					    i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
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
		    printf("**** request done.\n");
	    }
	    
	    printf("\nSERVER ************************************\n");
	    do
	    {
		    seg1.bytes = 0;
		    seg1.segs = 0;

		    /* process request */
		    retval = PINT_Process_request(file_state_server, NULL, &rf1, &seg1, PINT_SERVER);

		    if(retval >= 0)
		    {
			    printf("results of PINT_Process_request():\n");
			    printf("%d segments with %lld bytes\n", seg1.segs, seg1.bytes);
			    total_bytes_server += seg1.bytes;
			    for(i=0; i<seg1.segs; i++)
			    {
				    printf("  segment %d: offset: %d size: %d\n",
					    i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
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
		    printf("**** request done.\n");
	    }

	    printf("total bytes processed on client side: %Ld\n", (long long)total_bytes_client);
	    printf("total bytes processed on server side: %Ld\n", (long long)total_bytes_server);

	    if(total_bytes_client == total_bytes_server)
	    {
		printf("SUCCESS.\n");
	    }
	    else
	    {
		printf("FAILURE!!!\n");
	    }
	}

	return 0;
}

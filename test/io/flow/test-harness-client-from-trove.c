/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is a simple test harness that operates on top of the flow
 * interface
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "gossip.h"
#include "flow.h"
#include "flowproto-support.h"
#include "trove.h"
#include "pvfs-distribution.h"
#include "pvfs-request.h"

#define ROOT_HANDLE_STRING "root_handle"
#define TROVE_TEST_BSTREAM 3

int TEST_SIZE=1024*1024*20; /* 20M */
static int block_on_flow(flow_descriptor* flow_d, FLOW_context_id
	flow_context);
static double Wtime(void);

char storage_space[] = "/tmp/trove-test-space";
char file_system[] = "fs-foo";
TROVE_handle requested_file_handle = 9999;

int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p);

int main(int argc, char **argv)	
{
	int ret = -1;
	int outcount = 0;
	void* mybuffer;
	bmi_addr_t server_addr;
	bmi_op_id_t op;
	bmi_error_code_t error_code;
	flow_descriptor* flow_d = NULL;
	int i= 0;
	bmi_size_t actual_size;
	double time1, time2;
	PINT_Request* req;
	PINT_Request_file_data file_data;
	char* method_name;
	TROVE_coll_id coll_id;
	TROVE_op_id t_op_id;
	TROVE_handle file_handle, parent_handle;
	int count;
	TROVE_ds_state state;
	PVFS_size tmp_size;
	bmi_context_id context;
	FLOW_context_id flow_context;
        TROVE_extent cur_extent;
        TROVE_handle_extent_array extent_array;
	
	/* memory buffer to xfer */
	mybuffer = (void*)malloc(TEST_SIZE);
	if(!mybuffer)
	{
		fprintf(stderr, "mem.\n");
		return(-1);
	}
	/* mark it so that we can check correctness */
	for(i=0; i<(TEST_SIZE/(sizeof(int))); i++)
	{
		((int*)mybuffer)[i] = i;
	}


	/*************************************************************/
	/* initialization stuff */

	/* set debugging level */
	gossip_enable_stderr();
	gossip_set_debug_mask(1, FLOW_PROTO_DEBUG);

	/* start up BMI */
	ret = BMI_initialize("bmi_tcp", NULL, 0);
	if(ret < 0)
	{
		fprintf(stderr, "BMI init failure.\n");
		return(-1);
	}

	ret = BMI_open_context(&context);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_open_context() failure.\n");
		return(-1);
	}

	/* start up trove */
	ret = trove_initialize(storage_space, 0, &method_name, 0);
	if (ret < 0) {
		fprintf(stderr, "initialize failed.\n");
		return -1;
	}


	/* initialize the flow interface */
	ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
	if(ret < 0)
	{
		fprintf(stderr, "flow init failure.\n");
		return(-1);
	}

	ret = PINT_flow_open_context(&flow_context);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_flow_open_context() failure.\n");
		return(-1);
	}

	/* send some random crap to the other side to start up communication*/
	ret = BMI_addr_lookup(&server_addr, "tcp://localhost:3335");
	if(ret < 0)
	{
		fprintf(stderr, "BMI lookup failure.\n");
		return(-1);
	}

	ret = BMI_post_sendunexpected(&op, server_addr, &mybuffer, 1,
		BMI_EXT_ALLOC, 0, NULL, context);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_post_sendunexpected failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of request */
		do
		{
			ret = BMI_test(op, &outcount, &error_code, &actual_size,
			NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_test");
			}
			return(-1);
		}
	}
	/******************************************************/
	/* mess around with trove to find a handle to do i/o on */

	ret = trove_collection_lookup(file_system, &coll_id, NULL, &t_op_id);
	if(ret < 0)
	{
		fprintf(stderr, "collection lookup failed\n");
		return(-1);
	}

	ret = path_lookup(coll_id, "/", &parent_handle);
	if (ret < 0 ) {
		return -1;
	}
	
	file_handle = 0;

        cur_extent.first = cur_extent.last = requested_file_handle;
        extent_array.extent_count = 1;
        extent_array.extent_array = &cur_extent;
	ret = trove_dspace_create(coll_id,
                                  &extent_array,
				  &file_handle,
				  TROVE_TEST_BSTREAM,
				  NULL,
				  TROVE_SYNC,
				  NULL,
				  &t_op_id);
	while (ret == 0) ret = trove_dspace_test(coll_id, t_op_id, &count, NULL, 
		NULL, &state);
	if (ret < 0) {
		fprintf(stderr, "HEY: dspace create failed, I hope because it already existed\n");
	}

	/* now let's write TEST_SIZE bytes into the data space so taht there
	 * is something to read out when we are ready 
	 */
	tmp_size = TEST_SIZE;
	ret = trove_bstream_write_at(coll_id, file_handle, mybuffer,
		&tmp_size, 0, 0, NULL, NULL, &t_op_id);
	while(ret == 0) ret = trove_dspace_test(coll_id, t_op_id, &count,
		NULL, NULL, &state);
	
	if(ret < 0) {
		fprintf(stderr, "bstream write failed.\n");
		return(-1);
	}
	if(tmp_size != TEST_SIZE)
	{
		fprintf(stderr, "Short read.\n");
		return(-1);
	}


	/******************************************************/
	/* setup request/dist stuff */

	/* request description */
	/* just want one contiguous region */
	ret = PVFS_Request_contiguous(TEST_SIZE, PVFS_BYTE, &req);
	if(ret < 0)
	{
		fprintf(stderr, "PVFS_Request_contiguous() failure.\n");
		return(-1);
	}

	/* file data */
	file_data.fsize = TEST_SIZE; 
	file_data.iod_num = 0;
	file_data.iod_count = 1;
	file_data.extend_flag = 0;
	file_data.dist = PVFS_Dist_create("default_dist");
	if(!file_data.dist)
	{
		fprintf(stderr, "Error: failed to create dist.\n");
		return(-1);
	}
	ret = PINT_Dist_lookup(file_data.dist);
	if(ret != 0)
	{
		fprintf(stderr, "Error: failed to lookup dist.\n");
		return(-1);
	}

	/******************************************************/
	/* setup communicaton stuff */

	/* create a flow descriptor */
	flow_d = PINT_flow_alloc();
	if(!flow_d)
	{
		fprintf(stderr, "mem.\n");
		return(-1);
	}

	flow_d->request = req;
	flow_d->file_data =  &file_data;
	flow_d->flags = 0;
	flow_d->tag = 0;
	flow_d->user_ptr = NULL;

	/* fill in flow details */
	flow_d->src.endpoint_id = TROVE_ENDPOINT;
	flow_d->src.u.trove.handle = file_handle;
	flow_d->src.u.trove.coll_id = coll_id;


	flow_d->dest.endpoint_id = BMI_ENDPOINT;
	flow_d->dest.u.bmi.address = server_addr;

	/***************************************************
	 * test memory to bmi (analogous to client side write)
	 */

	time1 = Wtime();
	ret = block_on_flow(flow_d, flow_context);
	if(ret < 0)
	{
		return(-1);
	}
	time2 = Wtime();

	/*******************************************************/
	/* final cleanup and output */

	printf("Client bw (send): %f MB/sec\n",
		((TEST_SIZE)/((time2-time1)*1000000.0)));

	PINT_flow_free(flow_d);

	/* shut down flow interface */
	PINT_flow_close_context(flow_context);
	ret = PINT_flow_finalize();
	if(ret < 0)
	{
		fprintf(stderr, "flow finalize failure.\n");
		return(-1);
	}

	/* shut down BMI */
	BMI_close_context(context);
	BMI_finalize();

	trove_finalize();

	free(mybuffer);

	gossip_disable();
	return(0);
}


static int block_on_flow(flow_descriptor* flow_d, FLOW_context_id
	flow_context)
{
	int ret = -1;
	int count = 0;
	int index = 5;

	ret = PINT_flow_post(flow_d, flow_context);
	if(ret == 1)
	{
		return(0);
	}
	if(ret < 0)
	{
		fprintf(stderr, "flow post failure.\n");
		return(ret);
	}

	do
	{
		ret = PINT_flow_testsome(1, &flow_d, &count, &index, 10,
			flow_context);
	}while(ret == 0 && count == 0);
	if(ret < 0)
	{
		fprintf(stderr, "flow_test failure.\n");
		return(ret);
	}
	if(flow_d->state != FLOW_COMPLETE)
	{
		fprintf(stderr, "flow finished in error state: %d\n", flow_d->state);
		return(-1);
	}
	if(index != 0)
	{
		fprintf(stderr, "bad index.\n");
		return(-1);
	}
	return(0);
}

static double Wtime(void)
{	
	struct timeval t;

	gettimeofday(&t, NULL);
	return((double)t.tv_sec + (double)t.tv_usec / 1000000);
}
	
int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p)
{
    int ret, count;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_op_id op_id;
    TROVE_handle handle;

    char root_handle_string[] = ROOT_HANDLE_STRING;

    /* get root */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &handle;
    val.buffer_sz = sizeof(handle);
    ret = trove_collection_geteattr(coll_id, &key, &val, 0, NULL, &op_id);
    while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
        fprintf(stderr, "collection geteattr (for root handle) failed.\n");
        return -1;
    }                                                           
                                                                
    /* TODO: handle more than just a root handle! */

    *out_handle_p = handle;
    return 0;
}


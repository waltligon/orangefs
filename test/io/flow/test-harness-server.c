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
#include <gossip.h>
#include <flow.h>
#include <pvfs-distribution.h>
#include <pvfs-request.h>


int TEST_SIZE=1024*1024*20; /* 10M */
static int block_on_flow(flow_descriptor* flow_d, FLOW_context_id
	flow_context);
static double Wtime(void);

int main(int argc, char **argv)	
{
	int ret = -1;
	int outcount = 0;
	struct BMI_unexpected_info request_info;
	void* mybuffer;
	flow_descriptor* flow_d = NULL;
	double time1, time2;
	int i;
	PINT_Request* req;
	PINT_Request_file_data file_data;
	bmi_context_id context;
	FLOW_context_id flow_context;

	/*************************************************************/
	/* initialization stuff */

	/* set debugging level */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, FLOW_PROTO_DEBUG);

	/* start up BMI */
	ret = BMI_initialize("bmi_tcp", "tcp://NULL:3335", BMI_INIT_SERVER);
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

	/* wait for an initial communication via BMI */
	/* we don't give a crap about that message except that it tells us
	 * where to find the client 
	 */
	do
	{
		ret = BMI_testunexpected(1, &outcount, &request_info, 10);
	}while(ret == 0 && outcount == 0);
	if(ret < 0 || request_info.error_code != 0)
	{
		fprintf(stderr, "waitunexpected failure.\n");
		return(-1);
	}

	free(request_info.buffer);

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

	/* memory buffer to xfer */
	mybuffer = (void*)malloc(TEST_SIZE);
	if(!mybuffer)
	{
		fprintf(stderr, "mem.\n");
		return(-1);
	}
	memset(mybuffer, 0, TEST_SIZE);

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
	flow_d->dest.endpoint_id = MEM_ENDPOINT;
	flow_d->dest.u.mem.size = TEST_SIZE;
	flow_d->dest.u.mem.buffer = mybuffer;
	flow_d->src.endpoint_id = BMI_ENDPOINT;
	flow_d->src.u.bmi.address = request_info.addr;

	/***************************************************
	 * test bmi to memory (analogous to a client side read)
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

	if(time2 == time1)
	{
		printf("No time elapsed?\n");
	}
	else
	{
		printf("Server bw (recv): %f MB/sec\n",
			((TEST_SIZE)/((time2-time1)*1000000.0)));
	}

	/* verify memory */
	for(i=0; i<(TEST_SIZE/(sizeof(int))); i++)
	{
		if(((int*)mybuffer)[i] != i)
		{
			fprintf(stderr, "Failed Verification!!! (step 1)\n");
		}
	}

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

	free(mybuffer);

	gossip_disable();
	return(0);
}

static int block_on_flow(flow_descriptor* flow_d, FLOW_context_id
	flow_context)
{
	int ret = -1;
	int count = 0;

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
		ret = PINT_flow_test(flow_d, &count, 10, flow_context);
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
	return(0);
}

static double Wtime(void)
{	
	struct timeval t;

	gettimeofday(&t, NULL);
	return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}


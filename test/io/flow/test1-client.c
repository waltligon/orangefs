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
#include "pvfs-distribution.h"
#include "pvfs2-request.h"
#include "thread-mgr.h"

int TEST_SIZE = 1024 * 1024 * 20;	/* 20M */
static int block_on_flow(
    flow_descriptor * flow_d);
static double Wtime(
    void);

char storage_space[] = "/tmp/pvfs2-test-space";

int main(
    int argc,
    char **argv)
{
    int ret = -1;
    int outcount = 0;
    void *mybuffer;
    PVFS_BMI_addr_t server_addr;
    bmi_op_id_t op;
    bmi_error_code_t error_code;
    flow_descriptor *flow_d = NULL;
    int i = 0;
    bmi_size_t actual_size;
    double time1, time2;
    PINT_Request *req;
    bmi_context_id context;

	/*************************************************************/
    /* initialization stuff */

    /* set debugging level */
    gossip_enable_stderr();
    gossip_set_debug_mask(0, GOSSIP_FLOW_PROTO_DEBUG | GOSSIP_BMI_DEBUG_TCP);

    /* start up BMI */
    ret = BMI_initialize("bmi_tcp", NULL, 0);
    if (ret < 0)
    {
	fprintf(stderr, "BMI init failure.\n");
	return (-1);
    }

    ret = BMI_open_context(&context);
    if (ret < 0)
    {
	fprintf(stderr, "BMI_open_context() failure.\n");
	return (-1);
    }

    /* initialize the flow interface */
    ret = PINT_flow_initialize("flowproto_multiqueue", 0);
    if (ret < 0)
    {
	fprintf(stderr, "flow init failure.\n");
	return (-1);
    }

    /* send some random crap to the other side to start up communication */
    ret = BMI_addr_lookup(&server_addr, "tcp://localhost:3335");
    if (ret < 0)
    {
	fprintf(stderr, "BMI lookup failure.\n");
	return (-1);
    }

    ret = BMI_post_sendunexpected(&op, server_addr, &mybuffer, 1,
				  BMI_EXT_ALLOC, 0, NULL, context);
    if (ret < 0)
    {
	fprintf(stderr, "BMI_post_sendunexpected failure.\n");
	return (-1);
    }
    if (ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of request */
	do
	{
	    ret = BMI_test(op, &outcount, &error_code, &actual_size,
			   NULL, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Request send failed.\n");
	    if (ret < 0)
	    {
		errno = -ret;
		perror("BMI_test");
	    }
	    return (-1);
	}
    }

	/******************************************************/
    /* setup request/dist stuff */

    /* request description */
    /* just want one contiguous region */
    ret = PVFS_Request_contiguous(TEST_SIZE, PVFS_BYTE, &req);
    if (ret < 0)
    {
	fprintf(stderr, "PVFS_Request_contiguous() failure.\n");
	return (-1);
    }


	/******************************************************/
    /* setup communicaton stuff */

    /* memory buffer to xfer */
    mybuffer = (void *) malloc(TEST_SIZE);
    if (!mybuffer)
    {
	fprintf(stderr, "mem.\n");
	return (-1);
    }
    /* mark it so that we can check correctness */
    for (i = 0; i < (TEST_SIZE / (sizeof(int))); i++)
    {
	((int *) mybuffer)[i] = i;
    }

    /* create a flow descriptor */
    flow_d = PINT_flow_alloc();
    if (!flow_d)
    {
	fprintf(stderr, "mem.\n");
	return (-1);
    }

    /* file data */
    flow_d->file_data.fsize = TEST_SIZE;
    flow_d->file_data.server_nr = 0;
    flow_d->file_data.server_ct = 1;
    flow_d->file_data.extend_flag = 0;
    flow_d->file_data.dist = PVFS_Dist_create("default_dist");
    if (!flow_d->file_data.dist)
    {
	fprintf(stderr, "Error: failed to create dist.\n");
	return (-1);
    }
    ret = PINT_Dist_lookup(flow_d->file_data.dist);
    if (ret != 0)
    {
	fprintf(stderr, "Error: failed to lookup dist.\n");
	return (-1);
    }


    flow_d->file_req = req;
    flow_d->tag = 0;
    flow_d->user_ptr = NULL;
    flow_d->aggregate_size = TEST_SIZE;

    /* fill in flow details */
    flow_d->src.endpoint_id = MEM_ENDPOINT;
    flow_d->src.u.mem.buffer = mybuffer;
    flow_d->dest.endpoint_id = BMI_ENDPOINT;
    flow_d->dest.u.bmi.address = server_addr;

	/***************************************************
	 * test memory to bmi (analogous to client side write)
	 */

    time1 = Wtime();
    ret = block_on_flow(flow_d);
    if (ret < 0)
    {
	return (-1);
    }
    time2 = Wtime();

	/*******************************************************/
    /* final cleanup and output */

#if 0
    printf("Client bw (send): %f MB/sec\n",
	   ((TEST_SIZE) / ((time2 - time1) * 1000000.0)));
#endif

    PINT_flow_free(flow_d);

    /* shut down flow interface */
    ret = PINT_flow_finalize();
    if (ret < 0)
    {
	fprintf(stderr, "flow finalize failure.\n");
	return (-1);
    }

    /* shut down BMI */
    BMI_close_context(context);
    BMI_finalize();

    free(mybuffer);

    gossip_disable();
    return (0);
}

static int done_flag = 0;
static void callback_fn(flow_descriptor* flow_d)
{
    done_flag = 1;
    return;
}

static int block_on_flow(
    flow_descriptor * flow_d)
{
    int ret = -1;

    flow_d->callback = callback_fn;
    ret = PINT_flow_post(flow_d);
    if (ret == 1)
    {
	return (0);
    }
    if (ret < 0)
    {
	fprintf(stderr, "flow post failure.\n");
	return (ret);
    }

    while(!done_flag)
    {
	PINT_thread_mgr_bmi_push(10);
    }

    if (flow_d->state != FLOW_COMPLETE)
    {
	fprintf(stderr, "flow finished in error state: %d\n", flow_d->state);
	return (-1);
    }
    return (0);
}

static double Wtime(
    void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return ((double) t.tv_sec + (double) t.tv_usec / 1000000);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

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
#include <flowproto-support.h>
#include <pvfs-distribution.h>
#include <pvfs-request.h>

#include <wire-harness.h>

static int block_on_flow(flow_descriptor* flow_d);
static double Wtime(void);

int main(int argc, char **argv)	
{
	int ret = -1;
	int outcount = 0;
	bmi_addr_t server_addr;
	bmi_op_id_t op;
	bmi_error_code_t error_code;
	struct wire_harness_req* req = NULL;
	struct wire_harness_ack* ack = NULL;
	int total_req_size = 0;
	PVFS_size actual_size = 0;
	PINT_Request* io_req = NULL;
	PVFS_Dist* io_dist = NULL;
	PINT_Request* encode_io_req = NULL;
	PVFS_Dist* encode_io_dist = NULL;
	int commit_index = 0;
	PINT_Request_file_data file_data;
	flow_descriptor* flow_d = NULL;
	double time1 = 0, time2 = 0;
	PVFS_size io_size = 10*1024*1024; /* 10 M transfer */

	/*************************************************************/
	/* initialization stuff */

	/* set debugging level */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, FLOW_PROTO_DEBUG|BMI_DEBUG_TCP);

	/* start up BMI */
	ret = BMI_initialize("bmi_tcp", NULL, 0);
	if(ret < 0)
	{
		fprintf(stderr, "BMI init failure.\n");
		return(-1);
	}

	/* initialize the flow interface */
	ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
	if(ret < 0)
	{
		fprintf(stderr, "flow init failure.\n");
		return(-1);
	}

	/* resolve the server address */
	ret = BMI_addr_lookup(&server_addr, "tcp://localhost:3335");
	if(ret < 0)
	{
		fprintf(stderr, "BMI lookup failure.\n");
		return(-1);
	}

	/* setup the io description */
	ret = PVFS_Request_contiguous(io_size, PVFS_BYTE, &io_req);
	if(ret < 0)
	{
		fprintf(stderr, "PVFS_Request_contiguous() failure.\n");
		return(-1);
	}

	/* setup the distribution */
	io_dist = PVFS_Dist_create("default_dist");
	if(!io_dist)
	{
		fprintf(stderr, "Error: failed to create dist.\n");
		return(-1);
	}
	ret = PINT_Dist_lookup(io_dist);
	if(ret != 0)
	{
		fprintf(stderr, "Error: failed to lookup dist.\n");
		return(-1);
	}

	if (argc < 2) {
	    fprintf(stderr, "usage: foo <handle>\n");
	    return -1;
	}

	

	/* build the request packet */
	/* is this stuff right?  I'm not sure about the dist stuff... */
	total_req_size = sizeof(struct wire_harness_req) +
		PINT_REQUEST_PACK_SIZE(io_req) +
		sizeof(struct PVFS_Dist) + io_dist->name_size +
		io_dist->param_size;

	req = (struct wire_harness_req*)BMI_memalloc(server_addr,
		total_req_size, BMI_SEND_BUFFER);
	if(!req)
	{
		fprintf(stderr, "BMI_memalloc() failure.\n");
		return(-1);
	}
	memset(req, 0, total_req_size);

	req->io_req_size = PINT_REQUEST_PACK_SIZE(io_req);
	req->dist_size = sizeof(struct PVFS_Dist) + io_dist->name_size + 
		io_dist->param_size;
	
	/* pack the io description */
	encode_io_req = 
		(PINT_Request*)((char*)req + sizeof(struct wire_harness_req));
	
	commit_index = 0;
	ret = PINT_Request_commit(encode_io_req, io_req, &commit_index);
	if(ret < 0)
	{
		fprintf(stderr, "Error: request commit failure.\n");
		return(-1);
	}
	ret = PINT_Request_encode(encode_io_req);
	if(ret < 0)
	{
		fprintf(stderr, "Error: request encode failure.\n");
		return(-1);
	}

	/* pack the distribution */
	encode_io_dist = (PVFS_Dist*)((char*)encode_io_req + req->io_req_size);
	PINT_Dist_encode(encode_io_dist, io_dist);

	/* TODO: fill this in with real values about what we want to access */
	req->fs_id = 0;
	req->handle = (PVFS_handle) atoi(argv[1]);
	req->op = WIRE_HARNESS_READ;

	printf("** sending req: fsid (ignored) = %d, handle = %d, op = %d, io_r_sz = %d, dist_sz = %d\n",
	       (int) req->fs_id, (int) req->handle, req->op, (int) req->io_req_size, (int) req->dist_size);

	/* send request */
	ret = BMI_post_sendunexpected(&op, server_addr, req, total_req_size,
		BMI_PRE_ALLOC, 0, NULL);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_post_sendunexpected failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		/* check for completion of request */
		do
		{
			ret = BMI_wait(op, &outcount, &error_code, &actual_size, NULL);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_wait");
			}
			return(-1);
		}
	}

	/* get a response back */
	ack = BMI_memalloc(server_addr, sizeof(struct wire_harness_ack),
		BMI_RECV_BUFFER);
	if(!ack)
	{
		fprintf(stderr, "Error: BMI malloc failure.\n");
		return(-1);
	}
	ret = BMI_post_recv(&op, server_addr, ack, sizeof(struct
		wire_harness_ack), &actual_size, BMI_EXT_ALLOC, 0, NULL);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_post_recv failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		/* check for completion of request */
		do
		{
			ret = BMI_wait(op, &outcount, &error_code, &actual_size, NULL);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_wait");
			}
			return(-1);
		}
	}
	if(actual_size < sizeof(struct wire_harness_ack))
	{
		fprintf(stderr, "Error: short ack.\n");
		return(-1);
	}

	if(ack->error_code != 0)
	{
		fprintf(stderr, "Error: server gave negative ack.\n");
		return(-1);
	}

	printf("** received ack: handle = %d, err = %d, dspace_sz = %d\n", (int) ack->handle, ack->error_code, ack->dspace_size);

	/* setup flow */
	file_data.fsize = ack->dspace_size;
	file_data.iod_num = 0;
	file_data.iod_count = 1;
	file_data.extend_flag = 0;
	file_data.dist = io_dist;

	flow_d = PINT_flow_alloc();
	if(!flow_d)
	{
		fprintf(stderr, "mem.\n");
		return(-1);
	}

	flow_d->request = io_req;
	flow_d->file_data =  &file_data;
	flow_d->flags = 0;
	flow_d->tag = 0;
	flow_d->user_ptr = NULL;

	/* memory buffer */
	flow_d->src.u.mem.buffer = malloc(io_size);
	if(!flow_d->src.u.mem.buffer)
	{
		fprintf(stderr, "Error: malloc.\n");
		return(-1);
	}

	if (req->op == WIRE_HARNESS_WRITE) {
	    flow_d->src.endpoint_id = MEM_ENDPOINT;
	    flow_d->src.u.mem.size = io_size;
	    
	    /* server endpoint */
	    flow_d->dest.endpoint_id = BMI_ENDPOINT;
	    flow_d->dest.u.bmi.address = server_addr;
	}
	else
	{
	    /* server endpoint */
	    flow_d->src.endpoint_id = BMI_ENDPOINT;
	    flow_d->src.u.bmi.address = server_addr;

	    flow_d->dest.endpoint_id = MEM_ENDPOINT;
	    flow_d->dest.u.mem.size = io_size;
	}

	/* run the flow */
	time1 = Wtime();
	ret = block_on_flow(flow_d);
	if(ret < 0)
	{
		return(-1);
	}
	time2 = Wtime();

	printf("Client bw: %f MB/sec\n",
		((io_size)/((time2-time1)*1000000.0)));

	/* release buffers and such */
	free(flow_d->src.u.mem.buffer);
	PINT_flow_free(flow_d);
	BMI_memfree(server_addr, ack, sizeof(struct wire_harness_ack), 
		BMI_RECV_BUFFER);
	BMI_memfree(server_addr, req, total_req_size, BMI_SEND_BUFFER);

	/* shut down flow interface */
	ret = PINT_flow_finalize();
	if(ret < 0)
	{
		fprintf(stderr, "flow finalize failure.\n");
		return(-1);
	}

	/* shut down BMI */
	BMI_finalize();

	gossip_disable();
	return(0);
}


static int block_on_flow(flow_descriptor* flow_d)
{
	int ret = -1;
	int count = 0;
	int index = 5;

	ret = PINT_flow_post(flow_d);
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
		ret = PINT_flow_waitsome(1, &flow_d, &count, &index);
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


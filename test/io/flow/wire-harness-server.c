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
#include <wire-harness.h>
#include <trove.h>

static int block_on_flow(flow_descriptor* flow_d);
static double Wtime(void);

char storage_space[] = "/tmp/storage-space-foo";

int main(int argc, char **argv)	
{
	int ret = -1;
	int outcount = 0, count;
	struct unexpected_info request_info;
	struct wire_harness_req* req;
	struct wire_harness_ack ack;
	PINT_Request* io_req;
	PVFS_Dist* io_dist;
	bmi_op_id_t op;
	PVFS_size actual_size;
	bmi_error_code_t error_code;
	TROVE_ds_attributes_s s_attr;
	PINT_Request_file_data file_data;
	char *method_name;
	flow_descriptor* flow_d = NULL;
	double time1 = 0, time2 = 0;
	TROVE_coll_id coll_id;
	TROVE_op_id op_id;
	TROVE_ds_state state;
	TROVE_handle file_handle;


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

	/* initialize the flow interface */
	ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
	if(ret < 0)
	{
		fprintf(stderr, "flow init failure.\n");
		return(-1);
	}

	ret = trove_initialize(storage_space, 0, &method_name, 0);
	if (ret < 0) {
	    fprintf(stderr, "initialize failed.\n");
	    return -1;
	}

	while(1)
	{
		/* wait for a request via BMI */
		do
		{
			ret = BMI_waitunexpected(1, &outcount, &request_info);
		}while(ret == 0 && outcount == 0);
		if(ret < 0 || request_info.error_code != 0)
		{
			fprintf(stderr, "waitunexpected failure.\n");
			return(-1);
		}

		/* look at the request */
		req = (struct wire_harness_req*)request_info.buffer;
		
		/* sanity check sizes and stuff */
		if(request_info.size != (sizeof(struct wire_harness_req) +
			req->io_req_size + req->dist_size))
		{
			fprintf(stderr, "Badly formatted request received.\n");
			return(-1);
		}

		printf("** received req: fsid (ignored) = %d, handle = %d, op = %d, io_r_sz = %d, dist_sz = %d\n",
		       (int) req->fs_id, (int) req->handle, req->op, (int) req->io_req_size, (int) req->dist_size);

		/* decode io description */
		io_req = (PINT_Request*)((char*)req + sizeof(struct wire_harness_req));
		io_dist = (PVFS_Dist*)((char*)io_req + req->io_req_size);
		ret = PINT_Request_decode(io_req);
		if(ret < 0)
		{
			fprintf(stderr, "Error: io request decode failure.\n");
			return(-1);
		}

		/* decode distribution info */
		PINT_Dist_decode(io_dist, NULL);

		
		/* TODO: talk to trove; get file size, make sure file is there,
		 * etc.
		 *
		 * req->fs_id, handle, op define what to do; client supplies these.
		 *
		 * size of dspace goes in the ack back to the client
		 */

		/* NOTE: ignoring the fs_id for now...we really need a string. */
		/* in the long run i think that the server will be responsible
		 * for looking up the file systems that it is storing data for,
		 * and keeping the coll_ids around (as fs_ids, that it passes out).
		 * 
		 * it's important that the collection be looked up once before the
		 * coll_id is used, at least at the moment.  i need to fix that or
		 * make it part of the semantics, one of the two.  i think fixing it
		 * is the better of those ideas :).
		 */
		ret = trove_collection_lookup("fs-foo", &coll_id, NULL, &op_id);
		if (ret < 0) {
		    fprintf(stderr, "collection lookup failed.\n");
		    return -1;
		}
		file_handle = req->handle;

		ret = trove_dspace_getattr(coll_id, file_handle, &s_attr, NULL, &op_id);
		while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
		if (ret < 0 && req->op == WIRE_HARNESS_READ) {
		    ack.error_code = ENOENT;
		}
		else ack.error_code = 0;

		/* send an ack back (error code and data size) */
		/* TODO: for now just guessing at size and error code */
		ack.dspace_size = s_attr.b_size;
		ack.handle = file_handle;

		printf("** sending ack: handle = %d, err = %d, dspace_sz = %d\n", (int) ack.handle, ack.error_code, ack.dspace_size);


		ret = BMI_post_send(&op, request_info.addr, &ack, sizeof(ack),
			BMI_EXT_ALLOC, 0, NULL);
		if(ret < 0)
		{
			fprintf(stderr, "BMI_post_send failure.\n");
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
				fprintf(stderr, "ack send failed.\n");
				if(ret<0)
				{
					errno = -ret;
					perror("BMI_wait");
				}
				return(-1);
			}
		}

		/* setup flow */
		file_data.fsize = ack.dspace_size;
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

		/* endpoints */
		if (req->op == WIRE_HARNESS_READ)
		{
		    flow_d->src.endpoint_id = TROVE_ENDPOINT;
		    flow_d->src.u.trove.handle = req->handle;
		    flow_d->src.u.trove.coll_id = coll_id;
		    flow_d->dest.endpoint_id = BMI_ENDPOINT;
		    flow_d->dest.u.bmi.address = request_info.addr;
		}
		else
		{
		    flow_d->src.endpoint_id = BMI_ENDPOINT;
		    flow_d->src.u.bmi.address = request_info.addr;
		    flow_d->dest.endpoint_id = TROVE_ENDPOINT;
		    flow_d->dest.u.trove.handle = req->handle;
		    flow_d->dest.u.trove.coll_id = coll_id;
		}
		/* run the flow */
		time1 = Wtime();
		ret = block_on_flow(flow_d);
		if(ret < 0)
		{
			return(-1);
		}
		time2 = Wtime();

		printf("Server bw: %f MB/sec\n",
			((io_req->aggregate_size)/((time2-time1)*1000000.0)));

		/* discard the buffer we got for the incoming request */
		free(request_info.buffer);

		/* discard the flow descriptor */
		PINT_flow_free(flow_d);

	}

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
		ret = PINT_flow_wait(flow_d, &count);
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


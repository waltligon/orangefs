/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* DON'T RUN THIS CODE- it is just an example.  None of the required
 * functionality is implemented yet.  It also does not perform proper
 * error handling. 
 */

/* this is an example client application that uses the job interface */

#include <stdio.h>
#include <job.h>
#include <job-client-helper.h>
#include <errno.h>
#include <gossip.h>

/* some fake items to send around */
struct request_foo
{
	int x;
};
struct ack_foo
{
	int x;
};



int main(int argc, char **argv)	
{

	int ret = -1;
	struct ack_foo* ack = NULL;
	job_status_s status1;
	struct BMI_unexpected_info req_info;
	job_id_t job_id;
	int outcount;
#if 0
	/* used in undef'd flow code below */
	struct flow_endpoint* src;
	struct flow_endpoint* dest;
	struct flow_io_desc* io_desc = NULL;
	job_id_t job_id1 = 0;
	int index = 5;
#endif

	/* set debugging level */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, 0);


	/* start the BMI interface */
	ret = BMI_initialize("bmi_tcp", "tcp://NULL:3414", BMI_INIT_SERVER);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_initialize failure.\n");
		return(-1);
	}

	/* start the flow interface */
	ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
	if(ret < 0)
	{
		fprintf(stderr, "flow_init failure.\n");
		return(-1);
	}

	/* start the job interface */
	ret = job_initialize(0);
	if(ret < 0)
	{
		fprintf(stderr, "job_initialize failure.\n");
		return(-1);
	}


	/* post a job for unexpected receive */
	ret = job_bmi_unexp(&req_info, NULL, &status1, &job_id, 0);
	if(ret < 0)
	{
		fprintf(stderr, "job_bmi_unexp() failure.\n");
		return(-1);
	}
	if(ret != 1)
	{
		/* exercise testworld() interface, block indefinitely */
		outcount = 1;
		ret = job_testworld(&job_id, &outcount, NULL, &status1, -1);
		if(ret < 0 || outcount == 0)
		{	
			fprintf(stderr, "job_testworld() failure.\n");
			return(-1);
		}
	}

	/* check status */
	if(status1.error_code != 0)
	{
		fprintf(stderr, "Bad status in unexp recv.\n");
		return(-1);
	}

	/* allocate a buffer for the ack */
	ack = BMI_memalloc(req_info.addr, sizeof(struct ack_foo),
		BMI_SEND_BUFFER);
	if(!ack)
	{
		fprintf(stderr, "BMI_memalloc failure.\n");
		return(-1);
	}

	/* post a blocking BMI send job */
	ret = job_bmi_send_blocking(req_info.addr, ack, 
		sizeof(struct ack_foo), 0, BMI_PRE_ALLOC, 0, &status1);
	if(ret < 0)
	{
		fprintf(stderr, "job_bmi_send_blocking failure.\n");
		return(-1);
	}

	/* check status */
	if(status1.error_code != 0)
	{
		fprintf(stderr, "job failure.\n");
		return(-1);
	}
#if 0
	/* send the same thing using a flow */
	src = PINT_endpoint_alloc();
	dest = PINT_endpoint_alloc();
	io_desc = (struct flow_io_desc*)malloc(sizeof(struct flow_io_desc));
	if(!src || !dest || !io_desc)
	{
		fprintf(stderr, "Failed to alloc endpoints.\n");
		return(-1);
	}
	dest->endpoint_id = BMI_ENDPOINT;
	dest->u.bmi.address = req_info.addr;
	src->endpoint_id = MEM_ENDPOINT;
	src->u.mem.size = sizeof(struct ack_foo);
	src->u.mem.buffer = ack;
	io_desc->offset = 0;
	io_desc->size = sizeof(struct ack_foo);

	ret = job_flow(0, 0, src, dest, io_desc, NULL, 0, NULL, &status1,
		&job_id1);
	if(ret < 0)
	{
		fprintf(stderr, "job_flow() failure.\n");
		return(-1);
	}
	if(ret != 1)
	{
		/* wait until job finishes */
		do
		{
			outcount = 1;
			ret = job_waitsome(&job_id1, &outcount, &index, NULL, &status1);
		} while(ret == 0 && outcount == 0);
		if(ret < 0)
		{
			fprintf(stderr, "job_wait() failure.\n");
			errno = -ret;
			perror("foo");
			return(-1);
		}
	}

	/* check index */
	if(index != 0)
	{
		fprintf(stderr, "Bad index.\n");
		return(-1);
	}

	/* check status */
	if(status1.error_code != 0)
	{
		fprintf(stderr, "Bad status in flow.\n");
		return(-1);
	}
#endif
	BMI_memfree(req_info.addr, ack, sizeof(struct ack_foo), BMI_RECV_BUFFER);
	free(req_info.buffer);

	/* shut down the interfaces */
	job_finalize();
	PINT_flow_finalize();
	BMI_finalize();

	return(0);
}

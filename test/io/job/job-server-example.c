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
	job_id_t tmp_id;

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
#if 0
		/* exercise testworld() interface, block indefinitely */
		outcount = 1;
		ret = job_testworld(&job_id, &outcount, NULL, &status1, -1);
		if(ret < 0 || outcount == 0)
		{	
			fprintf(stderr, "job_testworld() failure.\n");
			return(-1);
		}

		/* alternatively, try out the testsome interface */
		outcount = 1;
		ret = job_testsome(&job_id, &outcount, &foo, NULL, &status1, -1);
		if(ret < 0 || outcount == 0)
		{
			fprintf(stderr, "job_testsome() failure.\n");
			return(-1);
		}
#else

		/* ... or maybe even give job_test() a whirl */
		ret = job_test(job_id, &outcount, NULL, &status1, 5000);
		if(ret < 0 || outcount == 0)
		{
			fprintf(stderr, "job_test() failure.\n");
			return(-1);
		}

#endif
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

	/* send a message */
	ret = job_bmi_send(req_info.addr, ack, sizeof(struct ack_foo),
		0, BMI_PRE_ALLOC, 0, NULL, &status1, &tmp_id);
	if(ret < 0)
	{
		fprintf(stderr, "job_bmi_send() failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		int count = 0;
		ret = job_test(tmp_id, &count, NULL, &status1, -1);
		if(ret < 0)
		{
			fprintf(stderr, "job_test() failure.\n");
			return(-1);
		}
	}


	/* check status */
	if(status1.error_code != 0)
	{
		fprintf(stderr, "job failure.\n");
		return(-1);
	}

	BMI_memfree(req_info.addr, ack, sizeof(struct ack_foo), BMI_RECV_BUFFER);
	free(req_info.buffer);

	/* shut down the interfaces */
	job_finalize();
	PINT_flow_finalize();
	BMI_finalize();

	return(0);
}

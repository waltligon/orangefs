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
	struct request_foo* req = NULL;
	struct ack_foo* ack = NULL;
	bmi_addr_t server_addr;
	job_status_s status1;
	job_id_t tmp_id;

	/* set debugging level */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, 0);

	/* start the BMI interface */
	ret = BMI_initialize("bmi_tcp", NULL, 0);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_initialize failure.\n");
		return(-1);
	}

	/* start the flow interface */
	ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
	if(ret < 0)
	{
		fprintf(stderr, "flow init failure.\n");
		return(-1);
	}

	/* start the job interface */
	ret = job_initialize(0);
	if(ret < 0)
	{
		fprintf(stderr, "job_initialize failure.\n");
		return(-1);
	}

	/* lookup the server to get a BMI style address for it */
	ret = BMI_addr_lookup(&server_addr, "tcp://localhost:3414");
	if(ret < 0)
	{
		fprintf(stderr, "BMI_addr_lookup failure.\n");
		return(-1);
	}

	/* allocate some buffers for the req and ack */
	req = BMI_memalloc(server_addr, sizeof(struct request_foo),
		BMI_SEND_BUFFER);
	ack = BMI_memalloc(server_addr, sizeof(struct ack_foo),
		BMI_RECV_BUFFER);
	if(!ack || ! req)
	{
		fprintf(stderr, "BMI_memalloc failure.\n");
		return(-1);
	}

	/* send a message */
	ret = job_bmi_send(server_addr, req, sizeof(struct request_foo),
		0, BMI_PRE_ALLOC, 1, NULL, &status1, &tmp_id);
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

	/* receive a message */
	ret = job_bmi_recv(server_addr, ack, sizeof(struct ack_foo),
		0, BMI_PRE_ALLOC, NULL, &status1, &tmp_id);
	if(ret < 0)
	{
		fprintf(stderr, "job_bmi_recv() failure.\n");
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

	/* check the size */
	if(status1.actual_size != sizeof(struct ack_foo))
	{
		fprintf(stderr, "short recv.\n");
		return(-1);
	}

	/* free memory buffers */
	BMI_memfree(server_addr, req, sizeof(struct request_foo), 
		BMI_SEND_BUFFER);
	BMI_memfree(server_addr, ack, sizeof(struct ack_foo), 
		BMI_RECV_BUFFER);

	/* shut down the interfaces */
	job_finalize();
	PINT_flow_finalize();
	BMI_finalize();

	return(0);
}

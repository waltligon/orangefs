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

/* some fake items to send around */
struct request_foo
{
	int x;
};

int main(int argc, char **argv)	
{

	int ret = -1;
	int num_servers = 4;
	struct request_foo req_array[4];
	char* server_strings[4] = { "foo", "bar", "blah", "yoohoo"};
	bmi_addr_t server_addr_array[4];
	job_status_s status_array[4];
	job_id_t id_array[4];
	job_id_t id;
	job_status_s status;
	int i;
	int num_pending;

	/* start the BMI interface */
	ret = BMI_initialize("/tmp/foo.so", NULL, 0);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_initialize failure.\n");
		return(-1);
	}

	/* start the flow interface */
	ret = PINT_flow_initialize("default_flowproto", 0);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_flow_initialize failure.\n");
		return(-1);
	}

	/* start the job interface */
	ret = job_initialize(0);
	if(ret < 0)
	{
		fprintf(stderr, "job_initialize failure.\n");
		return(-1);
	}

	for(i=0; i<num_servers; i++)
	{
		/* lookup the server to get a BMI style address for it */
		ret = BMI_addr_lookup(&(server_addr_array[i]), server_strings[i]);
		if(ret < 0)
		{
			fprintf(stderr, "BMI_addr_lookup failure.\n");
			return(-1);
		}
	}

	/* post each send job */
	for(i=0; i<num_servers; i++)
	{
		ret = job_bmi_send(server_addr_array[i], &(req_array[i]),
			sizeof(struct request_foo), 0, BMI_EXT_ALLOC, 1, NULL, 
			&status, &id);
		if(ret < 0)
		{
			fprintf(stderr, "job_bmi_send failure.\n");
			return(-1);
		}
		if(ret == 0)
		{
			/* did not complete; add this id to our array to wait on later */
			id_array[num_pending] = id;
			num_pending++;
		}
		else
		{
			/* finished immediately; check the status */
			if(status.error_code != 0)
			{
				fprintf(stderr, "send job %d failed.\n", i);
			}
		}
	}

	/* wait for all of the pending jobs to finish */
	ret = job_waitall_blocking(id_array, num_pending, NULL,
		status_array);
	if(ret < 0)
	{
		fprintf(stderr, "job_waitall_blocking failure.\n");
		return(-1);
	}

	/* check the status of each job */
	for(i=0; i<num_pending; i++)
	{
		if(status_array[i].error_code != 0)
		{
			fprintf(stderr, "One of the pending jobs failed.\n");
		}
	}

	/* shut down the interfaces */
	job_finalize();
	PINT_flow_finalize();
	BMI_finalize();

	return(0);
}

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* test program for the request scheduler API */

#include <stdio.h>

#include <request-scheduler.h>
#include <pvfs2-req-proto.h>
#include <job.h>

int main(int argc, char **argv)	
{
	int ret;
	struct PVFS_server_req req_array[3];
	job_id_t id_array[3];
	job_id_t id_arrayB[3];
	int count = 0;
	struct job_status stat;
	job_context_id context;

	/* setup some requests to test */
	req_array[0].op = PVFS_SERV_GETATTR;
	req_array[0].u.getattr.handle = 5;

	req_array[1].op = PVFS_SERV_SETATTR;
	req_array[1].u.setattr.handle = 5;

	req_array[2].op = PVFS_SERV_GETATTR;
	req_array[2].u.getattr.handle = 6;

	/* initialize scheduler */
	ret = PINT_req_sched_initialize();
	if(ret < 0)
	{
		fprintf(stderr, "Error: initialize failure.\n");
		return(-1);
	}

	/* initialize job interface */
	ret = job_initialize(0);
	if(ret < 0)
	{
		fprintf(stderr, "Error: job initialized failure.\n");
		return(-1);
	}

	ret = job_open_context(&context);
	if(ret < 0)
	{
		fprintf(stderr, "job_open_context() failure.\n");
		return(-1);
	}


	/* try to schedule first request- it should proceed */
	ret = job_req_sched_post(&(req_array[0]), NULL, &stat,
	&(id_array[0]), context);
	if(ret != 1)
	{
		fprintf(stderr, "Error: 1st post should immediately complete.\n");
		return(-1);
	}

	/* try to schedule second request- it should queue up */
	ret = job_req_sched_post(&(req_array[1]), NULL, &stat,
	&(id_array[1]), context);
	if(ret != 0)
	{
		fprintf(stderr, "Error: 2nd post should queue.\n");
		return(-1);
	}

	/* try to schedule third request- it should proceed */
	ret = job_req_sched_post(&(req_array[2]), NULL, &stat,
	&(id_array[2]), context);
	if(ret != 1)
	{
		fprintf(stderr, "Error: 3rd post should immediately complete.\n");
		return(-1);
	}

	/*********************************************************/

	/* test the second one and make sure it doesn't finish */
	ret = job_test(id_array[1], &count, NULL, &stat, 0, context); 
	if(ret != 0 || count != 0)
	{
		fprintf(stderr, "Error: test of 2nd request failed.\n");
		return(-1);
	}

	/* complete the first request */
	ret = job_req_sched_release(id_array[0], NULL,
		&stat, &(id_arrayB[0]), context);
	if(ret != 1)
	{
		fprintf(stderr, "Error: release didn't immediately complete.\n");
		return(-1);
	}
	
	/* now the 2nd request should be ready to go */
	ret = job_test(id_array[1], &count, NULL, &stat, 0, context); 
	if(ret != 1 || count != 1 || stat.error_code != 0)
	{
		fprintf(stderr, "Error: test of 2nd request failed.\n");
		return(-1);
	}

	/* complete the 3rd request */
	ret = job_req_sched_release(id_array[2], NULL,
		&stat, &(id_arrayB[2]), context);
	if(ret != 1)
	{
		fprintf(stderr, "Error: release didn't immediately complete.\n");
		return(-1);
	}

	/* shut down job interface */
	job_close_context(context);
	job_finalize();

	/* shut down scheduler */
	ret = PINT_req_sched_finalize();
	if(ret < 0)
	{
		fprintf(stderr, "Error: finalize failure.\n");
		return(-1);
	}

	return(0);
}

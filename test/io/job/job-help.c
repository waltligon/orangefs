#include<stdio.h>

#include<job-help.h>

int block_on_job(job_id_t id, void** returned_user_ptr,
	job_status_s* out_status_p)
{
	int outcount = 0;
	int ret = -1;

	ret = job_test(id, &outcount, returned_user_ptr, out_status_p, -1);

	if(ret < 0)
	{
		fprintf(stderr, "job_wait() failure.\n");
		return(-1);
	}

	if(out_status_p->error_code != 0)
	{
		fprintf(stderr, "job failed during job_wait(), error code: %d\n",
			out_status_p->error_code);
		return(-1);
	}

	return(0);
}


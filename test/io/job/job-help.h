#ifndef __JOB_HELP_H
#define __JOB_HELP_H

#include <job.h>

int block_on_job(job_id_t id, void** returned_user_ptr,
	job_status_s* out_status_p);

#endif /* JOB_HELP_H */

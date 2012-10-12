/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __JOB_TIME_MGR_H
#define __JOB_TIME_MGR_H

#include "pvfs2-types.h"
#include "job-desc-queue.h"
#include "job.h"

int job_time_mgr_init(void);
int job_time_mgr_finalize(void);
int job_time_mgr_add(struct job_desc* jd, int timeout_sec);
void job_time_mgr_rem(struct job_desc* jd);
int job_time_mgr_expire(void);

#endif /* __JOB_TIME_MGR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* functions for handling queues of jobs that the job interface is
 * managing
 */

#ifndef __JOB_DESC_QUEUE_H
#define __JOB_DESC_QUEUE_H

#include "quicklist.h"
#include "job.h"
#include "pvfs2-types.h"
#include "trove-types.h"
#include "request-scheduler.h"
#include "thread-mgr.h"

/* describes BMI operations */
struct bmi_desc
{
    bmi_op_id_t id;
    bmi_error_code_t error_code;
    bmi_size_t actual_size;
};

/* describes trove operations */
struct trove_desc
{
    TROVE_op_id id;
    PVFS_size actual_size;
    PVFS_vtag *vtag;
    PVFS_fs_id fsid;
    PVFS_error state;
    PVFS_handle handle;
    PVFS_ds_position position;
    PVFS_ds_attributes attr;
    PVFS_ds_type type;
    int count;
};

/* describes unexpected BMI operations */
struct bmi_unexp_desc
{
    struct BMI_unexpected_info *info;
};

/* describes unexpected dev operations */
struct dev_unexp_desc
{
    struct PINT_dev_unexp_info* info;
};

/* describes flows */
struct flow_desc
{
    flow_descriptor *flow_d;
};

/* describes request scheduler elements */
struct req_sched_desc
{
    req_sched_id id;
    int post_flag;
    int error_code;
};

enum job_type
{
    JOB_BMI = 1,
    JOB_BMI_UNEXP,
    JOB_TROVE,
    JOB_FLOW,
    JOB_REQ_SCHED,
    JOB_DEV_UNEXP
};

/* describes a job, which may be one of several types */
struct job_desc
{
    enum job_type type;		/* type of job */
    job_id_t job_id;		/* job interface identifier */
    void *job_user_ptr;		/* user pointer */
    PVFS_aint status_user_tag;  /* user supplied tag */
    int completed_flag;		/* has the job finished? */
    job_context_id context_id;  /* context */
    struct PINT_thread_mgr_bmi_callback bmi_callback;  /* callback information */
    struct PINT_thread_mgr_trove_callback trove_callback;  /* callback information */

    /* union of information for lower level interfaces */
    union
    {
	struct bmi_desc bmi;
	struct trove_desc trove;
	struct bmi_unexp_desc bmi_unexp;
	struct flow_desc flow;
	struct req_sched_desc req_sched;
	struct dev_unexp_desc dev_unexp;
    }
    u;

    struct qlist_head job_desc_q_link;	/* queue link */
};

typedef struct qlist_head *job_desc_q_p;

struct job_desc *alloc_job_desc(int type);
void dealloc_job_desc(struct job_desc *jd);
job_desc_q_p job_desc_q_new(void);
void job_desc_q_cleanup(job_desc_q_p jdqp);
void job_desc_q_add(job_desc_q_p jdqp,
		    struct job_desc *desc);
void job_desc_q_remove(struct job_desc *desc);
int job_desc_q_empty(job_desc_q_p jdqp);
struct job_desc *job_desc_q_shownext(job_desc_q_p jdqp);
void job_desc_q_dump(job_desc_q_p jdqp);

#endif /* __JOB_DESC_QUEUE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

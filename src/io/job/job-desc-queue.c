/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* functions for handling queues of jobs that the job interface is
 * managing
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "job-desc-queue.h"
#include "gossip.h"
#include "id-generator.h"
#include "gen-locks.h"

static gen_mutex_t *s_job_desc_q_mutex = NULL;

/***************************************************************
 * Visible functions
 */

/* alloc_job_desc()
 *
 * creates a new job desc struct and fills in default values
 *
 * returns pointer to structure on success, NULL on failure
 */
struct job_desc *alloc_job_desc(int type)
{
    struct job_desc *jd = NULL;

    jd = (struct job_desc *) malloc(sizeof(struct job_desc));
    if (!jd)
    {
	return (NULL);
    }
    memset(jd, 0, sizeof(struct job_desc));

    if (id_gen_safe_register(&(jd->job_id), jd) < 0)
    {
	free(jd);
	return (NULL);
    }

    jd->type = type;
    return (jd);
};

/* dealloc_job_desc()
 *
 * destroys an existing job desc structure
 *
 * no return value
 */
void dealloc_job_desc(struct job_desc *jd)
{
    id_gen_safe_unregister(jd->job_id);
    free(jd);
    return;
}

/* job_desc_q_new()
 *
 * creates a new queue of job descriptions
 *
 * returns pointer to queue on success, NULL on failure
 */
job_desc_q_p job_desc_q_new(void)
{
    struct qlist_head *tmp_job_desc_q = NULL;

    s_job_desc_q_mutex = gen_mutex_build();
    if (s_job_desc_q_mutex)
    {
        tmp_job_desc_q = (struct qlist_head *)
            malloc(sizeof(struct qlist_head));
        if (tmp_job_desc_q)
        {
            INIT_QLIST_HEAD(tmp_job_desc_q);
        }
    }
    return (tmp_job_desc_q);
}

/* job_desc_q_cleanup()
 *
 * destroys an existing queue of job descriptions
 *
 * no return value
 */
void job_desc_q_cleanup(job_desc_q_p jdqp)
{
    struct job_desc *tmp_job_desc = NULL;

    if (s_job_desc_q_mutex)
    {
        gen_mutex_lock(s_job_desc_q_mutex);
        if (jdqp)
        {
            do
            {
                tmp_job_desc = job_desc_q_shownext(jdqp);
                if (tmp_job_desc)
                {
                    job_desc_q_remove(tmp_job_desc);
                    free(tmp_job_desc);
                }
            } while (tmp_job_desc);

            free(jdqp);
            jdqp = NULL;
        }
        gen_mutex_unlock(s_job_desc_q_mutex);
        gen_mutex_destroy(s_job_desc_q_mutex);
        s_job_desc_q_mutex = NULL;
    }
    return;
}

/* job_desc_q_add()
 *
 * adds a new job description to a queue
 *
 * no return value
 */
void job_desc_q_add(job_desc_q_p jdqp,
		    struct job_desc *desc)
{
    if (jdqp)
    {
        gen_mutex_lock(s_job_desc_q_mutex);
        if (jdqp)
        {
            assert(desc);

            /* note that we are adding to tail to preserve fifo order */
            qlist_add_tail(&(desc->job_desc_q_link), jdqp);
        }
        gen_mutex_unlock(s_job_desc_q_mutex);
    }
}

/* job_desc_q_remove()
 *
 * removes an entry from a job desc queue
 *
 * no return value
 */
void job_desc_q_remove(struct job_desc *desc)
{
    assert(desc);
    qlist_del(&(desc->job_desc_q_link));
}

/* job_desc_q_empty()
 *
 * checks to see if a given queue is empty or not
 *
 * returns 1 if empty, 0 otherwise
 */
int job_desc_q_empty(job_desc_q_p jdqp)
{
    return (qlist_empty(jdqp));
}

/* job_desc_q_shownext()
 *
 * returns a pointer to the next item in the queue
 *
 * returns pointer to job desc on success, NULL on failure
 */
struct job_desc *job_desc_q_shownext(job_desc_q_p jdqp)
{
    if (jdqp->next == jdqp)
    {
	return (NULL);
    }
    return (qlist_entry(jdqp->next, struct job_desc, job_desc_q_link));
}


/* job_desc_q_dump()
 *
 * prints out the contents of the desired job desc queue
 *
 * no return value
 */
void job_desc_q_dump(job_desc_q_p jdqp)
{
    struct qlist_head *tmp_link = NULL;
    struct job_desc *tmp_entry = NULL;

    gossip_err("job_desc_q_dump():\n");
    gossip_err("------------------\n");

    /* iterate all the way through the queue */
    qlist_for_each(tmp_link, jdqp)
    {
	tmp_entry = qlist_entry(tmp_link, struct job_desc,
				job_desc_q_link);
	gossip_err("  job id: %ld.\n", (long) tmp_entry->job_id);
	switch (tmp_entry->type)
	{
	case JOB_BMI:
	    gossip_err("    type: JOB_BMI.\n");
	    gossip_err("    bmi_id: %ld.\n", (long) tmp_entry->u.bmi.id);
	    break;
	case JOB_BMI_UNEXP:
	    gossip_err("    type: JOB_BMI_UNEXP.\n");
	    break;
	case JOB_TROVE:
	    gossip_err("    type: JOB_TROVE.\n");
	    break;
	case JOB_FLOW:
	    gossip_err("    type: JOB_FLOW.\n");
	    break;
	case JOB_REQ_SCHED:
	    gossip_err("    type: JOB_REQ_SCHED.\n");
	    break;
	case JOB_DEV_UNEXP:
	    gossip_err("    type: JOB_DEV_UNEXP.\n");
	    break;
	case JOB_REQ_SCHED_TIMER:
	    gossip_err("    type: JOB_REQ_SCHED_TIMER.\n");
	    break;
	case JOB_NULL:
	    gossip_err("    type: JOB_NULL.\n");
	    break;
	}
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

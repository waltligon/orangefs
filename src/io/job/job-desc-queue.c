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

#include "quicklist.h"
#include "job-desc-queue.h"
#include "gossip.h"
#include "id-generator.h"
#include "pint-util.h"
#include "pvfs2-internal.h"

#ifdef WIN32
typedef enum job_type job_type_t;
#endif

/***************************************************************
 * Visible functions
 */

/* alloc_job_desc()
 *
 * creates a new job desc struct and fills in default values
 *
 * returns pointer to structure on success, NULL on failure
 */
struct job_desc *alloc_job_desc(enum job_type jtype)
{
    struct job_desc *jd = NULL;

    jd = (struct job_desc *) malloc(sizeof(struct job_desc));
    if (!jd)
    {
	return (NULL);
    }
    memset(jd, 0, sizeof(struct job_desc));

    id_gen_safe_register(&(jd->job_id), jd);

    jd->type = jtype;

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

    tmp_job_desc_q = (struct qlist_head *)
        malloc(sizeof(struct qlist_head));
    if (tmp_job_desc_q)
    {
        INIT_QLIST_HEAD(tmp_job_desc_q);
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
    job_desc_q_p iterator = NULL;
    job_desc_q_p scratch = NULL;
    struct job_desc *tmp_job_desc = NULL;

    if (jdqp)
    {
            qlist_for_each_safe(iterator, scratch, jdqp)
            {
                tmp_job_desc = qlist_entry(iterator, struct job_desc,
                        job_desc_q_link);
                /* qlist_for_each_safe lets us iterate and remove nodes.  no
                 * need to adjust pointers as we are freeing everything */
                free(tmp_job_desc);
            }

            free(jdqp);
            jdqp = NULL;
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
        assert(desc);
        gossip_ldebug(GOSSIP_SM_JOBQ_DEBUG, "adding (%p)-(%p)\n", desc, desc->job_user_ptr);

        /* note that we are adding to tail to preserve fifo order */
        qlist_add_tail(&(desc->job_desc_q_link), jdqp);
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
    gossip_ldebug(GOSSIP_SM_JOBQ_DEBUG, "removing (%p)-(%p)\n", desc, desc->job_user_ptr);
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
    job_desc_q_p iterator;
    job_desc_q_p scratch;
    struct job_desc *tmp_job_desc = NULL;

    gossip_if(GOSSIP_JOB_DEBUG)
    {
        gossip_log("job_desc_q_dump():\n");
        gossip_log("------------------\n");

        /* iterate all the way through the queue */
        qlist_for_each_safe(iterator, scratch, jdqp)
        {
            tmp_job_desc = qlist_entry(iterator, struct job_desc,
                            job_desc_q_link);
	    gossip_log("  job id: %ld.\n", (long) tmp_job_desc->job_id);
	    switch (tmp_job_desc->type)
	    {
	    case JOB_BMI:
	        gossip_log("    type: JOB_BMI.\n");
	        gossip_log("    bmi_id: %ld.\n", (long) tmp_job_desc->u.bmi.id);
	        break;
	    case JOB_BMI_UNEXP:
	        gossip_log("    type: JOB_BMI_UNEXP.\n");
	        break;
	    case JOB_TROVE:
	        gossip_log("    type: JOB_TROVE.\n");
	        break;
	    case JOB_FLOW:
	        gossip_log("    type: JOB_FLOW.\n");
	        break;
	    case JOB_REQ_SCHED:
	        gossip_log("    type: JOB_REQ_SCHED.\n");
	        break;
	    case JOB_DEV_UNEXP:
	        gossip_log("    type: JOB_DEV_UNEXP.\n");
	        break;
	    case JOB_REQ_SCHED_TIMER:
	        gossip_log("    type: JOB_REQ_SCHED_TIMER.\n");
	        break;
	    case JOB_NULL:
    	        gossip_log("    type: JOB_NULL.\n");
	        break;
	    }
            gossip_log("     user ptr:(%p)\n", tmp_job_desc->job_user_ptr);
        }
    }
    gossip_end;

    return;
}

/* job_desc_q_clear()
 *
 * removes jobs from completion queue that have a
 * particular SMCB - used during termination
 *
 * no return value
 */
void job_desc_q_clear(job_desc_q_p jdqp, void *target)
{
    job_desc_q_p iterator;
    job_desc_q_p scratch;
    struct job_desc *tmp_job_desc = NULL;

    gossip_ldebug(GOSSIP_JOB_DEBUG,
                  "removing (%p) from job desc q\n", target);

    /* iterate all the way through the queue */
    qlist_for_each_safe(iterator, scratch, jdqp)
    {
        tmp_job_desc = qlist_entry(iterator, struct job_desc,
                        job_desc_q_link);
        if (tmp_job_desc->job_user_ptr == target)
        {
            gossip_ldebug(GOSSIP_JOB_DEBUG, "job_desc_removed\n");
            job_desc_q_remove(tmp_job_desc);
            free(tmp_job_desc);
        }
    }
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

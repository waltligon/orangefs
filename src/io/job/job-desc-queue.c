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

#include <job-desc-queue.h>
#include <gossip.h>

/***************************************************************
 * Visible functions
 */

/* alloc_job_desc()
 *
 * creates a new job desc struct and fills in default values
 *
 * returns pointer to structure on success, NULL on failure
 */
struct job_desc* alloc_job_desc(int type)
{
	struct job_desc* jd = NULL;

	jd = (struct job_desc*)malloc(sizeof(struct job_desc));
	if(!jd)
	{
		return(NULL);
	}
	memset(jd, 0, sizeof(struct job_desc));

	if(id_gen_fast_register(&(jd->job_id), jd) < 0)
	{
		free(jd);
		return(NULL);
	}
	
	jd->type = type;
	return(jd);
};

/* dealloc_job_desc()
 *
 * destroys an existing job desc structure
 *
 * no return value
 */
void dealloc_job_desc(struct job_desc* jd)
{
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
	struct qlist_head* tmp_job_desc_q = NULL;

	tmp_job_desc_q = (struct qlist_head*)malloc(sizeof(struct
		qlist_head));
	if(tmp_job_desc_q)
	{
		INIT_QLIST_HEAD(tmp_job_desc_q);
	}

	return(tmp_job_desc_q);
}

/* job_desc_q_cleanup()
 *
 * destroys an existing queue of job descriptions
 *
 * no return value
 */
void job_desc_q_cleanup(job_desc_q_p jdqp)
{
	struct job_desc* tmp_job_desc = NULL;

	do
	{
		tmp_job_desc = job_desc_q_shownext(jdqp);
		if(tmp_job_desc)
		{
			job_desc_q_remove(tmp_job_desc);
			free(tmp_job_desc);
		}
	} while(tmp_job_desc);

	free(jdqp);
	jdqp = NULL;
	return;
}

/* job_desc_q_add()
 *
 * adds a new job description to a queue
 *
 * no return value
 */
void job_desc_q_add(job_desc_q_p jdqp, struct job_desc* desc)
{
	/* note that we are adding to tail to preserve fifo order */
	qlist_add_tail(&(desc->job_desc_q_link), jdqp);
	return;
}

/* job_desc_q_remove()
 *
 * removes an entry from a job desc queue
 *
 * no return value
 */
void job_desc_q_remove(struct job_desc* desc)
{
	qlist_del(&(desc->job_desc_q_link));
	return;
}

/* job_desc_q_empty()
 *
 * checks to see if a given queue is empty or not
 *
 * returns 1 if empty, 0 otherwise
 */
int job_desc_q_empty(job_desc_q_p jdqp)
{
	return(qlist_empty(jdqp));
}

/* job_desc_q_shownext()
 *
 * returns a pointer to the next item in the queue
 *
 * returns pointer to job desc on success, NULL on failure
 */
struct job_desc* job_desc_q_shownext(job_desc_q_p jdqp)
{
	if(jdqp->next == jdqp)
	{
		return(NULL);
	}
	return(qlist_entry(jdqp->next, struct job_desc, job_desc_q_link));
}

/* job_desc_q_search()
 *
 * searches a queue for a job description w/ the specified id
 *
 * returns pointer to matching entry on success, NULL if not found
 */
struct job_desc* job_desc_q_search(job_desc_q_p jdqp, job_id_t id)
{
	struct qlist_head* tmp_link = NULL;
	struct job_desc* tmp_entry = NULL;

	qlist_for_each(tmp_link, jdqp)
	{
		tmp_entry = qlist_entry(tmp_link, struct job_desc,
			job_desc_q_link);
		if(tmp_entry->job_id == id)
		{
			return(tmp_entry);
		}
	}
	
	return(NULL);
}

/* job_desc_q_search_multi()
 *
 * searches a given job desc queue for any of an array of jobs.
 *
 * returns 0 on success, -errno on failure
 */
int job_desc_q_search_multi(job_desc_q_p jdqp, job_id_t* id_array, int*
	inout_count_p, int* index_array)
{
	int num_real_descriptors = 0;
	struct qlist_head* tmp_link = NULL;
	struct job_desc* tmp_entry = NULL;
	int i=0;
	int incount = *inout_count_p;

	*inout_count_p = 0;
	/* quick check for null job descriptors */
	for(i=0; i<incount; i++)
	{
		if(id_array[i] != 0)
		{
			num_real_descriptors++;
		}
	}
	if(num_real_descriptors == 0)
	{
		return(-EINVAL);
	}

	/* iterate all the way through the queue */
	qlist_for_each(tmp_link, jdqp)
	{
		tmp_entry = qlist_entry(tmp_link, struct job_desc,
			job_desc_q_link);
		/* for each queue entry, loop through the array of jobs we are
		 * looking for
		 */
		for(i=0; i<incount; i++)
		{
			if(id_array[i] == tmp_entry->job_id)
			{
				index_array[*inout_count_p] = i;
				(*inout_count_p)++;
				break;
			}
		}
		/* quit early if we have already found everything */
		if(*inout_count_p == num_real_descriptors)
		{
			return(0);
		}
	}

	return(0);
}


/* job_desc_q_dump()
 *
 * prints out the contents of the desired job desc queue
 *
 * no return value
 */
void job_desc_q_dump(job_desc_q_p jdqp)
{
	struct qlist_head* tmp_link = NULL;
	struct job_desc* tmp_entry = NULL;

	gossip_err("job_desc_q_dump():\n");
	gossip_err("------------------\n");

	/* iterate all the way through the queue */
	qlist_for_each(tmp_link, jdqp)
	{
		tmp_entry = qlist_entry(tmp_link, struct job_desc,
			job_desc_q_link);
		gossip_err("  job id: %ld.\n", (long)tmp_entry->job_id);
		switch(tmp_entry->type)
		{
			case JOB_BMI:
				gossip_err("    type: JOB_BMI.\n");
				gossip_err("    bmi_id: %ld.\n",
					(long)tmp_entry->u.bmi.id);
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
		}
	}

	return;
}

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <sys/time.h>
#include <assert.h>
#include <string.h>

#include "pvfs2-types.h"
#include "job-desc-queue.h"
#include "job.h"
#include "quicklist.h"
#include "gen-locks.h"
#include "gossip.h"

static QLIST_HEAD(bucket_queue);
static gen_mutex_t bucket_mutex = GEN_MUTEX_INITIALIZER;

struct time_bucket
{
    long expire_time_sec;
    struct qlist_head bucket_link;
    struct qlist_head jd_queue;
};

/* job_time_mgr_init()
 *
 * initialize timeout mgmt interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int job_time_mgr_init(void)
{
    INIT_QLIST_HEAD(&bucket_queue);
    return(0);
}

/* job_time_mgr_finalize()
 *
 * shut down timeout mgmt interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int job_time_mgr_finalize(void)
{
    struct qlist_head* iterator = NULL;
    struct qlist_head* scratch = NULL;
    struct qlist_head* iterator2 = NULL;
    struct qlist_head* scratch2 = NULL;
    struct time_bucket* tmp_bucket = NULL;
    struct job_desc* jd = NULL;

    gen_mutex_lock(&bucket_mutex);

    qlist_for_each_safe(iterator, scratch, &bucket_queue)
    {
	tmp_bucket = qlist_entry(iterator, struct time_bucket, bucket_link);
        assert(tmp_bucket);

	qlist_del(&tmp_bucket->bucket_link);
	qlist_for_each_safe(iterator2, scratch2, &tmp_bucket->jd_queue)
	{
	    jd = qlist_entry(iterator2, struct job_desc, job_time_link);
	    qlist_del(&jd->job_time_link);
	    jd->time_bucket = NULL;
	}
	INIT_QLIST_HEAD(&tmp_bucket->jd_queue);
	free(tmp_bucket);
    }
    INIT_QLIST_HEAD(&bucket_queue);

    gen_mutex_unlock(&bucket_mutex);

    return(0);
}

static int __job_time_mgr_add(struct job_desc* jd, int timeout_sec)
{
    struct timeval tv;
    long expire_time_sec;
    struct qlist_head* tmp_link;
    struct time_bucket* tmp_bucket = NULL;
    struct time_bucket* prev_bucket = NULL;
    struct time_bucket* new_bucket = NULL;

    struct qlist_head* prev;
    struct qlist_head* next;

    if(timeout_sec == JOB_TIMEOUT_INF)
    {
	/* do nothing */
	return(0);
    }

    gettimeofday(&tv, NULL);

    /* round up to the second that this job should expire */
    expire_time_sec = tv.tv_sec + timeout_sec;

    if(jd->type == JOB_FLOW)
    {
	jd->u.flow.timeout_sec = timeout_sec;
    }

    /* look for a bucket matching the desired seconds value */
    qlist_for_each(tmp_link, &bucket_queue)
    {
        tmp_bucket = qlist_entry(
            tmp_link, struct time_bucket, bucket_link);
        assert(tmp_bucket);

        if(tmp_bucket->expire_time_sec >= expire_time_sec)
        {
            break;
        }

        tmp_bucket = NULL;
        prev_bucket = tmp_bucket;
    }

    if(!tmp_bucket || tmp_bucket->expire_time_sec != expire_time_sec)
    {
	/* make a new bucket, we didn't find an exact match */
	new_bucket = (struct time_bucket*)
            malloc(sizeof(struct time_bucket));
	assert(new_bucket);

	new_bucket->expire_time_sec = expire_time_sec;
	INIT_QLIST_HEAD(&new_bucket->bucket_link);
	INIT_QLIST_HEAD(&new_bucket->jd_queue);

	if(tmp_bucket)
	    next = &tmp_bucket->bucket_link;
	else
	    next = &bucket_queue;
	
	if(prev_bucket)
	    prev = &prev_bucket->bucket_link;
	else
	    prev = &bucket_queue;

	__qlist_add(&new_bucket->bucket_link, prev, next);

	tmp_bucket = new_bucket;
    }

    assert(tmp_bucket);
    assert(tmp_bucket->expire_time_sec >= expire_time_sec);

    /* add the job descriptor onto the correct bucket */
    qlist_add_tail(&jd->job_time_link, &tmp_bucket->jd_queue);
    jd->time_bucket = tmp_bucket;

    return(0);
}
/* job_time_mgr_add()
 *
 * adds a job to be monitored for timeout, timeout_sec is an interval in
 * seconds
 *
 * return 0 on success, -PVFS_error on failure
 */
int job_time_mgr_add(struct job_desc* jd, int timeout_sec)
{
    int ret = -1;

    gen_mutex_lock(&bucket_mutex);

    ret = __job_time_mgr_add(jd, timeout_sec);

    gen_mutex_unlock(&bucket_mutex);

    return(ret);
}

/* job_time_mgr_rem()
 *
 * remove a job from the set that is being monitored for timeout
 *
 * no return value
 */
void job_time_mgr_rem(struct job_desc* jd)
{
    struct time_bucket* tmp_bucket = NULL;

    gen_mutex_lock(&bucket_mutex);

    if(jd->time_bucket == NULL)
    {
	/* nothing to do, it is already removed */
        gen_mutex_unlock(&bucket_mutex);
	return;
    }

    tmp_bucket = (struct time_bucket*)jd->time_bucket;

    if(qlist_empty(&tmp_bucket->jd_queue))
    {
	/* no need for this bucket any longer; it is empty */
	qlist_del(&tmp_bucket->bucket_link);
	INIT_QLIST_HEAD(&tmp_bucket->jd_queue);
	free(tmp_bucket);
    }
    qlist_del(&jd->job_time_link);
    jd->time_bucket = NULL;

    gen_mutex_unlock(&bucket_mutex);

    return;
}

/* job_time_mgr_expire()
 *
 * look for expired jobs and cancel them
 *
 * returns 0 on success, -PVFS_error on failure
 */
int job_time_mgr_expire(void)
{
    struct timeval tv;
    struct qlist_head* iterator = NULL;
    struct qlist_head* scratch = NULL;
    struct qlist_head* iterator2 = NULL;
    struct qlist_head* scratch2 = NULL;
    struct time_bucket* tmp_bucket = NULL;
    struct job_desc* jd = NULL;
    int ret = -1;
    PVFS_size tmp_size = 0;

    gettimeofday(&tv, NULL);

    gen_mutex_lock(&bucket_mutex);

    qlist_for_each_safe(iterator, scratch, &bucket_queue)
    {
	tmp_bucket = qlist_entry(iterator, struct time_bucket, bucket_link);
        assert(tmp_bucket);

	/* stop when we see the first bucket that has not expired */
	if(tmp_bucket->expire_time_sec > tv.tv_sec)
	{
	    break;
	}

	/* cancel the associated jobs and remove the bucket */
	qlist_for_each_safe(iterator2, scratch2, &tmp_bucket->jd_queue)
	{
	    jd = qlist_entry(iterator2, struct job_desc, job_time_link);
	    qlist_del(&jd->job_time_link);

	    switch(jd->type)
	    {
	    case JOB_BMI:
		gossip_debug(GOSSIP_CANCEL_DEBUG, "job_timer: cancelling bmi.\n");
		ret = job_bmi_cancel(jd->job_id, jd->context_id);
		break;
	    case JOB_FLOW:
		/* have we made any progress since last time we checked? */
		PINT_flow_getinfo(jd->u.flow.flow_d,
                                  FLOW_AMT_COMPLETE_QUERY, &tmp_size);
		if(tmp_size > jd->u.flow.last_amt_complete)
		{
		    /* if so, then update progress and reset timer */
		    jd->u.flow.last_amt_complete = tmp_size;
		    __job_time_mgr_add(jd, jd->u.flow.timeout_sec);
		    ret = 0;
		}
		else
		{
		    /* otherwise kill the flow */
		    gossip_debug(GOSSIP_CANCEL_DEBUG,
                                 "job_timer: cancelling flow.\n");
		    ret = job_flow_cancel(jd->job_id, jd->context_id);
		}
		break;
	    case JOB_TROVE:
		gossip_debug(GOSSIP_CANCEL_DEBUG,
                             "job_timer: cancelling trove.\n");
		ret = job_trove_dspace_cancel(
                    jd->u.trove.fsid, jd->job_id, jd->context_id);
                break;
	    default:
		ret = 0;
		break;
	    }

	    /* FIXME: error handling */
	    assert(ret == 0);

	    jd->time_bucket = NULL;
	}
	qlist_del(&tmp_bucket->bucket_link);
        INIT_QLIST_HEAD(&tmp_bucket->jd_queue);
	free(tmp_bucket);
    }

    gen_mutex_unlock(&bucket_mutex);

    return(0);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


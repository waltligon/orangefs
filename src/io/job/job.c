/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this file contains a skeleton implementation of the job interface */

#include <errno.h>
#ifndef WIN32
#include <sys/time.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "state-machine.h"
#include "job.h"
#include "job-desc-queue.h"
#include "gen-locks.h"
#include "bmi.h"
#include "trove.h"
#include "gossip.h"
#include "id-generator.h"
#include "job-time-mgr.h"
#include "pvfs2-internal.h"

/* contexts for use within the job interface */
static bmi_context_id global_bmi_context = -1;
#ifdef __PVFS2_TROVE_SUPPORT__
static TROVE_context_id global_trove_context = -1;
#endif

/* queues of pending jobs */
static job_desc_q_p completion_queue_array[JOB_MAX_CONTEXTS] = {NULL};
static int completion_error = 0;
static job_desc_q_p bmi_unexp_queue = NULL;
static int bmi_unexp_pending_count = 0;
static int bmi_pending_count = 0;
static int trove_pending_count = 0;
static int flow_pending_count = 0;
static job_desc_q_p dev_unexp_queue = NULL;
#ifdef __PVFS2_CLIENT__
static int dev_unexp_pending_count = 0;
#endif
/* locks for internal queues */
static gen_mutex_t bmi_unexp_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t dev_unexp_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t completion_mutex = GEN_MUTEX_INITIALIZER;

static int initialized = 0;
static gen_mutex_t initialized_mutex = GEN_MUTEX_INITIALIZER;

#ifdef __PVFS2_JOB_THREADED__
static pthread_cond_t completion_cond = PTHREAD_COND_INITIALIZER;
#endif /* __PVFS2_JOB_THREADED__ */

/* number of jobs to test for at once inside of do_one_work_cycle() */
enum
{
    job_work_metric = 5,
    thread_wait_timeout = 10000        /* usecs */
};

/* cap how many keys we dump into trove at once when filling precreate pools
 * so that it doesn't clog up trove queues
 */
#define PRECREATE_POOL_MAX_KEYS 32

/********************************************************
 * function prototypes
 */

static int setup_queues(void);
static void teardown_queues(void);
static int do_one_test_cycle_req_sched(void);
static void fill_status(struct job_desc *jd,
                        void **returned_user_ptr_p,
                        job_status_s *status);
static int completion_query_some(job_id_t *id_array,
                                 int *inout_count_p,
                                 int *out_index_array,
                                 void **returned_user_ptr_array,
                                 job_status_s *out_status_array_p);
static int completion_query_context(job_id_t *out_id_array_p,
                                  int *inout_count_p,
                                  void **returned_user_ptr_array,
                                  job_status_s *out_status_array_p,
                                  job_context_id context_id);
static void bmi_thread_mgr_callback(void *data, 
                                    PVFS_size actual_size,
                                    PVFS_error error_code);
static void bmi_thread_mgr_unexp_handler(struct BMI_unexpected_info* unexp);
#ifdef __PVFS2_CLIENT__
static void dev_thread_mgr_unexp_handler(struct PINT_dev_unexp_info* unexp);
#endif
static void trove_thread_mgr_callback(void *data,
                                      PVFS_error error_code);
static void flow_callback(flow_descriptor *flow_d, int cancel_path);

#ifndef __PVFS2_JOB_THREADED__
static gen_mutex_t work_cycle_mutex = GEN_MUTEX_INITIALIZER;
static void do_one_work_cycle_all(int idle_time_ms);
#endif

/********************************************************
 * public interface 
 */

/* job_initialize()
 *
 * start up the job interface
 * 
 * returns 0 on success, -errno on failure
 */
int job_initialize(int flags)
{
    int ret = -1;

    ret = setup_queues();
    if (ret < 0)
    {
        return (ret);
    }

    /* startup threads */
    ret = PINT_thread_mgr_bmi_start();
    if (ret != 0)
    {
        teardown_queues();
        return (-ret);
    }
    ret = PINT_thread_mgr_bmi_getcontext((PVFS_context_id *)&global_bmi_context);
    /* this should never fail if the thread startup succeeded */
    assert(ret == 0);

#ifdef __PVFS2_CLIENT__
    ret = PINT_thread_mgr_dev_start();
    if (ret != 0)
    {
        PINT_thread_mgr_bmi_stop();
        teardown_queues();
        return(-ret);
    }
#endif

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = PINT_thread_mgr_trove_start();
    if(ret != 0)
    {
        PINT_thread_mgr_bmi_stop();
#ifdef __PVFS2_CLIENT__
        PINT_thread_mgr_dev_stop();
#endif
        teardown_queues();
        return (-ret);
    }
    ret = PINT_thread_mgr_trove_getcontext(&global_trove_context);
    /* this should never fail if thread startup succeeded */
    assert(ret == 0);
#endif

    id_gen_safe_initialize();

    gen_mutex_lock(&initialized_mutex);
    initialized = 1;
    gen_mutex_unlock(&initialized_mutex);

    return (0);
}

/* job_finalize()
 *
 * shuts down the job interface
 *
 * returns 0 on success, -errno on failure
 */
int job_finalize(void)
{
    gen_mutex_lock(&initialized_mutex);
    initialized = 0;
    gen_mutex_unlock(&initialized_mutex);

    id_gen_safe_finalize();

    PINT_thread_mgr_bmi_stop();
#ifdef __PVFS2_CLIENT__
    PINT_thread_mgr_dev_stop();
#endif
#ifdef __PVFS2_TROVE_SUPPORT__
    PINT_thread_mgr_trove_stop();
#endif
    teardown_queues();
    return 0;
}


/* job_open_context()
 *
 * opens a new context for the job interface
 *
 * returns 0 on success, -errno on failure
 */
int job_open_context(job_context_id *context_id)
{
    int context_index;

    /* find an unused context id */
    gen_mutex_lock(&completion_mutex);
    for(context_index = 0; context_index < JOB_MAX_CONTEXTS; context_index++)
    {
        if(completion_queue_array[context_index] == NULL)
        {
            break;
        }
    }

    if(context_index >= JOB_MAX_CONTEXTS)
    {
        /* we don't have any more available! */
        gen_mutex_unlock(&completion_mutex);
        return(-EBUSY);
    }

    /* create a new completion queue for the context */
    completion_queue_array[context_index] = job_desc_q_new();
    if(!completion_queue_array[context_index])
    {
        gen_mutex_unlock(&completion_mutex);
        return(-ENOMEM);
    }
    gen_mutex_unlock(&completion_mutex);

    *context_id = context_index;
    return(0);
}


/* job_close_context()
 *
 * destroys a context previously created with job_open_context()
 *
 * no return value 
 */
void job_close_context(job_context_id context_id)
{
    gen_mutex_lock(&completion_mutex);
    if(!completion_queue_array[context_id])
    {
        gen_mutex_unlock(&completion_mutex);
        return;
    }

    job_desc_q_cleanup(completion_queue_array[context_id]);

    completion_queue_array[context_id] = NULL;

    gen_mutex_unlock(&completion_mutex);
    return;
}

/* job_reset_timeout()
 *
 * resets the timeout associated with a job that has already been posted but
 * has not yet completed
 *
 * returns 0 on success, -PVFS_errno on failure
 */
int job_reset_timeout(job_id_t id, int timeout_sec)
{
    struct job_desc *query = NULL;
    int ret = -1;

    /* lock completion queue to make sure that a concurrent test call
     * doesn't pull the job out from under us somehow
     */
    gen_mutex_lock(&completion_mutex);

    query = id_gen_safe_lookup(id);
    if(!query)
    {        
        /* this id is not valid */
        gen_mutex_unlock(&completion_mutex);
        return(-PVFS_EINVAL);
    }

    if(query->type != JOB_BMI && query->type != JOB_FLOW)
    {
        /* trying to reset timeouts on a job that doesn't support the
         * concept 
         */
        gen_mutex_unlock(&completion_mutex);
        return(-PVFS_EINVAL);
    }

    /* pull the job out of the time mgr (thereby clearing old timer) */
    job_time_mgr_rem(query);

    /* put it back into the time mgr with new value */
    ret = job_time_mgr_add(query, timeout_sec);

    gen_mutex_unlock(&completion_mutex);

    return(ret);
}


/* job_bmi_send()
 *
 * posts a job to send a BMI message
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_bmi_send(PVFS_BMI_addr_t addr,
                 void *buffer,
                 bmi_size_t size,
                 bmi_msg_tag_t tag,
                 enum bmi_buffer_type buffer_type,
                 int send_unexpected,
                 void *user_ptr,
                 job_aint status_user_tag,
                 job_status_s *out_status_p,
                 job_id_t *id,
                 job_context_id context_id,
                 int timeout_sec,
                 PVFS_hint hints)
{
    /* post a bmi send.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal = NULL;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_BMI);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ERROR_CODE(errno);
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.bmi.actual_size = size;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->bmi_callback.fn = bmi_thread_mgr_callback;
    jd->bmi_callback.data = (void*)jd;
    user_ptr_internal = &jd->bmi_callback;

    jd->hints = hints;

    /* post appropriate type of send */
    if (!send_unexpected)
    {
        ret = BMI_post_send(&(jd->u.bmi.id), addr, buffer, size,
                            buffer_type, tag, user_ptr_internal,
                            global_bmi_context, jd->hints);
    }
    else
    {
        ret = BMI_post_sendunexpected(&(jd->u.bmi.id), addr,
                                      buffer, size, buffer_type, tag,
                                      user_ptr_internal, global_bmi_context,
                                      jd->hints);
    }

    if (ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = size;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    bmi_pending_count++;

    return(job_time_mgr_add(jd, timeout_sec));
}


/* job_bmi_send_list()
 *
 * posts a job to send a BMI list message
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_bmi_send_list(PVFS_BMI_addr_t addr,
                      void **buffer_list,
                      bmi_size_t *size_list,
                      int list_count,
                      bmi_size_t total_size,
                      bmi_msg_tag_t tag,
                      enum bmi_buffer_type buffer_type,
                      int send_unexpected,
                      void *user_ptr,
                      job_aint status_user_tag,
                      job_status_s *out_status_p,
                      job_id_t *id,
                      job_context_id context_id,
                      int timeout_sec,
                      PVFS_hint hints)
{
    /* post a bmi send.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal = NULL;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_BMI);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ERROR_CODE(errno);
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.bmi.actual_size = total_size;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->bmi_callback.fn = bmi_thread_mgr_callback;
    jd->bmi_callback.data = (void*)jd;
    user_ptr_internal = &jd->bmi_callback;

    jd->hints = hints;

    /* post appropriate type of send */
    if (!send_unexpected)
    {
        ret = BMI_post_send_list(&(jd->u.bmi.id),
                                 addr,
                                 (const void **) buffer_list,
                                 size_list,
                                 list_count,
                                 total_size,
                                 buffer_type,
                                 tag,
                                 user_ptr_internal,
                                 global_bmi_context,
                                 hints);
    }
    else
    {
        ret = BMI_post_sendunexpected_list(&(jd->u.bmi.id),
                                           addr,
                                           (const void **) buffer_list,
                                           size_list,
                                           list_count,
                                           total_size,
                                           buffer_type,
                                           tag,
                                           user_ptr_internal,
                                           global_bmi_context, hints);
    }

    if (ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = total_size;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    bmi_pending_count++;
    return(job_time_mgr_add(jd, timeout_sec));
}

/* job_bmi_recv()
 *
 * posts a job to receive a BMI message
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_bmi_recv(PVFS_BMI_addr_t addr,
                 void *buffer,
                 bmi_size_t size,
                 bmi_msg_tag_t tag,
                 enum bmi_buffer_type buffer_type,
                 void *user_ptr,
                 job_aint status_user_tag,
                 job_status_s *out_status_p,
                 job_id_t *id,
                 job_context_id context_id,
                 int timeout_sec,
                 PVFS_hint hints)
{
    /* post a bmi recv.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal = NULL;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_BMI);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->bmi_callback.fn = bmi_thread_mgr_callback;
    jd->bmi_callback.data = (void*)jd;
    user_ptr_internal = &jd->bmi_callback;


    ret = BMI_post_recv(&(jd->u.bmi.id),
                        addr,
                        buffer,
                        size,
                        &(jd->u.bmi.actual_size),
                        buffer_type,
                        tag,
                        user_ptr_internal,
                        global_bmi_context,
                        hints);
    if (ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = jd->u.bmi.actual_size;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    bmi_pending_count++;

    return(job_time_mgr_add(jd, timeout_sec));
}


/* job_bmi_recv_list()
 *
 * posts a job to receive a BMI list message
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_bmi_recv_list(PVFS_BMI_addr_t addr,
                      void **buffer_list,
                      bmi_size_t *size_list,
                      int list_count,
                      bmi_size_t total_expected_size,
                      bmi_msg_tag_t tag,
                      enum bmi_buffer_type buffer_type,
                      void *user_ptr,
                      job_aint status_user_tag,
                      job_status_s *out_status_p,
                      job_id_t *id,
                      job_context_id context_id,
                      int timeout_sec,
                      PVFS_hint hints)
{

    /* post a bmi recv.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal = NULL;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_BMI);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->bmi_callback.fn = bmi_thread_mgr_callback;
    jd->bmi_callback.data = (void*)jd;
    user_ptr_internal = &jd->bmi_callback;

    ret = BMI_post_recv_list(&(jd->u.bmi.id),
                             addr,
                             buffer_list,
                             size_list,
                             list_count,
                             total_expected_size,
                             &(jd->u.bmi.actual_size),
                             buffer_type,
                             tag,
                             user_ptr_internal,
                             global_bmi_context,
                             hints);

    if (ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = jd->u.bmi.actual_size;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    bmi_pending_count++;

    return(job_time_mgr_add(jd, timeout_sec));
}

/* job_bmi_unexp()
 *
 * posts a job to receive an unexpected BMI message
 *
 * returns 0 on succcess, 1 on immediate completion, and -errno on
 * failure
 */
int job_bmi_unexp(struct BMI_unexpected_info *bmi_unexp_d,
                  void *user_ptr,
                  job_aint status_user_tag,
                  job_status_s *out_status_p,
                  job_id_t *id,
                  enum job_flags flags,
                  job_context_id context_id)
{
    /* post a bmi recv for an unexpected message.  We will do a quick
     * test to see if an unexpected message is available.  If so, we
     * return the necessary info; if not we queue up to test again later
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    int outcount = 0;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_BMI_UNEXP);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.bmi_unexp.info = bmi_unexp_d;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;

   /*********************************************************
         * TODO: consider optimizations later, so that we avoid
         * disabling immediate completion.  See the mailing list thread
         * started here:
         * http://www.beowulf-underground.org/pipermail/pvfs2-internal/2003-February/000305.html
         *
         * At the moment, the server is not designed to be able to
         * handle immediate completion upon posting unexpected jobs.
         */

    /* only look for immediate completion if our flags allow it */
    if (!(flags & JOB_NO_IMMED_COMPLETE))
    {
        ret = BMI_testunexpected(1, &outcount, jd->u.bmi_unexp.info, 0);

        if (ret < 0)
        {
            /* error testing */
            dealloc_job_desc(jd);
            jd = NULL;
            out_status_p->error_code = ret;
            return 1;
        }

        if (outcount == 1)
        {
            /* there was an unexpected job available */
            out_status_p->error_code = jd->u.bmi_unexp.info->error_code;
            out_status_p->status_user_tag = status_user_tag;
            dealloc_job_desc(jd);
            jd = NULL;
            return (ret);
        }
    }

    /* if we fall through to this point, then there were not any
     * uenxpected receive's available; queue up to test later 
     */
    gen_mutex_lock(&bmi_unexp_mutex);
    *id = jd->job_id;
    job_desc_q_add(bmi_unexp_queue, jd);
    bmi_unexp_pending_count++;
    gen_mutex_unlock(&bmi_unexp_mutex);

    PINT_thread_mgr_bmi_unexp_handler(bmi_thread_mgr_unexp_handler);

    return (0);
}

int job_bmi_unexp_cancel(job_id_t id)
{
    struct job_desc *jd;

    gen_mutex_lock(&bmi_unexp_mutex);
    jd = id_gen_safe_lookup(id);
    job_desc_q_remove(jd);
    bmi_unexp_pending_count--;
    gen_mutex_unlock(&bmi_unexp_mutex);

    gen_mutex_lock(&completion_mutex);
    /* set completed flag while holding queue lock */
    jd->completed_flag = 1;
    if (completion_queue_array[jd->context_id])
    {
        job_desc_q_add(completion_queue_array[jd->context_id], jd);
    }

#ifdef __PVFS2_JOB_THREADED__
    /* wake up anyone waiting for completion */
    pthread_cond_signal(&completion_cond);
#endif
    gen_mutex_unlock(&completion_mutex);

    return 0;
}

/* job_bmi_cancel()
 *
 * cancels a job handling a BMI message
 *
 * returns 0 on succcess, 1 on immediate completion, and -errno on
 * failure
 */
int job_bmi_cancel(job_id_t id, job_context_id context_id)
{
    struct job_desc* query = NULL;
    int ret = -1;

    gen_mutex_lock(&completion_mutex);

    query = id_gen_safe_lookup(id);
    if (!query || query->completed_flag)
    {
        /* job has already completed, no cancellation needed */
        gen_mutex_unlock(&completion_mutex);
        return(0);
    }

    /* tell thread mgr to cancel operation.  This will result in
     * normal completion path through thread mgr callbacks; no more
     * work to do here */
    ret = PINT_thread_mgr_bmi_cancel(query->u.bmi.id, &(query->bmi_callback));

    gen_mutex_unlock(&completion_mutex);

    return(ret);
}


/* job_dev_unexp()
 *
 * posts a job for an unexpected device message
 *
 * returns 0 on success, -errno on failure, and 1 on immediate
 * completion
 */
int job_dev_unexp(struct PINT_dev_unexp_info *dev_unexp_d,
                  void *user_ptr,
                  job_aint status_user_tag,
                  job_status_s *out_status_p,
                  job_id_t *id,
                  enum job_flags flags,
                  job_context_id context_id)
{
#ifdef __PVFS2_CLIENT__
    /* post a dev recv for an unexpected message.  We will do a quick
     * test to see if an unexpected message is available.  If so, we
     * return the necessary info; if not we queue up to test again later
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    int outcount = 0;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the user ptr etc.
     */
    jd = alloc_job_desc(JOB_DEV_UNEXP);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.dev_unexp.info = dev_unexp_d;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;

    /* only look for immediate completion if our flags alow it */
    if (!(flags & JOB_NO_IMMED_COMPLETE))
    {
        ret = PINT_dev_test_unexpected(1, &outcount, jd->u.dev_unexp.info, 0);

        if (ret < 0)
        {
            /* error testing */
            dealloc_job_desc(jd);
            jd = NULL;
            out_status_p->error_code = ret;
            return 1;
        }

        if (outcount == 1)
        {
            /* there was an unexpected job available */
            out_status_p->error_code = 0;
            out_status_p->status_user_tag = status_user_tag;
            dealloc_job_desc(jd);
            jd = NULL;
            return (ret);
        }
    }

    /* if we fall through to this point, then there were not any
     * uenxpected receive's available (or none requested); queue up to
     * test later
     */
    gen_mutex_lock(&dev_unexp_mutex);
    *id = jd->job_id;
    job_desc_q_add(dev_unexp_queue, jd);
    dev_unexp_pending_count++;
    gen_mutex_unlock(&dev_unexp_mutex);

    PINT_thread_mgr_dev_unexp_handler(dev_thread_mgr_unexp_handler);

    return (0);
#else
    return(-PVFS_ENOSYS);
#endif
}

/* job_dev_write()
 *
 * posts a device write
 *
 * returns 0 on success, -errno on failure, and 1 on immediate completion
 */
int job_dev_write(void *buffer,
                  int size,
                  PVFS_id_gen_t tag,
                  enum PINT_dev_buffer_type buffer_type,
                  void *user_ptr,
                  job_aint status_user_tag,
                  job_status_s *out_status_p,
                  job_id_t *id,
                  job_context_id context_id)
{
    /* NOTE: This function will _always_ immediately complete for now.  
     * It is really just in the job interface for completeness, in case we 
     * decide later to make the function asynchronous
     */
#ifdef __PVFS2_CLIENT__
    int ret = -1;
    ret = PINT_dev_write(buffer, size, buffer_type, tag);
    if(ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return(1);
    }

    /* immediate completion */
    out_status_p->error_code = 0;
    out_status_p->status_user_tag = status_user_tag;
    out_status_p->actual_size = size;
    return(1);
#else
    return(-PVFS_ENOSYS);
#endif
}


/* job_dev_write_list()
 *
 * posts a device write for multiple buffers
 *
 * returns 0 on success, -errno on failure, and 1 on immediate completion
 */
int job_dev_write_list(void **buffer_list,
                       int *size_list,
                       int list_count,
                       int total_size,
                       PVFS_id_gen_t tag,
                       enum PINT_dev_buffer_type buffer_type,
                       void *user_ptr,
                       job_aint status_user_tag,
                       job_status_s *out_status_p,
                       job_id_t* id,
                       job_context_id context_id)
{
    /* NOTE: This function will _always_ immediately complete for now.  
     * It is really just in the job interface for completeness, in case we 
     * decide later to make the function asynchronous
     */
#ifdef __PVFS2_CLIENT__
    int ret = -1;
    ret = PINT_dev_write_list(buffer_list,
                              size_list,
                              list_count,
                              total_size,
                              buffer_type,
                              tag);
    if(ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return(1);
    }

    /* immediate completion */
    out_status_p->error_code = 0;
    out_status_p->status_user_tag = status_user_tag;
    out_status_p->actual_size = total_size;
    return(1);
#else
    return(-PVFS_ENOSYS);
#endif
}


/* job_req_sched_post()
 *
 * posts a request to the request scheduler
 *
 * returns 0 on success, -errno on failure, and 1 on immediate
 * completion 
 */
int job_req_sched_post(enum PVFS_server_op op,
                       PVFS_fs_id fs_id,
                       PVFS_handle handle,
                       enum PINT_server_req_access_type access_type,
                       enum PINT_server_sched_policy sched_policy,
                       void *user_ptr,
                       job_aint status_user_tag,
                       job_status_s *out_status_p,
                       job_id_t *id,
                       job_context_id context_id)
{
    /* post a request to the scheduler.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue up
     * a job_desc structure.        
     */
    /* NOTE: this function is unique in the job interface because
     * we have to track a job id even when it completes (to be
     * matched in release function).  We will use a seperate queue
     * for this.
     */
    struct job_desc *jd = NULL;
    int ret = -1;

    jd = alloc_job_desc(JOB_REQ_SCHED);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.req_sched.post_flag = 1;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;

    ret = PINT_req_sched_post(op,
                              fs_id,
                              handle,
                              access_type,
                              sched_policy,
                              jd,
                              &(jd->u.req_sched.id));

    if (ret < 0)
    {
        /* error posting */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        gossip_debug(GOSSIP_SERVER_DEBUG, 
                     "%s: failed to post on scheduler id: %ld\n",
                     __func__, jd->job_id);
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        *id = jd->job_id;
        /* don't delete the job desc until a matching release comes through */
        gossip_debug(GOSSIP_SERVER_DEBUG, 
                     "%s: immediate completion on scheduler id: %ld\n",
                     __func__, jd->job_id);
        return (1);
    }

    /* if we hit this point, job did not immediately complete-
     * queue to test later
     */
    gossip_debug(GOSSIP_SERVER_DEBUG, 
                 "%s: deferred completion on scheduler id: %ld\n",
                 __func__, jd->job_id);
    *id = jd->job_id;

    return (0);
}

int job_req_sched_change_mode(enum PVFS_server_mode mode,
                              void *user_ptr,
                              job_aint status_user_tag,
                              job_status_s *out_status_p,
                              job_id_t *id,
                              job_context_id context_id)
{
    struct job_desc *jd = NULL;
    int ret = -1;

    jd = alloc_job_desc(JOB_REQ_SCHED);
    if(!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.req_sched.post_flag = 1;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;

    ret = PINT_req_sched_change_mode(mode, jd, &(jd->u.req_sched.id));
    if (ret < 0)
    {
        /* error posting */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        *id = jd->job_id;
        /* don't delete the job desc until a matching release comes through */
        return (1);
    }

    *id = jd->job_id;
    return (0);
}

/* job_req_sched_post_timer()
 *
 * posts a timer to the request scheduler
 *
 * returns 0 on success, -errno on failure, and 1 on immediate
 * completion 
 */
int job_req_sched_post_timer(int msecs,
                             void *user_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id)
{
    /* post a timer to the scheduler.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue up
     * a job_desc structure.        
     */

    struct job_desc *jd = NULL;
    int ret = -1;

    jd = alloc_job_desc(JOB_REQ_SCHED);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;

    ret = PINT_req_sched_post_timer(msecs, jd, &(jd->u.req_sched.id));

    if (ret < 0)
    {
        /* error posting */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    /* if we hit this point, job did not immediately complete-
     * queue to test later
     */
    if (id)
    {
        *id = jd->job_id;
    }

    return (0);
}


/* job_req_sched_release
 *
 * releases a request from the request scheduler
 *
 * returns 0 on success, -errno on failure, and 1 on immediate
 * completion
 */
int job_req_sched_release(job_id_t in_completed_id,
                          void *user_ptr,
                          job_aint status_user_tag,
                          job_status_s *out_status_p,
                          job_id_t *out_id,
                          job_context_id context_id)
{
    /* this function is a little odd.  We need to do is retrieve the
     * job desc that we queued up in the inprogress queue to match the
     * release properly
     */
    struct job_desc *match_jd = NULL;
    struct job_desc *jd = NULL;
    int ret = -1;

    jd = alloc_job_desc(JOB_REQ_SCHED);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;

    match_jd = id_gen_safe_lookup(in_completed_id);
    if (!match_jd)
    {
        /* id has been released or was not registered */
        gossip_err("Error: %s: failed to locate descriptor: %ld\n",
                   __func__, in_completed_id);
        out_status_p->error_code = -PVFS_EINVAL;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return 1;
    }

    ret = PINT_req_sched_release(match_jd->u.req_sched.id,
                                 jd,
                                 &(jd->u.req_sched.id));

    /* delete the old req sched job desc; it is no longer needed */
    dealloc_job_desc(match_jd);
    match_jd = NULL;

    if (ret < 0)
    {
        /* error posting */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = -PVFS_ENOMEM;
        gossip_err("Error: %s: failed to post id: %ld\n",
                   __func__, in_completed_id);
        return 1;
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "%s: immediate completion on scheduler id: %ld\n",
                     __func__, in_completed_id);
        return (1);
    }

    /* if we hit this point, job did not immediately complete-
     * queue to test later
     */
    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "%s: deferred completion on scheduler id: %ld\n",
                 __func__, in_completed_id);
    *out_id = jd->job_id;

    return (0);
}


/* job_flow()
 *
 * posts a job to service a flow (where a flow is a complex I/O
 * operation between two endpoints, which may be memory, disk, or
 * network)
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_flow(flow_descriptor *flow_d,
             void *user_ptr,
             job_aint status_user_tag,
             job_status_s *out_status_p,
             job_id_t *id,
             job_context_id context_id,
             int timeout_sec,
             PVFS_hint hints)
{
    struct job_desc *jd = NULL;
    int ret = -1;

    /* allocate job descriptor first */
    jd = alloc_job_desc(JOB_FLOW);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    flow_d->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.flow.flow_d = flow_d;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    flow_d->user_ptr = jd;
    flow_d->callback = flow_callback;

    /* post the flow */
    ret = PINT_flow_post(flow_d);
    if (ret < 0)
    {
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }
    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = flow_d->total_transferred;
        dealloc_job_desc(jd);
        jd = NULL;
        return (1);
    }

    /* queue up the job desc. for later completion */
    *id = jd->job_id;
    flow_pending_count++;
    gossip_debug(GOSSIP_FLOW_DEBUG, "Job flows in progress (post time): %d\n",
                 flow_pending_count);

    return(job_time_mgr_add(jd, timeout_sec));
}

/* job_flow_cancel()
 *
 * cancels a posted job that is servicing a flow (where a flow is a
 * complex I/O operation between two endpoints, which may be memory,
 * disk, or network)
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_flow_cancel(job_id_t id, job_context_id context_id)
{
    struct job_desc* query = NULL;
    int ret = -1;

    gen_mutex_lock(&completion_mutex);

    query = id_gen_safe_lookup(id);

    if (!query || query->completed_flag)
    {
        /* job has already completed, no cancellation needed */
        gen_mutex_unlock(&completion_mutex);
        return(0);
    }

    /* cancel flow.  Normal completion path through flow callback will still
     * occur; no more work to do here.  NOTE: flow checks for races against
     * flow descriptors for which the callback process has already started
     */
    ret = PINT_flow_cancel(query->u.flow.flow_d);

    gen_mutex_unlock(&completion_mutex);

    return(ret);
}

int job_trove_bstream_write_list(TROVE_coll_id coll_id,
                                 TROVE_handle handle,
                                 char **mem_offset_array,
                                 TROVE_size *mem_size_array,
                                 int mem_count,
                                 TROVE_offset *stream_offset_array,
                                 TROVE_size *stream_size_array,
                                 int stream_count,
                                 TROVE_size *out_size_p,
                                 TROVE_ds_flags flags,
                                 TROVE_vtag_s *vtag,
                                 void *user_ptr,
                                 job_aint status_user_tag,
                                 job_status_s *out_status_p,
                                 job_id_t *id,
                                 job_context_id context_id,
                                 PVFS_hint hints)
{
    /* post a trove write.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->hints = hints;
    jd->u.trove.vtag = vtag;
    jd->u.trove.out_size_p = out_size_p;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_bstream_write_list(coll_id,
                                   handle,
                                   mem_offset_array,
                                   mem_size_array,
                                   mem_count,
                                   stream_offset_array,
                                   stream_size_array,
                                   stream_count,
                                   out_size_p,
                                   flags,
                                   vtag,
                                   user_ptr_internal,
                                   global_trove_context,
                                   &(jd->u.trove.id),
                                   hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = *out_size_p;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
} 

int job_trove_bstream_read_list(PVFS_fs_id coll_id,
                                PVFS_handle handle,
                                char **mem_offset_array,
                                PVFS_size *mem_size_array,
                                int mem_count,
                                PVFS_offset *stream_offset_array,
                                PVFS_size *stream_size_array,
                                int stream_count,
                                PVFS_size *out_size_p,
                                PVFS_ds_flags flags,
                                PVFS_vtag *vtag,
                                void *user_ptr,
                                job_aint status_user_tag,
                                job_status_s *out_status_p,
                                job_id_t *id,
                                job_context_id context_id,
                                PVFS_hint hints)
{
    /* post a trove read.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->u.trove.out_size_p = out_size_p;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_bstream_read_list(coll_id,
                                  handle,
                                  mem_offset_array,
                                  mem_size_array,
                                  mem_count,
                                  stream_offset_array,
                                  stream_size_array,
                                  stream_count,
                                  out_size_p,
                                  flags,
                                  jd->u.trove.vtag,
                                  user_ptr_internal,
                                  global_trove_context,
                                  &(jd->u.trove.id),
                                  hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->actual_size = *out_size_p;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_bstream_flush()
 *
 * ask the storage layer to flush data to disk
 *
 * returns 0 on success, 1 on immediate completion, and -errno on failure
 */

int job_trove_bstream_flush(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s *out_status_p,
                            job_id_t *id,
                            job_context_id context_id,
                            PVFS_hint hints)

{
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_bstream_flush(coll_id,
                              handle,
                              flags,
                              user_ptr_internal,
                              global_trove_context,
                              &(jd->u.trove.id),
                              hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }
    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }
    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_keyval_read()
 *
 * storage key/value read 
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_read(PVFS_fs_id coll_id,
                          PVFS_handle handle,
                          PVFS_ds_keyval *key_p,
                          PVFS_ds_keyval *val_p,
                          PVFS_ds_flags flags,
                          PVFS_vtag *vtag,
                          void *user_ptr,
                          job_aint status_user_tag,
                          job_status_s *out_status_p,
                          job_id_t *id,
                          job_context_id context_id,
                          PVFS_hint hints)
{
    //gossip_debug(GOSSIP_TROVE_DEBUG, "%s: coll_id %d\n", __func__, coll_id);

    /* post a trove keyval read.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_read(coll_id,
                            handle,
                            key_p,
                            val_p,
                            flags,
                            jd->u.trove.vtag,
                            user_ptr_internal,
                            global_trove_context,
                            &(jd->u.trove.id),
                            hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_keyval_read_list()
 *
 * storage key/value read list
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_read_list(PVFS_fs_id coll_id,
                               PVFS_handle handle,
                               PVFS_ds_keyval *key_array,
                               PVFS_ds_keyval *val_array,
                               PVFS_error *err_array,
                               int count,
                               PVFS_ds_flags flags,
                               PVFS_vtag *vtag,
                               void *user_ptr,
                               job_aint status_user_tag,
                               job_status_s *out_status_p,
                               job_id_t *id,
                               job_context_id context_id,
                               PVFS_hint hints)
{
    /* post a trove keyval read.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_read_list(coll_id,
                                 handle,
                                 key_array,
                                 val_array,
                                 err_array,
                                 count,
                                 flags,
                                 jd->u.trove.vtag,
                                 user_ptr_internal,
                                 global_trove_context,
                                 &(jd->u.trove.id),
                                 hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_keyval_write()
 *
 * storage key/value write
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_write(PVFS_fs_id coll_id,
                           PVFS_handle handle,
                           PVFS_ds_keyval *key_p,
                           PVFS_ds_keyval *val_p,
                           PVFS_ds_flags flags,
                           PVFS_vtag *vtag,
                           void *user_ptr,
                           job_aint status_user_tag,
                           job_status_s *out_status_p,
                           job_id_t *id,
                           job_context_id context_id,
                           PVFS_hint hints)
{
    /* post a trove keyval write.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_write(coll_id,
                             handle,
                             key_p,
                             val_p,
                             flags,
                             jd->u.trove.vtag,
                             user_ptr_internal,
                             global_trove_context,
                             &(jd->u.trove.id),
                             hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_keyval_write_list()
 *
 * storage key/value list write
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_write_list(PVFS_fs_id coll_id,
                           PVFS_handle handle,
                           PVFS_ds_keyval *key_array,
                           PVFS_ds_keyval *val_array,
                           int32_t count,
                           PVFS_ds_flags flags,
                           PVFS_vtag *vtag,
                           void *user_ptr,
                           job_aint status_user_tag,
                           job_status_s *out_status_p,
                           job_id_t *id,
                           job_context_id context_id,
                           PVFS_hint hints)
{
    /* post a trove keyval write.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void *user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    gossip_debug(GOSSIP_JOB_DEBUG, "job_trove_keyval_write_list() posting trove_keyval_write_list()\n");
    ret = trove_keyval_write_list(coll_id,
                                  handle,
                                  key_array,
                                  val_array,
                                  count,
                                  flags,
                                  jd->u.trove.vtag,
                                  user_ptr_internal,
                                  global_trove_context,
                                  &(jd->u.trove.id),
                                  hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

int job_trove_keyval_remove_list(PVFS_fs_id coll_id,
                                  PVFS_handle handle,
                                  PVFS_ds_keyval *key_array,
                                  PVFS_ds_keyval *val_array,
                                  int *error_array,
                                  int count,
                                  PVFS_ds_flags flags,
                                  PVFS_vtag *vtag,
                                  void *user_ptr,
                                  job_aint status_user_tag,
                                  job_status_s *out_status_p,
                                  job_id_t *id,
                                  job_context_id context_id,
                                  PVFS_hint hints)
{
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_remove_list(coll_id,
                                   handle,
                                   key_array,
                                   val_array,
                                   error_array,
                                   count,
                                   flags,
                                   jd->u.trove.vtag,
                                   user_ptr_internal, 
                                   global_trove_context,
                                   &(jd->u.trove.id),
                                   hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}


/* job_trove_keyval_flush()
 *
 * ask the storage layer to flush keyvals to disk
 *
 * returns 0 on success, 1 on immediate completion, and -errno on failure
 */

int job_trove_keyval_flush(PVFS_fs_id coll_id,
                           PVFS_handle handle,
                           PVFS_ds_flags flags,
                           void *user_ptr,
                           job_aint status_user_tag,
                           job_status_s *out_status_p,
                           job_id_t *id,
                           job_context_id context_id,
                           PVFS_hint hints)
{
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_flush(coll_id,
                             handle,
                             flags,
                             user_ptr_internal,
                             global_trove_context,
                             &(jd->u.trove.id),
                             hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

int job_trove_keyval_get_handle_info(PVFS_fs_id coll_id,
                                     PVFS_handle handle,
                                     PVFS_ds_flags flags,
                                     PVFS_ds_keyval_handle_info *info,
                                     void *user_ptr,
                                     job_aint status_user_tag,
                                     job_status_s *out_status_p,
                                     job_id_t *id,
                                     job_context_id context_id,
                                     PVFS_hint hints)
{
    /* post a trove operation keyval get handle info.  If it completes (or
     * fails) immediately, then return and fill in the status
     * structure.  If it needs to be tested for completion later,
     * then queue up a job desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;



#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_get_handle_info(coll_id,
                                       handle,
                                       flags,
                                       info,
                                       user_ptr_internal,
                                       global_trove_context,
                                       &(jd->u.trove.id),
                                       hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}


/* job_trove_dspace_getattr()
 *
 * read generic dspace attributes
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_getattr(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             void *user_ptr,
                             PVFS_ds_attributes *out_ds_attr_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id,
                             PVFS_hint hints)
{
    /* post a trove operation dspace get attr.  If it completes (or
     * fails) immediately, then return and fill in the status
     * structure.  If it needs to be tested for completion later,
     * then queue up a job desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_getattr(coll_id,
                               handle,
                               out_ds_attr_ptr,
                               0 /* flags */ ,
                               user_ptr_internal,
                               global_trove_context,
                               &(jd->u.trove.id),
                               hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_getattr_list()
 *
 * read generic dspace attributes for a set of handles
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_getattr_list(PVFS_fs_id coll_id,
                             int nhandles,
                             PVFS_handle *handle_array,
                             void *user_ptr,
                             PVFS_error *out_error_array,
                             PVFS_ds_attributes *out_ds_attr_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id,
                             PVFS_hint hints)
{
    /* post a trove operation dspace get attr list.  If it completes (or
     * fails) immediately, then return and fill in the status
     * structure.  If it needs to be tested for completion later,
     * then queue up a job desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;



#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_getattr_list(coll_id,
                                     nhandles,
                                     handle_array,
                                     out_ds_attr_ptr,
                                     out_error_array,
                                     0, /* flags */
                                     user_ptr_internal,
                                     global_trove_context,
                                     &(jd->u.trove.id),
                                     hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_setattr()
 *
 * write generic dspace attributes
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_setattr(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             PVFS_ds_attributes *ds_attr_p,
                             PVFS_ds_flags flags,
                             void *user_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id,
                             PVFS_hint hints)
{
    /* post a trove operation dspace set attr.  If it completes (or
     * fails) immediately, then return and fill in the status
     * structure.  If it needs to be tested for completion later,
     * then queue up a job desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_setattr(coll_id,
                               handle,
                               ds_attr_p,
                               flags,
                               user_ptr_internal,
                               global_trove_context,
                               &(jd->u.trove.id),
                               hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_bstream_resize()
 *
 * resize (truncate or preallocate) a storage byte stream
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_bstream_resize(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             PVFS_size size,
                             PVFS_ds_flags flags,
                             PVFS_vtag *vtag,
                             void *user_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id,
                             PVFS_hint hints)
{
    /* post a resize trove operation.  If it completes (or
     * fails) immediately, then return and fill in the status
     * structure.  If it needs to be tested for completion later,
     * then queue up a job desc structure.
     */

    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_bstream_resize(coll_id,
                               handle,
                               &size,
                               flags,
                               vtag,
                               user_ptr_internal,
                               global_trove_context,
                               &(jd->u.trove.id),
                               hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall to this point, the job did not immediately complete and
     * we must queue up to test it later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_bstream_validate()
 *
 * check consistency of a bytestream for a given vtag
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_bstream_validate(PVFS_fs_id coll_id,
                               PVFS_handle handle,
                               PVFS_vtag *vtag,
                               void *user_ptr,
                               job_aint status_user_tag,
                               job_status_s *out_status_p,
                               job_id_t *id,
                               job_context_id context_id,
                               PVFS_hint hints)
{
    gossip_lerr("Error: unimplemented.\n");
    out_status_p->error_code = -PVFS_ENOSYS;
    return 1;
}

/* job_trove_keyval_remove()
 *
 * remove a key/value entry
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_remove(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_keyval *key_p,
                            PVFS_ds_keyval *val_p,
                            PVFS_ds_flags flags,
                            PVFS_vtag *vtag,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s *out_status_p,
                            job_id_t *id,
                            job_context_id context_id,
                            PVFS_hint hints)
{
    /* post a trove keyval remove.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_remove(coll_id,
                              handle,
                              key_p,
                              val_p,
                              flags,
                              jd->u.trove.vtag,
                              user_ptr_internal,
                              global_trove_context,
                              &(jd->u.trove.id),
                              hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_keyval_validate()
 *
 * check consistency of a key/value pair for a given vtag
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_validate(PVFS_fs_id coll_id,
                              PVFS_handle handle,
                              PVFS_vtag * vtag,
                              void *user_ptr,
                              job_aint status_user_tag,
                              job_status_s *out_status_p,
                              job_id_t *id,
                              job_context_id context_id,
                              PVFS_hint hints)
{
    gossip_lerr("Error: unimplemented.\n");
    out_status_p->error_code = -PVFS_ENOSYS;
    return 1;
}

/* job_trove_keyval_iterate()
 *
 * iterate through all of the key/value pairs for a data space
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_iterate(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             PVFS_ds_position position,
                             PVFS_ds_keyval *key_array,
                             PVFS_ds_keyval *val_array,
                             int count,
                             PVFS_ds_flags flags,
                             PVFS_vtag *vtag,
                             void *user_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id,
                             PVFS_hint hints)
{
    /* post a trove keyval iterate.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->u.trove.position = position;
    jd->u.trove.count = count;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_iterate(coll_id,
                               handle,
                               &(jd->u.trove.position),
                               key_array,
                               val_array,
                               &(jd->u.trove.count),
                               flags,
                               jd->u.trove.vtag,
                               user_ptr_internal,
                               global_trove_context,
                               &(jd->u.trove.id),
                               hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        out_status_p->position = jd->u.trove.position;
        out_status_p->count = jd->u.trove.count;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_keyval_iterate_keys()
 *
 * iterate through all of the keys for a data space
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_keyval_iterate_keys(PVFS_fs_id coll_id,
                             PVFS_handle handle,
                             PVFS_ds_position position,
                             PVFS_ds_keyval * key_array,
                             int count,
                             PVFS_ds_flags flags,
                             PVFS_vtag * vtag,
                             void *user_ptr,
                             job_aint status_user_tag,
                             job_status_s *out_status_p,
                             job_id_t *id,
                             job_context_id context_id,
                             PVFS_hint hints)
{
    /* post a trove keyval iterate_keys.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->u.trove.position = position;
    jd->u.trove.count = count;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_keyval_iterate_keys(coll_id,
                                    handle,
                                    &(jd->u.trove.position),
                                    key_array,
                                    &(jd->u.trove.count),
                                    flags,
                                    jd->u.trove.vtag,
                                    user_ptr_internal,
                                    global_trove_context,
                                    &(jd->u.trove.id),
                                    hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        out_status_p->position = jd->u.trove.position;
        out_status_p->count = jd->u.trove.count;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_iterate_handles()
 *
 * iterates through all of the handles in a given collection
 *
 * returns 0 on success, 1 on immediate completion, -PVFS_error on failure
 */
int job_trove_dspace_iterate_handles(PVFS_fs_id coll_id,
                                     PVFS_ds_position position,
                                     PVFS_handle *handle_array,
                                     int count,
                                     PVFS_ds_flags flags,
                                     PVFS_vtag *vtag,
                                     void *user_ptr,
                                     job_aint status_user_tag,
                                     job_status_s *out_status_p,
                                     job_id_t *id,
                                     job_context_id context_id)
{
    /* post a trove keyval iterate_handles.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.trove.vtag = vtag;
    jd->u.trove.position = position;
    jd->u.trove.count = count;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_iterate_handles(coll_id,
                                       &(jd->u.trove.position),
                                       handle_array,
                                       &(jd->u.trove.count),
                                       flags,
                                       jd->u.trove.vtag,
                                       user_ptr_internal,
                                       global_trove_context,
                                       &(jd->u.trove.id));
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->vtag = jd->u.trove.vtag;
        out_status_p->position = jd->u.trove.position;
        out_status_p->count = jd->u.trove.count;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}


/* job_trove_dspace_create()
 *
 * create a new data space object
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_create(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_type type,
                            void *hint,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s *out_status_p,
                            job_id_t *id,
                            job_context_id context_id,
                            PVFS_hint hints)
{
    /* post a dspace create.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->u.trove.handle = PVFS_HANDLE_NULL;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;



#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_create(coll_id,
                              handle,
                              &(jd->u.trove.handle),
                              type,
                              hint,
                              flags,
                              user_ptr_internal,
                              global_trove_context,
                              &(jd->u.trove.id),
                              hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->handle = jd->u.trove.handle;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_create_list()
 *
 * create a new data space object 
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_create_list(PVFS_fs_id coll_id,
                                 PVFS_handle *handle_array,
                                 PVFS_handle *out_handle_array,
                                 int count,
                                 PVFS_ds_type type,
                                 void *hint,
                                 PVFS_ds_flags flags,
                                 void *user_ptr,
                                 job_aint status_user_tag,
                                 job_status_s *out_status_p,
                                 job_id_t *id,
                                 job_context_id context_id,
                                 PVFS_hint hints)
{
    /* post a dspace create list.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->u.trove.handle = PVFS_HANDLE_NULL;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_create_list(coll_id,
                                   handle_array,
                                   out_handle_array,
                                   count,
                                   type,
                                   hint,
                                   flags,
                                   user_ptr_internal, 
                                   global_trove_context,
                                   &(jd->u.trove.id),
                                   hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_remove_list()
 *
 * remove a list of data space objects (byte stream and key/value) 
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_remove_list(PVFS_fs_id coll_id,
			         PVFS_handle *handle_array,
                                 PVFS_error *out_error_array,
                                 int count,
                                 PVFS_ds_flags flags,
			         void *user_ptr,
			         job_aint status_user_tag,
			         job_status_s *out_status_p,
			         job_id_t *id,
			         job_context_id context_id,
                                 PVFS_hint hints)
{
    /* post a dspace remove_list.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void*)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_remove_list(coll_id,
                                   handle_array, 
                                   out_error_array,
                                   count,
                                   flags,
                                   user_ptr_internal, 
                                   global_trove_context,
                                   &(jd->u.trove.id),
                                   hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}



/* job_trove_dspace_remove()
 *
 * remove an entire data space object (byte stream and key/value)
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_remove(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s *out_status_p,
                            job_id_t *id,
                            job_context_id context_id,
                            PVFS_hint hints)
{
    /* post a dspace remove.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_remove(coll_id,
                              handle,
                              flags,
                              user_ptr_internal,
                              global_trove_context,
                              &(jd->u.trove.id),
                              hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_verify()
 *
 * verify that a given dataspace exists and discover its type
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_verify(PVFS_fs_id coll_id,
                            PVFS_handle handle,
                            PVFS_ds_flags flags,
                            void *user_ptr,
                            job_aint status_user_tag,
                            job_status_s *out_status_p,
                            job_id_t *id,
                            job_context_id context_id,
                            PVFS_hint hints)
{
    /* post a dspace verify.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_verify(coll_id,
                              handle,
                              &jd->u.trove.type,
                              flags,
                              user_ptr_internal,
                              global_trove_context,
                              &(jd->u.trove.id),
                              hints);
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        /* the trove_method will determine what value is returned in immediate
         * completion case */
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_dspace_cancel()
 *
 * used to cancel a trove dspace operation in progress
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_dspace_cancel(PVFS_fs_id coll_id,
                            job_id_t id,
                            job_context_id context_id)
{
    struct job_desc* query = NULL;
    int ret = -1;

    gen_mutex_lock(&completion_mutex);

    query = id_gen_safe_lookup(id);
    if (!query || query->completed_flag)
    {
        /* job has already completed, no cancellation needed */
        gen_mutex_unlock(&completion_mutex);
        return(0);
    }

    /* tell thread mgr to cancel operation.  This will result in normal
     * completion path through thread mgr callbacks; no more work to do here */
    ret = PINT_thread_mgr_trove_cancel(query->u.trove.id,
                                       coll_id,
                                       &(query->trove_callback));

    gen_mutex_unlock(&completion_mutex);

    return(ret);
}


/* job_trove_fs_create()
 *
 * create a new file system
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_fs_create(char *collname,
                        PVFS_fs_id new_coll_id,
                        void *user_ptr,
                        job_aint status_user_tag,
                        job_status_s *out_status_p,
                        job_id_t *id,
                        job_context_id context_id)
{
    /* post an fs create.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_collection_create(collname,
                                  new_coll_id,
                                  user_ptr_internal,
                                  &(jd->u.trove.id));
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_fs_remove()
 *
 * remove an existing file system
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_fs_remove(char *collname,
                        void *user_ptr,
                        job_aint status_user_tag,
                        job_status_s *out_status_p,
                        job_id_t *id,
                        job_context_id context_id)
{
    gossip_lerr("Error: unimplemented.\n");
    out_status_p->error_code = -PVFS_ENOSYS;
    return 1;
}

/* job_trove_fs_lookup()
 *
 * lookup a file system based on a string name
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_fs_lookup(char *collname,
                        void *user_ptr,
                        job_aint status_user_tag,
                        job_status_s *out_status_p,
                        job_id_t *id,
                        job_context_id context_id)
{
    /* post a collection lookup.  If it completes (or fails) immediately, then
     * return and fill in the status structure.  If it needs to be tested
     * for completion later, then queue up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collname,
                                  &(jd->u.trove.fsid),
                                  user_ptr_internal,
                                  &(jd->u.trove.id));
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        out_status_p->coll_id = jd->u.trove.fsid;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* there is no way we can test on this if we don't know the coll_id */
    gossip_lerr("Error: trove_collection_lookup() returned 0 ???\n");

    out_status_p->error_code = -PVFS_EINVAL;
    return 1;
}

/* job_trove_fs_set_eattr()
 *
 * sets extended attributes for a file system
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_fs_seteattr(PVFS_fs_id coll_id,
                          PVFS_ds_keyval *key_p,
                          PVFS_ds_keyval *val_p,
                          PVFS_ds_flags flags,
                          void *user_ptr,
                          job_aint status_user_tag,
                          job_status_s *out_status_p,
                          job_id_t *id,
                          job_context_id context_id,
                          PVFS_hint hints)
{
    /* post a trove collection set eattr.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.
     */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_collection_seteattr(coll_id,
                                    key_p,
                                    val_p,
                                    flags,
                                    user_ptr_internal,
                                    global_trove_context,
                                    &(jd->u.trove.id));
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_trove_fs_get_eattr()
 *
 * gets extended attributes for a file system
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_fs_geteattr(PVFS_fs_id coll_id,
                          PVFS_ds_keyval *key_p,
                          PVFS_ds_keyval *val_p,
                          PVFS_ds_flags flags,
                          void *user_ptr,
                          job_aint status_user_tag,
                          job_status_s *out_status_p,
                          job_id_t *id,
                          job_context_id context_id,
                          PVFS_hint hints)
{
    /* post a trove collection get eattr.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_collection_geteattr(coll_id,
                                    key_p,
                                    val_p,
                                    flags,
                                    user_ptr_internal,
                                    global_trove_context,
                                    &(jd->u.trove.id));
#else
    gossip_err("Error: Trove support not enabled.\n");
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}


/* job_trove_fs_del_eattr()
 *
 * delete extended attribute for a file system
 *
 * returns 0 on success, 1 on immediate completion, and -errno on
 * failure
 */
int job_trove_fs_deleattr(PVFS_fs_id coll_id,
                          PVFS_ds_keyval *key_p,
                          PVFS_ds_flags flags,
                          void *user_ptr,
                          job_aint status_user_tag,
                          job_status_s *out_status_p,
                          job_id_t *id,
                          job_context_id context_id,
                          PVFS_hint hints)
{
    /* post a trove collection del eattr.  If it completes (or fails)
     * immediately, then return and fill in the status structure.
     * If it needs to be tested for completion later, then queue
     * up a job_desc structure.  */
    int ret = -1;
    struct job_desc *jd = NULL;
    void* user_ptr_internal GCC_UNUSED;

    /* create the job desc first, even though we may not use it.  This
     * gives us somewhere to store the BMI id and user ptr
     */
    jd = alloc_job_desc(JOB_TROVE);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->hints = hints;
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->trove_callback.fn = trove_thread_mgr_callback;
    jd->trove_callback.data = (void *)jd;
    user_ptr_internal = &jd->trove_callback;

#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_collection_deleattr(coll_id,
                                    key_p,
                                    flags,
                                    user_ptr_internal,
                                    global_trove_context,
                                    &(jd->u.trove.id));
#else
    gossip_err("%s: error: Trove support not enabled.\n", __func__);
    ret = -ENOSYS;
#endif

    if (ret < 0)
    {
        /* error posting trove operation */
        dealloc_job_desc(jd);
        jd = NULL;
        out_status_p->error_code = ret;
        out_status_p->status_user_tag = status_user_tag;
        return (1);
    }

    if (ret == 1)
    {
        /* immediate completion */
        out_status_p->error_code = 0;
        out_status_p->status_user_tag = status_user_tag;
        dealloc_job_desc(jd);
        jd = NULL;
        return (ret);
    }

    /* if we fall through to this point, the job did not
     * immediately complete and we must queue up to test later
     */
    *id = jd->job_id;
    trove_pending_count++;

    return (0);
}

/* job_null()
 *
 * post null job; can be used to trigger asynchronous state transitions
 * without doing any underlying work
 *
 * returns 0 on success, -PVFS_error on failure
 * NOTE: immediate completion not allowed here
 */
int job_null(int error_code,
             void *user_ptr,
             job_aint status_user_tag,
             job_status_s *out_status_p,
             job_id_t *id,
             job_context_id context_id)
{
    struct job_desc *jd = NULL;

    jd = alloc_job_desc(JOB_NULL);
    if (!jd)
    {
        out_status_p->error_code = -PVFS_ENOMEM;
        return 1;
    }
    jd->job_user_ptr = user_ptr;
    jd->context_id = context_id;
    jd->status_user_tag = status_user_tag;
    jd->u.null_info.error_code = error_code;

    gen_mutex_lock(&completion_mutex);
    job_desc_q_add(completion_queue_array[jd->context_id], jd);
    /* set completed flag while holding queue lock */
    jd->completed_flag = 1;
#ifdef __PVFS2_JOB_THREADED__
    /* wake up anyone waiting for completion */
    pthread_cond_signal(&completion_cond);
#endif
    gen_mutex_unlock(&completion_mutex);

    return(0);
}


/* job_test()
 *
 * check for completion of a particular job, don't return until
 * either job completes or timeout expires
 *
 * returns 0 if nothing done, 1 if something done, -errno on failure
 */
int job_test(job_id_t id,
             int *out_count_p,
             void **returned_user_ptr_p,
             job_status_s *out_status_p,
             int timeout_ms,
             job_context_id context_id)
{
    int ret = -1;
    int tmp_index;

    *out_count_p = 1;

    /* job_test() is really just a special case of job_testsome() */
    ret = job_testsome(&id,
                       out_count_p,
                       &tmp_index,
                       returned_user_ptr_p,
                       out_status_p,
                       timeout_ms,
                       context_id);

    return(ret);
}

#ifdef __PVFS2_JOB_THREADED__

/* job_testsome()
 *
 * check for completion of a set of jobs, don't return until
 * either all jobs complete or timeout expires
 *
 * returns 0 on success, -errno on failure
 */
int job_testsome(job_id_t *id_array,
                 int *inout_count_p,
                 int *out_index_array,
                 void **returned_user_ptr_array,
                 job_status_s *out_status_array_p,
                 int timeout_ms,
                 job_context_id context_id)
{
    int ret = -1;
    struct timespec pthread_timeout;
    struct timeval start;
    int original_count = *inout_count_p;
    int pthread_ret = -1;

    /* use this as a chance to do a cheap test on the request
     * scheduler
     */
    if ((ret = do_one_test_cycle_req_sched()) < 0)
    {
        return (ret);
    }

    /* figure out how long to wait if we need to */
    if(timeout_ms > 0)
    {
        ret = gettimeofday(&start, NULL);
        if (ret < 0)
        {
            return (ret);
        }
        pthread_timeout.tv_sec = start.tv_sec + timeout_ms / 1000;
        pthread_timeout.tv_nsec = (start.tv_usec + ((timeout_ms % 1000)*1000))*1000;
        if (pthread_timeout.tv_nsec > 1000000000)
        {
            pthread_timeout.tv_nsec = pthread_timeout.tv_nsec - 1000000000;
            pthread_timeout.tv_sec++;
        }
    }

    /* check for completed jobs */
    gen_mutex_lock(&completion_mutex);
    pthread_ret = 0;
    while(((ret = completion_query_some(id_array,
                                        inout_count_p,
                                        out_index_array,
                                        returned_user_ptr_array,
                                        out_status_array_p)) == 0) &&
          ((pthread_ret == EINTR) || (pthread_ret == 0)))
    {
        *inout_count_p = original_count;

        if(timeout_ms > 0)
        {
            pthread_ret = pthread_cond_timedwait(&completion_cond,
                                                 &completion_mutex,
                                                 &pthread_timeout);
        }
        else if(timeout_ms == 0)
        {
            pthread_ret = ETIMEDOUT;
        }
        else
        {
            /* block indefinitely */
            pthread_ret = pthread_cond_wait(&completion_cond,
                                            &completion_mutex);
        }
    }
    gen_mutex_unlock(&completion_mutex);

    if(ret == 0)
    {
        *inout_count_p = 0;

        if ((pthread_ret != 0) && (pthread_ret != EINTR) &&
            (pthread_ret != EINVAL) && pthread_ret != ETIMEDOUT)
        {
            /* pthread_cond_wait() gave a weird return code; pass along to
             * caller
             */
            ret = pthread_ret;
        }
    }

    return(ret);
}

#else /* __PVFS2_JOB_THREADED__ */

/* job_testsome()
 *
 * check for completion of a set of jobs, don't return until
 * either all jobs complete or timeout expires
 *
 * returns 0 if nothing done, 1 if something done, -errno on failure
 */
int job_testsome(job_id_t *id_array,
                 int *inout_count_p,
                 int *out_index_array,
                 void **returned_user_ptr_array,
                 job_status_s *out_status_array_p,
                 int timeout_ms,
                 job_context_id context_id)
{
    int ret = -1;
    struct timeval target_time;
    struct timeval end;
    int original_count = *inout_count_p;
    int time_exhaust_flag = 0;

    /* use this as a chance to do a cheap test on the request
     * scheduler
     */
    if ((ret = do_one_test_cycle_req_sched()) < 0)
    {
        return (ret);
    }

    /* check before we do anything else to see if the completion queue
     * has anything in it
     */
    gen_mutex_lock(&completion_mutex);
    ret = completion_query_some(id_array,
                                inout_count_p,
                                out_index_array,
                                returned_user_ptr_array,
                                out_status_array_p);
    gen_mutex_unlock(&completion_mutex);
    /* return here on error or completion */
    if (ret < 0)
    {
        return (ret);
    }
    if (ret > 0)
    {
        return (1);
    }

    *inout_count_p = original_count;

    /* figure out when the function should time out */
    if(timeout_ms > 0)
    {
        ret = gettimeofday(&target_time, NULL);
        if (ret < 0)
        {
            return (ret);
        }
        target_time.tv_sec += (timeout_ms / 1000);
        target_time.tv_usec += (timeout_ms % 1000) * 1000;
        if(target_time.tv_usec > 1000000)
        {
            target_time.tv_sec++;
            target_time.tv_usec -= 1000000;
        }
    }

    /* if we fall through to this point, then we need to just try
     * to eat up the timeout until the jobs that we want hit the
     * completion queue
     */
    do
    {

        if (timeout_ms)
        {
            do_one_work_cycle_all(10);
        }
        else
        {
            do_one_work_cycle_all(0);
        }

        /* check queue now to see if anything is done */
        gen_mutex_lock(&completion_mutex);
        ret = completion_query_some(id_array,
                                    inout_count_p,
                                    out_index_array,
                                    returned_user_ptr_array,
                                    out_status_array_p);
        gen_mutex_unlock(&completion_mutex);
        /* return here on error or completion */
        if (ret < 0)
        {
            return (ret);
        }
        if (ret  > 0)
        {
            return (1);
        }

        *inout_count_p = original_count;

        /* if we reach this point, decide if we timeout or continue */
        if(timeout_ms == 0)
        {
            time_exhaust_flag = 1;
        }
        else if(timeout_ms < 0)
        {
            time_exhaust_flag = 0;
        }
        else
        {
            ret = gettimeofday(&end, NULL);
            if (ret < 0)
            {
                return (ret);
            }

            /* compare current time with our projected timeout point */
            if((end.tv_sec > target_time.tv_sec) ||
               ((end.tv_sec == target_time.tv_sec) &&
                (end.tv_usec >= target_time.tv_usec)))
            {
                time_exhaust_flag = 1;
            }
            else
            {
                time_exhaust_flag = 0;
            }
        }

    } while (!time_exhaust_flag);

    /* fall through, nothing done, time is used up */
    *inout_count_p = 0;

    return (0);
}
#endif /* __PVFS2_JOB_THREADED__ */

#ifdef __PVFS2_JOB_THREADED__
/* job_testcontext()
 *
 * check for completion of any jobs currently in progress.  Don't return
 * until either at least one job has completed or the timeout has
 * expired
 *
 * returns 0 on success, -errno on failure
 */
int job_testcontext(job_id_t *out_id_array_p,
                    int *inout_count_p,
                    void **returned_user_ptr_array,
                    job_status_s *out_status_array_p,
                    int timeout_ms,
                    job_context_id context_id)
{
    int ret = -1;
    struct timespec pthread_timeout;
    struct timeval start;
    int original_count = *inout_count_p;
    int pthread_ret = -1;

    /* use this as a chance to do a cheap test on the request
     * scheduler
     */
    if ((ret = do_one_test_cycle_req_sched()) < 0)
    {
        return (ret);
    }

    /* figure out how long to wait if we need to */
    if(timeout_ms > 0)
    {
        ret = gettimeofday(&start, NULL);
        if (ret < 0)
        {
            return (ret);
        }
        pthread_timeout.tv_sec = start.tv_sec + timeout_ms / 1000;
        pthread_timeout.tv_nsec =
                        (start.tv_usec + ((timeout_ms % 1000) * 1000)) * 1000;
        if (pthread_timeout.tv_nsec > 1000000000)
        {
            pthread_timeout.tv_nsec = pthread_timeout.tv_nsec - 1000000000;
            pthread_timeout.tv_sec++;
        }
    }

    /* check for completed jobs */
    gen_mutex_lock(&completion_mutex);
    pthread_ret = 0;
    while(((ret = completion_query_context(
                             out_id_array_p,
                             inout_count_p,
                             returned_user_ptr_array,
                             out_status_array_p, context_id)) == 0) &&
                             ((pthread_ret == EINTR) || (pthread_ret == 0)))
    {
        *inout_count_p = original_count;

        if(timeout_ms > 0)
        {
            pthread_ret = pthread_cond_timedwait(&completion_cond,
                                                 &completion_mutex,
                                                 &pthread_timeout);
        }
        else if(timeout_ms == 0)
        {
            pthread_ret = ETIMEDOUT;
        }
        else
        {
            /* block indefinitely */
            pthread_ret = pthread_cond_wait(&completion_cond,
                                            &completion_mutex);
        }
    }
    gen_mutex_unlock(&completion_mutex);

    if(ret == 0)
    {
        *inout_count_p = 0;

        if ((pthread_ret != 0) && (pthread_ret != EINTR) &&
            (pthread_ret != EINVAL) && pthread_ret != ETIMEDOUT)
        {
            /* pthread_cond_wait() gave a weird return code; pass along to
             * caller
             */
            ret = pthread_ret;
        }
    }

    return(ret);
}

#else /* __PVFS2_JOB_THREADED__ */

/* job_testcontext()
 *
 * check for completion of any jobs currently in progress.  Don't return
 * until either at least one job has completed or the timeout has
 * expired
 *
 * returns 0 on success, -errno on failure
 */
int job_testcontext(job_id_t *out_id_array_p,
                    int *inout_count_p,
                    void **returned_user_ptr_array,
                    job_status_s *out_status_array_p,
                    int timeout_ms,
                    job_context_id context_id)
{
    int ret = -1;
    struct timeval target_time;
    struct timeval end;
    int original_count = *inout_count_p;
    int time_exhaust_flag = 0;

    /* use this as a chance to do a cheap test on the request
     * scheduler
     */
    if ((ret = do_one_test_cycle_req_sched()) < 0)
    {
        return (ret);
    }

    /* check before we do anything else to see if the completion queue
     * has anything in it
     */
    gen_mutex_lock(&completion_mutex);
    ret = completion_query_context(out_id_array_p,
                                   inout_count_p,
                                   returned_user_ptr_array,
                                   out_status_array_p,
                                   context_id);
      gen_mutex_unlock(&completion_mutex);
    /* return here on error or completion */
    if (ret < 0)
    {
        return (ret);
    }
    if (ret > 0)
    {
        return (1);
    }

    *inout_count_p = original_count;

    /* figure out when the function should time out */
    if(timeout_ms > 0)
    {
        ret = gettimeofday(&target_time, NULL);
        if (ret < 0)
        {
            return (ret);
        }
        target_time.tv_sec += (timeout_ms / 1000);
        target_time.tv_usec += (timeout_ms % 1000) * 1000;
        if(target_time.tv_usec > 1000000)
        {
            target_time.tv_sec++;
            target_time.tv_usec -= 1000000;
        }
    }

    /* if we fall through to this point, then we need to just try
     * to eat up the timeout until the jobs that we want hit the
     * completion queue
     */
    do
    {

        if (timeout_ms)
        {
            do_one_work_cycle_all(10);
        }
        else
        {
            do_one_work_cycle_all(0);
        }

        /* check queue now to see if anything is done */
        gen_mutex_lock(&completion_mutex);
        ret = completion_query_context(out_id_array_p,
                                       inout_count_p,
                                       returned_user_ptr_array,
                                       out_status_array_p,
                                       context_id);

        gen_mutex_unlock(&completion_mutex);
        /* return here on error or completion */
        if (ret < 0)
        {
            return (ret);
        }
        if (ret > 0)
        {
            return (1);
        }

        *inout_count_p = original_count;

        /* if we reach this point, decide if we timeout or continue */
        if(timeout_ms == 0)
        {
            time_exhaust_flag = 1;
        }
        else if(timeout_ms < 0)
        {
            time_exhaust_flag = 0;
        }
        else
        {
            ret = gettimeofday(&end, NULL);
            if (ret < 0)
            {
                return (ret);
            }

            /* compare current time with our projected timeout point */
            if((end.tv_sec > target_time.tv_sec) ||
               ((end.tv_sec == target_time.tv_sec) &&
                (end.tv_usec >= target_time.tv_usec)))
            {
                time_exhaust_flag = 1;
            }
            else
            {
                time_exhaust_flag = 0;
            }
        }

    } while (!time_exhaust_flag);

    /* fall through, nothing done, time is used up */
    *inout_count_p = 0;

    return (0);
}
#endif /* __PVFS2_JOB_THREADED__ */


/*********************************************************
 * Internal utility functions
 */


/* setup_queues()
 *
 * initializes all of the job queues needed by the interface
 *
 * returns 0 on success, -errno on failure
 */
static int setup_queues(void)
{

    gen_mutex_lock(&bmi_unexp_mutex);
    bmi_unexp_queue = job_desc_q_new();
    gen_mutex_unlock(&bmi_unexp_mutex);

    gen_mutex_lock(&dev_unexp_mutex);
    dev_unexp_queue = job_desc_q_new();
    gen_mutex_unlock(&dev_unexp_mutex);

    if (!bmi_unexp_queue || !dev_unexp_queue)
    {
        /* cleanup any that were initialized */
        teardown_queues();
        return (-ENOMEM);
    }
    return (0);
}

/* teardown_queues()
 *
 * tears down any existing queues used by the job interface
 *
 * no return value
 */
static void teardown_queues(void)
{

    gen_mutex_lock(&bmi_unexp_mutex);
    if (bmi_unexp_queue)
    {
        job_desc_q_cleanup(bmi_unexp_queue);
    }
    gen_mutex_unlock(&bmi_unexp_mutex);

    gen_mutex_lock(&dev_unexp_mutex);
    if (dev_unexp_queue)
    {
        job_desc_q_cleanup(dev_unexp_queue);
    }
    gen_mutex_unlock(&dev_unexp_mutex);

    return;
}

/* trove_thread_mgr_callback()
 *
 * callback function executed by the thread manager for Trove when a Trove 
 * job completes
 *
 * no return value
 */
static void trove_thread_mgr_callback( void* data, PVFS_error error_code)
{
    struct job_desc* tmp_desc = (struct job_desc*)data; 
    assert(tmp_desc);

    gen_mutex_lock(&initialized_mutex);
    if(initialized == 0)
    {
        /* The job interface has been shutdown.  Silently ignore callback. */
        gen_mutex_unlock(&initialized_mutex);
        return;
    }
    gen_mutex_unlock(&initialized_mutex);

    gen_mutex_lock(&completion_mutex);
    if (tmp_desc->completed_flag == 0)
    {
        /* set job descriptor fields and put into completion queue */
        tmp_desc->u.trove.state = error_code;
        job_desc_q_add(completion_queue_array[tmp_desc->context_id], tmp_desc);
        /* set completed flag while holding queue lock */
        tmp_desc->completed_flag = 1;

/* the value of trove_pending_count is only used in the non-threaded
 * situation. so, to prevent reported data races from helgrind, we
 * will only modify it's value in the non-threaded case.
*/
#ifndef __PVFS2_JOB_THREADED__
        trove_pending_count--;
#endif

#ifdef __PVFS2_JOB_THREADED__
        /* wake up anyone waiting for completion */
        pthread_cond_signal(&completion_cond);
#endif
    }
    gen_mutex_unlock(&completion_mutex);
}

/* bmi_thread_mgr_callback()
 *
 * callback function executed by the thread manager for BMI when a BMI
 * job completes
 *
 * no return value
 */
static void bmi_thread_mgr_callback(void* data,
                                    PVFS_size actual_size,
                                    PVFS_error error_code)
{
    struct job_desc* tmp_desc = (struct job_desc*)data;
    assert(tmp_desc);

    gen_mutex_lock(&initialized_mutex);
    if (initialized == 0)
    {
        /* The job interface has been shutdown.  Silently ignore callback. */
        gen_mutex_unlock(&initialized_mutex);
        return;
    }
    gen_mutex_unlock(&initialized_mutex);

    gen_mutex_lock(&completion_mutex);
    if (tmp_desc->completed_flag == 0)
    {
        /* set job descriptor fields and put into completion queue */
        tmp_desc->u.bmi.error_code = error_code;
        tmp_desc->u.bmi.actual_size = actual_size;
        job_desc_q_add(completion_queue_array[tmp_desc->context_id], tmp_desc);
        /* set completed flag while holding queue lock */
        tmp_desc->completed_flag = 1;

        bmi_pending_count--;

#ifdef __PVFS2_JOB_THREADED__
        /* wake up anyone waiting for completion */
        pthread_cond_signal(&completion_cond);
#endif
    }
    gen_mutex_unlock(&completion_mutex);
}

/* bmi_thread_mgr_unexp_handler()
 *
 * callback function executed by the thread manager for BMI when an unexpected
 * BMI message arrives
 *
 * no return value
 */
static void bmi_thread_mgr_unexp_handler(struct BMI_unexpected_info* unexp)
{
    struct job_desc* tmp_desc = NULL;

    gen_mutex_lock(&initialized_mutex);
    if(initialized == 0)
    {
        /* The job interface has been shutdown.  Silently ignore callback. */
        gen_mutex_unlock(&initialized_mutex);
        return;
    }
    gen_mutex_unlock(&initialized_mutex);

    gen_mutex_lock(&bmi_unexp_mutex);

    /* remove the operation from the pending bmi_unexp queue */
    tmp_desc = job_desc_q_shownext(bmi_unexp_queue);
    assert(tmp_desc != NULL);
    if (tmp_desc->completed_flag == 0)
    {
        job_desc_q_remove(tmp_desc);
        bmi_unexp_pending_count--;
        gen_mutex_unlock(&bmi_unexp_mutex);

        /* set appropriate fields and store in completed queue */
        *(tmp_desc->u.bmi_unexp.info) = *unexp;
        gen_mutex_lock(&completion_mutex);

        /* set completed flag while holding queue lock */
        tmp_desc->completed_flag = 1;
        if (completion_queue_array[tmp_desc->context_id])
        {
            job_desc_q_add(completion_queue_array[tmp_desc->context_id],
                           tmp_desc);
        }

#ifdef __PVFS2_JOB_THREADED__
        /* wake up anyone waiting for completion */
        pthread_cond_signal(&completion_cond);
#endif
        gen_mutex_unlock(&completion_mutex);
    }
    else
    {
        gen_mutex_unlock(&bmi_unexp_mutex);
    }
}

#ifdef __PVFS2_CLIENT__
/* dev_thread_mgr_unexp_handler()
 *
 * callback function executed by the thread manager for dev when an unexpected
 * device message arrives
 *
 * no return value
 */
static void dev_thread_mgr_unexp_handler(struct PINT_dev_unexp_info* unexp)
{
    struct job_desc* tmp_desc = NULL;

    gen_mutex_lock(&dev_unexp_mutex);

    /* remove the operation from the pending dev_unexp queue */
    tmp_desc = job_desc_q_shownext(dev_unexp_queue);

    /* if the thread mgr accounting is accurate, then there _must_ be a
     * dev_unexp job posted for us to hit this point.
     */
    assert(tmp_desc != NULL);
    if (tmp_desc->completed_flag == 0)
    {
        job_desc_q_remove(tmp_desc);
        dev_unexp_pending_count--;
        gen_mutex_unlock(&dev_unexp_mutex);

        /* set appropriate fields and store in completed queue */
        *(tmp_desc->u.dev_unexp.info) = *unexp;
        gen_mutex_lock(&completion_mutex);

        /* set completed flag while holding queue lock */
        tmp_desc->completed_flag = 1;
        if (completion_queue_array[tmp_desc->context_id])
        {
            job_desc_q_add(completion_queue_array[tmp_desc->context_id],
                           tmp_desc);
        }

#ifdef __PVFS2_JOB_THREADED__
        /* wake up anyone waiting for completion */
        pthread_cond_signal(&completion_cond);
#endif
        gen_mutex_unlock(&completion_mutex);
    }
    else
    {
        gen_mutex_unlock(&dev_unexp_mutex);
    }
}
#endif /* __PVFS2_CLIENT__ */

/* fill_status()
 *
 * fills in the completion status based on the given job descriptor
 *
 * no return value
 */
static void fill_status(struct job_desc *jd,
                        void **returned_user_ptr_p,
                        job_status_s *status)
{
    assert(jd);
    assert(status);

#if 0
    gossip_debug(GOSSIP_JOB_DEBUG,
        "job fill_status() for id: %llu, type: %d\n",
        llu(jd->job_id), jd->type);
#endif

    status->status_user_tag = jd->status_user_tag;

    if (returned_user_ptr_p)
    {
        *returned_user_ptr_p = jd->job_user_ptr;
    }
    switch (jd->type)
    {
    case JOB_BMI:
        job_time_mgr_rem(jd);
        status->error_code = jd->u.bmi.error_code;
        status->actual_size = jd->u.bmi.actual_size;
        break;
    case JOB_BMI_UNEXP:
        status->error_code = jd->u.bmi_unexp.info->error_code;
        status->actual_size = jd->u.bmi_unexp.info->size;
        break;
    case JOB_FLOW:
        job_time_mgr_rem(jd);
        status->error_code = jd->u.flow.flow_d->error_code;
        status->actual_size = jd->u.flow.flow_d->total_transferred;
        break;
    case JOB_REQ_SCHED:
        status->error_code = jd->u.req_sched.error_code;
        break;
    case JOB_TROVE:
        status->error_code = jd->u.trove.state;
        if(jd->u.trove.out_size_p)
        {
            status->actual_size = *jd->u.trove.out_size_p;
        }
        status->vtag = jd->u.trove.vtag;
        status->coll_id = jd->u.trove.fsid;
        status->handle = jd->u.trove.handle;
        status->position = jd->u.trove.position;
        status->count = jd->u.trove.count;
        status->type = jd->u.trove.type;
        break;
    case JOB_DEV_UNEXP:
        status->error_code = 0;
        status->actual_size = jd->u.dev_unexp.info->size;
        break;
    case JOB_REQ_SCHED_TIMER:
        status->error_code = jd->u.req_sched.error_code;
        break;
    case JOB_NULL:
        status->error_code = jd->u.null_info.error_code;
        break;
    }

    return;
}

/* do_one_test_cycle_req_sched()
 *
 * tests the request scheduler to see if anything has completed.
 * Does not block at all.
 *
 * returns 0 on success, -errno on failure
 */
static int do_one_test_cycle_req_sched(void)
{
    int ret = -1;
    int count = job_work_metric;
    req_sched_id id_array[job_work_metric];
    void *user_ptr_array[job_work_metric];
    int error_code_array[job_work_metric];
    int i;
    struct job_desc *tmp_desc = NULL;

    ret = PINT_req_sched_testworld(&count,
                                   id_array,
                                   user_ptr_array,
                                   error_code_array);

    if (ret < 0)
    {
        /* critical failure */
        /* TODO: can I clean up anything else here? */
        gossip_lerr("Error: critical request scheduler failure.\n");
        return (ret);
    }

    for (i = 0; i < count; i++)
    {
        /* remove operation from queue */
        tmp_desc = (struct job_desc *) user_ptr_array[i];

        /* set appropriate fields and place in completed queue */
        tmp_desc->u.req_sched.error_code = error_code_array[i];
        gen_mutex_lock(&completion_mutex);

        /* set completed flag while holding queue lock */
        tmp_desc->completed_flag = 1;
        job_desc_q_add(completion_queue_array[tmp_desc->context_id], tmp_desc);
        gen_mutex_unlock(&completion_mutex);
    }

    return (0);
}


/*
 * Appears to return <0 if problem, 0 if not done, 1 if done.
 */
static int completion_query_some(job_id_t * id_array,
                                 int *inout_count_p,
                                 int *out_index_array,
                                 void **returned_user_ptr_array,
                                 job_status_s *out_status_array_p)
{
    int i;
    struct job_desc *tmp_desc;
    int incount = *inout_count_p;
    int real_id_count = 0;
    int done_count = 0;

    *inout_count_p = 0;

    if (completion_error)
    {
        return (completion_error);
    }

    /* count how many of the id's are non zero */
    for (i = 0; i < incount; i++)
    {
        if (id_array[i])
        {
            real_id_count++;
        }
    }

    if (!real_id_count)
    {
        gossip_lerr("Error: job_testXXX() called with no valid ids.\n");
        return (-EINVAL);
    }

    /* don't do anything unless all of the target ops are done */
    for(i=0; i<incount; i++)
    {
        tmp_desc = id_gen_safe_lookup(id_array[i]);
        if(tmp_desc && tmp_desc->completed_flag)
        {
            done_count++;
        }
    }

    if(done_count < real_id_count)
    {
        return(0);
    }

    /* all target ops are complete; pull them out of completion queue */
    for(i=0; i<incount; i++)
    {
        tmp_desc = id_gen_safe_lookup(id_array[i]);
        if(tmp_desc && tmp_desc->completed_flag)
        {
            if(returned_user_ptr_array)
            {
                fill_status(tmp_desc,
                            &(returned_user_ptr_array[*inout_count_p]),
                            &(out_status_array_p[*inout_count_p]));
            }
            else
            {
                fill_status(tmp_desc,
                            NULL,
                            &(out_status_array_p[*inout_count_p]));
            }
            job_desc_q_remove(tmp_desc);
            if (tmp_desc->type == JOB_REQ_SCHED &&
                tmp_desc->u.req_sched.post_flag == 1)
            {
                /* special case; don't delete job desc for req sched
                 * jobs; we need to hang on to them for use when entry
                 * is released
                 */
            }
            else
            {
                dealloc_job_desc(tmp_desc);
                tmp_desc = NULL;
            }
            out_index_array[*inout_count_p] = i;
            (*inout_count_p)++;
        }
    }

    /* we better not have lost any ops since the first loop through the job
     * list
     */
    assert((*inout_count_p) == done_count);

    return(1);
}

/* completion_query_context()
 *
 * retrieves completed jobs from specified context
 * 
 * returns 1 if anything completed, 0 otherwise 
 */
static int completion_query_context(job_id_t *out_id_array_p,
                                  int *inout_count_p,
                                  void **returned_user_ptr_array,
                                  job_status_s *out_status_array_p,
                                  job_context_id context_id)
{
    struct job_desc *query;
    int incount = *inout_count_p;
    *inout_count_p = 0;

    if (completion_error)
    {
        return (completion_error);
    }
    while ((*inout_count_p < incount) &&
           (query = job_desc_q_shownext(completion_queue_array[context_id])))
    {
        assert(query);

        if (returned_user_ptr_array)
        {
            fill_status(query,
                        &(returned_user_ptr_array[*inout_count_p]),
                        &(out_status_array_p[*inout_count_p]));
        }
        else
        {
            fill_status(query, NULL, &(out_status_array_p[*inout_count_p]));
        }
        out_id_array_p[*inout_count_p] = query->job_id;
        job_desc_q_remove(query);
        (*inout_count_p)++;

        /* special case for request scheduler */
        if (query->type == JOB_REQ_SCHED && query->u.req_sched.post_flag == 1)
        {
            /* special case; don't delete job desc for req sched
             * jobs; we need to hang on to them for use when entry
             * is released
             */
        }
        else
        {
            dealloc_job_desc(query);
            query = NULL;
        }
    }

    if((*inout_count_p) > 0)
    {
        return(1);
    }
    else
    {
        return (0);
    }
}

#ifndef __PVFS2_JOB_THREADED__
/* do_one_work_cycle_all()
 *
 * makes progress when threads are not used
 *
 * no return value
 */
static void do_one_work_cycle_all(int idle_time_ms)
{
    int total_pending_count = 0;
    
    gen_mutex_lock(&work_cycle_mutex);

    total_pending_count = bmi_pending_count + bmi_unexp_pending_count
        + flow_pending_count + dev_unexp_pending_count + trove_pending_count;

    if (bmi_pending_count || bmi_unexp_pending_count || flow_pending_count)
    {
        PINT_thread_mgr_bmi_push(idle_time_ms);
        idle_time_ms = 0;
    }
    if (dev_unexp_pending_count)
    {
        PINT_thread_mgr_dev_push(idle_time_ms);
    }
#ifdef __PVFS2_TROVE_SUPPORT__
    if(trove_pending_count || flow_pending_count)
    {
        PINT_thread_mgr_trove_push(idle_time_ms);
    }
#endif

    if(total_pending_count == 0 && idle_time_ms != 0)
    {
        /* The caller would like for us to idle if necessary, but we really
         * don't have a single thing to do.  Sleep here to prevent busy
         * spins.
         */
#ifdef WIN32
        Sleep(idle_time_ms);
#else
        struct timespec ts;
        ts.tv_sec = idle_time_ms / 1000;
        ts.tv_nsec = (idle_time_ms % 1000) * 1000 * 1000;
        nanosleep(&ts, NULL);
#endif
    }

    gen_mutex_unlock(&work_cycle_mutex);
    return;
}
#endif

/* flow_callback()
 *
 * function to be called upon completion of flows
 *
 * no return value
 */
static void flow_callback(flow_descriptor* flow_d, int cancel_path)
{
    struct job_desc* tmp_desc = (struct job_desc*)flow_d->user_ptr;

    gen_mutex_lock(&initialized_mutex);
    if(initialized == 0)
    {
        /* The job interface has been shutdown.  Silently ignore callback. */
        gen_mutex_unlock(&initialized_mutex);
        return;
    }
    gen_mutex_unlock(&initialized_mutex);

    /* set job descriptor fields and put into completion queue */

    /* if this is being triggered directly from PINT_flow_cancel(), then the
     * completion mutex is already held by the caller; skip the mutex.
     */
    if(!cancel_path)
    {
        gen_mutex_lock(&completion_mutex);
    }
    job_desc_q_add(completion_queue_array[tmp_desc->context_id], tmp_desc);
    /* set completed flag while holding queue lock */
    tmp_desc->completed_flag = 1;

    flow_pending_count--;
    gossip_debug(GOSSIP_FLOW_DEBUG,
                 "Job flows in progress (callback time): %d\n",
                 flow_pending_count);

#ifdef __PVFS2_JOB_THREADED__
    /* wake up anyone waiting for completion */
    pthread_cond_signal(&completion_cond);
#endif
    if(!cancel_path)
    {
        gen_mutex_unlock(&completion_mutex);
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

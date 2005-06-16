/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  Routines for state machine processing on clients.
 */
#include <string.h>
#include <assert.h>

#include "pvfs2-sysint.h"
#include "pint-sysint-utils.h"
#include "pint-servreq.h"
#include "pint-cached-config.h"
#include "PINT-reqproto-encode.h"

#include "client-state-machine.h"
#include "state-machine-fns.h"
#include "pvfs2-debug.h"
#include "job.h"
#include "gossip.h"
#include "pvfs2-util.h"
#include "id-generator.h"

#define MAX_RETURNED_JOBS   256

job_context_id pint_client_sm_context;

static int got_context = 0;

/*
  used for locally storing completed operations from test() call so
  that we can retrieve them in testsome() while still making progress
  (and possible completing operations in the test() call
*/
static int s_completion_list_index = 0;
static PINT_client_sm *s_completion_list[MAX_RETURNED_JOBS] = {NULL};
static gen_mutex_t s_completion_list_mutex = GEN_MUTEX_INITIALIZER;

#define CLIENT_SM_INIT_ONCE()                                        \
do {                                                                 \
    if (got_context == 0)                                            \
    {                                                                \
	/* get a context for our state machine operations */         \
	job_open_context(&pint_client_sm_context);                   \
	got_context = 1;                                             \
    }                                                                \
} while(0)

#define CLIENT_SM_ASSERT_INITIALIZED()  \
do { assert(got_context); } while(0)

static PVFS_error add_sm_to_completion_list(PINT_client_sm *sm_p)
{
    gen_mutex_lock(&s_completion_list_mutex);
    assert(s_completion_list_index < MAX_RETURNED_JOBS);
    s_completion_list[s_completion_list_index++] = sm_p;
    gen_mutex_unlock(&s_completion_list_mutex);
    return 0;
}

/*
  this method is used in the case of calling test() on an sm that was
  already completed by a previous call to testsome().  in this case,
  if the sm was added to the completion list, it MUST be removed
  before returning from test()
*/
static int conditional_remove_sm_if_in_completion_list(
    PINT_client_sm *sm_p)
{
    int found = 0, i = 0;

    gen_mutex_lock(&s_completion_list_mutex);
    for(i = 0; i < s_completion_list_index; i++)
    {
        if (s_completion_list[i] == sm_p)
        {
            s_completion_list[i] = NULL;
            found = 1;
            break;
        }
    }
    gen_mutex_unlock(&s_completion_list_mutex);
    return found;
}

static PVFS_error completion_list_retrieve_completed(
    PVFS_sys_op_id *op_id_array,
    void **user_ptr_array,
    int *error_code_array,
    int limit,
    int *out_count)
{
    int i = 0, new_list_index = 0;
    PINT_client_sm *sm_p = NULL;
    PINT_client_sm *tmp_completion_list[MAX_RETURNED_JOBS] = {NULL};

    assert(op_id_array);
    assert(error_code_array);
    assert(out_count);

    memset(tmp_completion_list, 0,
           (MAX_RETURNED_JOBS * sizeof(PINT_client_sm *)));

    gen_mutex_lock(&s_completion_list_mutex);
    for(i = 0; i < s_completion_list_index; i++)
    {
        if (s_completion_list[i] == NULL)
        {
            continue;
        }

        sm_p = s_completion_list[i];
        assert(sm_p);

        if (i < limit)
        {
            op_id_array[i] = sm_p->sys_op_id;
            error_code_array[i] = sm_p->error_code;

            if (user_ptr_array)
            {
                user_ptr_array[i] = (void *)sm_p->user_ptr;
            }
            s_completion_list[i] = NULL;

            PINT_sys_release(sm_p->sys_op_id);
        }
        else
        {
            tmp_completion_list[new_list_index++] = sm_p;
        }
    }
    *out_count = i;

    /* clean up and adjust the list and it's book keeping */
    s_completion_list_index = new_list_index;
    memcpy(s_completion_list, tmp_completion_list,
           (MAX_RETURNED_JOBS * sizeof(PINT_client_sm *)));
    
    gen_mutex_unlock(&s_completion_list_mutex);
    return 0;
}

static inline int cancelled_io_jobs_are_pending(PINT_client_sm *sm_p)
{
    /*
      NOTE: if the I/O cancellation has properly completed, the
      cancelled contextual jobs within that I/O operation will be
      popping out of the testcontext calls (in our testsome() or
      test()).  to avoid passing out the same completed op mutliple
      times, do not add the operation to the completion list until all
      cancellations on the I/O operation are accounted for
    */
    assert(sm_p);

    /*
      this *can* possibly be 0 in the case that the I/O has already
      completed and no job cancellation were issued at I/O cancel time
    */
    if (sm_p->u.io.total_cancellations_remaining > 0)
    {
        sm_p->u.io.total_cancellations_remaining--;
    }

    gossip_debug(
        GOSSIP_IO_DEBUG, "(%p) cancelled_io_jobs_are_pending: %d "
        "remaining (op %s)\n", sm_p,
        sm_p->u.io.total_cancellations_remaining,
        (sm_p->op_complete ? "complete" : "NOT complete"));

    return (sm_p->u.io.total_cancellations_remaining != 0);
}

/*
  NOTE: important usage notes regarding post(), test(), and testsome()

  thread safety: test() and testsome() can be called in any order by
  the same thread.  if you need to call test() and testsome()
  simultaneously from different threads, you need to serialize the
  calls yourself.

  calling semantics: the non-blocking calls (i.e. PVFS_isys_* or
  PVFS_imgmt_* calls) allocate the state machine pointer (sm_p) used
  for each operation.  the blocking calls DO NOT allocate this, but
  call the non-blocking method (which does allocate it) and waits for
  completion.  On completion, the blocking call frees the state
  machine pointer (via PINT_sys_release).  the blocking calls only
  ever call the test() function, which does not free the state machine
  pointer on completion.

  the testsome() function frees the state machine pointers allocated
  from the non-blocking calls on completion because any caller of
  testsome() *should* be using the non-blocking calls with it.  this
  means that if you are calling test() with a non-blocking operation
  that you manually issued (with a PVFS_isys* or PVFS_imgmt* call),
  you need to call PVFS_sys_release on your own when the operation
  completes.
*/

/** Adds a state machine into the list of machines that are being
 *  actively serviced.
 */
PVFS_error PINT_client_state_machine_post(
    PINT_client_sm *sm_p,
    int pvfs_sys_op,
    PVFS_sys_op_id *op_id,
    void *user_ptr /* in */)
{
    PVFS_error ret = -PVFS_EINVAL;
    job_status_s js;

#if 0
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_client_state_machine_post called\n");
#endif

    CLIENT_SM_INIT_ONCE();

    if (!sm_p)
    {
        return ret;
    }

    memset(&js, 0, sizeof(js));

    /* save operation type; mark operation as unfinished */
    sm_p->user_ptr = user_ptr;
    sm_p->op = pvfs_sys_op;
    sm_p->op_complete = 0;

    switch (pvfs_sys_op)
    {
	case PVFS_SYS_REMOVE:
	    sm_p->current_state =
                (pvfs2_client_remove_sm.state_machine + 1);
	    break;
	case PVFS_SYS_CREATE:
	    sm_p->current_state =
                (pvfs2_client_create_sm.state_machine + 1);
	    break;
	case PVFS_SYS_MKDIR:
	    sm_p->current_state =
                (pvfs2_client_mkdir_sm.state_machine + 1);
	    break;
	case PVFS_SYS_SYMLINK:
	    sm_p->current_state =
                (pvfs2_client_symlink_sm.state_machine + 1);
	    break;
	case PVFS_SYS_READDIR:
	    sm_p->current_state =
                (pvfs2_client_readdir_sm.state_machine + 1);
	    break;
	case PVFS_SYS_LOOKUP:
	    sm_p->current_state =
                (pvfs2_client_lookup_sm.state_machine + 1);
	    break;
	case PVFS_SYS_RENAME:
	    sm_p->current_state =
                (pvfs2_client_rename_sm.state_machine + 1);
	    break;
	case PVFS_SYS_GETATTR:
	    sm_p->current_state =
                (pvfs2_client_getattr_sm.state_machine + 1);
	    break;
	case PVFS_SYS_SETATTR:
	    sm_p->current_state =
                (pvfs2_client_setattr_sm.state_machine + 1);
	    break;
	case PVFS_SYS_IO:
	    sm_p->current_state =
                (pvfs2_client_io_sm.state_machine + 1);
	    break;
	case PVFS_SYS_FLUSH:
	    sm_p->current_state =
                (pvfs2_client_flush_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_SETPARAM_LIST:
	    sm_p->current_state =
                (pvfs2_client_mgmt_setparam_list_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_NOOP:
	    sm_p->current_state =
                (pvfs2_client_mgmt_noop_sm.state_machine + 1);
	    break;
	case PVFS_SYS_TRUNCATE:
	    sm_p->current_state =
                (pvfs2_client_truncate_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_STATFS_LIST:
	    sm_p->current_state =
                (pvfs2_client_mgmt_statfs_list_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_PERF_MON_LIST:
	    sm_p->current_state =
                (pvfs2_client_mgmt_perf_mon_list_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_EVENT_MON_LIST:
	    sm_p->current_state =
                (pvfs2_client_mgmt_event_mon_list_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_ITERATE_HANDLES_LIST:
	    sm_p->current_state = 
                (pvfs2_client_mgmt_iterate_handles_list_sm.state_machine + 1);
	    break;
	case PVFS_MGMT_GET_DFILE_ARRAY:
	    sm_p->current_state =
                (pvfs2_client_mgmt_get_dfile_array_sm.state_machine + 1);
	    break;
        case PVFS_MGMT_REMOVE_OBJECT:
            sm_p->current_state =
                (pvfs2_client_mgmt_remove_object_sm.state_machine + 1);
            break;
        case PVFS_MGMT_REMOVE_DIRENT:
            sm_p->current_state =
                (pvfs2_client_mgmt_remove_dirent_sm.state_machine + 1);
            break;
        case PVFS_MGMT_CREATE_DIRENT:
            sm_p->current_state =
                (pvfs2_client_mgmt_create_dirent_sm.state_machine + 1);
            break;
        case PVFS_MGMT_GET_DIRDATA_HANDLE:
            sm_p->current_state =
                (pvfs2_client_mgmt_get_dirdata_handle_sm.state_machine + 1);
            break;
	case PVFS_SYS_GETEATTR:
	    sm_p->current_state =
                (pvfs2_client_get_eattr_sm.state_machine + 1);
	    break;
	case PVFS_SYS_GETEATTR_LIST:
	    sm_p->current_state =
                (pvfs2_client_get_eattr_list_sm.state_machine + 1);
	    break;
	case PVFS_SYS_SETEATTR:
	    sm_p->current_state =
                (pvfs2_client_set_eattr_sm.state_machine + 1);
	    break;
	case PVFS_SYS_SETEATTR_LIST:
	    sm_p->current_state =
                (pvfs2_client_set_eattr_list_sm.state_machine + 1);
	    break;
	case PVFS_SERVER_GET_CONFIG:
	    sm_p->current_state =
                (pvfs2_server_get_config_sm.state_machine + 1);
	    break;
	case PVFS_CLIENT_JOB_TIMER:
	    sm_p->current_state =
                (pvfs2_client_job_timer_sm.state_machine + 1);
	    break;
        case PVFS_DEV_UNEXPECTED:
            gossip_err("You should be using PINT_sys_dev_unexp for "
                       "posting this type of operation!  Failing.\n");
            return ret;
	default:
            gossip_lerr("FIXME: Unrecognized sysint operation!\n");
            return ret;
    }

    if (op_id)
    {
        ret = PINT_id_gen_safe_register(op_id, (void *)sm_p);
        sm_p->sys_op_id = *op_id;
    }

    /*
      start state machine and continue advancing while we're getting
      immediate completions
    */
    ret = sm_p->current_state->state_action(sm_p, &js);
    while(ret == 1)
    {
        ret = PINT_state_machine_next(sm_p, &js);
    }

    if (sm_p->op_complete)
    {
        gossip_debug(
            GOSSIP_CLIENT_DEBUG, "Posted %s (immediate completion)\n",
            PINT_client_get_name_str(pvfs_sys_op));

        ret = add_sm_to_completion_list(sm_p);
        assert(ret == 0);
    }
    else
    {
        gossip_debug(
            GOSSIP_CLIENT_DEBUG, "Posted %s (waiting for test)\n",
            PINT_client_get_name_str(pvfs_sys_op));
    }
    return ret;
}

PVFS_error PINT_sys_dev_unexp(
    struct PINT_dev_unexp_info *info,
    job_status_s *jstat,
    PVFS_sys_op_id *op_id,
    void *user_ptr)
{
    PVFS_error ret = -PVFS_EINVAL;
    job_id_t id;
    PINT_client_sm *sm_p = NULL;

    CLIENT_SM_INIT_ONCE();

    /* we require more input args than the regular post method above */
    if (!info || !jstat || !op_id)
    {
        return -PVFS_EINVAL;
    }

    sm_p = (PINT_client_sm *)malloc(sizeof(PINT_client_sm));
    if (!sm_p)
    {
        return -PVFS_ENOMEM;
    }
    memset(sm_p, 0, sizeof(PINT_client_sm));
    sm_p->user_ptr = user_ptr;
    sm_p->op = PVFS_DEV_UNEXPECTED;
    sm_p->op_complete = 0;
    sm_p->cred_p = NULL;

    memset(jstat, 0, sizeof(job_status_s));
    ret = job_dev_unexp(info, (void *)sm_p, 0, jstat, &id,
                        JOB_NO_IMMED_COMPLETE, pint_client_sm_context);
    if (ret)
    {
        PVFS_perror_gossip("PINT_sys_dev_unexp failed", ret);
        free(sm_p);
    }
    else
    {
        ret = PINT_id_gen_safe_register(op_id, (void *)sm_p);
        sm_p->sys_op_id = *op_id;
    }
    return ret;
}

/** Cancels in progress I/O operations.
 *
 * \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PINT_client_io_cancel(PVFS_sys_op_id id)
{
    int i = 0;
    PVFS_error ret = -PVFS_EINVAL;
    PINT_client_sm *sm_p = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PINT_client_io_cancel called\n");

    sm_p = PINT_id_gen_safe_lookup(id);
    if (!sm_p)
    {
	/* if we can't find it, it may have already completed */
        return 0;
    }

    /* we can't cancel any arbitrary operation */
    assert(sm_p->op == PVFS_SYS_IO);

    if (sm_p->op_complete)
    {
	/* op already completed; nothing to cancel. */
        return 0;
    }

    /* if we fall to here, the I/O operation is still in flight */
    /* first, set a flag informing the sys_io state machine that the
     * operation has been cancelled so it doesn't post any new jobs 
     */
    sm_p->op_cancelled = 1;

    /*
      don't return an error if nothing is cancelled, because
      everything may have completed already
    */
    ret = 0;

    /* now run through and cancel the outstanding jobs */
    for(i = 0; i < sm_p->u.io.datafile_count; i++)
    {
        PINT_client_io_ctx *cur_ctx = &sm_p->u.io.contexts[i];
        assert(cur_ctx);

        if (cur_ctx->msg_send_in_progress)
        {
            gossip_debug(GOSSIP_CANCEL_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Send "
                         "(Request)\n",i);

            ret = job_bmi_cancel(cur_ctx->msg->send_id,
                                 pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_bmi_cancel failed", ret);
                break;
            }
            sm_p->u.io.total_cancellations_remaining++;
        }

        if (cur_ctx->msg_recv_in_progress)
        {
            gossip_debug(GOSSIP_CANCEL_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Recv "
                         "(Response)\n",i);

            ret = job_bmi_cancel(cur_ctx->msg->recv_id,
                                 pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_bmi_cancel failed", ret);
                break;
            }
            sm_p->u.io.total_cancellations_remaining++;
        }

        if (cur_ctx->flow_in_progress)
        {
            gossip_debug(GOSSIP_CANCEL_DEBUG,
                         "[%d] Posting cancellation of type: FLOW\n",i);

            ret = job_flow_cancel(
                cur_ctx->flow_job_id, pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_flow_cancel failed", ret);
                break;
            }
            sm_p->u.io.total_cancellations_remaining++;
        }

        if (cur_ctx->write_ack_in_progress)
        {
            gossip_debug(GOSSIP_CANCEL_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Recv "
                         "(Write Ack)\n",i);

            ret = job_bmi_cancel(cur_ctx->write_ack.recv_id,
                                 pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_bmi_cancel failed", ret);
                break;
            }
            sm_p->u.io.total_cancellations_remaining++;
        }
    }
    gossip_debug(GOSSIP_CANCEL_DEBUG, "(%p) Total cancellations "
                 "remaining: %d\n", sm_p,
                 sm_p->u.io.total_cancellations_remaining);
    return ret;
}

/** Checks for completion of a specific state machine.
 *
 *  If specific state machine has not completed, progress is made on
 *  all posted state machines.
 */
PVFS_error PINT_client_state_machine_test(
    PVFS_sys_op_id op_id,
    int *error_code)
{
    int i = 0, job_count = 0;
    PVFS_error ret = -PVFS_EINVAL;
    PINT_client_sm *sm_p, *tmp_sm_p = NULL;
    job_id_t job_id_array[MAX_RETURNED_JOBS];
    job_status_s job_status_array[MAX_RETURNED_JOBS];
    void *client_sm_p_array[MAX_RETURNED_JOBS] = {NULL};

#if 0
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_client_state_machine_test called\n");
#endif

    CLIENT_SM_ASSERT_INITIALIZED();

    job_count = MAX_RETURNED_JOBS;

    if (!error_code)
    {
        return ret;
    }

    sm_p = PINT_id_gen_safe_lookup(op_id);
    if (!sm_p)
    {
        return ret;
    }

    if (sm_p->op_complete)
    {
        *error_code = sm_p->error_code;
        conditional_remove_sm_if_in_completion_list(sm_p);
        return 0;
    }

    ret = job_testcontext(job_id_array,
			  &job_count, /* in/out parameter */
			  client_sm_p_array,
			  job_status_array,
			  10,
			  pint_client_sm_context);
    assert(ret > -1);

    /* do as much as we can on every job that has completed */
    for(i = 0; i < job_count; i++)
    {
	tmp_sm_p = (PINT_client_sm *)client_sm_p_array[i];
        assert(tmp_sm_p);

        if (tmp_sm_p->op == PVFS_DEV_UNEXPECTED)
        {
            tmp_sm_p->op_complete = 1;
        }

        if (!tmp_sm_p->op_complete)
        {
            do
            {
                ret = PINT_state_machine_next(
                    tmp_sm_p, &job_status_array[i]);

            } while (ret == 1);

            assert(ret == 0);
        }

        /* make sure we don't return internally cancelled I/O jobs */
        if ((tmp_sm_p->op == PVFS_SYS_IO) && (tmp_sm_p->op_cancelled) &&
            (cancelled_io_jobs_are_pending(tmp_sm_p)))
        {
            continue;
        }

        /*
          if we've found a completed operation and it's NOT the op
          being tested here, we add it to our local completion list so
          that later calls to the sysint test/testsome can find it
        */
        if ((tmp_sm_p != sm_p) && (tmp_sm_p->op_complete == 1))
        {
            ret = add_sm_to_completion_list(tmp_sm_p);
            assert(ret == 0);
        }
    }

    if (sm_p->op_complete)
    {
        *error_code = sm_p->error_code;
        conditional_remove_sm_if_in_completion_list(sm_p);
    }
    return 0;
}

/** Checks completion of one or more state machines.
 *
 *  If none of the state machines listed in op_id_array have completed,
 *  then progress is made on all posted state machines.
 */
PVFS_error PINT_client_state_machine_testsome(
    PVFS_sys_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms)
{
    PVFS_error ret = -PVFS_EINVAL;
    int i = 0, limit = 0, job_count = 0;
    PINT_client_sm *sm_p = NULL;
    job_id_t job_id_array[MAX_RETURNED_JOBS];
    job_status_s job_status_array[MAX_RETURNED_JOBS];
    void *client_sm_p_array[MAX_RETURNED_JOBS] = {NULL};

#if 0
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_client_state_machine_testsome called\n");
#endif

    CLIENT_SM_ASSERT_INITIALIZED();

    if (!op_id_array || !op_count || !error_code_array)
    {
        PVFS_perror_gossip("PINT_client_state_machine_testsome", ret);
        return ret;
    }

    if ((*op_count < 1) || (*op_count > MAX_RETURNED_JOBS))
    {
        PVFS_perror_gossip("testsome() got invalid op_count", ret);
        return ret;
    }

    job_count = MAX_RETURNED_JOBS;
    limit = *op_count;
    *op_count = 0;

    ret = completion_list_retrieve_completed(
        op_id_array, user_ptr_array, error_code_array, limit, op_count);

    if ((ret == 0) && (*op_count > 0))
    {
        return ret;
    }

    ret = job_testcontext(job_id_array,
			  &job_count, /* in/out parameter */
			  client_sm_p_array,
			  job_status_array,
			  timeout_ms,
			  pint_client_sm_context);
    assert(ret > -1);

    /* do as much as we can on every job that has completed */
    for(i = 0; i < job_count; i++)
    {
	sm_p = (PINT_client_sm *)client_sm_p_array[i];
        assert(sm_p);

        /*
          note that dev unexp messages found here are treated as
          complete since if we see them at all in here, they're ready
          to be passed back to the caller
        */
        if (sm_p->op == PVFS_DEV_UNEXPECTED)
        {
            sm_p->op_complete = 1;
        }

        if (!sm_p->op_complete)
        {
            do
            {
                ret = PINT_state_machine_next(
                    sm_p, &job_status_array[i]);

            } while (ret == 1);

            /* (ret < 0) indicates a problem from the job system
             * itself; the return value of the underlying operation is
             * kept in the job status structure.
             */
            assert(ret == 0);
        }

        /* make sure we don't return internally cancelled I/O jobs */
        if ((sm_p->op == PVFS_SYS_IO) && (sm_p->op_cancelled) &&
            (cancelled_io_jobs_are_pending(sm_p)))
        {
            continue;
        }

        /*
          by adding the completed op to our completion list, we can
          keep progressing on operations in progress here and just
          grab all completed operations when we're finished
          (i.e. outside of this loop).
        */
        if (sm_p->op_complete)
        {
            ret = add_sm_to_completion_list(sm_p);
            assert(ret == 0);
        }
    }

    return completion_list_retrieve_completed(
        op_id_array, user_ptr_array, error_code_array, limit, op_count);
}

/** Continually test on a specific state machine until it completes.
 *
 * This is what is called when PINT_sys_wait or PINT_mgmt_wait is used.
 */
PVFS_error PINT_client_wait_internal(
    PVFS_sys_op_id op_id,
    const char *in_op_str,
    int *out_error,
    const char *in_class_str)
{
    PVFS_error ret = -PVFS_EINVAL;
    PINT_client_sm *sm_p = NULL;

    if (in_op_str && out_error && in_class_str)
    {
        sm_p = (PINT_client_sm *)id_gen_safe_lookup(op_id);
        assert(sm_p);

        do
        {
            /*
            gossip_debug(GOSSIP_CLIENT_DEBUG,
              "%s: PVFS_i%s_%s calling test()\n",
              __func__, in_class_str, in_op_str);
            */
            ret = PINT_client_state_machine_test(op_id, out_error);

        } while (!sm_p->op_complete && (ret == 0));

        if (ret)
        {
            PVFS_perror_gossip("PINT_client_state_machine_test()", ret);
        }
        else
        {
            *out_error = sm_p->error_code;
        }
    }
    return ret;
}

/** Frees resources associated with state machine instance.
 */
void PINT_sys_release(PVFS_sys_op_id op_id)
{
    PINT_client_sm *sm_p = (PINT_client_sm *)id_gen_safe_lookup(op_id);
    if (sm_p)
    {
        PINT_id_gen_safe_unregister(op_id);

        if (sm_p->op && sm_p->cred_p)
        {
            PVFS_util_release_credentials(sm_p->cred_p);
            sm_p->cred_p = NULL;
        }

        if (sm_p->acache_hit && sm_p->pinode)
        {
            PINT_acache_release(sm_p->pinode);
        }
        free(sm_p);
    }
}

char *PINT_client_get_name_str(int op_type)
{
    typedef struct
    {
        int type;
        char *type_str;
    } __sys_op_info_t;

    static __sys_op_info_t op_info[] =
    {
        { PVFS_SYS_REMOVE, "PVFS_SYS_REMOVE" },
        { PVFS_SYS_CREATE, "PVFS_SYS_CREATE" },
        { PVFS_SYS_MKDIR, "PVFS_SYS_MKDIR" },
        { PVFS_SYS_SYMLINK, "PVFS_SYS_SYMLINK" },
        { PVFS_SYS_READDIR, "PVFS_SYS_READDIR" },
        { PVFS_SYS_LOOKUP, "PVFS_SYS_LOOKUP" },
	{ PVFS_SYS_RENAME, "PVFS_SYS_RENAME" },
        { PVFS_SYS_GETATTR, "PVFS_SYS_GETATTR" },
        { PVFS_SYS_SETATTR, "PVFS_SYS_SETATTR" },
        { PVFS_SYS_IO, "PVFS_SYS_IO" },
        { PVFS_SYS_FLUSH, "PVFS_SYS_FLUSH" },
        { PVFS_MGMT_SETPARAM_LIST, "PVFS_MGMT_SETPARAM_LIST" },
        { PVFS_MGMT_NOOP, "PVFS_MGMT_NOOP" },
        { PVFS_SYS_TRUNCATE, "PVFS_SYS_TRUNCATE" },
        { PVFS_MGMT_STATFS_LIST, "PVFS_MGMT_STATFS_LIST" },
        { PVFS_MGMT_PERF_MON_LIST, "PVFS_MGMT_PERF_MON_LIST" },
        { PVFS_MGMT_EVENT_MON_LIST, "PVFS_MGMT_EVENT_MON_LIST" },
        { PVFS_MGMT_ITERATE_HANDLES_LIST,
          "PVFS_MGMT_ITERATE_HANDLES_LIST" },
        { PVFS_MGMT_GET_DFILE_ARRAY, "PVFS_MGMT_GET_DFILE_ARRAY" },
        { PVFS_MGMT_REMOVE_OBJECT, "PVFS_MGMT_REMOVE_OBJECT" },
        { PVFS_MGMT_REMOVE_DIRENT, "PVFS_MGMT_REMOVE_DIRENT" },
        { PVFS_MGMT_CREATE_DIRENT, "PVFS_MGMT_CREATE_DIRENT" },
        { PVFS_MGMT_GET_DIRDATA_HANDLE,
          "PVFS_MGMT_GET_DIRDATA_HANDLE" },
        { PVFS_SYS_GETEATTR, "PVFS_SYS_GETEATTR" },
        { PVFS_SYS_GETEATTR_LIST, "PVFS_SYS_GETEATTR_LIST" },
        { PVFS_SYS_SETEATTR, "PVFS_SYS_SETEATTR" },
        { PVFS_SYS_SETEATTR_LIST, "PVFS_SYS_SETEATTR_LIST" },
        { PVFS_SERVER_GET_CONFIG, "PVFS_SERVER_GET_CONFIG" },
        { PVFS_CLIENT_JOB_TIMER, "PVFS_CLIENT_JOB_TIMER" },
        { PVFS_DEV_UNEXPECTED, "PVFS_DEV_UNEXPECTED" },
        { 0, "UNKNOWN" }
    };

    int i = 0, limit = (int)(sizeof(op_info) / sizeof(__sys_op_info_t));
    for(i = 0; i < limit; i++)
    {
        if (op_info[i].type == op_type)
        {
            return op_info[i].type_str;
        }
    }
    return op_info[limit-1].type_str;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

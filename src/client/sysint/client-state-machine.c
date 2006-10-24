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
#include "pint-cached-config.h"
#include "PINT-reqproto-encode.h"

#include "state-machine.h"
#include "client-state-machine.h"
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
static PINT_smcb *s_completion_list[MAX_RETURNED_JOBS] = {NULL};
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

static PVFS_error add_sm_to_completion_list(PINT_smcb *smcb)
{
    gen_mutex_lock(&s_completion_list_mutex);
    assert(s_completion_list_index < MAX_RETURNED_JOBS);
    if (!smcb->op_completed)
    {
        smcb->op_completed = 1;
        s_completion_list[s_completion_list_index++] = smcb;
    }
    gen_mutex_unlock(&s_completion_list_mutex);
    return 0;
}

/*
  this method is used in the case of calling test() on an sm that was
  already completed by a previous call to testsome().  in this case,
  if the sm was added to the completion list, it MUST be removed
  before returning from test()
*/
static int conditional_remove_sm_if_in_completion_list(PINT_smcb *smcb)
{
    int found = 0, i = 0;

    gen_mutex_lock(&s_completion_list_mutex);
    for(i = 0; i < s_completion_list_index; i++)
    {
        if (s_completion_list[i] == smcb)
        {
            if(i == (s_completion_list_index - 1))
            {
                /* we're at the end, so just set last sm to null */
                s_completion_list[i] = NULL;
            }
            else
            {
                memmove(s_completion_list[i],
                        s_completion_list[i+1],
                        (s_completion_list_index - (i + 1)) *
                        sizeof(PINT_client_sm *));
            }
            s_completion_list_index--;
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
    int *out_count)  /* what exactly is this supposed to return */
{
    int i = 0, new_list_index = 0;
    PINT_smcb *smcb = NULL;
    PINT_smcb *tmp_completion_list[MAX_RETURNED_JOBS] = {NULL};
    PINT_client_sm *sm_p;

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

        smcb = s_completion_list[i];
        assert(smcb);

        if (i < limit)
        {
            sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
            op_id_array[i] = sm_p->sys_op_id;
            error_code_array[i] = sm_p->error_code;

            if (user_ptr_array)
            {
                user_ptr_array[i] = (void *)sm_p->user_ptr;
            }
            s_completion_list[i] = NULL;

            PVFS_sys_release(sm_p->sys_op_id);
        }
        else
        {
            tmp_completion_list[new_list_index++] = smcb;
        }
    }
    *out_count = PVFS_util_min(i, limit);

    /* clean up and adjust the list and it's book keeping */
    s_completion_list_index = new_list_index;
    memcpy(s_completion_list, tmp_completion_list,
           (MAX_RETURNED_JOBS * sizeof(struct PINT_smcb *)));
    
    gen_mutex_unlock(&s_completion_list_mutex);
    return 0;
}

static inline int cancelled_io_jobs_are_pending(PINT_smcb *smcb)
{
    PINT_client_sm *sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
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
        (PINT_smcb_complete(smcb) ? "complete" : "NOT complete"));

    return (sm_p->u.io.total_cancellations_remaining != 0);
}

/* this array must be ordered to match the enum in client-state-machine.h */ 
struct PINT_client_op_entry_s PINT_client_sm_sys_table[] =
{
    {&pvfs2_client_remove_sm},
    {&pvfs2_client_create_sm},
    {&pvfs2_client_mkdir_sm},
    {&pvfs2_client_symlink_sm},
    {&pvfs2_client_sysint_getattr_sm},
    {&pvfs2_client_io_sm},
    {&pvfs2_client_flush_sm},
    {&pvfs2_client_truncate_sm},
    {&pvfs2_client_sysint_readdir_sm},
    {&pvfs2_client_setattr_sm},
    {&pvfs2_client_lookup_sm},
    {&pvfs2_client_rename_sm},
    {&pvfs2_client_get_eattr_sm},
    {&pvfs2_client_set_eattr_sm},
    {&pvfs2_client_del_eattr_sm},
    {&pvfs2_client_list_eattr_sm},
    {&pvfs2_client_small_io_sm},
    {&pvfs2_client_statfs_sm},
    {&pvfs2_fs_add_sm},
    {&pvfs2_client_readdirplus_sm},
};

struct PINT_client_op_entry_s PINT_client_sm_mgmt_table[] =
{
    {&pvfs2_client_mgmt_setparam_list_sm},
    {&pvfs2_client_mgmt_noop_sm},
    {&pvfs2_client_mgmt_statfs_list_sm},
    {&pvfs2_client_mgmt_perf_mon_list_sm},
    {&pvfs2_client_mgmt_iterate_handles_list_sm},
    {&pvfs2_client_mgmt_get_dfile_array_sm},
    {&pvfs2_client_mgmt_event_mon_list_sm},
    {&pvfs2_client_mgmt_remove_object_sm},
    {&pvfs2_client_mgmt_remove_dirent_sm},
    {&pvfs2_client_mgmt_create_dirent_sm},
    {&pvfs2_client_mgmt_get_dirdata_handle_sm}
};


/* This function allows the generic state-machine-fns.c locate function
 * to access the appropriate sm struct based on the client operation index
 * from the above enum.  Because the enum starts management operations at
 * 70, the management table was separated out from the sys table and the
 * necessary checks and subtractions are made in this macro.
 * Pointer to this func is put in SM control block for client SMs.
 */
/* NOTE; appears to be a latent bug that does not catch op values
 * between largest sys op and lowest mgmt op - need to check on this
 * WBL
 */
struct PINT_state_machine_s *client_op_state_get_machine(int op)
{
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "client_op_state_get_machine %d\n",op);

    switch (op)
    {
    /* special cases first */
    case PVFS_SERVER_GET_CONFIG :
        return &pvfs2_server_get_config_sm;
    case PVFS_CLIENT_JOB_TIMER :
        return &pvfs2_client_job_timer_sm;
    case PVFS_CLIENT_PERF_COUNT_TIMER :
        return &pvfs2_client_perf_count_timer_sm;
    case PVFS_DEV_UNEXPECTED :
        return &pvfs2_sysdev_unexp_sm;
    default:
        /* now check range for sys functions */
        if (op <= PVFS_OP_SYS_MAXVAL)
        {
            return PINT_client_sm_sys_table[op-1].sm;
        }
        else
        {
            /* now checjk range for mgmt functions */
            if (op <= PVFS_OP_MGMT_MAXVAL)
            {
                return PINT_client_sm_mgmt_table[op-PVFS_OP_SYS_MAXVAL-1].sm;
            }
            else
            {
                /* otherwise its out of range */
                return NULL;
            }
        }
    }
}

/* callback for a terminating state machine
 * the client adds terminted jobs to a completion list, unless
 * they were cancelled.
 */

int client_state_machine_terminate(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    int ret;

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "client_state_machine_terminate smcb %p\n",smcb);

    if (!((PINT_smcb_op(smcb) == PVFS_SYS_IO) &&
            (PINT_smcb_cancelled(smcb)) &&
            (cancelled_io_jobs_are_pending(smcb))))
    {
        gossip_debug(GOSSIP_CLIENT_DEBUG, 
                "add smcb %p to completion list\n", smcb);
        ret = add_sm_to_completion_list(smcb);
        assert(ret == 0);
    }
    return SM_ACTION_TERMINATE;
}

/*
  NOTE: important usage notes regarding post(), test(), and testsome()

  thread safety: test() and testsome() can be called in any order by
  the same thread.  if you need to call test() and testsome()
  simultaneously from different threads, you need to serialize the
  calls yourself.

  calling semantics: the non-blocking calls (i.e. PVFS_isys_* or
  PVFS_imgmt_* calls) allocate the state machine control block (smcb) used
  for each operation.  the blocking calls DO NOT allocate this, but
  call the non-blocking method (which does allocate it) and waits for
  completion.  On completion, the blocking call frees the state
  machine control block (via PVFS_sys_release).  the blocking calls only
  ever call the test() function, which does not free the state machine
  control block on completion.

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
    PINT_smcb *smcb,
    PVFS_sys_op_id *op_id,
    void *user_ptr /* in */)
{
    PVFS_error ret = -PVFS_EINVAL;
    job_status_s js;
    int pvfs_sys_op = PINT_smcb_op(smcb);
    PINT_client_sm *sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_client_state_machine_post smcb %p\n",smcb);

    CLIENT_SM_INIT_ONCE();

    if (!smcb)
    {
        return ret;
    }

    memset(&js, 0, sizeof(js));

    /* save operation type; mark operation as unfinished */
    sm_p->user_ptr = user_ptr;

#if 0
    if(pvfs_sys_op == PVFS_DEV_UNEXPECTED)
    {
        gossip_err("FAILURE: You should be using PINT_sys_dev_unexp for "
                   "posting this type of operation!\n");
        return ret;
    }
#endif

    if (op_id)
    {
        ret = PINT_id_gen_safe_register(op_id, (void *)smcb);
        sm_p->sys_op_id = *op_id;
    }

    /*
      start state machine and continue advancing while we're getting
      immediate completions
    */
    ret = PINT_state_machine_start(smcb, &js);

    /* else ret == SM_ACTION_DEFERRED */

    if (PINT_smcb_complete(smcb))
    {
        gossip_debug(
            GOSSIP_CLIENT_DEBUG, "Posted %s (%lld) (ran to termination)\n",
            PINT_client_get_name_str(pvfs_sys_op), (op_id ? *op_id : -1));

        /* this is done in client_state_machine_terminate now */
        /*
        ret = add_sm_to_completion_list(smcb);
        assert(ret == 0);
        */
    }
    else
    {
        gossip_debug(
            GOSSIP_CLIENT_DEBUG, "Posted %s (%lld) (waiting for test)\n",
            PINT_client_get_name_str(pvfs_sys_op), (op_id ? *op_id : -1));
    }
    return ret;
}

#if 0
PVFS_error PINT_sys_dev_unexp(
    struct PINT_dev_unexp_info *info,
    job_status_s *jstat,
    PVFS_sys_op_id *op_id,
    void *user_ptr)
{
    PVFS_error ret = -PVFS_EINVAL;
    job_id_t id;
    PINT_smcb *smcb = NULL;
    PINT_client_sm *sm_p = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_sys_dev_unexp\n");

    CLIENT_SM_INIT_ONCE();

    /* we require more input args than the regular post method above */
    if (!info || !jstat || !op_id)
    {
        return -PVFS_EINVAL;
    }

    ret = PINT_smcb_alloc(&smcb, PVFS_DEV_UNEXPECTED,
            sizeof(struct PINT_client_sm),
            client_op_state_get_machine,
            client_state_machine_terminate,
            pint_client_sm_context);
    if (ret < 0)
    {
        gossip_lerr("Error: failed to allocate SMCB "
                    "of op type %x\n", PVFS_DEV_UNEXPECTED);
        return ret;
    }
    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    sm_p->user_ptr = user_ptr;
    sm_p->cred_p = NULL;

    memset(jstat, 0, sizeof(job_status_s));
    ret = job_dev_unexp(info, (void *)smcb, 0, jstat, &id,
                        JOB_NO_IMMED_COMPLETE, pint_client_sm_context);
    if (ret < 0)
    {
        PVFS_perror_gossip("PINT_sys_dev_unexp failed", ret);
        PINT_smcb_free(&smcb);
    }
    else
    {
        ret = PINT_id_gen_safe_register(op_id, (void *)smcb);
        sm_p->sys_op_id = *op_id;
    }
    return ret;
}
#endif

/** Cancels in progress I/O operations.
 *
 * \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PINT_client_io_cancel(PVFS_sys_op_id id)
{
    int i = 0;
    PVFS_error ret = -PVFS_EINVAL;
    PINT_smcb *smcb = NULL;
    PINT_client_sm *sm_p = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG,
            "PINT_client_io_cancel id %lld\n",id);

    smcb = PINT_id_gen_safe_lookup(id);
    if (!smcb)
    {
	/* if we can't find it, it may have already completed */
        return 0;
    }
    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    if (!sm_p)
    {
	/* if we can't find it, it may have already completed */
        return 0;
    }

    /* we can't cancel any arbitrary operation */
    assert(PINT_smcb_op(smcb) == PVFS_SYS_IO);

    if (PINT_smcb_complete(smcb))
    {
	/* op already completed; nothing to cancel. */
        return 0;
    }

    /* if we fall to here, the I/O operation is still in flight */
    /* first, set a flag informing the sys_io state machine that the
     * operation has been cancelled so it doesn't post any new jobs 
     */
    PINT_smcb_set_cancelled(smcb);

    /*
      don't return an error if nothing is cancelled, because
      everything may have completed already
    */
    ret = 0;

    /* now run through and cancel the outstanding jobs */
    for(i = 0; i < sm_p->u.io.context_count; i++)
    {
        PINT_client_io_ctx *cur_ctx = &sm_p->u.io.contexts[i];
        assert(cur_ctx);

        if (cur_ctx->msg_send_in_progress)
        {
            gossip_debug(GOSSIP_CANCEL_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Send "
                         "(Request)\n",i);

            ret = job_bmi_cancel(cur_ctx->msg.send_id,
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

            ret = job_bmi_cancel(cur_ctx->msg.recv_id,
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
    PINT_smcb *smcb, *tmp_smcb = NULL;
    PINT_client_sm *sm_p = NULL;
    job_id_t job_id_array[MAX_RETURNED_JOBS];
    job_status_s job_status_array[MAX_RETURNED_JOBS];
    void *smcb_p_array[MAX_RETURNED_JOBS] = {NULL};

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_client_state_machine_test id %lld\n",op_id);

    CLIENT_SM_ASSERT_INITIALIZED();

    job_count = MAX_RETURNED_JOBS;

    if (!error_code)
    {
        return ret;
    }

    smcb = PINT_id_gen_safe_lookup(op_id);
    if (!smcb)
    {
        return ret;
    }

    if (PINT_smcb_complete(smcb))
    {
        sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
        *error_code = sm_p->error_code;
        conditional_remove_sm_if_in_completion_list(smcb);
        return 0;
    }

    ret = job_testcontext(job_id_array,
			  &job_count, /* in/out parameter */
			  smcb_p_array,
			  job_status_array,
			  10,
			  pint_client_sm_context);
    assert(ret > -1);

    /* do as much as we can on every job that has completed */
    for(i = 0; i < job_count; i++)
    {
	tmp_smcb = (PINT_smcb *)smcb_p_array[i];
        assert(tmp_smcb);

#if 0
        /* why is this here - why doesn't an unexpected just terminate? */
        if (PINT_smcb_op(tmp_smcb) == PVFS_DEV_UNEXPECTED)
        {
            /* this is bad - we are using this to skip over some logic */
            /* below - needs to be reworked - WBL */
            PINT_smcb_set_complete(tmp_smcb);
        }
#endif
        if (PINT_smcb_invalid_op(tmp_smcb))
        {
            gossip_err("Invalid sm control block op %d\n", PINT_smcb_op(tmp_smcb));
            continue;
        }
        gossip_debug(GOSSIP_CLIENT_DEBUG, "sm control op %d\n", PINT_smcb_op(tmp_smcb));

        if (!PINT_smcb_complete(tmp_smcb))
        {
            ret = PINT_state_machine_next(tmp_smcb, &job_status_array[i]);

            if (ret != SM_ACTION_DEFERRED &&
                    ret != SM_ACTION_TERMINATE); /* ret == 0 */
            {
                continue;
            }
        }

        /* make sure we don't return internally cancelled I/O jobs */
        /*  This is handled in terminate fn now
        if ((PINT_smcb_op(tmp_smcb) == PVFS_SYS_IO) &&
                (PINT_smcb_cancelled(tmp_smcb)) &&
                (cancelled_io_jobs_are_pending(tmp_smcb)))
        {
            continue;
        }
        */

        /*
         * If we've found a completed operation and it's NOT the op
         * being tested here, we add it to our local completion list so
         * that later calls to the sysint test/testsome can find it.
         * Unexpected messages need to go on the list but are not
         * automatically put there, thus we need this code until
         * we rework the unexpected stuff - WBL
         * add_sm_to_completion_list() does the right thing in
         * determining whether smcb has already been added to
         * completion Q or not 
         */
#if 0
        if ((tmp_smcb != smcb) && (PINT_smcb_complete(tmp_smcb)))
        {
            ret = add_sm_to_completion_list(tmp_smcb);
            assert(ret == 0);
        }
#endif
    }

    if (PINT_smcb_complete(smcb))
    {
        sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
        *error_code = sm_p->error_code;
        conditional_remove_sm_if_in_completion_list(smcb);
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
    PINT_smcb *smcb = NULL;
    job_id_t job_id_array[MAX_RETURNED_JOBS];
    job_status_s job_status_array[MAX_RETURNED_JOBS];
    void *smcb_p_array[MAX_RETURNED_JOBS] = {NULL};

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

    /* check for requests completed previously */
    ret = completion_list_retrieve_completed(
        op_id_array, user_ptr_array, error_code_array, limit, op_count);

    /* return them if found */
    if ((ret == 0) && (*op_count > 0))
    {
        return ret;
    }

    /* see if there are requests ready to make progress */
    ret = job_testcontext(job_id_array,
			  &job_count, /* in/out parameter */
			  smcb_p_array,
			  job_status_array,
			  timeout_ms,
			  pint_client_sm_context);
    assert(ret > -1);

    /* do as much as we can on every job that has completed */
    for(i = 0; i < job_count; i++)
    {
	smcb = (PINT_smcb *)smcb_p_array[i];
        assert(smcb);

#if 0
        /*
          note that dev unexp messages found here are treated as
          complete since if we see them at all in here, they're ready
          to be passed back to the caller
        */
        if (PINT_smcb_op(smcb) == PVFS_DEV_UNEXPECTED)
        {
            PINT_smcb_set_complete(smcb);
        }
#endif

        if (!PINT_smcb_complete(smcb))
        {
            ret = PINT_state_machine_next(smcb, &job_status_array[i]);

            /* (ret < 0) indicates a problem from the job system
             * itself; the return value of the underlying operation is
             * kept in the job status structure.
             */
            if (ret != SM_ACTION_DEFERRED &&
                    ret != SM_ACTION_TERMINATE)
            {
                continue;
            }
        }

        /* make sure we don't return internally cancelled I/O jobs */
        /* now done in terminate function
        if ((PINT_smcb_op(smcb) == PVFS_SYS_IO) &&
                (PINT_smcb_cancelled(smcb)) &&
                (cancelled_io_jobs_are_pending(smcb)))
        {
            continue;
        }
        */

        /*
          by adding the completed op to our completion list, we can
          keep progressing on operations in progress here and just
          grab all completed operations when we're finished
          (i.e. outside of this loop).

          Again, this is here to get the unexpected stuff added to
          the list - we really should not do it this way
        */

        /* add_sm_to_completion_list(smcb) does the right thing if an smcb
         *  was already added to the completion list
         */
#if 0
        if (PINT_smcb_complete(smcb))
        {
            ret = add_sm_to_completion_list(smcb);
            assert(ret == 0);
        }
#endif
    }

    /* terminated SMs have added themselves to the completion list */
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
    PINT_smcb *smcb = NULL;
    PINT_client_sm *sm_p;

    if (in_op_str && out_error && in_class_str)
    {
        smcb = (PINT_smcb *)PINT_id_gen_safe_lookup(op_id);
        assert(smcb);
        sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

        do
        {
            /*
            gossip_debug(GOSSIP_CLIENT_DEBUG,
              "%s: PVFS_i%s_%s calling test()\n",
              __func__, in_class_str, in_op_str);
            */
            ret = PINT_client_state_machine_test(op_id, out_error);

        } while (!PINT_smcb_complete(smcb) && (ret == 0));

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
void PVFS_sys_release(PVFS_sys_op_id op_id)
{
    PINT_smcb *smcb; 
    PINT_client_sm *sm_p; 
    PVFS_credentials *cred_p; 

    gossip_debug(GOSSIP_CLIENT_DEBUG,
              "PVFS_sys_release id %lld\n",op_id);
    smcb = PINT_id_gen_safe_lookup(op_id);
    if (smcb == NULL) 
    {
        return;
    }
    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    if (sm_p == NULL) 
    {
        cred_p = NULL;
    }
    else 
    {
        cred_p = sm_p->cred_p;
    }
    PINT_id_gen_safe_unregister(op_id);

    if (PINT_smcb_op(smcb) && cred_p)
    {
        PVFS_util_release_credentials(cred_p);
        if (sm_p) sm_p->cred_p = NULL;
    }

    PINT_smcb_free(&smcb);
}

/* why is this different??? */
void PVFS_mgmt_release(PVFS_mgmt_op_id op_id)
{
    PINT_smcb *smcb; 
    PINT_client_sm *sm_p; 
    PVFS_credentials *cred_p; 

    gossip_debug(GOSSIP_CLIENT_DEBUG,
              "PVFS_mgmt_release id %lld\n",op_id);
    smcb = PINT_id_gen_safe_lookup(op_id);
    if (smcb == NULL)
    {
        return;
    }
    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    if (sm_p == NULL)
    {
        cred_p = NULL;
    }
    else
    {
        cred_p = sm_p->cred_p;
    }
    PINT_id_gen_safe_unregister(op_id);

    if (PINT_smcb_op(smcb) && cred_p)
    {
        PVFS_util_release_credentials(cred_p);
        if (sm_p) sm_p->cred_p = NULL;
    }

    PINT_smcb_free(&smcb);
}

const char *PINT_client_get_name_str(int op_type)
{
    typedef struct
    {
        int type;
        const char *type_str;
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
        { PVFS_SYS_READDIRPLUS, "PVFS_SYS_READDIR_PLUS" },
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
        { PVFS_SYS_SETEATTR, "PVFS_SYS_SETEATTR" },
        { PVFS_SYS_DELEATTR, "PVFS_SYS_DELEATTR" },
        { PVFS_SYS_LISTEATTR, "PVFS_SYS_LISTEATTR" },
        { PVFS_SERVER_GET_CONFIG, "PVFS_SERVER_GET_CONFIG" },
        { PVFS_CLIENT_JOB_TIMER, "PVFS_CLIENT_JOB_TIMER" },
        { PVFS_DEV_UNEXPECTED, "PVFS_DEV_UNEXPECTED" },
        { PVFS_SYS_FS_ADD, "PVFS_SYS_FS_ADD" },
        { PVFS_SYS_STATFS, "PVFS_SYS_STATFS" },
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

/* exposed wrapper around the client-state-machine testsome function */
int PVFS_sys_testsome(
    PVFS_sys_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms)
{
    return PINT_client_state_machine_testsome(
        op_id_array, op_count, user_ptr_array,
        error_code_array, timeout_ms);
}

int PVFS_sys_wait(
    PVFS_sys_op_id op_id,
    const char *in_op_str,
    int *out_error)
{
    return PINT_client_wait_internal(
        op_id,
        in_op_str,
        out_error,
        "sys");
}

int PVFS_mgmt_testsome(
    PVFS_mgmt_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms)
{
    return PINT_client_state_machine_testsome(
        op_id_array, op_count, user_ptr_array,
        error_code_array, timeout_ms);
}

int PVFS_mgmt_wait(
    PVFS_mgmt_op_id op_id,
    const char *in_op_str,
    int *out_error)
{
    return PINT_client_wait_internal(
        op_id,
        in_op_str,
        out_error,
        "mgmt");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

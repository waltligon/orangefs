/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include <string.h>
#include <assert.h>

#include "pvfs2-sysint.h"
#include "pint-sysint-utils.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
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

static int add_sm_to_completion_list(PINT_client_sm *sm_p)
{
    gen_mutex_lock(&s_completion_list_mutex);
    assert(s_completion_list_index < MAX_RETURNED_JOBS);
    s_completion_list[s_completion_list_index++] = sm_p;
    gen_mutex_unlock(&s_completion_list_mutex);
    return 0;
}

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

static int completion_list_retrieve_completed(
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
int PINT_client_state_machine_post(
    PINT_client_sm *sm_p,
    int pvfs_sys_op,
    PVFS_sys_op_id *op_id,
    void *user_ptr /* in */)
{
    int ret = -PVFS_EINVAL;
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
        ret = add_sm_to_completion_list(sm_p);
        assert(ret == 0);
    }
    return ret;
}

int PINT_sys_dev_unexp(
    struct PINT_dev_unexp_info *info,
    job_status_s *jstat,
    PVFS_sys_op_id *op_id,
    void *user_ptr)
{
    int ret = -PVFS_EINVAL;
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

/* PINT_client_bmi_cancel()
 *
 * wrapper function for job_bmi_cancel
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_client_bmi_cancel(job_id_t id)
{
    return job_bmi_cancel(id, pint_client_sm_context);
}

/* PINT_client_io_cancel()
 *
 * cancels in progress I/O operations
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_client_io_cancel(PVFS_sys_op_id id)
{
    int ret = -PVFS_EINVAL, i = 0;
    PINT_client_sm *sm_p = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PINT_client_io_cancel called\n");

    sm_p = PINT_id_gen_safe_lookup(id);
    if (!sm_p)
    {
	/* we can't find it, maybe it already completed? */
        return 0;
    }

    /* we can't cancel any arbitrary operation */
    assert(sm_p->op == PVFS_SYS_IO);

    if (sm_p->op_complete)
    {
	/* found op, but it has already finished.  Nothing to cancel. */
        return 0;
    }

    /* if we fall to here, the I/O operation is still in flight */
    /* first, set a flag informing the sys_io state machine that the
     * operation has been cancelled so it doesn't post any new jobs 
     */
    sm_p->op_cancelled = 1;

    /* now run through and cancel the outstanding jobs */
    for(i = 0; i < sm_p->u.io.datafile_count; i++)
    {
        PINT_client_io_ctx *cur_ctx = &sm_p->u.io.contexts[i];
        assert(cur_ctx);

        if (cur_ctx->msg_send_in_progress)
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Send "
                         "(Request)\n",i);

            ret = job_bmi_cancel(cur_ctx->msg->send_id,
                                 pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_bmi_cancel failed", ret);
                break;
            }
        }

        if (cur_ctx->msg_recv_in_progress)
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Recv "
                         "(Response)\n",i);

            ret = job_bmi_cancel(cur_ctx->msg->recv_id,
                                 pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_bmi_cancel failed", ret);
                break;
            }
        }

        if (cur_ctx->flow_in_progress)
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG,
                         "[%d] Posting cancellation of type: FLOW\n",i);

            ret = job_flow_cancel(
                cur_ctx->flow_job_id, pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_flow_cancel failed", ret);
                break;
            }
        }

        if (cur_ctx->write_ack_in_progress)
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG,  "[%d] Posting "
                         "cancellation of type: BMI Recv "
                         "(Write Ack)\n",i);

            ret = job_bmi_cancel(cur_ctx->write_ack.recv_id,
                                 pint_client_sm_context);
            if (ret < 0)
            {
                PVFS_perror_gossip("job_bmi_cancel failed", ret);
                break;
            }
        }
    }
    return ret;
}

int PINT_client_state_machine_test(
    PVFS_sys_op_id op_id,
    int *error_code)
{
    int ret = -PVFS_EINVAL, i = 0, job_count = 0;
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

int PINT_client_state_machine_testsome(
    PVFS_sys_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms)
{
    int ret = -PVFS_EINVAL, i = 0;
    int limit = 0, job_count = 0;
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

int PINT_client_wait_internal(
    PVFS_sys_op_id op_id,
    const char *in_op_str,
    int *out_error,
    const char *in_class_str)
{
    int ret = -PVFS_EINVAL;
    PINT_client_sm *sm_p = NULL;

    if (in_op_str && out_error && in_class_str)
    {
        sm_p = (PINT_client_sm *)id_gen_safe_lookup(op_id);
        assert(sm_p);

        do
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG, "PVFS_i%s_%s calling "
                         "PINT_client_state_machine_test()\n",
                         in_class_str, in_op_str);
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

        if (sm_p->acache_hit)
        {
            PINT_acache_release(sm_p->pinode);
        }
        free(sm_p);
    }
}

int PINT_serv_decode_resp(PVFS_fs_id fs_id,
			  void *encoded_resp_p,
			  struct PINT_decoded_msg *decoded_resp_p,
			  PVFS_BMI_addr_t *svr_addr_p,
			  int actual_resp_sz,
			  struct PVFS_server_resp **resp_out_pp)
{
    int ret = -1, server_type = 0;
    PVFS_credentials creds;

    ret = PINT_decode(encoded_resp_p, PINT_DECODE_RESP,
                      decoded_resp_p, /* holds data on decoded resp */
                      *svr_addr_p, actual_resp_sz);
    if (ret > -1)
    {
        *resp_out_pp = (struct PVFS_server_resp *)decoded_resp_p->buffer;
	if ((*resp_out_pp)->op == PVFS_SERV_PROTO_ERROR)
	{

	    gossip_err("Error: server does not seem to understand "
                       "the protocol that this client is using.\n");
	    gossip_err("   Please check server logs for more "
                       "information.\n");

            PVFS_util_gen_credentials(&creds);
	    if (fs_id != PVFS_FS_ID_NULL)
	    {
		const char *server_string = PVFS_mgmt_map_addr(
                    fs_id, &creds, *svr_addr_p, &server_type);
		gossip_err("   Server: %s.\n", server_string);
	    }
	    else
	    {
		gossip_err("   Server: unknown; probably an error "
                           "contacting server listed in pvfs2tab "
                           "file.\n");
	    }
	    return(-EPROTONOSUPPORT);
	}
    }
    return ret;
}

int PINT_serv_free_msgpair_resources(
    struct PINT_encoded_msg *encoded_req_p,
    void *encoded_resp_p,
    struct PINT_decoded_msg *decoded_resp_p,
    PVFS_BMI_addr_t *svr_addr_p,
    int max_resp_sz)
{
    PINT_encode_release(encoded_req_p,
			PINT_ENCODE_REQ);

    /* sm_p->req doesn't go anywhere; we'll use it again. */

    PINT_decode_release(decoded_resp_p,
			PINT_DECODE_RESP);

    BMI_memfree(*svr_addr_p,
		encoded_resp_p,
		max_resp_sz,
		BMI_RECV);

    return 0;
}

/* PINT_serv_msgpair_array_resolve_addrs()
 *
 * fills in BMI address of server for each entry in the msgpair array,
 * based on the handle and fsid
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_serv_msgpairarray_resolve_addrs(
    int count, PINT_client_sm_msgpair_state *msgarray)
{
    int i = 0;
    int ret = -PVFS_EINVAL;

    if ((count > 0) && msgarray)
    {
        for(i = 0; i < count; i++)
        {
            PINT_client_sm_msgpair_state *msg_p = &msgarray[i];
            assert(msg_p);

            ret = PINT_bucket_map_to_server(&msg_p->svr_addr,
                                            msg_p->handle,
                                            msg_p->fs_id);
            if (ret != 0)
            {
                gossip_err("Failed to map server address to handle\n");
                break;
            }
            gossip_debug(GOSSIP_CLIENT_DEBUG,
                         " mapped handle %Lu to server %Ld\n",
                         Lu(msg_p->handle), Ld(msg_p->svr_addr));
        }
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

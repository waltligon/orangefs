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

static job_id_t job_id_array[MAX_RETURNED_JOBS];
static void *client_sm_p_array[MAX_RETURNED_JOBS];
static job_status_s job_status_array[MAX_RETURNED_JOBS];
static int job_count = 0;

int PINT_client_state_machine_post(
    PINT_client_sm *sm_p,
    int pvfs_sys_op,
    PVFS_sys_op_id *op_id,
    void *user_ptr /* in */)
{
    int ret = -PVFS_EINVAL;
    job_status_s js;
    static int got_context = 0;

    if (!sm_p)
    {
        return ret;
    }

    if (got_context == 0)
    {
	/* get a context for our state machine operations */
	job_open_context(&pint_client_sm_context);
	got_context = 1;
    }

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
	default:
            gossip_lerr("FIXME: Unrecognized sysint operation!\n");
            return ret;
    }

    /* clear job status structure */
    memset(&js, 0, sizeof(js));

    /* call function, continue calling as long as we get immediate
     * success.
     */
    ret = sm_p->current_state->state_action(sm_p, &js);
    while (ret == 1)
    {
	/* PINT_state_machine_next() calls next function and
	 * returns the result.
	 */
	ret = PINT_state_machine_next(sm_p, &js);
    }

    /* note: job_status_s pointed to by js_p is ok to use after
     * we return regardless of whether or not we finished.
     */

    if (op_id)
    {
        ret = PINT_id_gen_safe_register(op_id, (void *)sm_p);
    }
    return ret;
}

/* PINT_client_bmi_cancel()
 *
 * wrapper function for job_bmi_cancel that handles race conditions of
 * jobs that have completed in the job_testcontext() loop but have not
 * yet triggered a state transition
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_client_bmi_cancel(job_id_t id)
{
    int i;

    /* TODO: this is not thread safe */
    for(i=0; i<job_count; i++)
    {
	if(job_id_array[i] == id)
	{
	    /* job has already completed; do nothing */
	    return(0);
	}
    }

    return(job_bmi_cancel(id, pint_client_sm_context));
}

int PINT_client_state_machine_test(
    PVFS_sys_op_id op_id,
    int *error_code)
{
    int ret = -PVFS_EINVAL, i = 0;
    PINT_client_sm *sm_p, *tmp_sm_p = NULL;

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
    assert(sm_p);

    if (sm_p->op_complete)
    {
        *error_code = sm_p->error_code;
        return 0;
    }

    /* TODO: this isn't thread safe... */
    ret = job_testcontext(job_id_array,
			  &job_count, /* in/out parameter */
			  client_sm_p_array,
			  job_status_array,
			  100, /* timeout? */
			  pint_client_sm_context);
    assert(ret > -1);

    /* do as much as we can on every job that has completed */
    for(i = 0; i < job_count; i++)
    {
	tmp_sm_p = (PINT_client_sm *)client_sm_p_array[i];

        do
        {
            ret = PINT_state_machine_next(tmp_sm_p, &job_status_array[i]);

        } while (ret == 1);

        /* (ret < 0) indicates a problem from the job system
         * itself; the return value of the underlying operation
         * is kept in the job status structure.
         */
        assert(ret == 0);
    }

    if (sm_p->op_complete)
    {
        *error_code = sm_p->error_code;
    }
    return 0;
}

int PINT_client_state_machine_testsome(PVFS_sys_op_id *op_id_array,
                                       int *op_count) /* in/out */
{
    int ret = -PVFS_EINVAL, i = 0, count = 0;
    int limit = (*op_count - 1);
    PINT_client_sm *sm_p = NULL;

    job_count = MAX_RETURNED_JOBS;

    if (!op_id_array || !op_count)
    {
        return ret;
    }

    /* TODO: this isn't thread safe... */
    ret = job_testcontext(job_id_array,
			  &job_count, /* in/out parameter */
			  client_sm_p_array,
			  job_status_array,
			  100, /* timeout? */
			  pint_client_sm_context);
    assert(ret > -1);

    /* do as much as we can on every job that has completed */
    for(i = 0; i < job_count; i++)
    {
	sm_p = (PINT_client_sm *)client_sm_p_array[i];

        do
        {
            ret = PINT_state_machine_next(sm_p, &job_status_array[i]);

        } while (ret == 1);

        /* (ret < 0) indicates a problem from the job system
         * itself; the return value of the underlying operation
         * is kept in the job status structure.
         */
        assert(ret == 0);

        if (sm_p->op_complete)
        {
            op_id_array[count++] = sm_p->sys_op_id;
            if (count > limit)
            {
                break;
            }
        }
    }

    *op_count = count;

    return 0;
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

        assert(sm_p->cred_p);
        PVFS_util_release_credentials(sm_p->cred_p);
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

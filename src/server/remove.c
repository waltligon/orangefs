/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/*

SMS:  1. Very simple machine.  errors can be reported directly to client.
      2. Request scheduler used, but not sure how good this is.
      3. Documented
				      
SFS:  1. Almost all pre/post
      2. Some assertions
		      
TS:   1. Exists but not thorough.

My TODO list for this SM:

 This state machine is pretty simple and for the most part is hammered out.
 it might need a little more documentation, but that is it.

*/

#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <string.h>
#include <pvfs2-attr.h>
#include <assert.h>
#include <gossip.h>

static int remove_init(state_action_struct *s_op, job_status_s *ret);
static int remove_getattr(state_action_struct *s_op, job_status_s *ret);
static int remove_check_perms(state_action_struct *s_op, job_status_s *ret);
static int remove_cleanup(state_action_struct *s_op, job_status_s *ret);
static int remove_remove(state_action_struct *s_op, job_status_s *ret);
static int remove_release_posted_job(state_action_struct *s_op, job_status_s *ret);
static int remove_send_bmi(state_action_struct *s_op, job_status_s *ret);
void remove_init_state_machine(void);

#ifndef DO_NOT_DEBUG_SERVER_OPS
extern void delete_server_op(state_action_struct *s_op);
#endif
extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s remove_req_s = 
{
	NULL,
	"remove",
	remove_init_state_machine
};

%%

machine remove(init, getattr, check_perms, remove, send, release, cleanup)
{
	state init
	{
		run remove_init;
		default => getattr;
	}

	state getattr
	{
		run remove_getattr;
		success => check_perms;
		default => remove;
	}

	state check_perms
	{
		run remove_check_perms;
		success => remove;
		default => send;
	}

	state remove
	{
		run remove_remove;
		default => send;
	}

	state send
	{
		run remove_send_bmi;
		default => release;
	}

	state release
	{
		run remove_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run remove_cleanup;
		default => init;
	}
}

%%

/*
 * Function: remove_init_state_machine
 *
 * Params:   void
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Set up the state machine for remove. 
 *           
 */

void remove_init_state_machine(void)
{

    remove_req_s.state_machine = remove;

}

/*
 * Function: remove_init
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      Valid request
 *
 * Post:     Job Scheduled
 *
 * Returns:  int
 *
 * Synopsis: Remove is a relatively easy server operation. 
 *           Get attributes if possible after scheduling.
 *           
 */


static int remove_init(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;


    s_op->key.buffer = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));

    job_post_ret = job_req_sched_post(s_op->req,
	    s_op,
	    ret,
	    &(s_op->scheduled_id));

    return(job_post_ret);

}

/*
 * Function: remove_getattr
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      Valid handle
 *
 * Post:     Attributes Structure Obtained if available
 *             --This could be a datafile which has no attribs
 *
 * Returns:  int
 *
 * Synopsis: We need to get the attribute structure if it is available.
 *           If it is not, we just remove it.  TODO: Semantics here!
 *           
 *           
 */


static int remove_getattr(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;
    PVFS_vtag_s bs;

    job_post_ret = job_trove_keyval_read(s_op->req->u.remove.fs_id,
	    s_op->req->u.remove.handle,
	    &(s_op->key),
	    &(s_op->val),
	    0,
	    bs,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);

}

/*
 * Function: remove_check_perms
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      s_op->val.buffer is a valid PVFS_object_attr structure
 *
 * Post:     User has permission to perform operation
 *
 * Returns:  int
 *
 * Synopsis: This should use a global function that verifies that the user
 *           has the necessary permissions to perform the operation it wants
 *           to do.
 *           
 */

static int remove_check_perms(state_action_struct *s_op, job_status_s *ret)
{
    int job_post_ret;
    /*job_id_t i;*/

    job_post_ret = 1;  /* Just pretend it is good right now */
    /*IF THEY don't have permission, set ret->error_code to -ENOPERM!*/

    return(job_post_ret);
}

/*
 * Function: remove_remove
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  int
 *
 * Synopsis: Remove the Dspace
 *           
 */


static int remove_remove(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;

    job_post_ret = job_trove_dspace_remove(
	    s_op->req->u.remove.fs_id,
	    s_op->req->u.remove.handle,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);

}


/*
 * Function: remove_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  int
 *
 * Synopsis: Send a message to the client.  If the dataspace was successfully
 *           removed, send the new handle back.
 *           
 */


static int remove_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    s_op->resp->status = ret->error_code;
    s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);

    /* Set the handle IF it was removed */
    if(ret->error_code == 0) 
    {
	gossip_err("Handle Removed: %lld\n",s_op->req->u.remove.handle);
	s_op->resp->u.generic.handle = ret->handle;

    }

    /* Encode the message */
    job_post_ret = PINT_encode(s_op->resp,
	    PINT_ENCODE_RESP,
	    &(s_op->encoded),
	    s_op->addr,
	    s_op->enc_type);

    assert(job_post_ret == 0);

#ifndef PVFS2_SERVER_DEBUG_BMI

    job_post_ret = job_bmi_send_list(
	    s_op->addr,
	    s_op->encoded.buffer_list,
	    s_op->encoded.size_list,
	    s_op->encoded.list_count,
	    s_op->encoded.total_size,
	    s_op->tag,
	    s_op->encoded.buffer_flag,
	    0,
	    s_op, 
	    ret, 
	    &i);

#else

    job_post_ret = job_bmi_send(
	    s_op->addr,
	    s_op->encoded.buffer_list[0],
	    s_op->encoded.total_size,
	    s_op->tag,
	    s_op->encoded.buffer_flag,
	    0,
	    s_op,
	    ret,
	    &i);

#endif


    return(job_post_ret);

}

/*
 * Function: remove_release_posted_job
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      We are done!
 *
 * Post:     We need to let the next operation go.
 *
 * Returns:  int
 *
 * Synopsis: Free the job from the scheduler to allow next job to proceed.
 */

static int remove_release_posted_job(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    job_post_ret = job_req_sched_release(s_op->scheduled_id,
	    s_op,
	    ret,
	    &i);
    return job_post_ret;
}


/*
 * Function: remove_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


static int remove_cleanup(state_action_struct *s_op, job_status_s *ret)
{

    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    
    if(s_op->val.buffer)
    {
	free(s_op->val.buffer);
    }

    if(s_op->resp)
    {
	free(s_op->resp);
    }

    /*
    BMI_memfree(
	    s_op->addr,
	    s_op->req,
	    s_op->unexp_bmi_buff->size,
	    BMI_RECV_BUFFER
	    );

    */
    free(s_op);

    return(0);

}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

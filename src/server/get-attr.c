/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */


/*

SMS:  1. Errors should be handled correctly
      2. Request Scheduler used
      3. Documented
					      
SFS:  1. Needs some pre/post
      2. Some assertions
		      
TS:   Implemented but not thorough.

My TODO list for this SM:

 Finish asserts and documentation.  Again, this is a fairly trivial machine

*/


#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <string.h>
#include <pvfs2-attr.h>
#include <job-consist.h>
#include <assert.h>

static int getattr_init(state_action_struct *s_op, job_status_s *ret);
static int getattr_cleanup(state_action_struct *s_op, job_status_s *ret);
static int getattr_getobj_attribs(state_action_struct *s_op, job_status_s *ret);
static int getattr_release_posted_job(state_action_struct *s_op, job_status_s *ret);
static int getattr_send_bmi(state_action_struct *s_op, job_status_s *ret);
void getattr_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s getattr_req_s = 
{
	NULL,
	"getattr",
	getattr_init_state_machine
};

%%

machine get_attr(init, cleanup, getobj_attrib, send_bmi, release)
{
	state init
	{
		run getattr_init;
		default => getobj_attrib;
	}

	state getobj_attrib
	{
		run getattr_getobj_attribs;
		default => send_bmi;
	}

	state send_bmi
	{
		run getattr_send_bmi;
		default => release;
	}

	state release
	{
		run getattr_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run getattr_cleanup;
		default => init;
	}
}

%%

/*
 * Function: getattr_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for get_attrib. 
 *           
 */


void getattr_init_state_machine(void)
{

    getattr_req_s.state_machine = get_attr;

}

/*
 * Function: getattr_init
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: We will need to allocate a buffer large enough to store the 
 *           attributes the client is requesting.  Also, schedule it for
 *           consistency semantics.
 *           
 */


static int getattr_init(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;

    s_op->key.buffer = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));
    s_op->resp->op = s_op->req->op;

    job_post_ret = job_req_sched_post(s_op->req,
	    s_op,
	    ret,
	    &(s_op->scheduled_id));

    return(job_post_ret);

}


/*
 * Function: getattr_getobj_attrib
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Post a trove operation to fetch the attributes
 *           
 *           
 */

static int getattr_getobj_attribs(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;
    PVFS_vtag_s bs;

    job_post_ret = job_trove_keyval_read(s_op->req->u.getattr.fs_id,
	    s_op->req->u.getattr.handle,
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
 * Function: getattr_send_bmi
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Send the message and resulting data to the client.
 *           
 */

static int getattr_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    /* This comes from the trove operation.  Note, this operation is still
     * valid even though the operation may have failed.
     */
    memcpy(&(s_op->resp->u.getattr.attr),s_op->val.buffer,sizeof(PVFS_object_attr));

    /* Prepare the message */

    s_op->resp->status = ret->error_code;
    s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);

    if (s_op->val.buffer && ret->error_code == 0)
    {
	job_post_ret = PINT_encode(s_op->resp,
		PINT_ENCODE_RESP,
		&(s_op->encoded),
		s_op->addr,
		s_op->enc_type);
    }
    assert(job_post_ret == 0);
    if(ret->error_code == 0)
	assert(s_op->encoded.buffer_list[0] != NULL);
    else
    {
	/* We have failed somewhere... However, we still need to send what we have */
	/* Set it to a noop for an error so we don't encode all the stuff we don't need to */
	s_op->resp->op = PVFS_SERV_NOOP;
	PINT_encode(s_op->resp,PINT_ENCODE_RESP,&(s_op->encoded),s_op->addr,s_op->enc_type);
	/* set it back */
	((struct PVFS_server_req_s *)s_op->encoded.buffer_list[0])->op = s_op->req->op;
    }

    /* Post message */
    job_post_ret = job_bmi_send(s_op->addr,
	    s_op->encoded.buffer_list[0],
	    s_op->encoded.total_size,
	    s_op->tag,
	    0,
	    0,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);

}


/*
 * Function: getattr_release_posted_job
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

static int getattr_release_posted_job(state_action_struct *s_op, job_status_s *ret)
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
 * Function: getattr_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      Memory has been allocated
 *
 * Post:     All Allocated memory has been freed.
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


static int getattr_cleanup(state_action_struct *s_op, job_status_s *ret)
{

    if(s_op->resp)
    {
	free(s_op->resp);
    }

    if(s_op->req)
    {
	free(s_op->req);
    }

    if(s_op->val.buffer)
    {
	free(s_op->val.buffer);
    }

    free(s_op->unexp_bmi_buff);

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


/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */


#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <string.h>
#include <pvfs2-attr.h>
#include <assert.h>

static int create_init(state_action_struct *s_op, job_status_s *ret);
static int create_cleanup(state_action_struct *s_op, job_status_s *ret);
static int create_create(state_action_struct *s_op, job_status_s *ret);
static int create_release_posted_job(state_action_struct *s_op, job_status_s *ret);
static int create_send_bmi(state_action_struct *s_op, job_status_s *ret);
void create_init_state_machine(void);

PINT_state_machine_s create_req_s = 
{
	NULL,
	"create",
	create_init_state_machine
};

%%

machine create(init, create, send, release, cleanup)
{
	state init
	{
		run create_init;
		default => create;
	}

	state create
	{
		run create_create;
		default => send;
	}

	state send
	{
		run create_send_bmi;
		default => release;
	}

	state release
	{
		run create_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run create_cleanup;
		default => init;
	}
}

%%

/*
 * Function: create_init_state_machine
 *
 * Params:   void
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Set up the state machine for create. 
 *           
 */

void create_init_state_machine(void)
{
	
	create_req_s.state_machine = create;
	
}

/*
 * Function: create_init
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
 * Synopsis: Create is a relatively easy server operation.  We just need to 
 *           schedule the creation of the new dataspace.
 *           
 */


static int create_init(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;

	job_post_ret = job_req_sched_post(s_op->req,
												 s_op,
												 ret,
												 &(s_op->scheduled_id));
	
	return(job_post_ret);
	
}


/*
 * Function: create_create
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
 * Synopsis: Create the new dataspace with the values provided in the response.
 *           
 */


static int create_create(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;

	job_post_ret = job_trove_dspace_create(s_op->req->u.create.fs_id,
														s_op->req->u.create.bucket,
														s_op->req->u.create.handle_mask,
														s_op->req->u.create.object_type,
														NULL,
														s_op,
													 	ret,
													 	&i);
	
	return(job_post_ret);
	
}


/*
 * Function: create_bmi_send
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
 *           created, send the new handle back.
 *           
 */


static int create_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret=0;
	job_id_t i;
	void *a[1];

	s_op->resp->status = ret->error_code;
	s_op->encoded.buffer_list = a[0];

	/* Set the handle IF it was created */
	if(ret->error_code == 0) 
	{
		s_op->resp->u.create.handle = ret->handle;

		/* Encode the message */
		job_post_ret = PINT_encode(s_op->resp,
				PINT_ENCODE_RESP,
				&(s_op->encoded),
				s_op->addr,
				s_op->enc_type);
	}
	assert(job_post_ret == 0);
	
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
 * Function: create_release_posted_job
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

static int create_release_posted_job(state_action_struct *s_op, job_status_s *ret)
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
 * Function: create_cleanup
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


static int create_cleanup(state_action_struct *s_op, job_status_s *ret)
{

	if(s_op->resp)
	{
		free(s_op->resp);
	}

	if(s_op->req)
	{
		free(s_op->req);
	}

	free(s_op->unexp_bmi_buff);

	free(s_op);

	return(0);
	
}

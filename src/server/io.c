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
#include <job-consist.h>
#include <assert.h>

static int io_init(state_action_struct *s_op, job_status_s *ret);
static int io_send_ack(state_action_struct *s_op, job_status_s *ret);
static int io_release(state_action_struct *s_op, job_status_s *ret);
static int io_cleanup(state_action_struct *s_op, job_status_s *ret);
void io_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s io_req_s = 
{
	NULL,
	"io",
	io_init_state_machine
};

/* Ok, here begins the state machine representation */

/* All states must appear within the parameter list to the declared machine
 *
 * Correspondingly, there are two end products, the first of which is success
 * (job_post_ret = 0).  This is denoted in the first state.  
 *
 * There is also a default value.  All states should have a default next state
 * just so that there is no unexpected behaviour.  
 *
 * The termination of each state should be with a call to a non-blocking job
 * interface function.  The value returned from this call should be passed back
 * to the higher level callers.  
 *
 * 
 */

%%

machine io(init, send_ack, cleanup, release)
{
	state init
	{
		run io_init;
		default => send_ack;
	}

	state send_ack 
	{
		run io_send_ack;
		default => release;
	}

	state release
	{
		run io_release;
		default => cleanup;
	}

	state cleanup
	{
		run io_cleanup;
		default => init;
	}
}

%%

/*
 * Function: io_init_state_machine()
 *
 * Params:   void
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Point the state machine at the array produced by the
 * state-comp preprocessor for request
 *           
 */
void io_init_state_machine(void)
{
	io_req_s.state_machine = io;
}

/*
 * Function: io_init()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      a properly formatted request structure.
 *
 * Post:     s_op->scheduled_id filled in.
 *            
 * Returns:  int
 *
 * Synopsis: This function sets up the buffers in preparation for any
 *           operations that require them.  Also runs the operation through
 *           the request scheduler for consistency.
 *           
 */
static int io_init(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_init() executed.\n");
	/* Here, any prep work should be done including calls to allocate memory, 
		including using the cache mechanism
	 */


	/* Note that this call is non-blocking and the return value will be
		handled by the server deamon and correspondingly the state-machine 
		NOTE: THIS SHOULD BE DONE BY ALL REQUESTS
	 */

	/* post a scheduler job */
	job_post_ret = job_req_sched_post(s_op->req,
												 s_op,
												 ret,
												 &(s_op->scheduled_id));

	/* This return value will denote completion of the non-blocking interface call
	   This DOES NOT denote the actual success of the operation.  In other words,
		job_post_ret can be 1 signifying that a job completed, but it completed in
		failure, and that value is denoted in ret->error_code
	*/
	return(job_post_ret);
}

/*
 * Function: io_send_ack()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      Memory is allocated, and we are ready to do what we are going
 *           to do.
 *
 * Post:     Some type of work has been done!
 *            
 * Returns:  int
 *
 * Synopsis: This function should make a call that will perform an operation
 *           be it to Trove, BMI, server_config, etc.  But, the operation is
 *           non-blocking.
 *           
 */
static int io_send_ack(state_action_struct *s_op, job_status_s *ret)
{
	int err = -1;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_send_ack() executed.\n");

	s_op->resp->status = -ENOSYS;
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);

	err = PINT_encode(
		s_op->resp, 
		PINT_ENCODE_RESP, 
		&(s_op->encoded),
		s_op->addr,
		s_op->enc_type);

	if(err < 0)
	{
		/* TODO: what do I do here? */
		gossip_lerr("IO: AIEEEeee! PINT_encode() failure.\n");
		gossip_lerr("IO: returning -1 from state function.\n");
	}
	else
	{
		err = job_bmi_send_list(
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
			&tmp_id);
	}

	return(err);
}

/*
 * Function: io_release()
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
 *           Once we reach this point, we assume all communication has ceased
 *           with the client with respect to this operation, so we must tell 
 *           the scheduler to proceed with the next operation on our handle!
 */
static int io_release(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret=0;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG, "IO: io_release() executed.\n");

	/* let go of our encoded buffer */
	PINT_encode_release(&s_op->encoded, PINT_ENCODE_RESP, 0);

	/* tell the scheduler that we are done with this operation */
	job_post_ret = job_req_sched_release(s_op->scheduled_id,
													  s_op,
													  ret,
													  &i);
	return job_post_ret;
}

/*
 * Function: io_cleanup()
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      NONE
 *
 * Post:     everything is free!
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           We want to return 0 here so that the server thinks
 *           this job is "still processing" and does not try to 
 *           continue working on it.  After this state is done, 
 *           none of the work with respect to this request is valid.
 */
static int io_cleanup(state_action_struct *s_op, job_status_s *ret)
{
	gossip_ldebug(SERVER_DEBUG, "IO: io_cleanup() executed.\n");
	/* Free the encoded message if necessary! */

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

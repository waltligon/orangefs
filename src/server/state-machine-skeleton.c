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

static int request_init(PINT_server_op *s_op, job_status_s *ret);
static int request_do_some_work(PINT_server_op *s_op, job_status_s *ret);
static int request_release_posted_job(PINT_server_op *s_op, job_status_s *ret);
static int request_cleanup(PINT_server_op *s_op, job_status_s *ret);
void request_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s request_req_s = 
{
	NULL,
	"String_Representation",
	request_init_state_machine
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

machine request(init, do_work, cleanup, release)
{
	state init
	{
		run request_init;
		success => do_work;
		default => cleanup;
	}

	state do_work
	{
		run request_do_some_work;
		default => release;
	}

	state release
	{
		run request_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run request_cleanup;
		default => init;
	}
}

%%

/*
 * Function: request_init_state_machine
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

void request_init_state_machine(void)
{
	request_req_s.state_machine = request;
}

/*
 * Function: request_init
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

static int request_init(PINT_server_op *s_op, job_status_s *ret)
{
	int job_post_ret;
	gossip_ldebug(SERVER_DEBUG,
			"Got A Request for %d\n",
			s_op->op);
	
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
 * Function: request_do_some_work
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

static int request_do_some_work(PINT_server_op *s_op, job_status_s *ret)
{
	int job_post_ret = 1;
	
	/* Note that this call is non-blocking and the return value will be
		handled by the server deamon and correspondingly the state-machine 
		NOTE: THIS SHOULD BE DONE BY ALL REQUESTS
	 */

	/* We would post another job here. */
	/*job_post_ret = job_interface_call_goes_here(params!);*/

	/* This return value will denote completion of the non-blocking interface call
	   This DOES NOT denote the actual success of the operation.  In other words,
		job_post_ret can be 1 signifying that a job completed, but it completed in
		failure, and that value is denoted in ret->error_code
	*/
	return(job_post_ret);
}

/*
 * Function: request_release_posted_job
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

static int request_release_posted_job(PINT_server_op *s_op, job_status_s *ret)
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
 * Function: request_cleanup
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

static int request_cleanup(PINT_server_op *s_op, job_status_s *ret)
{
	gossip_ldebug(SERVER_DEBUG,"clean Fxn for request\n");

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

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

#define SKIP_FLOW_STATE 2

static int io_init(state_action_struct *s_op, job_status_s *ret);
static int io_get_size(state_action_struct *s_op, job_status_s *ret);
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

/* This is the state machine for file system I/O operations (both
 * read and write)
 */

/* TODO: need some more states to do things like permissions and
 * access control?
 */

%%

machine io(init, get_size, send_ack, cleanup, release)
{
	state init
	{
		run io_init;
		default => get_size;
	}

	state get_size
	{
		run io_get_size;
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
 *           operations that require them.  Also runs the operation 
 *           through the request scheduler for consistency.
 *           
 */
static int io_init(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_init() executed.\n");

	/* post a scheduler job */
	job_post_ret = job_req_sched_post(s_op->req,
												 s_op,
												 ret,
												 &(s_op->scheduled_id));

	return(job_post_ret);
}

/*
 * Function: io_get_size()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      Memory is allocated, and we are ready to do what we are 
 *           going to do.
 *
 * Post:     Some type of work has been done!
 *            
 * Returns:  int
 *
 * Synopsis: This function should make a call that will perform an 
 *           operation be it to Trove, BMI, server_config, etc.  
 *           But, the operation is non-blocking.
 *           
 */
static int io_get_size(state_action_struct *s_op, job_status_s *ret)
{
	int err = -ENOSYS;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_get_size() executed.\n");

	err = job_trove_dspace_getattr(
		s_op->req->u.io.fs_id,
		s_op->req->u.io.handle,
		s_op,
		ret,
		&tmp_id);

	return(err);
}


/*
 * Function: io_send_ack()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      Memory is allocated, and we are ready to do what we are 
 *           going to do.
 *
 * Post:     Some type of work has been done!
 *            
 * Returns:  int
 *
 * Synopsis: This function should make a call that will perform an 
 *           operation be it to Trove, BMI, server_config, etc.  
 *           But, the operation is non-blocking.
 *           
 */
static int io_send_ack(state_action_struct *s_op, job_status_s *ret)
{
	int err = -1;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_send_ack() executed.\n");

	/* this is where we report the file size to the client before
	 * starting the I/O transfer, or else report an error if we
	 * failed to get the size, or failed for permission reasons
	 */
	s_op->resp->status = ret->error_code;
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);
	s_op->resp->u.io.bstream_size = ret->ds_attr.b_size;

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
 * Synopsis: Free the job from the scheduler to allow next job to 
 *           proceed.  Once we reach this point, we assume all 
 *           communication has ceased with the client with respect 
 *           to this operation, so we must tell the scheduler to 
 *           proceed with the next operation on our handle!
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

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

STATE_FXN_HEAD(create_init);
STATE_FXN_HEAD(create_cleanup);
STATE_FXN_HEAD(create_create);
STATE_FXN_HEAD(create_send_bmi);
void create_init_state_machine(void);

extern char *TROVE_COMMON_KEYS[KEYVAL_ARRAY_SIZE];

PINT_state_machine_s create_req_s = 
{
	NULL,
	"create",
	create_init_state_machine
};

%%

machine create(init, send, cleanup)
{
	state init
	{
		run create_init;
		default => send;
	}

	state send
	{
		run create_send_bmi;
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
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
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
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(create_init)
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
	
	STATE_FXN_RET(job_post_ret);
	
}


/*
 * Function: create_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(create_send_bmi)
{

	int job_post_ret;
	job_id_t i;

	s_op->resp->status = ret->error_code;

	/* Set the handle IF it was created */
	if(ret->error_code == 0) 
		s_op->resp->u.create.handle = ret->handle;
	
	job_post_ret = job_bmi_send(s_op->addr,
										 s_op->resp,
										 sizeof(struct PVFS_server_resp_s),
										 s_op->tag,
										 0,
										 0,
										 s_op, 
										 ret, 
										 &i);
	
	STATE_FXN_RET(job_post_ret);
	
}



/*
 * Function: create_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


STATE_FXN_HEAD(create_cleanup)
{

	if(s_op->resp)
	{
		BMI_memfree(s_op->addr,
				      s_op->resp,
						sizeof(struct PVFS_server_resp_s),
						BMI_SEND_BUFFER);
	}

	if(s_op->req)
	{
		BMI_memfree(s_op->addr,
				      s_op->req,
						sizeof(struct PVFS_server_resp_s),
						BMI_SEND_BUFFER);
	}

	free(s_op->unexp_bmi_buff);

	free(s_op);

	STATE_FXN_RET(0);
	
}

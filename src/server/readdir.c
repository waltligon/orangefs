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

STATE_FXN_HEAD(readdir_init);
STATE_FXN_HEAD(readdir_cleanup);
STATE_FXN_HEAD(readdir_kvread);
STATE_FXN_HEAD(readdir_get_kvspace);
STATE_FXN_HEAD(readdir_send_bmi);
void readdir_init_state_machine(void);

extern char *TROVE_COMMON_KEYS[KEYVAL_ARRAY_SIZE];

PINT_state_machine_s readdir_req_s = 
{
	NULL,
	"readdir",
	readdir_init_state_machine
};

%%

machine readdir(init, kvspace, kvread, send, cleanup)
{
	state init
	{
		run readdir_init;
		default => kvspace;
	}

	state send
	{
		run readdir_send_bmi;
		default => cleanup;
	}

	state cleanup
	{
		run readdir_cleanup;
		default => init;
	}

	state kvspace
	{
		run readdir_get_kvspace;
		success => kvread;
		default => send;
	}
	
	state kvread
	{
		run readdir_kvread;
		default => send;
	}
}

%%

/*
 * Function: readdir_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
 *           
 */

void readdir_init_state_machine(void)
{
	
	readdir_req_s.state_machine = readdir;
	
}

/*
 * Function: readdir_init
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(readdir_init)
{

	int job_post_ret = 0;
	job_id_t i;

#if 0
	job_post_ret = job_trove_dspace_readdir(s_op->req->u.readdir.fs_id,
														s_op->req->u.readdir.bucket,
														s_op->req->u.readdir.handle_mask,
														s_op->req->u.readdir.object_type,
														NULL,
														s_op,
													 	ret,
													 	&i);
#endif
	
	STATE_FXN_RET(job_post_ret);
	
}

/*
 * Function: readdir_kvread
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */

STATE_FXN_HEAD(readdir_kvread)
{

	STATE_FXN_RET(-1);

}

/*
 * Function: readdir_kvspace
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */

STATE_FXN_HEAD(readdir_get_kvspace)
{

	STATE_FXN_RET(-1);

}


/*
 * Function: readdir_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(readdir_send_bmi)
{

	int job_post_ret;
	job_id_t i;

	s_op->resp->status = ret->error_code;
	
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
 * Function: readdir_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


STATE_FXN_HEAD(readdir_cleanup)
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

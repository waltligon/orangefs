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

STATE_FXN_HEAD(getattr_init);
STATE_FXN_HEAD(getattr_cleanup);
STATE_FXN_HEAD(getattr_getobj_attribs);
STATE_FXN_HEAD(getattr_send_bmi);
void getattr_init_state_machine(void);

extern char *TROVE_COMMON_KEYS[KEYVAL_ARRAY_SIZE];

PINT_state_machine_s getattr_req_s = 
{
	NULL,
	"getattr",
	getattr_init_state_machine
};

%%

machine get_attr(init, cleanup, getobj_attrib, send_bmi)
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
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(getattr_init)
{

	int job_post_ret;
	job_id_t i;

	s_op->key.buffer = TROVE_COMMON_KEYS[METADATA_KEY];
	s_op->key.buffer_sz = atoi(TROVE_COMMON_KEYS[METADATA_KEY+1]);

	s_op->val.buffer = (void *) malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));

	job_post_ret = job_check_consistency(s_op->op,
													 s_op->req->u.getattr.fs_id,
													 s_op->req->u.getattr.handle,
													 s_op,
													 ret,
													 &i);
	
	STATE_FXN_RET(job_post_ret);
	
}


/*
 * Function: getattr_getobj_attrib
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 *           
 */

STATE_FXN_HEAD(getattr_getobj_attribs)
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

	STATE_FXN_RET(job_post_ret);

}

/*
 * Function: getattr_send_bmi
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */

STATE_FXN_HEAD(getattr_send_bmi)
{
	
	int job_post_ret=0;
	job_id_t i;
	PVFS_object_attr *foo = NULL;

	if (s_op->val.buffer)
	{
		foo = s_op->val.buffer;
		memcpy(&(s_op->resp->u.getattr.attr),foo,sizeof(PVFS_object_attr));
		gossip_debug(SERVER_DEBUG,"Copying\n");
		//s_op->resp->u.getattr.attr = *(foo);
	}

	/* Prepare the message */
	
	s_op->resp->status = ret->error_code;

	/* Will need to add in the Metafile stuff!!! */
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);

	/* Post message */

	job_post_ret = job_bmi_send(s_op->addr,
										 s_op->resp,
										 s_op->resp->rsize,
										 s_op->tag,
										 0,
										 0,
										 s_op,
										 ret,
										 &i);

	STATE_FXN_RET(job_post_ret);

}

/*
 * Function: getattr_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


STATE_FXN_HEAD(getattr_cleanup)
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

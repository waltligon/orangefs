/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */


#include <state-machine.h>
#include <pvfs2-server.h>
#include <pvfs2-server.h>
#include <string.h>
#include <pvfs2-attr.h>
#include <job-consist.h>

STATE_FXN_HEAD(setattr_init);
STATE_FXN_HEAD(setattr_cleanup);
STATE_FXN_HEAD(setattr_getobj_attribs);
STATE_FXN_HEAD(setattr_setobj_attribs);
STATE_FXN_HEAD(setattr_send_bmi);
void setattr_init_state_machine(void);

extern char *TROVE_COMMON_KEYS[KEYVAL_ARRAY_SIZE];

PINT_state_machine_s setattr_req_s = 
{
	NULL,
	"setattr",
	setattr_init_state_machine
};

%%

machine set_attr(init, cleanup, getobj_attrib, setobj_attrib, send_bmi)
{
	state init
	{
		run setattr_init;
		default => getobj_attrib;
	}

	state getobj_attrib
	{
		run setattr_getobj_attribs;
		success => setobj_attrib;
		default => send_bmi;
	}

	state setobj_attrib
	{
		run setattr_setobj_attribs;
		default => send_bmi;
	}

	state send_bmi
	{
		run setattr_send_bmi;
		default => cleanup;
	}

	state cleanup
	{
		run setattr_cleanup;
		default => init;
	}
}

%%

/*
 * Function: setattr_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
 *           
 */


void setattr_init_state_machine(void)
{
	
	setattr_req_s.state_machine = set_attr;
	
}

/*
 * Function: setattr_init
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(setattr_init)
{

	int job_post_ret;
	job_id_t i;

	s_op->key.buffer = TROVE_COMMON_KEYS[METADATA_KEY];
	s_op->key.buffer_sz = atoi(TROVE_COMMON_KEYS[METADATA_KEY+1]);

	s_op->val.buffer = (void *) malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));
	
	job_post_ret = job_check_consistency(s_op->op,
													 s_op->req->u.setattr.fs_id,
													 s_op->req->u.setattr.handle,
													 s_op,
													 ret,
													 &i);
	
	STATE_FXN_RET(job_post_ret);
	
}


/*
 * Function: setattr_getobj_attrib
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

STATE_FXN_HEAD(setattr_getobj_attribs)
{

	int job_post_ret=0;
	job_id_t i;
	PVFS_vtag_s bs;

	/* 
		If ATTR_TYPE is set, assume that the object was just created.
		Therefore the keyval space does not exist. =)
		TODO: Can I do that?  dw
	*/


#if 0
	if (s_op->req->u.setattr.attrmask & ATTR_TYPE)
	{
		gossip_debug(SERVER_DEBUG,"Returning 1\n");
		STATE_FXN_RET(1);
	}
	else
		{
#endif
		job_post_ret = job_trove_keyval_read(s_op->req->u.setattr.fs_id,
													 	s_op->req->u.setattr.handle,
													 	&(s_op->key),
													 	&(s_op->val),
													 	0,
													 	bs,
													 	s_op,
													 	ret,
													 	&i);
#if 0
		}
#endif

	STATE_FXN_RET(job_post_ret);

}

/*
 * Function: setattr_setobj_attribs
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */

STATE_FXN_HEAD(setattr_setobj_attribs)
{

	PVFS_object_attr *old_attr;
	int job_post_ret=0;
	job_id_t i;

	/* TODO: Check Credentials here */
	if (s_op->val.buffer)
		old_attr = s_op->val.buffer;
	else
		old_attr = (PVFS_object_attr *) malloc(sizeof(PVFS_object_attr));

	/* From here, we check the mask of the attributes. */

#if 0 // Harish changed it!
	if(s_op->req->u.setattr.attrmask & ATTR_UID)
		old_attr->owner = s_op->req->u.setattr.attr.owner;

	if(s_op->req->u.setattr.attrmask & ATTR_GID)
		old_attr->group = s_op->req->u.setattr.attr.group;

	if(s_op->req->u.setattr.attrmask & ATTR_PERM)
		old_attr->perms = s_op->req->u.setattr.attr.perms;

	if(s_op->req->u.setattr.attrmask & ATTR_ATIME)
		old_attr->atime = s_op->req->u.setattr.attr.atime;

	if(s_op->req->u.setattr.attrmask & ATTR_CTIME)
		old_attr->ctime = s_op->req->u.setattr.attr.ctime;

	if(s_op->req->u.setattr.attrmask & ATTR_MTIME)
		old_attr->mtime = s_op->req->u.setattr.attr.mtime;

	if(s_op->req->u.setattr.attrmask & ATTR_TYPE)
		old_attr->objtype = s_op->req->u.setattr.attr.objtype;
#endif

	/* TODO: What to do about these unions, inc. the one with a ptr.
	 *	TODO:	Also what about ATTR_SIZE?? 
	 */
	
	if(s_op->req->u.setattr.attrmask & ATTR_META)
		old_attr->u.meta = s_op->req->u.setattr.attr.u.meta;

	if(s_op->req->u.setattr.attrmask & ATTR_DATA)
		old_attr->u.data = s_op->req->u.setattr.attr.u.data;

	if(s_op->req->u.setattr.attrmask & ATTR_DIR)
		old_attr->u.dir = s_op->req->u.setattr.attr.u.dir;

	if(s_op->req->u.setattr.attrmask & ATTR_SYM)
		old_attr->u.sym = s_op->req->u.setattr.attr.u.sym;


	job_post_ret = job_trove_keyval_write(s_op->req->u.setattr.fs_id,
													 s_op->req->u.setattr.handle,
													 &(s_op->key),
													 &(s_op->val),
													 0,
													 ret->vtag, /* This needs to change for vtags */
													 s_op,      /* Or is that right? dw */
													 ret,
													 &i);
	
	STATE_FXN_RET(job_post_ret);

}

/*
 * Function: setattr_send_bmi
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */

STATE_FXN_HEAD(setattr_send_bmi)
{
	
	int job_post_ret=0;
	job_id_t i;

	/* Prepare the message */
	
	s_op->resp->u.generic.handle = s_op->req->u.setattr.handle;
	s_op->resp->status = ret->error_code;
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
 * Function: setattr_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


STATE_FXN_HEAD(setattr_cleanup)
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

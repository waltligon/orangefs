/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <state-machine.h>
#include <pvfs2-server.h>
#include <pvfs2-server.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-attr.h"
#include "job-consist.h"

static int setattr_init(state_action_struct *s_op, job_status_s *ret);
static int setattr_cleanup(state_action_struct *s_op, job_status_s *ret);
static int setattr_getobj_attribs(state_action_struct *s_op, job_status_s *ret);
static int setattr_setobj_attribs(state_action_struct *s_op, job_status_s *ret);
static int setattr_send_bmi(state_action_struct *s_op, job_status_s *ret);
static int setattr_release_posted_job(state_action_struct *s_op, job_status_s *ret);
void setattr_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s setattr_req_s = 
{
	NULL,
	"setattr",
	setattr_init_state_machine
};


%%

machine set_attr(init, cleanup, getobj_attrib, setobj_attrib, send_bmi, release)
{
	state init
	{
		run setattr_init;
		default => getobj_attrib;
	}

	state getobj_attrib
	{
		run setattr_getobj_attribs;
		default => setobj_attrib;
	}

	state setobj_attrib
	{
		run setattr_setobj_attribs;
		default => send_bmi;
	}

	state send_bmi
	{
		run setattr_send_bmi;
		default => release;
	}

	state release
	{
		run setattr_release_posted_job;
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


static int setattr_init(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;

    s_op->key.buffer = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer = (void *) malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));

    /* post a scheduler job */
    job_post_ret = job_req_sched_post(s_op->req,
	    s_op,
	    ret,
	    &(s_op->scheduled_id));

    return(job_post_ret);

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

static int setattr_getobj_attribs(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    /* 
       If ATTR_TYPE is set, assume that the object was just created.
       Therefore the keyval space does not exist. =)
TODO: Can I do that?  dw
     */


#if 0
    if (s_op->req->u.setattr.attrmask & ATTR_TYPE)
    {
	gossip_debug(SERVER_DEBUG,"Returning 1\n");
	return(1);
    }
    else
    {
#endif
	job_post_ret = job_trove_keyval_read(s_op->req->u.setattr.fs_id,
		s_op->req->u.setattr.handle,
		&(s_op->key),
		&(s_op->val),
		0,
		NULL,
		s_op,
		ret,
		&i);
#if 0
    }
#endif

    return(job_post_ret);

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

static int setattr_setobj_attribs(state_action_struct *s_op, job_status_s *ret)
{

    /*PVFS_object_attr *old_attr;*/
    int job_post_ret=0;
    job_id_t i;

#if 0
    /* TODO: Check Credentials here */
    if (s_op->val.buffer)
	old_attr = s_op->val.buffer;
    else
#endif

	free(s_op->val.buffer);
    s_op->val.buffer = &(s_op->req->u.setattr.attr);
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
#endif


    gossip_debug(SERVER_DEBUG,"Writing trove values\n");
    job_post_ret = job_trove_keyval_write(
	    s_op->req->u.setattr.fs_id,
	    s_op->req->u.setattr.handle,
	    &(s_op->key),
	    &(s_op->val),
	    TROVE_SYNC /* flags */,
	    NULL,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);

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

static int setattr_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    /* Prepare the message */

    s_op->resp->u.generic.handle = s_op->req->u.setattr.handle;
    s_op->resp->status = ret->error_code;
    gossip_ldebug(SERVER_DEBUG,"Returning Status %d\n",ret->error_code);
    s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);
    
    s_op->encoded.dest = s_op->addr;

    job_post_ret = PINT_encode(s_op->resp,
	    PINT_ENCODE_RESP,
	    &(s_op->encoded),
	    s_op->addr,
	    s_op->enc_type);

    assert(job_post_ret == 0);
    
    /* Post message */

#ifdef PVFS2_SERVER_DEBUG_BMI

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
#ifdef PVFS2_SERVER_DEBUG_BMI
	    s_op->encoded.buffer_flag,
#else
	    0,
#endif
	    0,
	    s_op,
	    ret,
	    &i);

#endif


    return(job_post_ret);

}

/*
 * Function: setattr_release_posted_job
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

static int setattr_release_posted_job(state_action_struct *s_op, job_status_s *ret)
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


static int setattr_cleanup(state_action_struct *s_op, job_status_s *ret)
{

    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);

    /*
    if(s_op->val.buffer)
    {
	free(s_op->val.buffer);
    }
    */

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


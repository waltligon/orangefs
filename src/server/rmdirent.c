/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/*

SMS:  1. Very simple machine.  errors can be reported directly to client.
      2. Request scheduler used, but not sure how good this is.
      3. Documented
				      
SFS:  1. Almost all pre/post
      2. Some assertions
		      
TS:   1. Exists but not thorough.

My TODO list for this SM:

 This state machine is pretty simple and for the most part is hammered out.
 it might need a little more documentation, but that is it.

*/

#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <string.h>
#include <pvfs2-attr.h>
#include <assert.h>
#include <gossip.h>

static int rmdirent_init(state_action_struct *s_op, job_status_s *ret);
static int rmdirent_getattr(state_action_struct *s_op, job_status_s *ret);
static int rmdirent_check_perms(state_action_struct *s_op, job_status_s *ret);
static int rmdirent_cleanup(state_action_struct *s_op, job_status_s *ret);
static int rmdirent_rmdirent(state_action_struct *s_op, job_status_s *ret);
static int rmdirent_release_job(state_action_struct *s_op, job_status_s *ret);
static int rmdirent_send_bmi(state_action_struct *s_op, job_status_s *ret);
void rmdirent_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s rmdirent_req_s = 
{
	NULL,
	"rmdirent",
	rmdirent_init_state_machine
};

%%

machine rmdirent(init, getattr, check_perms, rmdirent_entry, send, release, cleanup)
{
	state init
	{
		run rmdirent_init;
		default => getattr;
	}

	state getattr
	{
		run rmdirent_getattr;
		success => check_perms;
		default => send;
	}

	state check_perms
	{
		run rmdirent_check_perms;
		success => rmdirent_entry;
		default => send;
	}

	state rmdirent_entry
	{
		run rmdirent_rmdirent;
		default => send;
	}

	state send
	{
		run rmdirent_send_bmi;
		default => release;
	}

	state release
	{
		run rmdirent_release_job;
		default => cleanup;
	}

	state cleanup
	{
		run rmdirent_cleanup;
		default => init;
	}
}

%%

/*
 * Function: rmdirent_init_state_machine
 *
 * Params:   void
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Set up the state machine for rmdirent. 
 *           
 */

void rmdirent_init_state_machine(void)
{

    rmdirent_req_s.state_machine = rmdirent;

}

/*
 * Function: rmdirent_init
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
 * Synopsis: Remove is a relatively easy server operation. 
 *           Get attributes if possible after scheduling.
 *           
 */


static int rmdirent_init(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;


    s_op->key.buffer = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));

    job_post_ret = job_req_sched_post(s_op->req,
	    s_op,
	    ret,
	    &(s_op->scheduled_id));

    return(job_post_ret);

}

/*
 * Function: rmdirent_getattr
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      Valid handle
 *
 * Post:     Attributes Structure Obtained if available. If not... 
 *           send an error right now.
 *
 * Returns:  int
 *
 * Synopsis: We need to get the attribute structure if it is available.
 *           If it is not, we just send an error to the client.  
 *           TODO: Semantics here!
 *           
 *           
 */


static int rmdirent_getattr(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;

    job_post_ret = job_trove_keyval_read(
	    s_op->req->u.rmdirent.fs_id,
	    s_op->req->u.rmdirent.parent_handle,
	    &(s_op->key),
	    &(s_op->val),
	    0,
	    NULL,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);

}

/*
 * Function: rmdirent_check_perms
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      s_op->val.buffer is a valid PVFS_object_attr structure
 *
 * Post:     User has permission to perform operation
 *
 * Returns:  int
 *
 * Synopsis: This should use a global function that verifies that the user
 *           has the necessary permissions to perform the operation it wants
 *           to do.
 *           
 */

static int rmdirent_check_perms(state_action_struct *s_op, job_status_s *ret)
{
    int job_post_ret;
    job_id_t i;

    job_post_ret = 1;  /* Just pretend it is good right now */
    /*IF THEY don't have permission, set ret->error_code to -ENOPERM!*/

    /* From here, the user has permission.  We need to get the second k/v
       space so we can remove the key */
    s_op->key.buffer = Trove_Common_Keys[DIR_ENT_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

    /* 
       Recall that we previously allocated a buffer for object attribs that 
       we used above!
     */
    assert(s_op->val.buffer_sz >= sizeof(PVFS_handle));

    s_op->val.buffer_sz = sizeof(PVFS_handle);

    job_post_ret = job_trove_keyval_read(
	    s_op->req->u.rmdirent.fs_id,
	    s_op->req->u.rmdirent.parent_handle,
	    &(s_op->key),
	    &(s_op->val),
	    0,
	    NULL,
	    s_op,
	    ret,
	    &i);


    return(job_post_ret);
}

/*
 * Function: rmdirent_rmdirent
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
 * Synopsis: Remove the Dspace
 *           
 */


static int rmdirent_rmdirent(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;
    PVFS_handle h;

    h = *((PVFS_handle *)s_op->val.buffer);

    s_op->key.buffer = s_op->req->u.rmdirent.entry;
    s_op->key.buffer_sz = strlen((char *)s_op->key.buffer)+1;

    job_post_ret = job_trove_keyval_remove(
	    s_op->req->u.rmdirent.fs_id,
	    h,
	    &(s_op->key),
	    TROVE_SYNC,
	    NULL,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);

}


/*
 * Function: rmdirent_bmi_send
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
 * Synopsis: Send a message to the client.  
 *           If the entry was successfully
 *           removed, send the handle back.
 *           
 */


static int rmdirent_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    s_op->resp->status = ret->error_code;
    s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);

    /* Set the handle IF it was removed */
    if(ret->error_code == 0) 
    {
	gossip_err("Dirent Removed from : %lld\n",
		s_op->req->u.rmdirent.parent_handle);
	s_op->resp->u.generic.handle = s_op->req->u.rmdirent.parent_handle;

    }

    /* Encode the message */
    job_post_ret = PINT_encode(s_op->resp,
	    PINT_ENCODE_RESP,
	    &(s_op->encoded),
	    s_op->addr,
	    s_op->enc_type);

    assert(job_post_ret == 0);

#ifndef PVFS2_SERVER_DEBUG_BMI

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
	    s_op->encoded.buffer_flag,
	    0,
	    s_op,
	    ret,
	    &i);

#endif


    return(job_post_ret);

}

/*
 * Function: rmdirent_release_job
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

static int rmdirent_release_job(state_action_struct *s_op, job_status_s *ret)
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
 * Function: rmdirent_cleanup
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


static int rmdirent_cleanup(state_action_struct *s_op, job_status_s *ret)
{

    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    
    if(s_op->val.buffer)
    {
	free(s_op->val.buffer);
    }

    if(s_op->resp)
    {
	free(s_op->resp);
    }

    /*
    BMI_memfree(
	    s_op->addr,
	    s_op->unexp_bmi_buff.buffer,
	    s_op->unexp_bmi_buff.size,
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

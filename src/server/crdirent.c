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

static int crdirent_init(state_action_struct *s_op, job_status_s *ret);
static int crdirent_gethandle(state_action_struct *s_op, job_status_s *ret);
static int crdirent_getattr(state_action_struct *s_op, job_status_s *ret);
static int crdirent_check_perms(state_action_struct *s_op, job_status_s *ret);
static int crdirent_create(state_action_struct *s_op, job_status_s *ret);
static int crdirent_create_dir_handle_ph1(state_action_struct *s_op, job_status_s *ret);
static int crdirent_create_dir_handle_ph2(state_action_struct *s_op, job_status_s *ret);
static int crdirent_send_bmi(state_action_struct *s_op, job_status_s *ret);
static int crdirent_cleanup(state_action_struct *s_op, job_status_s *ret);
static int crdirent_release_posted_job(state_action_struct *s_op, job_status_s *ret);
void crdirent_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s crdirent_req_s = 
{
	NULL,
	"crdirent_dirent",
	crdirent_init_state_machine
};

%%

machine crdirent(init, get_handle, get_attrib, check_perms, create, send, cleanup, create_handle1, create_handle2,release)
{
	state init
	{
		run crdirent_init;
		default => get_attrib;
	}

	state get_attrib
	{
		run crdirent_getattr;
		success => check_perms;
		default => send;
	}

	state check_perms
	{
		run crdirent_check_perms;
		success => get_handle;
		default => send;
	}
	
	state get_handle
	{
		run crdirent_gethandle;
		success => create;
		default => create_handle1;
	}

	state create
	{
		run crdirent_create;
		default => send;
	}

	state create_handle1
	{
		run crdirent_create_dir_handle_ph1;
		success => create_handle2;
		default => send;
	}

	state create_handle2
	{
		run crdirent_create_dir_handle_ph2;
		success => create;
		default => send;
	}

	state send
	{
		run crdirent_send_bmi;
		default => release;
	}

	state release
	{
		run crdirent_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run crdirent_cleanup;
		default => init;
	}
}

%%

/*
 * Function: crdirent_init_state_machine
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
 * state-comp preprocessor for crdirent
 *           
 */

void crdirent_init_state_machine(void)
{
	crdirent_req_s.state_machine = crdirent;
}

/*
 * Function: crdirent_init
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
 * Synopsis: This function sets up the buffers in preparation for
 *           the trove operation to get the attribute structure
 *           used in check permissions.  Also runs the operation through
 *           the request scheduler for consistency.
 *           
 */

static int crdirent_init(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	gossip_ldebug(SERVER_DEBUG,
			"Got CrDirent for %s,%lld in %lld\n",
			s_op->req->u.crdirent.name,
			s_op->req->u.crdirent.new_handle,
			s_op->req->u.crdirent.parent_handle);

	/* get the key and key size out of our list of common keys */
	s_op->key.buffer = Trove_Common_Keys[METADATA_KEY].key;
	s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

	/* create a buffer for the trove operations (get_attrib and get_handle) */
	s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));
	
	/* post a scheduler job */
	job_post_ret = job_req_sched_post(s_op->req,
												 s_op,
												 ret,
												 &(s_op->scheduled_id));
	gossip_ldebug(SERVER_DEBUG,"jpr: %d\n",job_post_ret);
	return(job_post_ret);
}

/*
 * Function: crdirent_gethandle
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      s_op->u.crdirent.parent_handle is handle of directory
 *
 * Post:     s_op->val.buffer is the directory entry k/v space OR NULL
 *           if first entry
 *
 * Returns:  int
 *
 * Synopsis: Get the directory entry handle for the directory entry k/v space.
 *           Recall that directories have two key-val spaces, one of which is 
 *           synonymous with files where the metadata is stored.  The other
 *           space holds the filenames and their handles.  In this function, 
 *           we attempt to retrieve the handle for the filename/handle key/val
 *           space and if it does not exist, we need to create it.
 *
 *           TODO: Semantics here of whether we want to create it here, or upon
 *                 the creation of the directory. 
 *           
 */

static int crdirent_gethandle(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	job_id_t i;
	PVFS_vtag_s bs;

	gossip_ldebug(SERVER_DEBUG,"Get Handle Fxn for crdirent\n");
	
	/* get the key and key size out of our list of common keys */
	s_op->key.buffer = Trove_Common_Keys[DIR_ENT_KEY].key;
	s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

	/* 
	 *	Assert that the buffer is big enough to hold a handle.
	 * Recall that the buffer was previously allocated for the permission checking
	 */
	assert(s_op->val.buffer != NULL && s_op->val.buffer_sz >= sizeof(PVFS_handle));
	
	job_post_ret = job_trove_keyval_read(s_op->req->u.crdirent.fs_id,
													 	 s_op->req->u.crdirent.parent_handle,
													 	 &(s_op->key),
													 	 &(s_op->val),
													 	 0,
													 	 bs,
													 	 s_op,
													 	 ret,
													 	 &i);


	return(job_post_ret);
}

/*
 * Function: crdirent_getattr
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      s_op->val.buffer is big enough to hold sizeof(PVFS_object_attr)
 *           s_op->u.crdirent.parent_handle is the correct directory entry.
 *
 * Post:     s_op->val.buffer contains the object attribs for directory used
 *                     in check permissions.
 *
 * Returns:  int
 *
 * Synopsis: Just returned from scheduler. init has set up key and val buffer.
 *           Post Trove job to read metadata for the parent directory so we can make sure
 *           that the credentials are valid.
 *           
 */

static int crdirent_getattr(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	PVFS_vtag_s bs;

	gossip_ldebug(SERVER_DEBUG,"Get attr Fxn for crdirent\n");
	job_post_ret = job_trove_keyval_read(s_op->req->u.crdirent.fs_id,
													 s_op->req->u.crdirent.parent_handle,
													 &(s_op->key),
													 &(s_op->val),
													 0,
													 bs,
													 s_op,
													 ret,
													 &i);

	return(job_post_ret);
}

/*
 * Function: crdirent_check_perms
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

static int crdirent_check_perms(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	/*job_id_t i;*/

	gossip_ldebug(SERVER_DEBUG,"CheckPerms Fxn for crdirent\n");
	job_post_ret = 1;  /* Just pretend it is good right now */
	/*IF THEY don't have permission, set ret->error_code to -ENOPERM!*/

	return(job_post_ret);
}

/*
 * Function: crdirent_create_dir_handle_ph2
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      ret->handle is the new directory entry k/v space
 *
 * Post:     ret->handle is stored in the original k/v space for the
 *           parent handle.
 *
 * Returns:  int
 *
 * Synopsis: We are storing the newly created k/v space for future
 *           directory entries.
 *           
 */

static int crdirent_create_dir_handle_ph2(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG,"phase2 Fxn for crdirent\n");

	/* get the key and key size out of our list of common keys */
	s_op->key.buffer = Trove_Common_Keys[DIR_ENT_KEY].key;
	s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

	/*
	 * Recall again, this buffer was originally allocated for check_permissions
	 * Let's assert and continue.
	 */
	assert(s_op->val.buffer != NULL && s_op->val.buffer_sz >= sizeof(PVFS_handle));

	/* we are writing to Trove, so the val is set up here */
	*((PVFS_handle *)s_op->val.buffer) = ret->handle;
	s_op->val.buffer_sz = sizeof(PVFS_handle);

	/* adds the k/v space */
	job_post_ret = job_trove_keyval_write(s_op->req->u.crdirent.fs_id,
													  s_op->req->u.crdirent.parent_handle,
													  &(s_op->key),
													  &(s_op->val),
													  0,
													  ret->vtag,
													  s_op,
													  ret,
													  &i);
	return(job_post_ret);
}

/*
 * Function: crdirent_create_dir_handle_ph1
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      ret->handle = NULL
 *           ret->error_code < 0
 *
 * Post:     ret->handle contains a new handle for a k/v space
 *
 * Returns:  int
 *
 * Synopsis: If we execute this function, this directory does not have
 *           any entries in it.  So we need to create a key val space
 *           for these entries.  This is the first part, and we store
 *           it in part two.
 */

static int crdirent_create_dir_handle_ph1(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG,"CrDirent Phase 1 Creating Handle:  %d,%lld\n",
			s_op->req->u.crdirent.fs_id,\
			s_op->req->u.crdirent.parent_handle);

	job_post_ret = job_trove_dspace_create(s_op->req->u.crdirent.fs_id,
													   s_op->req->u.crdirent.parent_handle,
													   0x00000000, /* TODO: Change this */
													   ATTR_DIR,
													   NULL,
													   s_op,
													   ret,
													   &i);
	return(job_post_ret);
}

/*
 * Function: crdirent_create
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      ret->handle is the directory entry k/v space
 *           s_op->req->u.crdirent.name != NULL
 *           s_op->req->u.crdirent.new_handle != NULL
 *           ADD ASSERTS FOR THESE!
 *
 * Post:     key/val pair stored
 *
 * Returns:  int
 *
 * Synopsis: We are now ready to store the name/handle pair in the k/v
 *           space for directory handles.
 */

static int crdirent_create(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret;
	job_id_t i;
	PVFS_handle h;

	gossip_ldebug(SERVER_DEBUG,"create Fxn for crdirent\n");

	/* Verify that we have all the information we need to store the pair */
	assert(s_op->req->u.crdirent.name != NULL && s_op->req->u.crdirent.new_handle != 0);
	
	/* This buffer came from one of two places, either phase two of creating the
	 * directory space when we wrote the value back to trove, or from the initial read
	 * from trove.
	 */
	h = *((PVFS_handle *)s_op->val.buffer);
	
	/* this is the name for the parent entry */
	s_op->key.buffer = &(s_op->req->u.crdirent.name);
	s_op->key.buffer_sz = strlen(s_op->req->u.crdirent.name) + 1;

	/* this is the name for the new entry */
	s_op->val.buffer = &(s_op->req->u.crdirent.new_handle);
	s_op->val.buffer_sz = sizeof(PVFS_handle);
	
	job_post_ret = job_trove_keyval_write(s_op->req->u.crdirent.fs_id,
													  h,
													  &(s_op->key),
													  &(s_op->val),
													  0,
													  ret->vtag, /* This needs to change for vtags */
													  s_op,      /* Or is that right? dw */
													  ret,
													  &i);
	return(job_post_ret);
}

/*
 * Function: crdirent_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      NONE
 *
 * Post:     BMI_message sent.
 *
 * Returns:  int
 *
 * Synopsis: We sent a response to the request using BMI.
 *           This function is abstract because we really don't know where
 *           we failed or if we succeeded in our mission.  It sets the
 *           error_code, and here, it is just an acknowledgement.
 */

static int crdirent_send_bmi(state_action_struct *s_op, job_status_s *ret)
{
	int job_post_ret=0;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG,"send Fxn for crdirent\n");

	s_op->resp->status = ret->error_code;

	/* Set the ack IF it was created */
	if(ret->error_code == 0) 
	{
		s_op->resp->u.generic.handle = s_op->req->u.crdirent.new_handle;

		/* Encode the message */
		job_post_ret = PINT_encode(s_op->resp,
				PINT_ENCODE_RESP,
				&(s_op->encoded),
				s_op->addr,
				s_op->enc_type);
	}
	else
	{
		/* Set it to a noop for an error so we don't encode all the stuff we don't need to */
		s_op->resp->op = PVFS_SERV_NOOP;
		PINT_encode(s_op->resp,PINT_ENCODE_RESP,&(s_op->encoded),s_op->addr,s_op->enc_type);
		/* set it back */
		((struct PVFS_server_req_s *)s_op->encoded.buffer_list[0])->op = s_op->req->op;
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
 * Function: crdirent_release_posted_job
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

static int crdirent_release_posted_job(state_action_struct *s_op, job_status_s *ret)
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
 * Function: crdirent_cleanup
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
 */

static int crdirent_cleanup(state_action_struct *s_op, job_status_s *ret)
{
	gossip_ldebug(SERVER_DEBUG,"clean Fxn for crdirent\n");

/* TODO: FREE Encoded message! */
	
	if(s_op->val.buffer)
	{
		free(s_op->val.buffer);
	}

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

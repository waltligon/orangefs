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

STATE_FXN_HEAD(crdirent_init);
STATE_FXN_HEAD(crdirent_gethandle);
STATE_FXN_HEAD(crdirent_getattr);
STATE_FXN_HEAD(crdirent_check_perms);
STATE_FXN_HEAD(crdirent_create);
STATE_FXN_HEAD(crdirent_create_dir_handle_ph1);
STATE_FXN_HEAD(crdirent_create_dir_handle_ph2);
STATE_FXN_HEAD(crdirent_send_bmi);
STATE_FXN_HEAD(crdirent_cleanup);
void crdirent_init_state_machine(void);

extern char *TROVE_COMMON_KEYS[KEYVAL_ARRAY_SIZE];

PINT_state_machine_s crdirent_req_s = 
{
	NULL,
	"crdirent_dirent",
	crdirent_init_state_machine
};

%%

machine crdirent(init, get_handle, get_attrib, check_perms, create, send, cleanup, create_handle1, create_handle2)
{
	state init
	{
		run crdirent_init;
		default => get_attrib;
	}

	state create_handle2
	{
		run crdirent_create_dir_handle_ph2;
		success => create;
		default => send;
	}

	state create_handle1
	{
		run crdirent_create_dir_handle_ph1;
		success => create_handle2;
		default => send;
	}

	state get_handle
	{
		run crdirent_gethandle;
		success => create;
		default => create_handle1;
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
	
	state create
	{
		run crdirent_create;
		default => send;
	}

	state send
	{
		run crdirent_send_bmi;
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
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
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
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(crdirent_init)
{

	int job_post_ret;
	job_id_t i;
	gossip_debug(SERVER_DEBUG,"Got CrDirent for %s,%lld in %lld\n",s_op->req->u.crdirent.name,s_op->req->u.crdirent.new_handle,s_op->req->u.crdirent.parent_handle);

#if 0
	s_op->key_a = (PVFS_ds_keyval_s *) malloc(2*sizeof(PVFS_ds_keyval_s));
	s_op->val_a = (PVFS_ds_keyval_s *) malloc(2*sizeof(PVFS_ds_keyval_s));

	s_op->key_a[0].buffer_sz = s_op->key_a[1].buffer_sz = \
	atoi(TROVE_COMMON_KEYS[DIR_ENT_KEY+1]) > atoi(TROVE_COMMON_KEYS[METADATA_KEY+1]) ? \
	atoi(TROVE_COMMON_KEYS[DIR_ENT_KEY+1]) : atoi(TROVE_COMMON_KEYS[METADATA_KEY+1]);

	s_op->val_a[0].buffer_sz = s_op->val_a[1].buffer_sz = \
	PVFS_NAME_MAX+1 > sizeof(PVFS_object_attr) ? \
	PVFS_NAME_MAX+1 : sizeof(PVFS_object_attr);

	s_op->key_a[0].buffer = malloc(s_op->key_a[0].buffer_sz);
	s_op->key_a[1].buffer = malloc(s_op->key_a[1].buffer_sz);
	s_op->val_a[0].buffer = malloc(s_op->val_a[0].buffer_sz);
	s_op->val_a[1].buffer = malloc(s_op->val_a[1].buffer_sz);
#endif

	s_op->key.buffer = TROVE_COMMON_KEYS[METADATA_KEY];
	s_op->key.buffer_sz = atoi(TROVE_COMMON_KEYS[METADATA_KEY+1]);

	s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));
	
	job_post_ret = job_check_consistency(s_op->op,
													 s_op->req->u.crdirent.fs_id,
													 s_op->req->u.crdirent.new_handle,
													 s_op,
												 	 ret,
													 &i);
	
	STATE_FXN_RET(job_post_ret);
	
}


/*
 * Function: crdirent_gethandle
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Get the directory entry handle
 *           
 */


STATE_FXN_HEAD(crdirent_gethandle)
{

	int job_post_ret;
	job_id_t i;
	PVFS_vtag_s bs;

	gossip_debug(SERVER_DEBUG,"Get Handle Fxn for crdirent\n");
	
	s_op->key.buffer = TROVE_COMMON_KEYS[DIR_ENT_KEY];
	s_op->key.buffer_sz = atoi(TROVE_COMMON_KEYS[DIR_ENT_KEY+1]);

	s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_handle)));
	
	job_post_ret = job_trove_keyval_read(s_op->req->u.crdirent.fs_id,
													 	 s_op->req->u.crdirent.parent_handle,
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
 * Function: crdirent_getattr
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Ok... here is what we have... Two Values, one of which is the handle,
 *           The other is the metadata... yay... at this stage, insert a new key and rock!
 *           
 */


STATE_FXN_HEAD(crdirent_getattr)
{

	int job_post_ret;
	job_id_t i;
	PVFS_vtag_s bs;

	gossip_debug(SERVER_DEBUG,"Get attr Fxn for crdirent\n");
	job_post_ret = job_trove_keyval_read(s_op->req->u.crdirent.fs_id,
													 s_op->req->u.crdirent.parent_handle,
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
 * Function: crdirent_check_perms
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(crdirent_check_perms)
{

	int job_post_ret;
	//job_id_t i;

	gossip_debug(SERVER_DEBUG,"CheckPerms Fxn for crdirent\n");
	job_post_ret = 1;  /* Just pretend it is good right now */
	// IF THEY don't have permission, set ret->error_code to -ENOPERM!

	STATE_FXN_RET(job_post_ret);
	
}

/*
 * Function: crdirent_create_dir_handle_ph2
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(crdirent_create_dir_handle_ph2)
{

	int job_post_ret;
	job_id_t i;

	gossip_debug(SERVER_DEBUG,"phase2 Fxn for crdirent\n");
	s_op->key.buffer = TROVE_COMMON_KEYS[DIR_ENT_KEY];
	s_op->key.buffer_sz = atoi(TROVE_COMMON_KEYS[DIR_ENT_KEY+1]);

	s_op->val.buffer = &(ret->handle);
	s_op->val.buffer_sz = sizeof(PVFS_handle);

	job_post_ret = job_trove_keyval_write(s_op->req->u.crdirent.fs_id,
													  s_op->req->u.crdirent.parent_handle,
													  &(s_op->key),
													  &(s_op->val),
													  0,
													  ret->vtag,
													  s_op,
													  ret,
													  &i);

	STATE_FXN_RET(job_post_ret);

}

/*
 * Function: crdirent_create_dir_handle_ph1
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(crdirent_create_dir_handle_ph1)
{

	int job_post_ret;
	job_id_t i;

	gossip_debug(SERVER_DEBUG,"phase1 Fxn for crdirent\n");
	gossip_debug(SERVER_DEBUG,"Creating Handle:  %d,%lld\n",s_op->req->u.crdirent.fs_id,\
					 s_op->req->u.crdirent.parent_handle);
	job_post_ret = job_trove_dspace_create(s_op->req->u.crdirent.fs_id,
													   s_op->req->u.crdirent.parent_handle,
													   0xFF000000, /* TODO: Change this */
													   ATTR_DIR,
													   NULL,
													   s_op,
													   ret,
													   &i);

	STATE_FXN_RET(job_post_ret);

}

/*
 * Function: crdirent_create
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(crdirent_create)
{

	int job_post_ret;
	job_id_t i;
	PVFS_handle h;


	gossip_debug(SERVER_DEBUG,"create Fxn for crdirent\n");
	h = *((PVFS_handle *)s_op->val.buffer);
	
	strcpy(s_op->key.buffer,s_op->req->u.crdirent.name);
	s_op->key.buffer_sz = strlen(s_op->req->u.crdirent.name) + 1;

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

	STATE_FXN_RET(job_post_ret);

}


/*
 * Function: crdirent_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


STATE_FXN_HEAD(crdirent_send_bmi)
{

	int job_post_ret;
	job_id_t i;

	s_op->resp->status = ret->error_code;

	gossip_debug(SERVER_DEBUG,"send Fxn for crdirent\n");
	/* Set the ack IF it was created */
	if(ret->error_code == 0) 
		s_op->resp->u.generic.handle = s_op->req->u.crdirent.new_handle;
	
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
 * Function: crdirent_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


STATE_FXN_HEAD(crdirent_cleanup)
{
	
	gossip_debug(SERVER_DEBUG,"clean Fxn for crdirent\n");
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

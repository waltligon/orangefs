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
#include <trove.h>
#include <assert.h>

static int readdir_init(state_action_struct *s_op, job_status_s *ret);
static int readdir_cleanup(state_action_struct *s_op, job_status_s *ret);
static int readdir_kvread(state_action_struct *s_op, job_status_s *ret);
static int readdir_get_kvspace(state_action_struct *s_op, job_status_s *ret);
static int readdir_send_bmi(state_action_struct *s_op, job_status_s *ret);
void readdir_init_state_machine(void);

extern PINT_server_trove_keys_s *Trove_Common_Keys;

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
 * Synopsis: Allocate Memory for readdir.pvfs_dirent_count
 *           
 */


static int readdir_init(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	int j;
	gossip_ldebug(SERVER_DEBUG,"Init\n");

	/**** 
	  We need to malloc key and val space for dirents and handles.
	  Also malloc a space for the structures to refer to the handle.
	 ****/
	s_op->key_a = malloc(sizeof(TROVE_keyval_s)*s_op->req->u.readdir.pvfs_dirent_count);
	s_op->val_a = malloc(sizeof(TROVE_keyval_s)*s_op->req->u.readdir.pvfs_dirent_count);
	s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_handle)));
	for(j=0;j<s_op->req->u.readdir.pvfs_dirent_count;j++)
	{
		s_op->key_a[j].buffer = (char *) malloc((s_op->key_a[j].buffer_sz = PVFS_NAME_MAX+1));
		s_op->val_a[j].buffer = (PVFS_handle *) malloc((s_op->val_a[j].buffer_sz = sizeof(PVFS_handle)));
	}
	s_op->resp->u.readdir.pvfs_dirent_array = (PVFS_dirent *)
		malloc(s_op->req->u.readdir.pvfs_dirent_count*sizeof(PVFS_dirent));

	job_post_ret = job_req_sched_post(s_op->req,
												 s_op,
												 ret,
												 &(s_op->scheduled_id));

	return(job_post_ret);
	
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

static int readdir_kvread(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	PVFS_vtag_s vtag;

	gossip_ldebug(SERVER_DEBUG,"Kvread\n");
	s_op->key.buffer = Trove_Common_Keys[DIR_ENT_KEY].key;
	s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

	s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(PVFS_handle)));

	job_post_ret = job_trove_keyval_read(s_op->req->u.crdirent.fs_id,
													 s_op->req->u.crdirent.parent_handle,
													 &(s_op->key),
													 &(s_op->val),
													 0,
													 vtag,
													 s_op,
													 ret,
													 &i);

	return(job_post_ret);

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

static int readdir_get_kvspace(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	PVFS_handle h;
	PVFS_vtag_s vtag;

	h = *((PVFS_handle *)s_op->val.buffer);
	gossip_ldebug(SERVER_DEBUG,"Dammit\n");
	job_post_ret = job_trove_keyval_iterate(s_op->req->u.readdir.fs_id,
				      								 h,
				      								 s_op->req->u.readdir.token,
				      								 s_op->key_a,
				      								 s_op->val_a,
				      								 s_op->req->u.readdir.pvfs_dirent_count,
														 0,
				      								 vtag,
				      								 s_op,
				      								 ret, 
				      								 &i);


	return(job_post_ret);

}


/*
 * Function: readdir_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Assembles the directory space entries... and ships them off 
 *           
 */


static int readdir_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	//char *dirent_c, *handle_c;
	int j;

	s_op->resp->status = ret->error_code;

	s_op->resp->u.readdir.pvfs_dirent_count = ret->count;

	for(j=0;j<ret->count;j++)
	{
		memcpy(&(s_op->resp->u.readdir.pvfs_dirent_array[j].d_name),s_op->key_a[j].buffer,PVFS_NAME_MAX+1);
		memcpy(&(s_op->resp->u.readdir.pvfs_dirent_array[j].handle),s_op->val_a[j].buffer,sizeof(PVFS_handle));
	}

 	job_post_ret = PINT_encode(s_op->resp,PINT_ENCODE_RESP,&(s_op->encoded),s_op->addr,s_op->enc_type);

	gossip_ldebug(SERVER_DEBUG,"%d\n",s_op->encoded.list_count);
	assert(s_op->encoded.list_count == 1);
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


static int readdir_cleanup(state_action_struct *s_op, job_status_s *ret)
{
	int j;

	for(j=0;j<ret->count;j++)
	{
	free(s_op->key_a[j].buffer);
	free(s_op->val_a[j].buffer);
	}

	free(s_op->val.buffer);

	free(s_op->resp->u.readdir.pvfs_dirent_array);
	free(s_op->resp);

	free(s_op->req);

	free(s_op->unexp_bmi_buff);

	free(s_op);

	return(0);
	
}

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
static int readdir_unpost_req(state_action_struct *s_op, job_status_s *ret);
void readdir_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s readdir_req_s = 
{
	NULL,
	"readdir",
	readdir_init_state_machine
};

%%

machine readdir(init, kvspace, kvread, send, unpost, cleanup)
{
	state init
	{
		run readdir_init;
		default => kvread;
	}

	state send
	{
		run readdir_send_bmi;
		default => unpost;
	}

	state unpost
	{
		run readdir_unpost_req;
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
		default => send;
	}
	
	state kvread
	{
		run readdir_kvread;
		success => kvspace;
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
    int j;
    int key_a_sz, val_a_sz, dirent_buff_sz;
    int handle_sz;
    char *big_memory_buffer,*check_buffer;

    gossip_ldebug(SERVER_DEBUG,"Init\n");

    /**** 
      We need to malloc key and val space for dirents and handles.
     ****/
    key_a_sz = sizeof(TROVE_keyval_s)*s_op->req->u.readdir.pvfs_dirent_count;
    val_a_sz = sizeof(TROVE_keyval_s)*s_op->req->u.readdir.pvfs_dirent_count;
    s_op->val.buffer_sz = sizeof(PVFS_handle);
    dirent_buff_sz = s_op->req->u.readdir.pvfs_dirent_count*sizeof(PVFS_dirent);

    handle_sz = sizeof(PVFS_handle);

    big_memory_buffer = (char *) malloc(key_a_sz+val_a_sz+s_op->val.buffer_sz+dirent_buff_sz);
    check_buffer = big_memory_buffer;

    s_op->key_a = (TROVE_keyval_s *) big_memory_buffer;
    big_memory_buffer+=key_a_sz;

    s_op->val_a = (TROVE_keyval_s *) big_memory_buffer;
    big_memory_buffer+=val_a_sz;

    s_op->val.buffer = (void *) big_memory_buffer;
    big_memory_buffer+=s_op->val.buffer_sz;

    s_op->resp->u.readdir.pvfs_dirent_array = (PVFS_dirent *) big_memory_buffer;
    big_memory_buffer+=dirent_buff_sz;

    assert(big_memory_buffer - check_buffer ==  key_a_sz + val_a_sz + s_op->val.buffer_sz + dirent_buff_sz);

    for(j=0;j<s_op->req->u.readdir.pvfs_dirent_count;j++)
    {
	s_op->key_a[j].buffer = &(s_op->resp->u.readdir.pvfs_dirent_array[j].d_name);
	s_op->key_a[j].buffer_sz = PVFS_NAME_MAX+1;

	s_op->val_a[j].buffer = &(s_op->resp->u.readdir.pvfs_dirent_array[j].handle);
	s_op->key_a[j].buffer_sz = handle_sz;
    }

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

    gossip_ldebug(SERVER_DEBUG,"Kvread %lld\n",s_op->req->u.readdir.handle);
    s_op->key.buffer = Trove_Common_Keys[DIR_ENT_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

    job_post_ret = job_trove_keyval_read(s_op->req->u.readdir.fs_id,
	    s_op->req->u.readdir.handle,
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

    gossip_ldebug(SERVER_DEBUG,"KVSpace %lld\n",s_op->req->u.readdir.handle);
    h = *((PVFS_handle *)s_op->val.buffer);
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

    gossip_debug(SERVER_DEBUG,"Kvsend -- %d\n",ret->error_code);
    gossip_debug(SERVER_DEBUG,"Kvsend -- %d\n",ret->count);
    if((s_op->resp->status = ret->error_code) < 0)
    {
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);
    }
    else
    {
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s) 
	    + ret->count *sizeof(PVFS_dirent);
    }
    

    s_op->resp->u.readdir.pvfs_dirent_count = ret->count;

    if(ret->error_code == 0)
	job_post_ret = PINT_encode(s_op->resp,PINT_ENCODE_RESP,&(s_op->encoded),s_op->addr,s_op->enc_type);
    else
    {
	/* Set it to a noop for an error so we don't encode all the stuff we don't need to */
	s_op->resp->op = PVFS_SERV_NOOP;
	PINT_encode(s_op->resp,PINT_ENCODE_RESP,&(s_op->encoded),s_op->addr,s_op->enc_type);
	/* set it back */
	((struct PVFS_server_req_s *)s_op->encoded.buffer_list[0])->op = s_op->req->op;
    }
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
 * Function: readdir_unpost_req
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

static int readdir_unpost_req(state_action_struct *s_op, job_status_s *ret)
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

    if(s_op->key_a)
	free(s_op->key_a);

    if(s_op->resp)
	free(s_op->resp);

    free(s_op->req);

    free(s_op->unexp_bmi_buff);

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


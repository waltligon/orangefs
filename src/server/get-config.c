/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "state-machine.h"
#include "server-config.h"
#include "pvfs2-server.h"

static int getconfig_cleanup(state_action_struct *s_op, job_status_s *ret);
static int getconfig_job_bmi_send(state_action_struct *s_op, job_status_s *ret);
static int getconfig_job_trove(state_action_struct *s_op, job_status_s *ret);
static int getconfig_init(state_action_struct *s_op, job_status_s *ret);
void getconfig_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s getconfig_req_s = 
{
	NULL,
	"getconfig",
	getconfig_init_state_machine
};

%%

machine get_config(init, trove, bmi_send, cleanup)
{
	state init
	{
		run getconfig_init;
		success => trove;
		default => bmi_send;
	}

	state trove
	{
		run getconfig_job_trove;
		default => bmi_send;
	}

	state bmi_send
	{
		run getconfig_job_bmi_send;
		default => cleanup;
	}

	state cleanup
	{
		run getconfig_cleanup;
		default => init;
	}

}

%%

/*
 * Function: getconfig_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for get config and
 *           return a pointer to it.
 *           
 */


void getconfig_init_state_machine(void)
{

    getconfig_req_s.state_machine = get_config;

}

/*
 * Function: getconfig_init
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      fs_id mapping exists on server
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Get information from config structure
 *           then move to next state.
 *           
 */

static int getconfig_init(state_action_struct *s_op, job_status_s *ret)
{

    server_configuration_s *user_opts;
    int job_post_ret = 1;
    int i;
    filesystem_configuration_s *file_system;

    user_opts = get_server_config_struct();

    /* Set up the values we have in our Config Struct user_opts */
    for(i=0;i<user_opts->number_filesystems;i++)
    {
	if(strcmp(s_op->req->u.getconfig.fs_name,
		    user_opts->file_systems[i]->file_system_name) == 0)
	    break;
    }

    if(i == user_opts->number_filesystems)
    {
	ret->error_code = -99;
	return(1);
    }

    file_system = user_opts->file_systems[i];

    s_op->resp->u.getconfig.meta_server_count = file_system->count_meta_servers;
    s_op->resp->u.getconfig.io_server_count = file_system->count_io_servers;

    /* The new way of doing things because we have an encoding system! dw*/
    s_op->resp->u.getconfig.meta_server_mapping = file_system->meta_server_list;
    s_op->resp->u.getconfig.io_server_mapping = file_system->io_server_list;
    s_op->resp->u.getconfig.fs_id = file_system->coll_id;
    s_op->strsize = strlen(file_system->meta_server_list)+1;
    s_op->strsize += strlen(file_system->io_server_list)+1;

    /* Set up the key/val pair for trove to get root handle */
    s_op->key.buffer = Trove_Common_Keys[ROOT_HANDLE_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[ROOT_HANDLE_KEY].size;
    s_op->val.buffer = malloc((s_op->val.buffer_sz = sizeof(TROVE_handle)));


    return(job_post_ret);

}

/*
 * Function: getconfig_job_trove
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Submit a job to the trove interface 
 *           requesting root handle and fs_id
 *           
 */

static int getconfig_job_trove(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;

    /*job_post_ret = job_trove_fs_lookup(s_op->req->u.getconfig.fs_name,s_op,ret,&i);*/

    job_post_ret = job_trove_fs_geteattr(s_op->resp->u.getconfig.fs_id,
	    &(s_op->key),
	    &(s_op->val),
	    0,
	    s_op,
	    ret,
	    &i);


    return(job_post_ret);

}

/*
 * Function: getconfig_job_bmi_send
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Send a message to the client with 
 *           either an error or the requested data
 *           
 */

static int getconfig_job_bmi_send(state_action_struct *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;

    s_op->resp->status = ret->error_code;
    if(!ret->error_code)
    {
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s) + s_op->strsize;
	s_op->resp->u.getconfig.root_handle = *((TROVE_handle *)s_op->val.buffer);
    }
    else
    {
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);
    }
    
    job_post_ret = PINT_encode(
	    s_op->resp,
	    PINT_ENCODE_RESP,
	    &(s_op->encoded),
	    s_op->addr,
	    s_op->enc_type);

    assert(job_post_ret == 0);

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

    return(job_post_ret);

}

/*
 * Function: getconfig_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: cleans up string memory
 *           response structure
 */

static int getconfig_cleanup(state_action_struct *s_op, job_status_s *ret)
{

    if (s_op->resp)
    {
	free(s_op->resp);
    }

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


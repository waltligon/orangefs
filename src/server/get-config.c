/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "state-machine.h"
#include "server-config.h"
#include "pvfs2-server.h"

static int getconfig_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int getconfig_job_bmi_send(PINT_server_op *s_op, job_status_s *ret);
static int getconfig_job_trove(PINT_server_op *s_op, job_status_s *ret);
static int getconfig_init(PINT_server_op *s_op, job_status_s *ret);
void getconfig_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

#if 0
PINT_state_machine_s getconfig_req_s = 
{
	NULL,
	"getconfig",
	getconfig_init_state_machine
};
#endif

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

#if 0
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
#endif

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

static int getconfig_init(PINT_server_op *s_op, job_status_s *ret)
{
    int fd = 0, nread = 0;
    int job_post_ret = 1;
    char *meta_server, *data_server;
    struct stat statbuf;
    struct llist *cur = NULL;
    struct server_configuration_s *user_opts;
    struct filesystem_configuration_s *cur_fs;

    user_opts = get_server_config_struct();
    assert(user_opts);

    cur = user_opts->file_systems;
    while(cur)
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        assert(cur_fs->file_system_name);

        if (strcmp(s_op->req->u.getconfig.fs_name,
                   cur_fs->file_system_name) == 0)
        {
            break;
        }
        cur = llist_next(cur);
    }

    if (!cur_fs)
    {
        ret->error_code = -99;
        return 1;
    }

    /*
      FIXME:
      if we're sure we're going to send back a whole file,
      maybe there's a better more reliable way to do this
    */
    if (stat(user_opts->fs_config_filename,&statbuf) == -1)
    {
        ret->error_code = -98;
        return 1;
    }

    s_op->resp->u.getconfig.fs_id = cur_fs->coll_id;
    s_op->resp->u.getconfig.config_buflen = (PVFS_count32)statbuf.st_size;
    s_op->resp->u.getconfig.config_buf = (PVFS_string)malloc((int)statbuf.st_size);

    if (!s_op->resp->u.getconfig.config_buf)
    {
        ret->error_code = -97;
        return 1;
    }

    if ((fd = open(user_opts->fs_config_filename,O_RDONLY)) == -1)
    {
        ret->error_code = -96;
        return 1;
    }

    nread = read(fd,s_op->resp->u.getconfig.config_buf,statbuf.st_size);
    if (nread != statbuf.st_size)
    {
        close(fd);
        ret->error_code = -95;
        return 1;
    }
    close(fd);

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

static int getconfig_job_trove(PINT_server_op *s_op, job_status_s *ret)
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

static int getconfig_job_bmi_send(PINT_server_op *s_op, job_status_s *ret)
{
    int job_post_ret;
    job_id_t i;

    /* fill in response -- status field is the only generic one we should have to set */
    s_op->resp->status = ret->error_code;
    if(!ret->error_code)
    {
#if 0
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s) + s_op->u.getconfig.strsize;
#endif
	s_op->resp->u.getconfig.root_handle = *((TROVE_handle *)s_op->val.buffer);
    }
    else
    {
#if 0
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);
#endif
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

static int getconfig_cleanup(PINT_server_op *s_op, job_status_s *ret)
{

    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    
    if(s_op->val.buffer)
    {
	free(s_op->val.buffer);
    }

    if (s_op->resp)
    {
        if (s_op->resp->u.getconfig.config_buf)
        {
            free(s_op->resp->u.getconfig.config_buf);
        }
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


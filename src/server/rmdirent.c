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

static int rmdirent_init(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_read_parent_metadata(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_verify_parent_metadata_and_read_directory_entry_handle(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_remove_directory_entry(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_read_directory_entry(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_release_job(PINT_server_op *s_op, job_status_s *ret);
static int rmdirent_send_bmi(PINT_server_op *s_op, job_status_s *ret);
void rmdirent_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s rmdirent_req_s = 
{
	NULL,
	"rmdirent",
	rmdirent_init_state_machine
};

%%

machine rmdirent(init,
		 read_parent_metadata,
		 verify_parent_metadata_and_read_directory_entry_handle,
		 read_directory_entry,
		 remove_directory_entry,
		 send,
		 release,
		 cleanup)
{
	state init
	{
		run rmdirent_init;
		default => read_parent_metadata;
	}

	state read_parent_metadata
	{
		run rmdirent_read_parent_metadata;
		success => verify_parent_metadata_and_read_directory_entry_handle;
		default => send;
	}

	state verify_parent_metadata_and_read_directory_entry_handle
	{
		run rmdirent_verify_parent_metadata_and_read_directory_entry_handle;
		success => read_directory_entry;
		default => send;
	}

	state read_directory_entry
	    {
		run rmdirent_read_directory_entry;
		success => remove_directory_entry;
		default => send;
	    }

	state remove_directory_entry
	{
		run rmdirent_remove_directory_entry;
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
 * Synopsis: Set up the state machine for rmdirent. 
 *           
 */

void rmdirent_init_state_machine(void)
{
    rmdirent_req_s.state_machine = rmdirent;
}

/*
 * Function: rmdirent_init
 */
static int rmdirent_init(PINT_server_op *s_op, job_status_s *ret)
{
    int job_post_ret;

    gossip_debug(SERVER_DEBUG, "rmdirent state: init\n");

    job_post_ret = job_req_sched_post(s_op->req,
				      s_op,
				      ret,
				      &(s_op->scheduled_id));
    return job_post_ret;

}

/*
 * Function: rmdirent_read_parent_metadata
 *
 * Synopsis: We need to get the attribute structure if it is available.
 *           If it is not, we just send an error to the client.  
 *           TODO: Semantics here!
 */
static int rmdirent_read_parent_metadata(PINT_server_op *s_op,
					 job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "rmdirent state: read_parent_metadata\n");

    /* fill in key and value structures prior to keyval read */
    s_op->key.buffer = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    /* we'll store the parent attributes in the rmdirent scratch space in the s_op */
    s_op->val.buffer = &s_op->u.rmdirent.parent_attr;
    s_op->val.buffer_sz = sizeof(PVFS_object_attr);

    gossip_debug(SERVER_DEBUG,
		 "  reading metadata (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.rmdirent.fs_id,
		 s_op->req->u.rmdirent.parent_handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.rmdirent.fs_id,
					 s_op->req->u.rmdirent.parent_handle,
					 &s_op->key,
					 &s_op->val,
					 0,
					 NULL,
					 s_op,
					 ret,
					 &j_id);
    return job_post_ret;

}

/*
 * Function: rmdirent_verify_parent_metadata_and_read_directory_entry_handle
 *
 * (sorry for the long function name, but it performs multiple steps -- rob)
 *
 *
 * Synopsis: This should use a global function that verifies that the user
 *           has the necessary permissions to perform the operation it wants
 *           to do.
 *           
 */
static int rmdirent_verify_parent_metadata_and_read_directory_entry_handle(PINT_server_op *s_op,
									   job_status_s *ret)
{
    int job_post_ret;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "rmdirent state: verify_parent_metadata_and_read_directory_entry_handle\n");

    /* TODO: PERFORM PERMISSION CHECKING */

    /* set up key and value structures to read directory entry */
    s_op->key.buffer    = Trove_Common_Keys[DIR_ENT_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

    /* we will read the dirdata handle from the entry into the rmdirent scratch space */
    s_op->val.buffer    = &s_op->u.rmdirent.dirdata_handle;
    s_op->val.buffer_sz = sizeof(PVFS_handle);

    gossip_debug(SERVER_DEBUG,
		 "  reading dirdata handle (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.rmdirent.fs_id,
		 s_op->req->u.rmdirent.parent_handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.rmdirent.fs_id,
					 s_op->req->u.rmdirent.parent_handle,
					 &s_op->key,
					 &s_op->val,
					 0,
					 NULL,
					 s_op,
					 ret,
					 &i);
    return job_post_ret;
}

/* Function: rmdirent_read_directory_entry
 *
 * Synopsis: In order to return the handle of the removed entry (which is part
 * of the rmdirent response), we must read the directory entry prior to removing
 * it.
 */
static int rmdirent_read_directory_entry(PINT_server_op *s_op,
					 job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "rmdirent state: read_directory_entry\n");

    gossip_debug(SERVER_DEBUG,
		 "  reading from dirent handle = 0x%08Lx, name = %s\n",
		 s_op->u.rmdirent.dirdata_handle,
		 s_op->req->u.rmdirent.entry);

    /* initialize keyval prior to read call
     *
     * We will read the handle into the rmdirent scratch space
     * (s_op->u.rmdirent.entry_handle).
     */
    s_op->key.buffer    = s_op->req->u.rmdirent.entry;
    s_op->key.buffer_sz = strlen(s_op->req->u.rmdirent.entry) + 1;
    s_op->val.buffer    = &s_op->u.rmdirent.entry_handle;
    s_op->val.buffer_sz = sizeof(PVFS_handle);

    job_post_ret = job_trove_keyval_read(s_op->req->u.rmdirent.fs_id,
					 s_op->u.rmdirent.dirdata_handle,
					 &s_op->key,
					 &s_op->val,
					 0,
					 NULL,
					 s_op,
					 ret,
					 &j_id);
    return job_post_ret;
}

/*
 * Function: rmdirent_remove_directory_entry
 *
 * Synopsis: posts a trove keyval remove to remove the directory entry from
 * the dirdata object.
 *           
 */
static int rmdirent_remove_directory_entry(PINT_server_op *s_op, job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "rmdirent state: remove_directory_entry\n");

    /* set up key and structure for keyval remove */
    s_op->key.buffer    = s_op->req->u.rmdirent.entry;
    s_op->key.buffer_sz = strlen(s_op->req->u.rmdirent.entry) + 1;

    gossip_debug(SERVER_DEBUG,
		 "  removing entry %s from dirdata object (handle = 0x%08Lx)\n",
		 s_op->req->u.rmdirent.entry,
		 s_op->u.rmdirent.dirdata_handle);

    job_post_ret = job_trove_keyval_remove(s_op->req->u.rmdirent.fs_id,
					   s_op->u.rmdirent.dirdata_handle,
					   &s_op->key,
					   TROVE_SYNC,
					   NULL,
					   s_op,
					   ret,
					   &j_id);
    return job_post_ret;
}


/*
 * Function: rmdirent_bmi_send
 *
 * Synopsis: Send a message to the client.  
 *           If the entry was successfully
 *           removed, send the handle back.
 *           
 */
static int rmdirent_send_bmi(PINT_server_op *s_op,
			     job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "rmdirent state: send_bmi\n");

    /* fill in response -- status field is the only generic one we should have to set */
    s_op->resp->status = ret->error_code;
    s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);

    /* Set the handle if it was removed */
    if(ret->error_code == 0) {
	/* we return the handle from the directory entry in the response */
	s_op->resp->u.rmdirent.entry_handle = s_op->u.rmdirent.entry_handle;
	gossip_debug(SERVER_DEBUG,
		     "  succeeded; returning handle 0x%08Lx in response\n",
		     s_op->resp->u.rmdirent.entry_handle);
    }
    else {
	gossip_debug(SERVER_DEBUG,
		     "  sending error response\n");
    }

    /* Encode the message */
    job_post_ret = PINT_encode(s_op->resp,
			       PINT_ENCODE_RESP,
			       &(s_op->encoded),
			       s_op->addr,
			       s_op->enc_type);
    assert(job_post_ret == 0);

    job_post_ret = job_bmi_send_list(s_op->addr,
				     s_op->encoded.buffer_list,
				     s_op->encoded.size_list,
				     s_op->encoded.list_count,
				     s_op->encoded.total_size,
				     s_op->tag,
				     s_op->encoded.buffer_flag,
				     0,
				     s_op, 
				     ret, 
				     &j_id);
    return job_post_ret;
}

/*
 * Function: rmdirent_release_job
 *
 * Synopsis: Free the job from the scheduler to allow next job to proceed.
 */

static int rmdirent_release_job(PINT_server_op *s_op, job_status_s *ret)
{
    int job_post_ret=0;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "rmdirent state: release_job\n");

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
static int rmdirent_cleanup(PINT_server_op *s_op, job_status_s *ret)
{

    gossip_debug(SERVER_DEBUG, "rmdirent state: cleanup\n");

    /* free decoded, encoded requests */
    PINT_decode_release(&(s_op->decoded), PINT_DECODE_REQ, 0);
    free(s_op->unexp_bmi_buff.buffer);
    
    /* free original, encoded responses */
    PINT_encode_release(&(s_op->encoded), PINT_ENCODE_RESP, 0);
    free(s_op->resp);

    /* free server operation structure */
    free(s_op);

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

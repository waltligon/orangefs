/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "state-machine.h"
#include "server-config.h"
#include "pvfs2-server.h"
#include "pvfs2-attr.h"
#include "gossip.h"

static int remove_init(PINT_server_op *s_op, job_status_s *ret);
static int remove_read_object_metadata(PINT_server_op *s_op, job_status_s *ret);
static int remove_verify_object_metadata(PINT_server_op *s_op, job_status_s *ret);
static int remove_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int remove_remove_dspace(PINT_server_op *s_op, job_status_s *ret);
static int remove_release_posted_job(PINT_server_op *s_op, job_status_s *ret);
static int remove_send_response(PINT_server_op *s_op, job_status_s *ret);
void remove_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s remove_req_s = 
{
	NULL,
	"remove",
	remove_init_state_machine
};

%%

machine remove(init,
	       read_object_metadata,
	       verify_object_metadata,
	       remove_dspace,
	       send_response,
	       release,
	       cleanup)
{
	state init
	{
		run remove_init;
		default => read_object_metadata;
	}

	state read_object_metadata
	{
		run remove_read_object_metadata;
		success => verify_object_metadata;
		default => send_response;
	}

	state verify_object_metadata
	{
		run remove_verify_object_metadata;
		success => remove_dspace;
		default => send_response;
	}

	state remove_dspace
	{
		run remove_remove_dspace;
		default => send_response;
	}

	state send_response
	{
		run remove_send_response;
		default => release;
	}

	state release
	{
		run remove_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run remove_cleanup;
		default => init;
	}
}

%%

/*
 * Function: remove_init_state_machine
 *
 */

void remove_init_state_machine(void)
{

    remove_req_s.state_machine = remove;

}

/*
 * Function: remove_init
 *
 * Post operation to request scheduler.
 */
static int remove_init(PINT_server_op *s_op,
		       job_status_s *ret)
{

    int job_post_ret;

    gossip_debug(SERVER_DEBUG, "remove state: init\n");

    job_post_ret = job_req_sched_post(s_op->req,
				      s_op,
				      ret,
				      &(s_op->scheduled_id));
    return job_post_ret;
}

/*
 * Function: remove_read_object_metadata
 *
 * Initiate a keyval read operation to read the metadata for the object.
 * Metadata will be read into the remove scratch space (s_op->u.remove.object_attr).
 *
 */
static int remove_read_object_metadata(PINT_server_op *s_op,
				       job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "remove state: read_object_metadata\n");

    /* set up key and value structures for reading metadata */
    s_op->key.buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer    = &s_op->u.remove.object_attr;
    s_op->val.buffer_sz = sizeof(PVFS_object_attr);

    gossip_debug(SERVER_DEBUG,
		 "  reading metadata (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.remove.fs_id,
		 s_op->req->u.remove.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.remove.fs_id,
					 s_op->req->u.remove.handle,
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
 * Function: remove_verify_object_metadata
 *
 * Verifies that metadata was successfully read.  If so, verifies that
 * the user has permission to access the file (not yet implemented).
 *
 * TODO: do permission checking, probably with some shared function.
 */

static int remove_verify_object_metadata(PINT_server_op *s_op,
					 job_status_s *ret)
{

    gossip_debug(SERVER_DEBUG, "remove state: verify_object_metadata\n");

    if (ret->error_code != 0) {
	gossip_debug(SERVER_DEBUG,
		     "  previous keyval read had an error (new metafile?); data is useless\n");
	ret->error_code = -EINVAL;
    }
    else {
	PVFS_object_attr *a_p = &s_op->u.remove.object_attr;

	gossip_debug(SERVER_DEBUG,
		     "  attrs read from dspace = (owner = %d, group = %d, perms = %o, type = %d)\n",
		     a_p->owner,
		     a_p->group,
		     a_p->perms,
		     a_p->objtype);
    }

    /* TODO: CHECK PERMISSIONS */

    return 1;
}

/*
 * Function: remove_remove_dspace
 *
 * Remove the dspace using the handle from the incoming request
 * (which was verified in previous states).
 */
static int remove_remove_dspace(PINT_server_op *s_op,
				job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "remove state: remove_dspace\n");

    job_post_ret = job_trove_dspace_remove(s_op->req->u.remove.fs_id,
					   s_op->req->u.remove.handle,
					   s_op,
					   ret,
					   &j_id);
    return job_post_ret;
}


/*
 * Function: remove_bmi_response
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Send acknowledgement back to client that job has been completed
 * (possibly in error).
 *
 * NOTE: there was something in here about sending the handle back if
 * successfully removed, but I don't see the point in that.  dropped.  -- rob
 */
static int remove_send_response(PINT_server_op *s_op,
				job_status_s *ret)
{

    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "remove state: send_response\n");

    /* fill in response -- status field is the only generic one we should have to set */
    s_op->resp->status = ret->error_code;

    if(ret->error_code == 0) 
    {
	gossip_debug(SERVER_DEBUG,
		     "  successfully removed dspace 0x%08Lx\n",
		     s_op->req->u.remove.handle);
    }
    else {
	gossip_debug(SERVER_DEBUG,
		     "  failed to remove dspace 0x%08Lx\n",
		     s_op->req->u.remove.handle);
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
 * Function: remove_release_posted_job
 *
 * Notify request scheduling system that this request has been completed.
 */
static int remove_release_posted_job(PINT_server_op *s_op,
				     job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "remove state: release_posted_job\n");

    job_post_ret = job_req_sched_release(s_op->scheduled_id,
					 s_op,
					 ret,
					 &j_id);
    return job_post_ret;
}


/*
 * Function: remove_cleanup
 *
 * Free all memory associated with this request and return 0, indicating
 * we're done processing.
 */
static int remove_cleanup(PINT_server_op *s_op,
			  job_status_s *ret)
{

    gossip_debug(SERVER_DEBUG, "remove state: cleanup\n");

    /* free decoded, encoded requests */
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    free(s_op->unexp_bmi_buff.buffer);

    /* free encoded, original responses */
    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
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

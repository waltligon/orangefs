/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <malloc.h>
#include <string.h>
#include <assert.h>

#include "state-machine.h"
#include "server-config.h"
#include "pvfs2-server.h"
#include "pvfs2-attr.h"
#include "trove.h" /* TODO -- REMOVE TROVE REFERENCES IF POSSIBLE */

enum {
    STATE_ENOTDIR = 7
};

static int readdir_init(PINT_server_op *s_op, job_status_s *ret);
static int readdir_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int readdir_read_dirdata_handle(PINT_server_op *s_op, job_status_s *ret);
static int readdir_iterate_on_entries(PINT_server_op *s_op, job_status_s *ret);
static int readdir_send_bmi(PINT_server_op *s_op, job_status_s *ret);
static int readdir_unpost_req(PINT_server_op *s_op, job_status_s *ret);
static int readdir_verify_directory_metadata(PINT_server_op *s_op, job_status_s *ret);
static int readdir_read_directory_metadata(PINT_server_op *s_op, job_status_s *ret);
void readdir_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s readdir_req_s = 
{
	NULL,
	"readdir",
	readdir_init_state_machine
};

%%

machine readdir(init,
		iterate_on_entries,
		read_dirdata_handle,
		read_directory_metadata,
		verify_directory_metadata,
		send,
		unpost,
		cleanup)
{
	state init
	{
		run readdir_init;
		default => read_directory_metadata;
	}

	state read_directory_metadata
	    {
		run readdir_read_directory_metadata;
		success => verify_directory_metadata;
		default => send;
	    }

	state verify_directory_metadata
	    {
		run readdir_verify_directory_metadata;
		success => read_dirdata_handle;
		default => send;
	    }

	state read_dirdata_handle
	{
		run readdir_read_dirdata_handle;
		success => iterate_on_entries;
		default => send;
	}

	state iterate_on_entries
	{
		run readdir_iterate_on_entries;
		default => send;
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
}

%%

/*
 * Function: readdir_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
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
static int readdir_init(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret;

    gossip_debug(SERVER_DEBUG, "readdir state: init\n");

    s_op->key_a = NULL; /* we're going to use this to point to an allocated region later, so initialize it now. */

    job_post_ret = job_req_sched_post(s_op->req,
				      s_op,
				      ret,
				      &(s_op->scheduled_id));

    return job_post_ret;
}


/*
 * Function: readdir_read_directory_metadata
 *
 * Synopsis: Given an object handle, looks up the attributes (metadata)
 * for that handle.
 *
 * Posts the keyval read to trove.
 */
static int readdir_read_directory_metadata(PINT_server_op *s_op,
					   job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "readdir state: read_directory_metadata\n");

    /* initialize keyvals prior to read list call */
    s_op->key.buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;
    s_op->val.buffer    = &s_op->u.readdir.directory_attr;
    s_op->val.buffer_sz = sizeof(PVFS_object_attr);

    gossip_debug(SERVER_DEBUG,
		 "  reading metadata (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.readdir.fs_id,
		 s_op->req->u.readdir.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.readdir.fs_id,
					 s_op->req->u.readdir.handle,
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
 * Function: readdir_verify_directory_metadata
 *
 * Synopsis: Examine the metadata returned from the keyval read.
 * If the metadata is for a directory, continue.  Otherwise prepare error.
 *
 * This function does not post an operation, but rather returns 1
 * immediately.
 */
static int readdir_verify_directory_metadata(PINT_server_op *s_op,
					     job_status_s *ret)
{
    PVFS_object_attr *a_p;

    gossip_debug(SERVER_DEBUG, "readdir state: verify_directory_metadata\n");

    a_p = &s_op->u.readdir.directory_attr;

    gossip_debug(SERVER_DEBUG,
		 "  attrs = (owner = %d, group = %d, perms = %o, type = %d)\n",
		 a_p->owner,
		 a_p->group,
		 a_p->perms,
		 a_p->objtype);

    if (a_p->objtype != PVFS_TYPE_DIRECTORY) {
	gossip_debug(SERVER_DEBUG, "  object is not a directory; halting readdir and sending response\n");
	ret->error_code = STATE_ENOTDIR;
	return 1;
    }

    /* TODO: permission checking */

    return 1;
}


/*
 * Function: readdir_read_dirdata_handle
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: read the handle of the dirdata space from the directory's keyval space
 *           
 */
static int readdir_read_dirdata_handle(PINT_server_op *s_op,
				       job_status_s *ret)
{
    int job_post_ret;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "readdir state: read_dirdata handle\n");

    s_op->key.buffer    = Trove_Common_Keys[DIR_ENT_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;

    /* read handle into readdir scratch space */
    s_op->val.buffer    = &s_op->u.readdir.dirent_handle;
    s_op->val.buffer_sz = sizeof(PVFS_handle);

    gossip_debug(SERVER_DEBUG,
		 "  reading metadata (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.readdir.fs_id,
		 s_op->req->u.readdir.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.readdir.fs_id,
					 s_op->req->u.readdir.handle,
					 &s_op->key,
					 &s_op->val,
					 0,
					 NULL,
					 s_op,
					 ret,
					 &i);
    return job_post_ret;
}

/*
 * Function: readdir_iterate_on_entries
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *
 * NOTE: allocates a big chunk of memory, pointed to by s_op->key_a!
 */
static int readdir_iterate_on_entries(PINT_server_op *s_op,
				      job_status_s *ret)
{
    int j;
    int memory_size, kv_array_size;
    char *memory_buffer;
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "readdir state: iterate_on_entries\n");

    /* calculate total memory needed:
     * - 2 * dirent_count keyval structures to pass to iterate function
     * - dirent_count dirent structures to hold the results
     */
    kv_array_size = s_op->req->u.readdir.pvfs_dirent_count * sizeof(TROVE_keyval_s);
    memory_size = 2 * kv_array_size + s_op->req->u.readdir.pvfs_dirent_count * sizeof(PVFS_dirent);

    memory_buffer = malloc(memory_size);
    assert(memory_buffer != NULL);

    /* set up all the pointers into the one big buffer */
    s_op->key_a = (TROVE_keyval_s *) memory_buffer;
    memory_buffer += kv_array_size;

    s_op->val_a = (TROVE_keyval_s *) memory_buffer;
    memory_buffer += kv_array_size;

    s_op->resp->u.readdir.pvfs_dirent_array = (PVFS_dirent *) memory_buffer;

    /* fill in values into all keyval structures prior to iterate call */
    for (j = 0; j < s_op->req->u.readdir.pvfs_dirent_count; j++)
    {
	s_op->key_a[j].buffer    = s_op->resp->u.readdir.pvfs_dirent_array[j].d_name;
	s_op->key_a[j].buffer_sz = PVFS_NAME_MAX;

	s_op->val_a[j].buffer    = &(s_op->resp->u.readdir.pvfs_dirent_array[j].handle);
	s_op->val_a[j].buffer_sz = sizeof(PVFS_handle);
    }

    gossip_debug(SERVER_DEBUG,
		 "  iterating over keyvals (coll_id = 0x%x, handle = 0x%08Lx, token = 0x%Lx, count = %d)\n",
		 s_op->req->u.readdir.fs_id,
		 s_op->u.readdir.dirent_handle,
		 s_op->req->u.readdir.token,
		 s_op->req->u.readdir.pvfs_dirent_count);

    job_post_ret = job_trove_keyval_iterate(s_op->req->u.readdir.fs_id,
					    s_op->u.readdir.dirent_handle,
					    s_op->req->u.readdir.token,
					    s_op->key_a,
					    s_op->val_a,
					    s_op->req->u.readdir.pvfs_dirent_count,
					    0,
					    NULL,
					    s_op,
					    ret, 
					    &j_id);
    return job_post_ret;
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
static int readdir_send_bmi(PINT_server_op *s_op, job_status_s *ret)
{
    int encode_ret, job_post_ret;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "readdir state: send_bmi\n");

    /* fill in response -- status field is the only generic one we should have to set */
    if (ret->error_code == STATE_ENOTDIR) {

	gossip_debug(SERVER_DEBUG,
		     "  handle didn't refer to a directory\n");

	s_op->resp->status = -EINVAL;
    }
    else if (ret->error_code != 0) {
	/* for now let's assume this is an "empty" directory */

	gossip_debug(SERVER_DEBUG,
		     "  error code = %d; assuming empty directory\n",
		     ret->error_code);

	s_op->resp->status = 0;
	s_op->resp->u.readdir.pvfs_dirent_count = 0;
    }
    else {
	s_op->resp->status = 0;
	s_op->resp->u.readdir.pvfs_dirent_count = ret->count;
    }

    encode_ret = PINT_encode(s_op->resp,
			     PINT_ENCODE_RESP,
			     &(s_op->encoded),
			     s_op->addr,
			     s_op->enc_type);

    assert(encode_ret == 0);

    /* Post message */
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
    return job_post_ret;
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
static int readdir_unpost_req(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "readdir state: unpost_req\n");

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
static int readdir_cleanup(PINT_server_op *s_op,
			   job_status_s *ret)
{

    gossip_debug(SERVER_DEBUG, "readdir state: cleanup\n");

    /* free decoded, encoded requests */
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    free(s_op->unexp_bmi_buff.buffer);

    /* free encoded, original responses */
    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    free(s_op->resp);

    /* free dynamically allocated space */
    if (s_op->key_a) free(s_op->key_a);

    /* free server operation structure */
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

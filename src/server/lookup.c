/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>

#include "state-machine.h"
#include "server-config.h"
#include "pvfs2-server.h"
#include "pvfs2-attr.h"
#include "job-consist.h"

/* TODO: DO WE NEED THESE TO BE HERE?  WHAT'S THE RIGHT WAY TO DO THIS? */
enum 
{
    STATE_ENOTDIR = 22,
    STATE_NOMORESEGS = 23
};

/* TODO: PUT THESE IN A HEADER SOMEWHERE */
extern int PINT_string_count_segments(char *pathname);
extern int PINT_string_next_segment(char *pathname,
				    char **inout_segp,
				    void **opaquep);

static int lookup_init(PINT_server_op *s_op, job_status_s *ret);
static int lookup_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int lookup_verify_object_metadata(PINT_server_op *s_op, job_status_s *ret);
static int lookup_send_response(PINT_server_op *s_op, job_status_s *ret);
static int lookup_read_directory_entry(PINT_server_op *s_op, job_status_s *ret);
static int lookup_read_directory_entry_handle(PINT_server_op *s_op, job_status_s *ret);
static int lookup_read_object_metadata(PINT_server_op *s_op, job_status_s *ret);
static int lookup_release_job(PINT_server_op *s_op, job_status_s *ret);
void lookup_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s lookup_req_s = 
{
    NULL,
    "lookup",
    lookup_init_state_machine
};

%%

machine lookup(init,
	       read_object_metadata,
	       read_directory_entry_handle,
	       read_directory_entry,
	       verify_object_metadata,
	       send_response,
	       release,
	       cleanup)
{
    state init
	{
	    run lookup_init;
	    STATE_ENOTDIR => send_response;
	    default       => read_object_metadata;
	}

    state read_object_metadata
	{
	    run lookup_read_object_metadata;
	    success => verify_object_metadata;
	    default => send_response;
	}

    state verify_object_metadata
	{
	    run lookup_verify_object_metadata;
	    success          => read_directory_entry_handle;
	    STATE_ENOTDIR    => send_response;
	    STATE_NOMORESEGS => send_response;
	    default          => send_response;
	}

    state read_directory_entry_handle
	{
	    run lookup_read_directory_entry_handle;
	    success => read_directory_entry;
	    default => send_response;
	}

    state read_directory_entry
	{
	    run lookup_read_directory_entry;
	    success => read_object_metadata;
	    default => send_response;
	}

    state send_response
	{
	    run lookup_send_response;
	    default => release;
	}

    state release
	{
	    run lookup_release_job;
	    default => cleanup;
	}

    state cleanup
	{
	    run lookup_cleanup;
	    default => init;
	}
}

%%

/*
 * Function: lookup_init_state_machine           
 */
void lookup_init_state_machine(void)
{
    lookup_req_s.state_machine = lookup;
}

/*
 * Function: lookup_init
 *
 * Synopsis: initializes internal structures and posts job to request
 * scheduler.
 *
 * Assumes req structure holds a valid path.
 *
 * Initializes segp, seg_ct, seg_nr fields in s_op->u.lookup.
 *
 * Allocates memory for handle and attribute arrays that will be returned
 * in the response.
 *
 * Posts the request to the request scheduler.
 *
 * Note: memory is allocated as one big chunk, pointed to by
 * s_op->resp->u.lookup_path.handle_array.
 *
 */
static int lookup_init(PINT_server_op *s_op,
		       job_status_s *ret)
{
    int job_post_ret;
    char *ptr;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_init\n");

    /* fill in the lookup portion of the PINT_server_op */
    /* NOTE: it would be nice if we just decoded into this in the first place. */
    s_op->u.lookup.segp   = NULL;
    s_op->u.lookup.seg_nr = 0;
    s_op->u.lookup.seg_ct = PINT_string_count_segments(s_op->req->u.lookup_path.path);

    if (s_op->u.lookup.seg_ct < 0) {
	gossip_err("  invalid path %s; sending error response\n",
		   s_op->req->u.lookup_path.path);
	ret->error_code = ENOTDIR;
    }

    /* allocate memory
     *
     * Note: all memory is allocated in a single block, pointed to by s_op->resp->u.lookup_path.handle_array
     */
    ptr = malloc(s_op->u.lookup.seg_ct * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr)));
    assert(ptr != NULL);

    s_op->resp->u.lookup_path.handle_array = (PVFS_handle *) ptr;
    ptr += s_op->u.lookup.seg_ct * sizeof(PVFS_handle);
    s_op->resp->u.lookup_path.attr_array = (PVFS_object_attr *) ptr;

    job_post_ret = job_req_sched_post(s_op->req,
				      s_op,
				      ret,
				      &(s_op->scheduled_id));

    return job_post_ret;
}

/*
 * Function: lookup_read_object_metadata
 *
 * Synopsis: Given an object handle, looks up the attributes (metadata)
 * for that handle.
 *
 * Initializes key and value structures to direct metadata:
 * - if this is the starting (base) handle, store in s_op->u.lookup.base_attr
 * - otherwise store it in the appropriate slot in the resp handle array
 *
 * Posts the keyval read to trove.
 */
static int lookup_read_object_metadata(PINT_server_op *s_op,
				       job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;
    PVFS_handle handle;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_read_object_metadata\n");

    assert(s_op->u.lookup.seg_nr <= s_op->u.lookup.seg_ct);

    /* use the base handle if we haven't looked up a segment yet */
    if (s_op->u.lookup.seg_nr == 0) handle = s_op->req->u.lookup_path.starting_handle;
    else                            handle = s_op->resp->u.lookup_path.handle_array[s_op->u.lookup.seg_nr-1];

    /* initialize keyvals prior to read list call */

    s_op->key.buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    if (s_op->u.lookup.seg_nr == 0) s_op->val.buffer = &s_op->u.lookup.base_attr;
    else                            s_op->val.buffer = &s_op->resp->u.lookup_path.attr_array[s_op->u.lookup.seg_nr-1];

    s_op->val.buffer_sz = sizeof(PVFS_object_attr);

    gossip_debug(SERVER_DEBUG,
		 "  reading metadata (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.lookup_path.fs_id,
		 handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.lookup_path.fs_id,
					 handle,
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
 * Function: lookup_verify_object_metadata
 *
 * Synopsis: Examine the metadata returned from the keyval read.
 * If the metadata is for a directory, prepare to read the handle
 * of the next segment, if there is one.  If the metadata is for
 * a file, prepare to send a response.
 *
 * If the object is a directory, this function sets the
 * s_op->u.lookup.segp value to point to the next segment to look up;
 * this is used in lookup_read_directory_entry.
 *
 * This function does not post an operation, but rather returns 1
 * immediately.
 */
static int lookup_verify_object_metadata(PINT_server_op *s_op,
					 job_status_s *ret)
{
    int seg_ret;
    PVFS_object_attr *a_p;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_verify_object_metadata\n");

    if (s_op->u.lookup.seg_nr == 0) a_p = &s_op->u.lookup.base_attr;
    else                            a_p = &s_op->resp->u.lookup_path.attr_array[s_op->u.lookup.seg_nr - 1];

    assert(a_p->objtype == PVFS_TYPE_DIRECTORY || a_p->objtype == PVFS_TYPE_METAFILE);

    gossip_debug(SERVER_DEBUG,
		 "  attrs = (owner = %d, group = %d, perms = %o, type = %d)\n",
		 a_p->owner,
		 a_p->group,
		 a_p->perms,
		 a_p->objtype);

    /* if we hit a metafile, we are done */
    if (a_p->objtype == PVFS_TYPE_METAFILE) {
	gossip_debug(SERVER_DEBUG, "  object is a metafile; halting lookup and sending response\n");
	ret->error_code = STATE_ENOTDIR;
	return 1;
    }

    /* if we looked up all the segments, we are done */
    if (s_op->u.lookup.seg_nr == s_op->u.lookup.seg_ct) {
	gossip_debug(SERVER_DEBUG,
		     "  no more segments in path; sending response\n");
	ret->error_code = STATE_NOMORESEGS;
	return 1;
    }

    /* find the segment that we should look up in the directory */
    seg_ret = PINT_string_next_segment(s_op->req->u.lookup_path.path,
				       &s_op->u.lookup.segp,
				       &s_op->u.lookup.segstate);
    assert(seg_ret == 0);

    gossip_debug(SERVER_DEBUG,
		 "  object is a datafile; will be looking for handle for segment %s in a bit\n",
		 s_op->u.lookup.segp);

    return 1;
}

/*
 * Function: lookup_read_directory_entry_handle
 *
 * Synopsis: Given a directory handle, look up the handle used to store
 * directory entries for this directory.
 *
 * Initializes key and value structures to direct handle into
 * s_op->u.lookup.dirent_handle, which is where we always store the handle
 * used to read directory entries.  The handle to use for the read is either:
 * - the starting handle from the req (if we haven't looked up a segment yet), or
 * - the previous segment's handle (from response handle array).
 *
 * Posts the keyval read to trove.
 */
static int lookup_read_directory_entry_handle(PINT_server_op *s_op,
					      job_status_s *ret)
{
    TROVE_handle handle;
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_read_directory_entry_handle\n");

    /* initialize keyval prior to read call
     *
     * We will read the handle of the object holding dirents here.
     */

    /* use the base handle if we haven't looked up a segment yet */
    if (s_op->u.lookup.seg_nr == 0) handle = s_op->req->u.lookup_path.starting_handle;
    else                            handle = s_op->resp->u.lookup_path.handle_array[s_op->u.lookup.seg_nr-1];

    gossip_debug(SERVER_DEBUG,
		 "  reading dirent handle value from handle 0x%08Lx\n",
		 handle);

    s_op->key.buffer    = Trove_Common_Keys[DIR_ENT_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;
    s_op->val.buffer    = &s_op->u.lookup.dirent_handle;
    s_op->val.buffer_sz = sizeof(PVFS_handle);

    job_post_ret = job_trove_keyval_read(s_op->req->u.lookup_path.fs_id,
					 handle,
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
 * Function: lookup_read_directory_entry
 *
 * Synopsis: Given a handle for a dspace holding directory entries, look
 * up the current segment and obtain its handle.
 *
 * Initializes the key and value structures to direct the handle into the
 * resp handle array.  The segment is pointed to by s_op->u.lookup.segp.
 *
 * After initializing the key and value structures, this function increments
 * the seg_nr field so that subsequent operations will occur on the next segment.
 *
 * Posts the keyval read to trove.
 */
static int lookup_read_directory_entry(PINT_server_op *s_op,
				       job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_read_directory_entry\n");

    gossip_debug(SERVER_DEBUG,
		 "  reading from dirent handle = 0x%08Lx, segment = %s\n",
		 s_op->u.lookup.dirent_handle,
		 s_op->u.lookup.segp);

    /* initialize keyval prior to read call
     *
     * We will read the handle of the current segment here.
     */
    s_op->key.buffer    = s_op->u.lookup.segp;
    s_op->key.buffer_sz = strlen(s_op->u.lookup.segp) + 1;
    s_op->val.buffer    = &s_op->resp->u.lookup_path.handle_array[s_op->u.lookup.seg_nr];
    s_op->val.buffer_sz = sizeof(PVFS_handle);

    s_op->u.lookup.seg_nr++;

    job_post_ret = job_trove_keyval_read(s_op->req->u.lookup_path.fs_id,
					 s_op->u.lookup.dirent_handle,
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
 * Function: lookup_release_job
 *
 * Synopsis: Free the job from the scheduler to allow next job to proceed.
 *
 * Posts release request to job scheduler.
 */
static int lookup_release_job(PINT_server_op *s_op,
			      job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG,"lookup state: lookup_release_job\n");

    job_post_ret = job_req_sched_release(s_op->scheduled_id,
					 s_op,
					 ret,
					 &j_id);
    return job_post_ret;
}

/*
 * Function: lookup_send_response
 *
 * Synopsis: Encode and send response
 *
 * This function fills in the remaining sections of the response structure
 * prior to encoding the response.  It then calls PINT_encode() to
 * encode the response.
 *
 * Posts send to BMI.
 */
static int lookup_send_response(PINT_server_op *s_op,
				job_status_s *ret)
{
    int encode_ret, job_post_ret, payload_sz;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_send_response\n");

    payload_sz = s_op->u.lookup.seg_nr * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));

    /* fill in response -- WHAT IS GUARANTEED TO BE FILLED IN ALREADY??? */
    s_op->resp->op                  = PVFS_SERV_LOOKUP_PATH;
    s_op->resp->rsize               = sizeof(struct PVFS_server_resp_s) + payload_sz;
    s_op->resp->status              = 0; /* ??? */
    s_op->resp->u.lookup_path.count = s_op->u.lookup.seg_nr; /* # actually completed */

    gossip_debug(SERVER_DEBUG,
		 "  sending response with %d segment(s) (size %Ld)\n",
		 s_op->u.lookup.seg_nr,
		 s_op->resp->rsize);

    encode_ret = PINT_encode(s_op->resp,
			     PINT_ENCODE_RESP,
			     &(s_op->encoded),
			     s_op->addr,
			     s_op->enc_type);
    assert(encode_ret == 0);

    /* Post message */
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
 * Function: lookup_cleanup
 *
 * Synopsis: Free memory allocated during request processing.
 *
 * There are a bunch of regions that must be freed after processing
 * completes:
 * - decoded request (s_op->decoded)
 * - encoded request (s_op->unexp_bmi_buff.buffer)
 * - encoded response (s_op->encoded)
 * - original (decoded) response (s_op->resp)
 * - dynamically allocated space (in this case 
 *   s_op->resp->u.lookup_path.handle_array)
 * - the server operation structure itself
 *
 * This function does not post an operation, but instead returns 0,
 * indicating that this state machine is finished.           
 */
static int lookup_cleanup(PINT_server_op *s_op,
			  job_status_s *ret)
{
    gossip_debug(SERVER_DEBUG, "lookup state: lookup_cleanup\n");
    
    /* free decoded, encoded requests */
    PINT_decode_release(&(s_op->decoded), PINT_DECODE_REQ, 0);
    free(s_op->unexp_bmi_buff.buffer);

    /* free encoded response */
    PINT_encode_release(&(s_op->encoded), PINT_ENCODE_RESP, 0);

    /* free original response, including dynamically allocated memory */
    free(s_op->resp->u.lookup_path.handle_array);
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

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

#define LOOKUP_BUFF_SZ 5

enum 
{
    ENO_METADATA = 200
};

static int PINT_string_count_segments(char *pathname);
static int PINT_string_next_segment(char *pathname,
				    char **inout_segp,
				    void **opaquep);

static int lookup_init(PINT_server_op *s_op, job_status_s *ret);
static int lookup_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int lookup_verify_directory_metadata(PINT_server_op *s_op, job_status_s *ret);
static int lookup_send_response(PINT_server_op *s_op, job_status_s *ret);
static int lookup_read_directory_entry(PINT_server_op *s_op, job_status_s *ret);
static int lookup_read_directory_metadata(PINT_server_op *s_op, job_status_s *ret);
static int lookup_release_job(PINT_server_op *s_op, job_status_s *ret);
static int lookup_setup_next_segment(PINT_server_op *s_op, job_status_s *ret);
void lookup_init_state_machine(void);
/* TODO: Release Scheduled Job */

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s lookup_req_s = 
{
    NULL,
    "lookup",
    lookup_init_state_machine
};

%%

machine lookup(init, read_directory_metadata, read_directory_entry, verify_directory_metadata, send_response, release, cleanup, setup_next_segment)
{
    state init
	{
	    run lookup_init;
	    default => read_directory_metadata;
	}

    state read_directory_metadata
	{
	    run lookup_read_directory_metadata;
	    success => verify_directory_metadata;
	    default => send_response;
	}

    state verify_directory_metadata
	{
	    run lookup_verify_directory_metadata;
	    success      => read_directory_entry;
	    ENO_METADATA => send_response;
	    default      => send_response;
	}

    state read_directory_entry
	{
	    run lookup_read_directory_entry;
	    success => setup_next_segment;
	    default => send_response;
	}

    state setup_next_segment
	{
	    run lookup_setup_next_segment;
            success => read_directory_metadata;
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
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
 *           
 */

void lookup_init_state_machine(void)
{
    lookup_req_s.state_machine = lookup;
}

/*
 * Function: lookup_init
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     Scheduled
 *
 * Returns:  int
 *
 * Synopsis: Allocate all memory.  Should we just malloc one big region?
 *           
 */
static int lookup_init(PINT_server_op *s_op, job_status_s *ret)
{
    int seg_ret, job_post_ret;
    char *ptr;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_init\n");

    /* fill in the lookup portion of the PINT_server_op */
    /* NOTE: it would be nice if we just decoded into this in the first place. */
    s_op->u.lookup.path = s_op->req->u.lookup_path.path;
    s_op->u.lookup.segp = NULL;

    s_op->u.lookup.seg_ct = PINT_string_count_segments(s_op->u.lookup.path);
    s_op->u.lookup.seg_nr = 0;

    seg_ret = PINT_string_next_segment(s_op->u.lookup.path,
				       &s_op->u.lookup.segp,
				       &s_op->u.lookup.segstate);

    s_op->u.lookup.base_handle = s_op->req->u.lookup_path.starting_handle;
    s_op->u.lookup.fs_id       = s_op->req->u.lookup_path.fs_id;

    /* allocate memory
     *
     * Note: all memory is allocated in a single block, pointed to by s_op->u.lookup.h_a
     */
    ptr = malloc(s_op->u.lookup.seg_ct * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr)));
    assert(ptr != NULL);

    s_op->u.lookup.h_a = (PVFS_handle *) ptr;
    ptr += s_op->u.lookup.seg_ct * sizeof(PVFS_handle);
    s_op->u.lookup.oa_a = (PVFS_object_attr *) ptr;

    job_post_ret = job_req_sched_post(s_op->req,
				      s_op,
				      ret,
				      &(s_op->scheduled_id));

    return job_post_ret;
}

/*
 * Function: lookup_read_directory_metadata
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  int
 *
 * Synopsis: Given a directory handle, find the data space holding directory
 *           entries and the attributes for the directory by looking up the
 *           keyval pairs.
 *           
 */
static int lookup_read_directory_metadata(PINT_server_op *s_op, job_status_s *ret)
{
    int job_post_ret;
    job_id_t j_id;
    PVFS_handle handle;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_read_directory_metadata\n");

    /* use the base handle if we haven't looked up a segment yet */
    if (s_op->u.lookup.seg_nr == 0) handle = s_op->u.lookup.base_handle;
    else                            handle = s_op->u.lookup.h_a[s_op->u.lookup.seg_nr-1];

    gossip_debug(SERVER_DEBUG,
		 "  looking up segment %s using directory handle = 0x%08Lx\n",
		 s_op->u.lookup.segp,
		 handle);

    /* initialize keyvals prior to read list call
     *
     * We will read the handle of the dspace with direntries with the first
     * keyval, and the attributes with the second.
     *
     * If this is the base handle, we read attributes into base_attr rather
     * than into the object attribute array (oa_a).
     */
    s_op->u.lookup.k_a[0].buffer    = Trove_Common_Keys[DIR_ENT_KEY].key;
    s_op->u.lookup.k_a[0].buffer_sz = Trove_Common_Keys[DIR_ENT_KEY].size;
    s_op->u.lookup.v_a[0].buffer    = &s_op->u.lookup.dirent_handle;
    s_op->u.lookup.v_a[0].buffer_sz = sizeof(PVFS_handle);

    s_op->u.lookup.k_a[1].buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->u.lookup.k_a[1].buffer_sz = Trove_Common_Keys[METADATA_KEY].size;
    if (s_op->u.lookup.seg_nr == 0) s_op->u.lookup.v_a[1].buffer = &s_op->u.lookup.base_attr;
    else                            s_op->u.lookup.v_a[1].buffer = &s_op->u.lookup.oa_a[s_op->u.lookup.seg_nr-1];

    s_op->u.lookup.v_a[1].buffer_sz = sizeof(PVFS_object_attr);

    job_post_ret = job_trove_keyval_read_list(s_op->u.lookup.fs_id,
					      handle,
					      s_op->u.lookup.k_a,
					      s_op->u.lookup.v_a,
					      2,
					      0 /* flags */,
					      NULL,
					      s_op,
					      ret,
					      &j_id);
    return job_post_ret;
}

/*
 * Function: lookup_verify_directory_metadata
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  int
 *
 * Synopsis: The previous state tried to acquire the attribs for a directory
 *           and the handle for the dspace with directory entries.  Here
 *           we simply check permissions and check to see if we were able
 *           to successfully grab this data.
 *
 *           If we succeeded (in the last step), we will continue to look up
 *           path components.
 *           If we failed, we go on to send our response (partial??? verify!).
 *           
 */
static int lookup_verify_directory_metadata(PINT_server_op *s_op, job_status_s *ret)
{
    PVFS_object_attr *a_p;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_verify_directory_metadata\n");

    if (s_op->u.lookup.seg_nr == 0) a_p = &s_op->u.lookup.base_attr;
    else                            a_p = &s_op->u.lookup.oa_a[s_op->u.lookup.seg_nr - 1];

    assert(a_p->objtype == PVFS_TYPE_DIRECTORY);

    gossip_debug(SERVER_DEBUG,
		 "  dirent handle = 0x%08Lx, attrs = (owner = %d, group = %d, perms = %o, type = %d)\n",
		 s_op->u.lookup.dirent_handle,
		 a_p->owner,
		 a_p->group,
		 a_p->perms,
		 a_p->objtype);

    return 1;
}


/*
 * Function: lookup_read_directory_entry
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Given a path component and a parent handle, find the handle
 *           for the next component of the path.
 *           
 */
static int lookup_read_directory_entry(PINT_server_op *s_op, job_status_s *ret)
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
    s_op->u.lookup.k_a[0].buffer    = s_op->u.lookup.segp;
    s_op->u.lookup.k_a[0].buffer_sz = strlen(s_op->u.lookup.segp) + 1;
    s_op->u.lookup.v_a[0].buffer    = &s_op->u.lookup.h_a[s_op->u.lookup.seg_nr];
    s_op->u.lookup.v_a[0].buffer_sz = sizeof(PVFS_handle);

    job_post_ret = job_trove_keyval_read(s_op->u.lookup.fs_id,
					 s_op->u.lookup.dirent_handle,
					 s_op->u.lookup.k_a,
					 s_op->u.lookup.v_a,
					 0,
					 NULL,
					 s_op,
					 ret,
					 &j_id);
    return job_post_ret;
}

/*
 * Function: lookup_setup_next_segment
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis:
 *           
 */
static int lookup_setup_next_segment(PINT_server_op *s_op, job_status_s *ret)
{
    int seg_ret;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_setup_next_segment\n");

    s_op->u.lookup.seg_nr++;

    assert(s_op->u.lookup.seg_nr <= s_op->u.lookup.seg_ct);

    if (s_op->u.lookup.seg_nr == s_op->u.lookup.seg_ct) {
	gossip_debug(SERVER_DEBUG,
		     "  hit end of segments\n");
	ret->error_code = 99; /* TODO: MAKE THIS A REAL VALUE; THIS SEEMS POOR. */
	return 1;
    }
    else {
	gossip_debug(SERVER_DEBUG,
		     "  now on segment %d of %d\n",
		     s_op->u.lookup.seg_nr,
		     s_op->u.lookup.seg_ct);
    }
	
    seg_ret = PINT_string_next_segment(s_op->u.lookup.path,
				       &s_op->u.lookup.segp,
				       &s_op->u.lookup.segstate);
    if (seg_ret != 0) assert(0);
    return 1;
}

/*
 * Function: lookup_release_job
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


static int lookup_release_job(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    gossip_debug(SERVER_DEBUG,"lookup state: lookup_release_job\n");

    job_post_ret = job_req_sched_release(s_op->scheduled_id,
					 s_op,
					 ret,
					 &i);
    return job_post_ret;
}

/*
 * Function: lookup_send_response
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  int
 *
 * Synopsis: Encode and send
 *           
 */
static int lookup_send_response(PINT_server_op *s_op, job_status_s *ret)
{
    int encode_ret, job_post_ret, payload_sz;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "lookup state: lookup_send_response\n");

    payload_sz = s_op->u.lookup.seg_nr * (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));

    /* fill in response -- WHAT IS GUARANTEED TO BE FILLED IN ALREADY??? */
    s_op->resp->op = PVFS_SERV_LOOKUP_PATH;
    s_op->resp->rsize = sizeof(struct PVFS_server_resp_s) + payload_sz;
    s_op->resp->status = 0; /* ??? */
    s_op->resp->u.lookup_path.count        = s_op->u.lookup.seg_nr; /* # actually completed */
    s_op->resp->u.lookup_path.handle_array = s_op->u.lookup.h_a;
    s_op->resp->u.lookup_path.attr_array   = s_op->u.lookup.oa_a;

    gossip_debug(SERVER_DEBUG,
		 "  sending response with %d segment(s) (size %Ld)\n",
		 s_op->u.lookup.seg_nr,
		 s_op->resp->rsize);

    encode_ret = PINT_encode(s_op->resp,
			     PINT_ENCODE_RESP,
			     &(s_op->encoded),
			     s_op->addr,
			     s_op->enc_type);

    /* Q: IS ALL MY DATA OK TO BE FREED AFTER THE ENCODE??? */

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
				     &i);
    return(job_post_ret);
}

/*
 * Function: lookup_cleanup
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
static int lookup_cleanup(PINT_server_op *s_op, job_status_s *ret)
{
    gossip_debug(SERVER_DEBUG, "lookup state: lookup_cleanup\n");

    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);

    free(s_op->u.lookup.h_a);

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

/* PINT_string_count_segments()
 *
 * Count number of segments in a path.
 *
 * TODO: CLEAN UP!
 */
static int PINT_string_count_segments(char *pathname)
{
    int pos, nullpos = 0, killed_slash, segct = 0;

    /* insist on an absolute path */
    if (pathname[0] != '/') return -1;
    
    for(;;) {
	pos = nullpos;
	killed_slash = 0;

	/* jump to next separator */
	while ((pathname[nullpos] != '\0') && (pathname[nullpos] != '/')) nullpos++;

	if (nullpos == pos) {
	    /* extra slash or trailing slash; ignore */
	}
	else {
	    segct++; /* found another real segment */
	}
	if (pathname[nullpos] == '\0') break;
	nullpos++;
    }

    return segct;
}

/* PINT_string_next_segment()
 *
 * Parameters:
 * pathname   - pointer to string
 * inout_segp - address of pointer; NULL to get first segment,
 *              pointer to last segment to get next segment
 * opaquep    - address of void *, used to maintain state outside
 *              the function
 *
 * Returns 0 if segment is available, -1 on end of string.
 *
 * This approach is nice because it keeps all the necessary state
 * outside the function and concisely stores it in two pointers.
 *
 * Internals:
 * We're using opaquep to store the location of where we placed a
 * '\0' separator to get a segment.  The value is undefined when
 * called with *inout_segp == NULL.  Afterwards, if we place a '\0',
 * *opaquep will point to where we placed it.  If we do not, then we
 * know that we've hit the end of the path string before we even
 * start processing.
 *
 * Note that it is possible that *opaquep != NULL and still there are
 * no more segments; a trailing '/' could cause this, for example.
 */
static int PINT_string_next_segment(char *pathname,
				    char **inout_segp,
				    void **opaquep)
{
    char *ptr;

    /* insist on an absolute path */
    if (pathname[0] != '/') return -1;

    /* initialize our starting position */
    if (*inout_segp == NULL) {
	ptr = pathname;
    }
    else if (*opaquep != NULL) {
	/* replace the '/', point just past it */
	ptr = (char *) *opaquep;
	*ptr = '/';
	ptr++;
    }
    else return -1; /* NULL *opaquep indicates last segment returned last time */

    /* at this point, the string is back in its original state */

    /* jump past separators */
    while ((*ptr != '\0') && (*ptr == '/')) ptr++;
    if (*ptr == '\0') return -1; /* all that was left was trailing '/'s */

    *inout_segp = ptr;

    /* find next separator */
    while ((*ptr != '\0') && (*ptr != '/')) ptr++;
    if (*ptr == '\0') *opaquep = NULL; /* indicate last segment */
    else {
	/* terminate segment and save position of terminator */
	*ptr = '\0';
	*opaquep = ptr;
    }
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

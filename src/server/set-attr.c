/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <state-machine.h>
#include <pvfs2-server.h>
#include <pvfs2-server.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-attr.h"

enum {
    STATE_METAFILE = 7,
};

static int setattr_init(PINT_server_op *s_op, job_status_s *ret);
static int setattr_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int setattr_getobj_attribs(PINT_server_op *s_op, job_status_s *ret);
static int setattr_setobj_attribs(PINT_server_op *s_op, job_status_s *ret);
static int setattr_write_metafile_datafile_handles(PINT_server_op *s_op, job_status_s *ret);
static int setattr_write_metafile_distribution(PINT_server_op *s_op, job_status_s *ret);
static int setattr_verify_attribs(PINT_server_op *s_op, job_status_s *ret);
static int setattr_release_posted_job(PINT_server_op *s_op, job_status_s *ret);
static int setattr_send_bmi(PINT_server_op *s_op, job_status_s *ret);
void setattr_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

#if 0
PINT_state_machine_s setattr_req_s = 
{
	NULL,
	"setattr",
	setattr_init_state_machine
};
#endif


%%

machine set_attr(init,
		 cleanup,
		 getobj_attrib,
		 verify_attribs,
		 write_metafile_datafile_handles,
		 write_metafile_distribution,
		 setobj_attrib,
		 send_bmi,
		 release)
{
	state init
	{
		run setattr_init;
		default => getobj_attrib;
	}

	state getobj_attrib
	{
		run setattr_getobj_attribs;
		default => verify_attribs;
	}

	state verify_attribs
	{
		run setattr_verify_attribs;
		STATE_METAFILE => write_metafile_datafile_handles;
		success        => setobj_attrib;
		default        => send_bmi;
	}

	state write_metafile_datafile_handles
        {
		run setattr_write_metafile_datafile_handles;
		success => write_metafile_distribution;
		default => send_bmi;
	}

	state write_metafile_distribution
        {
		run setattr_write_metafile_distribution;
		success => setobj_attrib;
		default => send_bmi;
	}

	state setobj_attrib
	{
		run setattr_setobj_attribs;
		default => send_bmi;
	}

	state send_bmi
	{
		run setattr_send_bmi;
		default => release;
	}

	state release
	{
		run setattr_release_posted_job;
		default => cleanup;
	}

	state cleanup
	{
		run setattr_cleanup;
		default => init;
	}
}

%%

#if 0
/*
 * Function: setattr_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
 *           
 */

void setattr_init_state_machine(void)
{
	setattr_req_s.state_machine = set_attr;
}
#endif

/*
 * Function: setattr_init
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


static int setattr_init(PINT_server_op *s_op, job_status_s *ret)
{
    PVFS_object_attr *a_p;
    int job_post_ret;

    gossip_debug(SERVER_DEBUG, "setattr state: init\n");

    /* Dale noted that if ATTR_TYPE is set, the object might have just been
     * created and we might want to just skip this.  But he wasn't sure.
     */

    a_p = &s_op->req->u.setattr.attr;

    gossip_debug(SERVER_DEBUG,
		 "  attributes from request = (owner = %d, group = %d, perms = %o, type = %d)\n",
		 a_p->owner,
		 a_p->group,
		 a_p->perms,
		 a_p->objtype);

    gossip_debug(SERVER_DEBUG,
		 "  setting attributes for fs_id = 0x%x, handle = 0x%08Lx\n",
		 s_op->req->u.setattr.fs_id,
		 s_op->req->u.setattr.handle);

    /* post a scheduler job */
    job_post_ret = job_req_sched_post(s_op->req,
	    s_op,
	    ret,
	    &(s_op->scheduled_id));

    return(job_post_ret);

}


/*
 * Function: setattr_getobj_attrib
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 */

static int setattr_getobj_attribs(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t j_id;


    gossip_debug(SERVER_DEBUG, "setattr state: getobj_attribs\n");

    s_op->key.buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    /* allocate temporary space for holding read attributes; freed in verify step. */
    /* TODO: MOVE THIS INTO A SETATTR-SPECIFIC SCRATCH SPACE IN THE S_OP STRUCTURE */
    s_op->val.buffer = (void *) malloc((s_op->val.buffer_sz = sizeof(PVFS_object_attr)));

    gossip_debug(SERVER_DEBUG,
		 "  reading attributes (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.setattr.fs_id,
		 s_op->req->u.setattr.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_read(s_op->req->u.setattr.fs_id,
					 s_op->req->u.setattr.handle,
					 &(s_op->key),
					 &(s_op->val),
					 0,
					 NULL,
					 s_op,
					 ret,
					 &j_id);
    return(job_post_ret);
}


/*
 * Function: setattr_verify_attribs
 *
 * Notes:
 * - attributes were placed in s_op->val.buffer by setattr_getobj_attribs()
 *   - we free this buffer here rather than later on
 */
static int setattr_verify_attribs(PINT_server_op *s_op, job_status_s *ret)
{
    PVFS_object_attr *a_p;

    gossip_debug(SERVER_DEBUG, "setattr state: verify_attribs\n");

    a_p = (PVFS_object_attr *) s_op->val.buffer;

    if (ret->error_code != 0) {
	gossip_debug(SERVER_DEBUG,
		     "  previous keyval read had an error (new metafile?); data is useless\n");
    }
    else {
	gossip_debug(SERVER_DEBUG,
		     "  attrs read from dspace = (owner = %d, group = %d, perms = %o, type = %d)\n",
		     a_p->owner,
		     a_p->group,
		     a_p->perms,
		     a_p->objtype);
    }

    /* TODO: LOOK AT ATTRIBUTE MASK, SET UP s_op->req->u.setattr.attr for writing
     * in next step
     */

    /* TODO: HANDLE TYPES OTHER THAN METAFILES TOO, SOME DAY... */
    if ((ret->error_code != 0 && s_op->req->u.setattr.attr.objtype == PVFS_TYPE_METAFILE) || a_p->objtype == PVFS_TYPE_METAFILE)
    {
	free(s_op->val.buffer);
	gossip_debug(SERVER_DEBUG,
		     "  handle 0x%08Lx refers to a metafile\n",
		     s_op->req->u.setattr.handle);
	ret->error_code = STATE_METAFILE;
    }
    else {
	free(s_op->val.buffer);
	gossip_debug(SERVER_DEBUG,
		     "  handle 0x%08Lx refers to something other than a metafile\n",
		     s_op->req->u.setattr.handle);
	ret->error_code = 0;
    }
    
    return 1;
}


/*
 * Function: setattr_setobj_attribs
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */
static int setattr_setobj_attribs(PINT_server_op *s_op, job_status_s *ret)
{

    PVFS_object_attr *a_p;
    int job_post_ret=0;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "setattr state: setobj_attribs\n");

    a_p = &s_op->req->u.setattr.attr;

    gossip_debug(SERVER_DEBUG,
		 "  attrs to write = (owner = %d, group = %d, perms = %o, type = %d, atime = %ld, mtime = %ld, ctime = %ld)\n",
		 a_p->owner,
		 a_p->group,
		 a_p->perms,
		 a_p->objtype,
		 a_p->atime,
		 a_p->mtime,
		 a_p->ctime);

    /* set up key and value structure for keyval write */
    s_op->key.buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer    = &s_op->req->u.setattr.attr;
    s_op->val.buffer_sz = sizeof(struct PVFS_object_attr);

    gossip_debug(SERVER_DEBUG,
		 "  writing attributes (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.setattr.fs_id,
		 s_op->req->u.setattr.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_write(s_op->req->u.setattr.fs_id,
					  s_op->req->u.setattr.handle,
					  &s_op->key,
					  &s_op->val,
					  TROVE_SYNC /* flags */,
					  NULL,
					  s_op,
					  ret,
					  &j_id);
    return job_post_ret;
}


/*
 * Function: setattr_write_metafile_datafile_handles
 */
static int setattr_write_metafile_datafile_handles(PINT_server_op *s_op, job_status_s *ret)
{
    /*PVFS_object_attr *old_attr;*/
    int job_post_ret=0;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "setattr state: write_metafile_datafile_handles\n");

    /* we should have at least one datafile... */
    assert(s_op->req->u.setattr.attr.u.meta.nr_datafiles > 0);

    /* set up key and value structure for keyval write */
    s_op->key.buffer    = Trove_Common_Keys[METAFILE_HANDLES_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METAFILE_HANDLES_KEY].size;

    gossip_debug(SERVER_DEBUG,
		 "  metafile has %d datafiles associated with it\n",
		 s_op->req->u.setattr.attr.u.meta.nr_datafiles);

    s_op->val.buffer    = s_op->req->u.setattr.attr.u.meta.dfh;
    s_op->val.buffer_sz = s_op->req->u.setattr.attr.u.meta.nr_datafiles * sizeof(PVFS_handle);

    gossip_debug(SERVER_DEBUG,
		 "  writing datafile handles (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.setattr.fs_id,
		 s_op->req->u.setattr.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_write(s_op->req->u.setattr.fs_id,
					  s_op->req->u.setattr.handle,
					  &(s_op->key),
					  &(s_op->val),
					  TROVE_SYNC /* flags */,
					  NULL,
					  s_op,
					  ret,
					  &j_id);

    return job_post_ret;
}

/*
 * Function: setattr_write_metafile_distribution
 */
static int setattr_write_metafile_distribution(PINT_server_op *s_op, job_status_s *ret)
{
    /*PVFS_object_attr *old_attr;*/
    int job_post_ret=0;
    job_id_t j_id;

    gossip_debug(SERVER_DEBUG, "setattr state: write_metafile_distribution\n");

    /* distribution should take up non-negative space :) */
    assert(s_op->req->u.setattr.attr.u.meta.dist_size >= 0);

    /* set up key and value structure for keyval write */
    s_op->key.buffer    = Trove_Common_Keys[METAFILE_DIST_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METAFILE_DIST_KEY].size;

    gossip_debug(SERVER_DEBUG,
		 "  metafile distribution size = %Ld\n",
		 s_op->req->u.setattr.attr.u.meta.dist_size);

    s_op->val.buffer    = s_op->req->u.setattr.attr.u.meta.dist;
    s_op->val.buffer_sz = s_op->req->u.setattr.attr.u.meta.dist_size;

    /* TODO: figure out if we need a different packing mechanism here */
    gossip_err("KLUDGE: storing distribution on disk in network encoded format.\n");
    PINT_Dist_encode(NULL, s_op->req->u.setattr.attr.u.meta.dist);    

    gossip_debug(SERVER_DEBUG,
		 "  writing distribution (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.setattr.fs_id,
		 s_op->req->u.setattr.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_write(s_op->req->u.setattr.fs_id,
					  s_op->req->u.setattr.handle,
					  &(s_op->key),
					  &(s_op->val),
					  TROVE_SYNC /* flags */,
					  NULL,
					  s_op,
					  ret,
					  &j_id);
    return(job_post_ret);
}

/*
 * Function: setattr_send_bmi
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */

static int setattr_send_bmi(PINT_server_op *s_op, job_status_s *ret)
{
    int job_post_ret=0;
    job_id_t i;

    gossip_ldebug(SERVER_DEBUG, "setattr state: setattr_send_bmi\n");

    /* fill in response -- status field is the only generic one we should have to set */
    s_op->resp->u.generic.handle = s_op->req->u.setattr.handle;
    s_op->resp->status = ret->error_code;

    gossip_debug(SERVER_DEBUG,"  returning Status %d\n",ret->error_code);

    job_post_ret = PINT_encode(s_op->resp,
			       PINT_ENCODE_RESP,
			       &(s_op->encoded),
			       s_op->addr,
			       s_op->enc_type);
    assert(job_post_ret == 0);
    
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
 * Function: setattr_release_posted_job
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

static int setattr_release_posted_job(PINT_server_op *s_op, job_status_s *ret)
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
 * Function: setattr_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */
static int setattr_cleanup(PINT_server_op *s_op, job_status_s *ret)
{

    gossip_debug(SERVER_DEBUG, "setattr state: setattr_cleanup\n");

    /* free decoded, encoded requests */
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    free(s_op->unexp_bmi_buff.buffer);

    /* free encoded response */
    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);

    /* free original response */
    free(s_op->resp);

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


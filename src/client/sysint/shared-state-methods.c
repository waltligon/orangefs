/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "client-state-machine.h"
#include "state-machine-fns.h"
#include "pvfs2-debug.h"
#include "job.h"
#include "gossip.h"
#include "str-utils.h"

#include "pinode-helper.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "pcache.h"
#include "PINT-reqproto-encode.h"
#include "shared-state-methods.h"

int sm_common_parent_getattr_setup_msgpair(PINT_client_sm *sm_p,
                                           job_status_s *js_p)
{
    int ret = -1;

    gossip_debug(CLIENT_DEBUG,
                 "sm_common_parent_getattr_setup_msgpair\n");

    memset(&sm_p->msgpair, 0, sizeof(PINT_client_sm_msgpair_state));

    /* parameter range checks */
    assert(sm_p->parent_ref.fs_id != 0);
    assert(sm_p->parent_ref.handle != 0);

    /* fill in getattr request */
    PINT_SERVREQ_GETATTR_FILL(sm_p->msgpair.req,
			      *sm_p->cred_p,
			      sm_p->parent_ref.fs_id,
			      sm_p->parent_ref.handle,
			      PVFS_ATTR_COMMON_ALL);

    /* fill in msgpair structure components */
    sm_p->msgpair.fs_id   = sm_p->parent_ref.fs_id;
    sm_p->msgpair.handle  = sm_p->parent_ref.handle;
    sm_p->msgpair.comp_fn = sm_common_getattr_comp_fn;

    ret = PINT_bucket_map_to_server(&sm_p->msgpair.svr_addr,
				    sm_p->msgpair.handle,
				    sm_p->msgpair.fs_id);
    if (ret != 0)
    {
	gossip_err("Error: failure mapping to server.\n");
	assert(ret < 0); /* return value range check */
	assert(0); /* TODO: real error handling */
    }

    js_p->error_code = 0;
    return 1;
}

int sm_common_parent_getattr_failure(PINT_client_sm *sm_p,
                                     job_status_s *js_p)
{
    gossip_debug(CLIENT_DEBUG, "sm_common_parent_getattr_failure\n");

    return 1;
}

int sm_common_dspace_create_setup_msgpair(PINT_client_sm *sm_p,
                                          job_status_s *js_p)
{
    gossip_debug(CLIENT_DEBUG,
                 "state: sm_common_dspace_create_setup_msgpair\n");

    return 1;
}

int sm_common_dspace_create_failure(PINT_client_sm *sm_p,
                                    job_status_s *js_p)
{
    return 1;
}

int sm_common_crdirent_setup_msgpair(PINT_client_sm *sm_p,
                                     job_status_s *js_p)
{
    gossip_debug(CLIENT_DEBUG, "state: sm_common_crdirent_setup_msgpair\n");
    return 1;
}

int sm_common_crdirent_failure(PINT_client_sm *sm_p,
                               job_status_s *js_p);
int sm_common_setattr_setup_msgpair(PINT_client_sm *sm_p,
                                    job_status_s *js_p);
int sm_common_setattr_failure(PINT_client_sm *sm_p,
                              job_status_s *js_p);

/*
  shared/common msgpair completion functions
*/
int sm_common_getattr_comp_fn(void *v_p,
                              struct PVFS_server_resp *resp_p,
                              int index)
{
    int ret = 0;
    PVFS_object_attr *attr = NULL;
    PINT_client_sm *sm_p = (PINT_client_sm *) v_p;
    
    assert(resp_p->op == PVFS_SERV_GETATTR);

    gossip_debug(CLIENT_DEBUG, "sm_common_getattr_comp_fn\n");

    /* if we get an error, just return immediately, don't try to
     * actually fill anything in.
     */
    if (resp_p->status != 0)
    {
        gossip_err("Error: getattr failure\n");
	return resp_p->status;
    }

    /*
      if we didn't get a cache hit, we're making a
      copy of the attributes here so that we can add
      a pcache entry later in cleanup.
    */
    if (!sm_p->pcache_hit)
    {
        PINT_pcache_object_attr_deep_copy(
            &sm_p->pcache_attr, &resp_p->u.getattr.attr);
    }

    /*
      if we got a cache hit, use those attributes,
      otherwise use the real server replied attrs
    */
    attr = (sm_p->pcache_hit ?
            &sm_p->pinode->attr :
            &resp_p->u.getattr.attr);
    assert(attr);

    if (attr->objtype == PVFS_TYPE_DIRECTORY)
    {
        /*
          check permissions against parent directory to determine
          if we're allowed to create a new entry there
        */
        ret = PINT_check_perms(*attr, attr->perms,
                          sm_p->cred_p->uid, sm_p->cred_p->gid);
        if (ret < 0)
        {
            gossip_err("Error: Permission failure\n");
            return -PVFS_EPERM;
        }
    }
    else
    {
        gossip_err("Error: Parent is not a directory\n");
	return -PVFS_ENOTDIR;
    }

    /*
      if our parent directory attributes are good, and not present
      int the pcache, put them in the pcache now
    */
    if (!sm_p->pcache_hit)
    {
        int release_required = 1;
        PINT_pinode *pinode =
            PINT_pcache_lookup(sm_p->u.getattr.object_ref);
        if (!pinode)
        {
            pinode = PINT_pcache_pinode_alloc();
            assert(pinode);
            release_required = 0;
        }
        pinode->refn = sm_p->u.getattr.object_ref;
        pinode->size = ((attr->mask & PVFS_ATTR_DATA_ALL) ?
                        attr->u.data.size : 0);

        PINT_pcache_object_attr_deep_copy(
            &pinode->attr, &resp_p->u.getattr.attr);

        PINT_pcache_set_valid(pinode);

        if (release_required)
        {
            PINT_pcache_release(pinode);
        }
    }
    return 0;
}

int sm_common_create_comp_fn(void *v_p,
                             struct PVFS_server_resp *resp_p,
                             int index);
int sm_common_crdirent_comp_fn(void *v_p,
                               struct PVFS_server_resp *resp_p,
                               int index);
int sm_common_setattr_comp_fn(void *v_p,
                              struct PVFS_server_resp *resp_p,
                              int index);


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

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
#include "pint-util.h"

#include "pint-servreq.h"
#include "pint-cached-config.h"
#include "PINT-reqproto-encode.h"
#include "shared-state-methods.h"

int PINT_sm_common_parent_getattr_setup_msgpair(PINT_client_sm *sm_p,
                                                job_status_s *js_p)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_sm_common_parent_getattr_setup_msgpair\n");

    js_p->error_code = 0;

    memset(&sm_p->msgpair, 0, sizeof(PINT_sm_msgpair_state));

    sm_p->msgarray = &(sm_p->msgpair);
    sm_p->msgarray_count = 1;

    assert(sm_p->parent_ref.fs_id != PVFS_FS_ID_NULL);
    assert(sm_p->parent_ref.handle != PVFS_HANDLE_NULL);

    PINT_SERVREQ_GETATTR_FILL(
        sm_p->msgpair.req,
        *sm_p->cred_p,
        sm_p->parent_ref.fs_id,
        sm_p->parent_ref.handle,
        PVFS_ATTR_COMMON_ALL);

    sm_p->msgpair.fs_id   = sm_p->parent_ref.fs_id;
    sm_p->msgpair.handle  = sm_p->parent_ref.handle;
    sm_p->msgpair.retry_flag = PVFS_MSGPAIR_RETRY;
    sm_p->msgpair.comp_fn = PINT_sm_common_object_getattr_comp_fn;

    ret = PINT_cached_config_map_to_server(
        &sm_p->msgpair.svr_addr, sm_p->msgpair.handle,
        sm_p->msgpair.fs_id);

    if (ret)
    {
        PVFS_perror_gossip("Failed to map meta server address", ret);
        js_p->error_code = ret;
    }
    return 1;
}

int PINT_sm_common_parent_getattr_failure(PINT_client_sm *sm_p,
                                          job_status_s *js_p)
{
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_sm_common_parent_getattr_failure\n");

    PVFS_perror_gossip("PINT_sm_common_parent_getattr_failed",
                       js_p->error_code);
    return 1;
}

int PINT_sm_common_object_getattr_setup_msgpair(PINT_client_sm *sm_p,
                                                job_status_s *js_p)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_sm_common_object_getattr_setup_msgpair\n");

    js_p->error_code = 0;

    memset(&sm_p->msgpair, 0, sizeof(PINT_sm_msgpair_state));

    sm_p->msgarray = &(sm_p->msgpair);
    sm_p->msgarray_count = 1;

    assert(sm_p->object_ref.fs_id != PVFS_FS_ID_NULL);
    assert(sm_p->object_ref.handle != PVFS_HANDLE_NULL);

    PINT_SERVREQ_GETATTR_FILL(
        sm_p->msgpair.req,
        *sm_p->cred_p,
        sm_p->object_ref.fs_id,
        sm_p->object_ref.handle,
        (PVFS_ATTR_COMMON_ALL | PVFS_ATTR_META_ALL));

    sm_p->msgpair.fs_id = sm_p->object_ref.fs_id;
    sm_p->msgpair.handle = sm_p->object_ref.handle;
    sm_p->msgpair.retry_flag = PVFS_MSGPAIR_RETRY;
    sm_p->msgpair.comp_fn = PINT_sm_common_object_getattr_comp_fn;

    ret = PINT_cached_config_map_to_server(
        &sm_p->msgpair.svr_addr, sm_p->msgpair.handle,
        sm_p->msgpair.fs_id);

    if (ret)
    {
        PVFS_perror_gossip("Failed to map meta server address", ret);
        js_p->error_code = ret;
    }
    return 1;
}

int PINT_sm_common_object_getattr_failure(PINT_client_sm *sm_p,
                                          job_status_s *js_p)
{
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_sm_common_object_getattr_failure\n");

    PVFS_perror_gossip("PINT_sm_common_object_getattr_failed",
                       js_p->error_code);
    return 1;
}

int PINT_sm_common_object_getattr_comp_fn(
    void *v_p,
    struct PVFS_server_resp *resp_p,
    int index)
{
    int ret = -PVFS_EINVAL;
    PINT_client_sm *sm_p = (PINT_client_sm *) v_p;
    
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_sm_common_getattr_object_comp_fn\n");

    assert(resp_p->op == PVFS_SERV_GETATTR);
    assert(sm_p->msgarray == &sm_p->msgpair);

    sm_p->msgarray = NULL;
    sm_p->msgarray_count = 0;

    /* check for an error from the server */
    if (resp_p->status)
    {
        PVFS_perror_gossip("Getattr failed", resp_p->status);
	return resp_p->status;
    }

    /*
      check if we are supposed to make sure this object is of a
      particular type and return an error if it doesn't match.  this
      is useful so that the client can know (in some cases) that it
      can avoid issuing an operation to the server since it will pass
      an error back anyway
    */
    if (sm_p->ref_type &&
        (!(sm_p->ref_type & resp_p->u.getattr.attr.objtype)))
    {
        gossip_debug(GOSSIP_CLIENT_DEBUG, "*** PINT_sm_common_object_"
                     "getattr_comp_fn: Object type mismatch.\n Possibly "
                     "saving network roundtrip by returning an error\n");

        if (sm_p->ref_type == PVFS_TYPE_DIRECTORY)
        {
            ret = -PVFS_ENOTDIR;
        }
        else if (sm_p->ref_type == PVFS_TYPE_METAFILE)
        {
            ret = ((resp_p->u.getattr.attr.objtype ==
                    PVFS_TYPE_DIRECTORY) ? -PVFS_EISDIR : -PVFS_EBADF);
        }
        return ret;
    }

    /*
      if we didn't get a acache hit, we're making a copy of the
      attributes here so that we can add a acache entry later in
      cleanup.
    */
    if (!sm_p->acache_hit)
    {
        memset(&sm_p->acache_attr, 0, sizeof(PVFS_object_attr));
        PINT_copy_object_attr(&sm_p->acache_attr,
                              &resp_p->u.getattr.attr);
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

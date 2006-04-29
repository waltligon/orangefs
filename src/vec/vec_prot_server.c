/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "vec_config.h"
#include "vec_prot.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "pvfs2-types.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "rpcutils.h"
#include "vec_common.h"
#include "vec_grant_version.h"

static void deserialize_handle(char **synch_handle, PVFS_object_ref *ref)
{
    decode_PVFS_fs_id(synch_handle, &ref->fs_id);
    decode_PVFS_handle(synch_handle, &ref->handle);
}

static void init_opstatus(vec_status_t *pstatus, int status, int error)
{
    pstatus->vs_status = status;
    pstatus->vs_error  = error;
    return;
}

bool_t
vec_get_1_svc(vec_get_req arg1, vec_get_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
    handle_vec_cache_args req;
    int err;
    char *ptr = arg1.vr_handle;

    memset(&req, 0, sizeof(handle_vec_cache_args));
    deserialize_handle((char **) &ptr, &req.ref);
    req.offset = arg1.vr_offset;
    req.size   = arg1.vr_size;
    req.stripe_size = arg1.vr_stripe_size;
    req.nservers = arg1.vr_nservers;

    if ((err = svec_ctor(&result->vr_vectors, arg1.vr_max_vectors, req.nservers)) < 0) {
        init_opstatus(&result->vr_status, -1, err);
    }
    else {
        err = PINT_get_vec(&req, &result->vr_vectors);
        init_opstatus(&result->vr_status, (err < 0) ? -1 : 0, err);
        if (err < 0) {
            svec_dtor(&result->vr_vectors, arg1.vr_max_vectors);
        }
    }
	return retval;
}

bool_t
vec_put_1_svc(vec_put_req arg1, vec_put_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
    handle_vec_cache_args req;
    int err;
    char *ptr = arg1.vr_handle;

    memset(&req, 0, sizeof(handle_vec_cache_args));
    deserialize_handle((char **) &ptr, &req.ref);
    req.offset = arg1.vr_offset;
    req.size   = arg1.vr_size;
    req.stripe_size = arg1.vr_stripe_size;
    req.nservers = arg1.vr_nservers;

    if ((err = vec_ctor(&result->vr_vector, req.nservers)) < 0) {
        init_opstatus(&result->vr_status, -1, err);
    }
    else {
        err = PINT_inc_vec(&req, &result->vr_vector);
        init_opstatus(&result->vr_status, (err < 0) ? -1 : 0, err);
        if (err < 0) {
            vec_dtor(&result->vr_vector);
        }
    }

	return retval;
}

int
vec_mgr_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	xdr_free (xdr_result, result);

	/*
	 * Insert additional freeing code here, if needed
	 */

	return 1;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

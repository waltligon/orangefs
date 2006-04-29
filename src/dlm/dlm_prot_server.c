/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "dlm_config.h"
#include "dlm_prot.h"

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
#include "dlm_prot.h"
#include "rpcutils.h"

static void decode_handle(char **synch_handle, PVFS_fs_id *fsid, PVFS_handle *handle)
{
    decode_PVFS_fs_id(synch_handle, fsid);
    decode_PVFS_handle(synch_handle, handle);
}

/*
 * All these functions must return 1(true).
 * Any errors must be indicated in result-> ..
 */
bool_t
dlm_lock_1_svc(dlm_lock_req arg1, dlm_lock_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;

	/*
	 * TODO: insert server code here
	 * Do not change retval!
	 */

	return retval;
}

bool_t
dlm_lockv_1_svc(dlm_lockv_req arg1, dlm_lockv_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;

	/*
	 * TODO: insert server code here
	 * Do not change retval!
	 */

	return retval;
}

bool_t
dlm_unlock_1_svc(dlm_unlock_req arg1, dlm_unlock_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;

	/*
	 * TODO: insert server code here
	 * Do not change retval!
	 */

	return retval;
}

int
dlm_mgr_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
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

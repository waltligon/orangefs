/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-mgmt.h"
#include "gossip.h"

/* PVFS_mgmt_setparam_all()
 *
 * sets a particular global parameter on all servers associated with 
 * the given fs_id.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    enum PVFS_server_param param,
    int64_t value)
{
    gossip_lerr("Not implemented.\n");
    return(-PVFS_ENOSYS);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

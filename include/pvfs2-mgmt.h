/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header includes prototypes for management functions.  */

#ifndef __PVFS2_MGMT_H
#define __PVFS2_MGMT_H

#include "pvfs2-types.h"

int PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    enum PVFS_server_param param,
    int64_t value);

#endif /* __PVFS2_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

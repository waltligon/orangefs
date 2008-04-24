/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_TYPES_DEBUG_H
#define __PVFS2_TYPES_DEBUG_H

/* This file defined PINT_attrmask_print(), a useful debugging tool for
 * printing the contents of attrmasks.
 */

#include "gossip.h"
#include "pvfs2-types.h"

/* helper function for debugging */
static inline void PINT_attrmask_print(int debug, uint32_t attrmask)
{
    gossip_debug(debug, "mask = 0x%x:\n", attrmask);
    if (attrmask & PVFS_ATTR_COMMON_UID) gossip_debug(debug, "\tPVFS_ATTR_COMMON_UID\n");
    if (attrmask & PVFS_ATTR_COMMON_GID) gossip_debug(debug, "\tPVFS_ATTR_COMMON_GID\n");
    if (attrmask & PVFS_ATTR_COMMON_PERM) gossip_debug(debug, "\tPVFS_ATTR_COMMON_PERM\n");
    if (attrmask & PVFS_ATTR_COMMON_ATIME) gossip_debug(debug, "\tPVFS_ATTR_COMMON_ATIME\n");
    if (attrmask & PVFS_ATTR_COMMON_CTIME) gossip_debug(debug, "\tPVFS_ATTR_COMMON_CTIME\n");
    if (attrmask & PVFS_ATTR_COMMON_MTIME) gossip_debug(debug, "\tPVFS_ATTR_COMMON_MTIME\n");
    if (attrmask & PVFS_ATTR_COMMON_TYPE) gossip_debug(debug, "\tPVFS_ATTR_COMMON_TYPE\n");
    if (attrmask & PVFS_ATTR_META_DIST) gossip_debug(debug, "\tPVFS_ATTR_META_DIST\n");
    if (attrmask & PVFS_ATTR_META_DFILES) gossip_debug(debug, "\tPVFS_ATTR_META_DFILES\n");
    if (attrmask & PVFS_ATTR_DATA_SIZE) gossip_debug(debug, "\tPVFS_ATTR_DATA_SIZE\n");
    if (attrmask & PVFS_ATTR_SYS_SIZE) gossip_debug(debug, "\tPVFS_ATTR_SYS_SIZE\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif

/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gossip.h"

#include "pvfs2-types-debug.h"

/* helper function for debugging */
void PINT_attrmask_print(int debug, uint32_t attrmask)
{
    gossip_debug(debug, "mask = 0x%x:\n", attrmask);
    if (attrmask & PVFS_ATTR_COMMON_UID)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_UID\n");
    if (attrmask & PVFS_ATTR_COMMON_GID)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_GID\n");
    if (attrmask & PVFS_ATTR_COMMON_PERM)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_PERM\n");
    if (attrmask & PVFS_ATTR_COMMON_ATIME)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_ATIME\n");
    if (attrmask & PVFS_ATTR_COMMON_CTIME)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_CTIME\n");
    if (attrmask & PVFS_ATTR_COMMON_MTIME)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_MTIME\n");
    if (attrmask & PVFS_ATTR_COMMON_TYPE)
        gossip_debug(debug, "\tPVFS_ATTR_COMMON_TYPE\n");
    if (attrmask & PVFS_ATTR_META_DIST)
        gossip_debug(debug, "\tPVFS_ATTR_META_DIST\n");
    if (attrmask & PVFS_ATTR_META_DFILES)
        gossip_debug(debug, "\tPVFS_ATTR_META_DFILES\n");
    if (attrmask & PVFS_ATTR_META_MIRROR_DFILES)
        gossip_debug(debug, "\tPVFS_ATTR_META_MIRROR_DFILES\n");
    if (attrmask & PVFS_ATTR_DATA_SIZE)
        gossip_debug(debug, "\tPVFS_ATTR_DATA_SIZE\n");
    if (attrmask & PVFS_ATTR_SYMLNK_TARGET)
        gossip_debug(debug, "\tPVFS_ATTR_SYMLINK_TARGET\n");
    if (attrmask & PVFS_ATTR_DIR_DIRENT_COUNT)
        gossip_debug(debug, "\tPVFS_ATTR_DIR_DIRENT_COUNT\n");
    if (attrmask & PVFS_ATTR_DIR_HINT)
        gossip_debug(debug, "\tPVFS_ATTR_DIR_HINT\n");
    if (attrmask & PVFS_ATTR_DISTDIR_ATTR)
        gossip_debug(debug, "\tPVFS_ATTR_DIR_DISTDIR_ATTR\n");
    if (attrmask & PVFS_ATTR_SYS_SIZE)
        gossip_debug(debug, "\tPVFS_ATTR_SYS_SIZE\n");
}

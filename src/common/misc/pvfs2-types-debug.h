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

static inline void PINT_attr_dump_object_type(uint64_t gossip_mask, PVFS_object_attr *attr_p)
{
    /* Now, show type specific fields */
    if(attr_p->mask & PVFS_ATTR_COMMON_TYPE)
    {
        switch(attr_p->objtype)
        {
            case PVFS_TYPE_NONE:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_NONE", PVFS_TYPE_NONE);
                break;
            case PVFS_TYPE_METAFILE:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_METAFILE", PVFS_TYPE_METAFILE);
                break;
            case PVFS_TYPE_DATAFILE:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_DATAFILE", PVFS_TYPE_DATAFILE);
                break;
            case PVFS_TYPE_DIRECTORY:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_DIRECTORY", PVFS_TYPE_DIRECTORY);
                break;
            case PVFS_TYPE_SYMLINK:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_SYMLINK", PVFS_TYPE_SYMLINK);
                break;
            case PVFS_TYPE_DIRDATA:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_DIRDATA", PVFS_TYPE_DIRDATA);
                break;
            case PVFS_TYPE_INTERNAL:
                gossip_debug(gossip_mask, "attr->objtype == %s == %u\n", "PVFS_TYPE_INTERNAL", PVFS_TYPE_INTERNAL);
                break;
        }
    }
}

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
    if (attrmask & PVFS_ATTR_META_MIRROR_DFILES) gossip_debug(debug, "\tPVFS_ATTR_META_MIRROR_DFILES\n");
    if (attrmask & PVFS_ATTR_DATA_SIZE) gossip_debug(debug, "\tPVFS_ATTR_DATA_SIZE\n");
    if (attrmask & PVFS_ATTR_SYMLNK_TARGET) gossip_debug(debug, "\tPVFS_ATTR_SYMLINK_TARGET\n");
    if (attrmask & PVFS_ATTR_DIR_DIRENT_COUNT) gossip_debug(debug, "\tPVFS_ATTR_DIR_DIRENT_COUNT\n");
    if (attrmask & PVFS_ATTR_DIR_HINT) gossip_debug(debug, "\tPVFS_ATTR_DIR_HINT\n");
    if (attrmask & PVFS_ATTR_DISTDIR_ATTR) gossip_debug(debug, "\tPVFS_ATTR_DIR_DISTDIR_ATTR\n");
    if (attrmask & PVFS_ATTR_CAPABILITY) gossip_debug(debug, "\tPVFS_ATTR_CAPABILITY\n");
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

/* 
 * (C) 2018 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *
 *  PVFS2 internal debugging routines for looking at attributes.
 */

#include <pvfs2-internal.h>
#include <pvfs2-types.h>
#include <pvfs2-attr.h>

#if 0
#define DATTRPRINT(fmt) printf(fmt);

void DEBUG_attr_mask(uint64_t mask)
{
    DATTRPRINT("DEBUG_attr_mask in src/proto/pvfs2-attr.c");
    if (mask & PVFS_ATTR_COMMON_UID) DATTRPRINT("UID\n");
    if (mask & PVFS_ATTR_COMMON_GID) DATTRPRINT("GID\n");
    if (mask & PVFS_ATTR_COMMON_PERM) DATTRPRINT("PERM\n");
    if (mask & PVFS_ATTR_COMMON_ATIME) DATTRPRINT("ATIME\n");
    if (mask & PVFS_ATTR_COMMON_CTIME) DATTRPRINT("CTIME\n");
    if (mask & PVFS_ATTR_COMMON_MTIME) DATTRPRINT("MTIME\n");
    if (mask & PVFS_ATTR_COMMON_NTIME) DATTRPRINT("NTIME\n");
    if (mask & PVFS_ATTR_COMMON_TYPE) DATTRPRINT("TYPE\n");
    if (mask & PVFS_ATTR_COMMON_ATIME_SET) DATTRPRINT("ATIME SET\n");
    if (mask & PVFS_ATTR_COMMON_CTIME_SET) DATTRPRINT("CTIME SET\n");
    if (mask & PVFS_ATTR_COMMON_MTIME_SET) DATTRPRINT("MTIME SET\n");
    if (mask & PVFS_ATTR_COMMON_NTIME_SET) DATTRPRINT("NTIME SET\n");
    if (mask & PVFS_ATTR_META_DIST) DATTRPRINT("DIST\n");
    if (mask & PVFS_ATTR_META_FLAGS) DATTRPRINT("FLAGS\n");
    if (mask & PVFS_ATTR_DIR_DIRDATA) DATTRPRINT("DIrDAYA\n");
    if (mask & PVFS_ATTR_FASTEST) DATTRPRINT("FASTEST\n");
    if (mask & PVFS_ATTR_LATEST) DATTRPRINT("LATEST\n");
    if (mask & PVFS_ATTR_META_MIRROR_MODE) DATTRPRINT("MIRROE\n");
    if (mask & PVFS_ATTR_CAPABILITY) DATTRPRINT("CAP\n");
    if (mask & PVFS_ATTR_SYMLNK_TARGET) DATTRPRINT("SYMLINK\n");
    if (mask & PVFS_ATTR_DATA_SIZE) DATTRPRINT("SIZE\n");
    if (mask & PVFS_ATTR_DISTDIR_ATTR) DATTRPRINT("ATTR\n");
    if (mask & PVFS_ATTR_DIR_HINT) DATTRPRINT("HINT\n");
    if (mask & PVFS_ATTR_META_DFILES) DATTRPRINT("DFIBES\n");
    if (mask & PVFS_ATTR_DIR_DIRENT_COUNT) DATTRPRINT("COUNT\n");
}

#undef DATTRPRINT
#endif

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */


/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Extended attributes for PVFS2 that handles all setxattr
 *  stuff even for those keys that do not have a prefix!
 *  This is the 2.6 kernels way of doing extended attributes
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR)

#include <linux/xattr.h>

static int pvfs2_xattr_get_default(struct inode *inode,
        const char *name, void *buffer, size_t size)
{
    if (strcmp(name, "") == 0)
        return -EINVAL;
    return pvfs2_inode_getxattr(inode, name, buffer, size);
}

static int pvfs2_xattr_set_default(struct inode *inode, 
        const char *name, const void *buffer, size_t size, int flags)
{
    int internal_flag = 0;

    if (strcmp(name, "") == 0)
        return -EINVAL;
    internal_flag = convert_to_internal_xattr_flags(flags);
    return pvfs2_inode_setxattr(inode, name, buffer, size, internal_flag);
}

struct xattr_handler pvfs2_xattr_default_handler = {
    /* 
     * NOTE: this is set to be the empty string.
     * so that all un-prefixed xattrs keys get caught
     * here!
     */
    .prefix = PVFS2_XATTR_NAME_DEFAULT, 
    .get    = pvfs2_xattr_get_default,
    .set    = pvfs2_xattr_set_default,
};

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

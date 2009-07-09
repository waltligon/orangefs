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

#ifdef HAVE_XATTR

#include <linux/xattr.h>

int pvfs2_xattr_set_default(struct inode *inode, 
    const char *name, const void *buffer, size_t size, int flags)
{
    int internal_flag = 0;

    if (strcmp(name, "") == 0)
        return -EINVAL;
    if (!S_ISREG(inode->i_mode) &&
       (!S_ISDIR(inode->i_mode) || inode->i_mode & S_ISVTX))
    {
        gossip_err("pvfs2_xattr_set_default: Returning EPERM for inode %p.\n", inode);
        return -EPERM;
    }
    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_setxattr_default %s\n", name);
    internal_flag = convert_to_internal_xattr_flags(flags);
    return pvfs2_inode_setxattr(inode, PVFS2_XATTR_NAME_DEFAULT_PREFIX,
        name, buffer, size, internal_flag);
}

int pvfs2_xattr_get_default(struct inode *inode,
    const char *name, void *buffer, size_t size)
{
    if (strcmp(name, "") == 0)
        return -EINVAL;
    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_getxattr_default %s\n", name);
    return pvfs2_inode_getxattr(inode, PVFS2_XATTR_NAME_DEFAULT_PREFIX,
        name, buffer, size);
}
#endif

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR)

struct xattr_handler pvfs2_xattr_default_handler = {
    /* 
     * NOTE: this is set to be the empty string.
     * so that all un-prefixed xattrs keys get caught
     * here!
     */
    .prefix = PVFS2_XATTR_NAME_DEFAULT_PREFIX, 
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

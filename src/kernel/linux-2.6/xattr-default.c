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
#include <linux/xattr.h>

int pvfs2_xattr_set_default(struct dentry *dentry,
                            const char *name, 
                            const void *buffer, 
                            size_t size, 
                            int flags, 
                            int handler_flags)
{
    int internal_flag = 0;

    if (strcmp(name, "") == 0)
        return -EINVAL;

    if (!S_ISREG(dentry->d_inode->i_mode) &&
       (!S_ISDIR(dentry->d_inode->i_mode) || 
        dentry->d_inode->i_mode & S_ISVTX))
    {
        gossip_err("pvfs2_xattr_set_default: Returning EPERM for inode %p.\n",
                    dentry->d_inode);
        return -EPERM;
    }

    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_setxattr_default %s\n", name);
    internal_flag = convert_to_internal_xattr_flags(flags);

    return pvfs2_inode_setxattr(dentry->d_inode, 
        PVFS2_XATTR_NAME_DEFAULT_PREFIX, name, buffer, size, internal_flag);
}

int pvfs2_xattr_get_default(struct dentry *dentry,
                            const char *name, 
                            void *buffer, 
                            size_t size,
                            int handler_flags)
{
    if (strcmp(name, "") == 0)
        return -EINVAL;
    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_getxattr_default %s\n", name);

    return pvfs2_inode_getxattr(dentry->d_inode, 
        PVFS2_XATTR_NAME_DEFAULT_PREFIX, name, buffer, size);

}

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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

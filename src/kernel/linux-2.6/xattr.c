/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS extended attribute operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include <linux/xattr.h>

/* All pointers are in kernel-space */
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags)
{
    struct inode *inode = dentry->d_inode;
    int internal_flag = 0;

    /* VFS does a whole bunch of checks, but we still do
    *  some here */
    if (name == NULL || value == NULL ||
            size < 0 || size >= PVFS_MAX_XATTR_VALUELEN)
    {
        pvfs2_error("pvfs2_setxattr: invalid parameters\n");
        return -EINVAL;
    }
    /* Attribute must exist! */
    if (flags & XATTR_REPLACE)
    {
        internal_flag = PVFS_XATTR_REPLACE;
    }
    /* Attribute must not exist */
    else if (flags & XATTR_CREATE)
    {
        internal_flag = PVFS_XATTR_CREATE;
    }
    return pvfs2_inode_setxattr(inode, name, value, size, internal_flag);
}

ssize_t pvfs2_getxattr(struct dentry *dentry, const char *name,
		         void *buffer, size_t size)
{
    struct inode *inode = dentry->d_inode;
    return pvfs2_inode_getxattr(inode, name, buffer, size);
}

ssize_t pvfs2_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
    /* 
     * FIXME: The reason I am returning this value here is because 
     * we need server-side support for this, and I need to talk
     * to Walt and RobR about this...
     * Should not be too hard, but nonetheless a pain to get this
     * right.
     */
    return -EOPNOTSUPP;
}

int pvfs2_removexattr(struct dentry *dentry, const char *name)
{
    struct inode *inode = dentry->d_inode;
    return pvfs2_inode_removexattr(inode, name);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

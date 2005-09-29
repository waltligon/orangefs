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

#ifdef HAVE_XATTR

#include <linux/xattr.h>

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR)

/*
 * NOTES from fs/xattr.c
 * In order to implement different sets of xattr operations for each xattr
 * prefix with the generic xattr API, a filesystem should create a
 * null-terminated array of struct xattr_handler (one for each prefix) and
 * hang a pointer to it off of the s_xattr field of the superblock.
 */
struct xattr_handler *pvfs2_xattr_handlers[] = {
    /*
     * ACL xattrs have special prefixes that I am handling separately
     * so that we get control when the acl's are set or listed or queried!
     */
    &pvfs2_xattr_acl_access_handler,
    &pvfs2_xattr_acl_default_handler,
    /* 
     * NOTE: Please add prefix-based xattrs before this comment. 
     * Don't forget to change the handler map above and the associated
     * defines in pvfs2-kernel.h!
     * The pvfs2_xattr_default_handler handles all xattrs with "" (the empty)
     * prefix string! No one seemed to have any strong opinions on whether
     * this will hurt us/help us in the long run.
     */
    &pvfs2_xattr_default_handler,
    NULL
};

#else 

/* These routines are used only for the 2.4 kernel xattr callbacks or for early 2.6 kernels */

/* All pointers are in kernel-space */
#ifdef PVFS2_LINUX_KERNEL_2_4
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		void *value, size_t size, int flags)
#else
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags)
#endif
{
    struct inode *inode = dentry->d_inode;
    int internal_flag = 0;

    internal_flag = convert_to_internal_xattr_flags(flags);
    return pvfs2_inode_setxattr(inode, name, value, size, internal_flag);
}

ssize_t pvfs2_getxattr(struct dentry *dentry, const char *name,
		         void *buffer, size_t size)
{
    struct inode *inode = dentry->d_inode;
    return pvfs2_inode_getxattr(inode, name, buffer, size);
}

#endif

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

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

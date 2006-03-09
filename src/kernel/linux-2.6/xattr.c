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

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)

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
    &pvfs2_xattr_trusted_handler,
    &pvfs2_xattr_default_handler,
    NULL
};

#else 

/* prefix comparison function; taken from RedHat patched 2.4 kernel with
 * xattr support
 */
static inline const char * pvfs2_strcmp_prefix(
    const char *a, 
    const char *a_prefix)
{               
    while (*a_prefix && *a == *a_prefix) {
        a++;    
        a_prefix++;
    }       
    return *a_prefix ? NULL : a;
}       

/* These routines are used only for the 2.4 kernel xattr callbacks or for early 2.6 kernels */

/* All pointers are in kernel-space */
#ifdef HAVE_SETXATTR_CONST_ARG
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags)
#else
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		void *value, size_t size, int flags)
#endif
{
    struct inode *inode = dentry->d_inode;
    const char* n;
    int ret = -EOPNOTSUPP;

    if((n = pvfs2_strcmp_prefix(name, PVFS2_XATTR_NAME_TRUSTED_PREFIX)))
    {
        ret = pvfs2_xattr_set_trusted(inode, n, value, size, flags);
    }
    else
    {
        ret = pvfs2_xattr_set_default(inode, name, value, size, flags);
    }

    return ret;
}

ssize_t pvfs2_getxattr(struct dentry *dentry, const char *name,
		         void *buffer, size_t size)
{
    struct inode *inode = dentry->d_inode;
    const char* n;
    int ret = -EOPNOTSUPP;

    if((n = pvfs2_strcmp_prefix(name, PVFS2_XATTR_NAME_TRUSTED_PREFIX)))
    {
        ret = pvfs2_xattr_get_trusted(inode, n, buffer, size);
    }
    else
    {
        ret = pvfs2_xattr_get_default(inode, name, buffer, size);
    }

    return ret;
}

#endif

ssize_t pvfs2_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
    struct inode *inode = dentry->d_inode;
    return pvfs2_inode_listxattr(inode, buffer, size);
}

int pvfs2_removexattr(struct dentry *dentry, const char *name)
{
    struct inode *inode = dentry->d_inode;
    return pvfs2_inode_removexattr(inode, NULL, name);
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

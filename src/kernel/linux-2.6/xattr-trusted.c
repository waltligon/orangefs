/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Extended attributes for PVFS2 that handle the trusted prefix.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

#ifdef HAVE_XATTR

#include <linux/xattr.h>

int pvfs2_xattr_set_trusted(
#ifdef HAVE_XATTR_HANDLER_SET_4_4
                            const struct xattr_handler *handler,
                            struct dentry *dentry,
                            const char *name,
                            const void *buffer,
                            size_t size,
                            int flags)
#elif defined (HAVE_XATTR_HANDLER_SET_2_6_33)
                            struct dentry *dentry,
                            const char *name,
                            const void *buffer,
                            size_t size,
                            int flags,
                            int handler_flags)
#else
                            struct inode *inode, 
                            const char *name, 
                            const void *buffer, 
                            size_t size,
                            int flags)
#endif
{
    int internal_flag = 0;

    gossip_debug(GOSSIP_XATTR_DEBUG,
                 "pvfs2_xattr_set_trusted: name %s, buffer_size %zd\n",
                  name,
                  size);

    if (strcmp(name, "") == 0)
        return -EINVAL;

    if (!capable(CAP_SYS_ADMIN))
    {
        gossip_err("pvfs2_xattr_set_trusted: operation not permitted\n");
        return -EPERM;
    }

    internal_flag = convert_to_internal_xattr_flags(flags);

#if defined(HAVE_XATTR_HANDLER_SET_4_4) || \
    defined(HAVE_XATTR_HANDLER_SET_2_6_33)
    return pvfs2_inode_setxattr(dentry->d_inode,
#else /* pre 2.6.33 */
    return pvfs2_inode_setxattr(inode,
#endif
                                PVFS2_XATTR_NAME_TRUSTED_PREFIX,
                                name,
                                buffer,
                                size,
                                internal_flag);
}

int pvfs2_xattr_get_trusted(
#ifdef HAVE_XATTR_HANDLER_GET_4_4
                            const struct xattr_handler *handler,
                            struct dentry *dentry,
                            const char *name,
                            void *buffer,
                            size_t size)
#elif defined(HAVE_XATTR_HANDLER_GET_2_6_33)
                            struct dentry *dentry,
                            const char *name,
                            void *buffer,
                            size_t size,
                            int handler_flags)
#else
                            struct inode *inode,
                            const char *name,
                            void *buffer,
                            size_t size)
#endif
{
    gossip_debug(GOSSIP_XATTR_DEBUG,
                 "pvfs2_xattr_get_trusted: name %s, buffer_size %zd\n",
                 name,
                 size);

    if (strcmp(name, "") == 0)
        return -EINVAL;

    if(!capable(CAP_SYS_ADMIN))
    {
        gossip_err("pvfs2_xattr_get_trusted: operation not permitted\n");
        return -EPERM;
    }

#if defined(HAVE_XATTR_HANDLER_GET_4_4) || \
    defined(HAVE_XATTR_HANDLER_GET_2_6_33)
    return pvfs2_inode_getxattr(dentry->d_inode,
#else
    return pvfs2_inode_getxattr(inode,
#endif
                                PVFS2_XATTR_NAME_TRUSTED_PREFIX,
                                name,
                                buffer,
                                size);
}

#endif /* most of the file is wrapped in this */

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR)

struct xattr_handler pvfs2_xattr_trusted_handler = {
    .prefix = PVFS2_XATTR_NAME_TRUSTED_PREFIX, 
    .get    = pvfs2_xattr_get_trusted,
    .set    = pvfs2_xattr_set_trusted,
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

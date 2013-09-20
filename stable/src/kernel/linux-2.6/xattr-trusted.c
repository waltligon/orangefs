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
#ifdef HAVE_XATTR_HANDLER_SET_SIX_PARAM
        struct dentry *dentry,
#else
    struct inode *inode, 
#endif /* HAVE_XATTR_HANDLER_SET_SIX_PARAM */
    const char *name, 
    const void *buffer, 
    size_t size,
    int flags
#ifdef HAVE_XATTR_HANDLER_SET_SIX_PARAM
    , int handler_flags
#endif /* HAVE_XATTR_HANDLER_SET_SIX_PARAM */
    )
{
    int internal_flag = 0;

    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_xattr_set_trusted: name %s, buffer_size %zd\n",
            name, size);
    if (strcmp(name, "") == 0)
        return -EINVAL;
    if(!capable(CAP_SYS_ADMIN))
    {
        gossip_err("pvfs2_xattr_set_trusted: operation not permitted\n");
        return -EPERM;
    }
    internal_flag = convert_to_internal_xattr_flags(flags);

#ifdef HAVE_XATTR_HANDLER_SET_SIX_PARAM
    return pvfs2_inode_setxattr(dentry->d_inode, 
        PVFS2_XATTR_NAME_TRUSTED_PREFIX, name, buffer, size, internal_flag);
#else
    return pvfs2_inode_setxattr(inode, PVFS2_XATTR_NAME_TRUSTED_PREFIX,
        name, buffer, size, internal_flag);
#endif /* HAVE_XATTR_HANDLER_SET_SIX_PARAM */
}

int pvfs2_xattr_get_trusted(
#ifdef HAVE_XATTR_HANDLER_GET_FIVE_PARAM
    struct dentry *dentry,
#else
    struct inode *inode,
#endif /* HAVE_XATTR_HANDLER_GET_FIVE_PARAM */
    const char *name, 
    void *buffer, 
    size_t size
#ifdef HAVE_XATTR_HANDLER_GET_FIVE_PARAM
    , int handler_flags
#endif /* HAVE_XATTR_HANDLER_GET_FIVE_PARAM */
    )
{
    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_xattr_get_trusted: name %s, buffer_size %zd\n",
            name, size);
    if (strcmp(name, "") == 0)
        return -EINVAL;
    if(!capable(CAP_SYS_ADMIN))
    {
        gossip_err("pvfs2_xattr_get_trusted: operation not permitted\n");
        return -EPERM;
    }
#ifdef HAVE_XATTR_HANDLER_GET_FIVE_PARAM
    return pvfs2_inode_getxattr(dentry->d_inode, 
        PVFS2_XATTR_NAME_TRUSTED_PREFIX, name, buffer, size);
#else
    return pvfs2_inode_getxattr(inode, PVFS2_XATTR_NAME_TRUSTED_PREFIX,
        name, buffer, size);
#endif /* HAVE_XATTR_HANDLER_GET_FIVE_PARAM */
}

#endif

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

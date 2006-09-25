/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"

static int pvfs2_readlink(
    struct dentry *dentry, char __user *buffer, int buflen)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_readlink called on inode %llu\n",
                llu(get_handle_from_ino(dentry->d_inode)));

    /*
      if we're getting called, the vfs has no doubt already done a
      getattr, so we should always have the link_target string
      available in the pvfs2_inode private data
    */
    return vfs_readlink(dentry, buffer, buflen, pvfs2_inode->link_target);
}

#ifdef HAVE_INT_RETURN_INODE_OPERATIONS_FOLLOW_LINK
static int pvfs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2: pvfs2_follow_link called on %s (target is %p)\n",
                (char *)dentry->d_name.name, pvfs2_inode->link_target);

    return vfs_follow_link(nd, pvfs2_inode->link_target);
}
#else
static void *pvfs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2: pvfs2_follow_link called on %s (target is %p)\n",
                (char *)dentry->d_name.name, pvfs2_inode->link_target);

    return ERR_PTR(vfs_follow_link(nd, pvfs2_inode->link_target));
}
#endif

struct inode_operations pvfs2_symlink_inode_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    readlink : pvfs2_readlink,
    follow_link : pvfs2_follow_link,
    setattr : pvfs2_setattr,
    revalidate : pvfs2_revalidate,
#ifdef HAVE_XATTR
    setxattr: pvfs2_setxattr,
    listxattr: pvfs2_listxattr,
#endif
#else
    .readlink = pvfs2_readlink,
    .follow_link = pvfs2_follow_link,
    .setattr = pvfs2_setattr,
    .getattr = pvfs2_getattr,
    .listxattr = pvfs2_listxattr,
#if defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
    .setxattr = generic_setxattr,
#else
    .setxattr = pvfs2_setxattr,
#endif
#if defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
    .permission = pvfs2_permission,
#endif
#endif
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

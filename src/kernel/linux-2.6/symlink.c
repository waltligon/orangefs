/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"

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

   /* we used to use vfs_follow_link here, instead of nd_set_link.
    * vfs_follow_link is not just deprecated, it is
    * gone now. Whenever the follow_link in inode_operations returns
    * void, nd_set_link should be available, so now we'll just call
    * nd_set_link and return NULL...
    */
    nd_set_link(nd, pvfs2_inode->link_target);
    return NULL;

}
#endif

struct inode_operations pvfs2_symlink_inode_operations =
{
    .readlink = generic_readlink,
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
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

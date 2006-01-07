/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern struct dentry_operations pvfs2_dentry_operations;
extern int debug;

static int pvfs2_readlink(
    struct dentry *dentry, char __user *buffer, int buflen)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

    pvfs2_print("pvfs2_readlink called on inode %d\n",
                (int)dentry->d_inode->i_ino);

    /*
      if we're getting called, the vfs has no doubt already done a
      getattr, so we should always have the link_target string
      available in the pvfs2_inode private data
    */
    return vfs_readlink(dentry, buffer, buflen, pvfs2_inode->link_target);
}

static int pvfs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

    pvfs2_print("pvfs2: pvfs2_follow_link called on %s (target is %p)\n",
                (char *)dentry->d_name.name, pvfs2_inode->link_target);

    return vfs_follow_link(nd, pvfs2_inode->link_target);
}

struct inode_operations pvfs2_symlink_inode_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    readlink : pvfs2_readlink,
    follow_link : pvfs2_follow_link,
    setattr : pvfs2_setattr,
    revalidate : pvfs2_revalidate,
#else
    .readlink = pvfs2_readlink,
    .follow_link = pvfs2_follow_link,
    .setattr = pvfs2_setattr,
    .getattr = pvfs2_getattr,
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

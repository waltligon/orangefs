/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern struct dentry_operations pvfs2_dentry_operations;

/* defined in namei.c */
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd);

static int pvfs2_readlink(struct dentry *dentry, char *buffer, int buflen)
{
    int len = 0;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);
    pvfs2_print("pvfs2_readlink called on inode %d\n",
                (int)dentry->d_inode->i_ino);

    /*
      if we're getting called, the vfs has no doubt already done
      a getattr, so we should always have the link_target string
      available in the pvfs2_inode private data
    */
    if (pvfs2_inode && pvfs2_inode->link_target)
    {
        len = vfs_readlink(
            dentry, buffer, buflen, pvfs2_inode->link_target);
        pvfs2_print("pvfs2_readlink filled in %s\n", buffer);
    }
    return len;
}

/*
  see some rules at:
  http://www.cryptofreak.org/projects/port/#inode_ops
*/
static int pvfs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
    pvfs2_print("pvfs2: pvfs2_follow_link called on inode %d\n",
                (int)dentry->d_inode->i_ino);
    return 0;
}

struct inode_operations pvfs2_symlink_inode_operations = {
    .readlink     = pvfs2_readlink,
    .follow_link  = pvfs2_follow_link
/*     .setxattr    = pvfs2_setxattr, */
/*     .getxattr    = pvfs2_getxattr, */
/*     .listxattr   = pvfs2_listxattr, */
/*     .removexattr = pvfs2_removexattr */
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

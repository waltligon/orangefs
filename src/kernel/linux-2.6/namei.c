/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include "pvfs2-kernel.h"

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;

extern struct dentry_operations pvfs2_dentry_operations;

/*
  called with a negative dentry, so we need to hook
  it up with a newly allocated inode
*/
static int pvfs2_create(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    struct nameidata *nd)
{
    struct inode *inode =
	pvfs2_create_entry(dir, dentry, NULL, mode, PVFS2_VFS_OP_CREATE);

    return (inode ? 0 : -1);
}

struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    struct inode *inode = NULL;
    pvfs2_kernel_op_t *new_op = (pvfs2_kernel_op_t *) 0;
    pvfs2_inode_t *parent = NULL;
    pvfs2_inode_t *found_pvfs2_inode = NULL;
    struct super_block *sb = NULL;

    /*
      we can skip doing anything knowing that the intent is to
      create.  normally this results in an expensive failed
      lookup; we're avoiding that here.
    */
    if (nd && (nd->flags & LOOKUP_CREATE) &&
        !(nd->flags & LOOKUP_CONTINUE))
    {
        pvfs2_print("pvfs2: pvfs2_lookup -- skipping operation "
                    "based on create intent\n");
        return NULL;
    }

    /* same thing for an exclusive open */
    if (nd && (nd->flags & LOOKUP_OPEN) &&
        (nd->intent.open.flags & O_EXCL))
    {
        pvfs2_print("pvfs2: pvfs2_lookup -- skipping operation "
                    "based on excl open intent\n");
        return NULL;
    }

    if (dentry->d_name.len > PVFS2_NAME_LEN)
    {
	return ERR_PTR(-ENAMETOOLONG);
    }

    new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
    if (!new_op)
    {
	pvfs2_error("pvfs2: pvfs2_lookup -- kmem_cache_alloc failed!\n");
	return NULL;
    }
    new_op->upcall.type = PVFS2_VFS_OP_LOOKUP;

    if (dir)
    {
        sb = dir->i_sb;
        parent = PVFS2_I(dir);
        if (parent && parent->refn.handle && parent->refn.fs_id)
        {
            new_op->upcall.req.lookup.parent_refn = parent->refn;
        }
        else
        {
            new_op->upcall.req.lookup.parent_refn.handle =
                pvfs2_ino_to_handle(dir->i_ino);
            new_op->upcall.req.lookup.parent_refn.fs_id =
                PVFS2_SB(sb)->fs_id;
        }
    }
    else
    {
        /*
          if no parent at all was provided, use the root
          handle and file system id stored in the super
          block for the specified dentry's inode
        */
        sb = dentry->d_inode->i_sb;
	new_op->upcall.req.lookup.parent_refn.handle =
	    PVFS2_SB(sb)->handle;
	new_op->upcall.req.lookup.parent_refn.fs_id =
	    PVFS2_SB(sb)->fs_id;
    }
    strncpy(new_op->upcall.req.lookup.d_name,
	    dentry->d_name.name, PVFS2_NAME_LEN);

    service_operation_with_timeout_retry(
        new_op, "pvfs2_lookup", retries);

    /* check what kind of goodies we got */
    pvfs2_print("Lookup Got PVFS2 handle %Lu on fsid %d\n",
                new_op->downcall.resp.lookup.refn.handle,
                new_op->downcall.resp.lookup.refn.fs_id);

    /* lookup inode matching name (or add if not there) */
    if (new_op->downcall.status > -1)
    {
	inode = iget(sb, pvfs2_handle_to_ino(
                         new_op->downcall.resp.lookup.refn.handle));
	if (inode)
	{
	    found_pvfs2_inode = PVFS2_I(inode);

	    /* store the retrieved handle and fs_id */
	    found_pvfs2_inode->refn = new_op->downcall.resp.lookup.refn;

	    /* update dentry/inode pair into dcache */
	    dentry->d_op = &pvfs2_dentry_operations;
	    d_splice_alias(inode, dentry);
	}
	else
	{
	    pvfs2_error("FIXME: Invalid pvfs2 private data\n");
	}
    }

  error_exit:
    /* when request is serviced properly, free req op struct */
    op_release(new_op);

    return NULL;
}

static int pvfs2_link(
    struct dentry *old_dentry,
    struct inode *dir,
    struct dentry *dentry)
{
/*     struct inode *inode = pvfs2_create_entry( */
/*         dir, dentry, mode, PVFS2_VFS_OP_LINK); */

    pvfs2_print("pvfs2: pvfs2_link called\n");
    if (dir->i_nlink >= PVFS2_LINK_MAX)
    {
	return -EMLINK;
    }
    return 0;
}

/* return 0 on success; non-zero otherwise */
static int pvfs2_unlink(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = pvfs2_remove_entry(dir, dentry);
    if (ret == 0)
    {
	dir->i_nlink--;
    }
    return ret;
}

static int pvfs2_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname)
{
    int mode = 755;
    struct inode *inode = NULL;

    pvfs2_print("pvfs2: pvfs2_symlink called\n");

    inode = pvfs2_create_entry(
        dir, dentry, symname, mode, PVFS2_VFS_OP_SYMLINK);
    return (inode ? 0 : -1);
}

static int pvfs2_mknod(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    dev_t rdev)
{
    pvfs2_print("pvfs2: pvfs2_mknod called\n");
    return 0;
}

static int pvfs2_mkdir(
    struct inode *dir,
    struct dentry *dentry,
    int mode)
{
    int ret = -1;
    struct inode *inode =
	pvfs2_create_entry(dir, dentry, NULL, mode, PVFS2_VFS_OP_MKDIR);

    if (inode)
    {
	dir->i_nlink++;
	ret = 0;
    }
    return ret;
}

static int pvfs2_rmdir(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = pvfs2_remove_entry(dir, dentry);
    if (ret == 0)
    {
	dir->i_nlink--;
    }
    return ret;
}

static int pvfs2_rename(
    struct inode *old_dir,
    struct dentry *old_dentry,
    struct inode *new_dir,
    struct dentry *new_dentry)
{
    pvfs2_print("pvfs2: pvfs2_rename called (%s to %s)\n",
                old_dentry->d_name.name, new_dentry->d_name.name);
    return 0;
}

struct inode_operations pvfs2_dir_inode_operations = {
    .create = pvfs2_create,
    .lookup = pvfs2_lookup,
    .link = pvfs2_link,
    .unlink = pvfs2_unlink,
    .symlink = pvfs2_symlink,
    .mkdir = pvfs2_mkdir,
    .rmdir = pvfs2_rmdir,
    .mknod = pvfs2_mknod,
    .rename = pvfs2_rename,
    .setattr = pvfs2_setattr
/*     .follow_link = pvfs2_follow_link */
/*     .setxattr = pvfs2_setxattr, */
/*     .getxattr = pvfs2_getxattr, */
/*     .listxattr = pvfs2_listxattr, */
/*     .removexattr = pvfs2_removexattr */
/*     .setattr = pvfs2_setattr, */
/*     .permission = pvfs2_permission */
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

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
#include <linux/smp_lock.h>
#include "pvfs2-kernel.h"

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
#ifdef DEVREQ_WAITQ_INTERFACE
extern wait_queue_head_t pvfs2_request_list_waitq;
#endif

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
    int ret = -1;
    struct inode *inode =
	pvfs2_create_entry(dir, dentry, NULL, mode, PVFS2_VFS_OP_CREATE);

    if (inode)
    {
        inode->i_nlink++;
        ret = 0;
    }
    return ret;
}

struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT, error_exit = 0;
    struct inode *inode = NULL;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = NULL, *found_pvfs2_inode = NULL;
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

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!new_op)
    {
	pvfs2_error("pvfs2: pvfs2_lookup -- kmem_cache_alloc failed!\n");
	return NULL;
    }
    new_op->upcall.type = PVFS2_VFS_OP_LOOKUP;

    /* if we're at a symlink, should we follow it? */
    new_op->upcall.req.lookup.sym_follow =
        ((nd && (nd->flags & LOOKUP_FOLLOW)) ?
         PVFS2_LOOKUP_LINK_FOLLOW : PVFS2_LOOKUP_LINK_NO_FOLLOW);

    pvfs2_print("pvfs2: pvfs2_lookup -- follow %s? %s\n",
                dentry->d_name.name,
                (new_op->upcall.req.lookup.sym_follow ?
                 "yes" : "no"));

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

    service_lookup_op_with_timeout_retry(
        new_op, "pvfs2_lookup", retries, error_exit);

    /* check what kind of goodies we got */
    pvfs2_print("Lookup Got PVFS2 handle %Lu on fsid %d\n",
                new_op->downcall.resp.lookup.refn.handle,
                new_op->downcall.resp.lookup.refn.fs_id);

    ret = new_op->downcall.status;

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

            pvfs2_print("Lookup success (inode ct = %d)\n",
                        (int)atomic_read(&inode->i_count));
	}
	else
	{
	    pvfs2_error("FIXME: Invalid pvfs2 private data\n");
            op_release(new_op);
            return ERR_PTR(-EACCES);
	}
    }

  error_exit:
    /* when request is serviced properly, free req op struct */
    op_release(new_op);

    /*
      if no inode was found, add a negative dentry to dcache
      anyway; if we don't, we don't hold expected lookup semantics
      and we most noticeably break during directory renames.

      if the operation failed or exited, do not add the dentry.
    */
    if (!inode && !error_exit)
    {
        d_add(dentry, inode);
    }
    return NULL;
}

static int pvfs2_link(
    struct dentry *old_dentry,
    struct inode *dir,
    struct dentry *dentry)
{
    pvfs2_print("pvfs2: pvfs2_link called\n");
    return -ENOSYS;
}

/* return 0 on success; non-zero otherwise */
static int pvfs2_unlink(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = -ENOENT;
    struct inode *inode = dentry->d_inode;

    ret = pvfs2_remove_entry(dir, dentry);
    if (ret == 0)
    {
        inode->i_ctime = dir->i_ctime;
    }
    return ret;
}

static int pvfs2_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname)
{
    int ret = -1, mode = 755;
    struct inode *inode = NULL;

    pvfs2_print("pvfs2: pvfs2_symlink called\n");

    inode = pvfs2_create_entry(
        dir, dentry, symname, mode, PVFS2_VFS_OP_SYMLINK);

    if (inode)
    {
        dir->i_nlink++;
        ret = 0;
    }
    return ret;
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
    int ret = -ENOTEMPTY;
    struct inode *inode = dentry->d_inode;

    if (pvfs2_empty_dir(dentry))
    {
        ret = pvfs2_unlink(dir, dentry);
        if (ret == 0)
        {
            inode->i_size = 0;
            dir->i_nlink--;
        }
    }
    return ret;
}

static int pvfs2_rename(
    struct inode *old_dir,
    struct dentry *old_dentry,
    struct inode *new_dir,
    struct dentry *new_dentry)
{
    int ret = -1, retries = 5, are_directories = 0;
    pvfs2_inode_t *pvfs2_old_parent_inode = PVFS2_I(old_dir);
    pvfs2_inode_t *pvfs2_new_parent_inode = PVFS2_I(new_dir);
    pvfs2_kernel_op_t *new_op = NULL;
    struct super_block *sb = NULL;

    pvfs2_print("pvfs2: pvfs2_rename called (%s/%s => %s/%s) ct=%d\n",
                old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
                new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
                atomic_read(&new_dentry->d_count));

    are_directories = S_ISDIR(old_dentry->d_inode->i_mode);
    if (are_directories && (new_dir->i_nlink >= PVFS2_LINK_MAX))
    {
        pvfs2_error("pvfs2: pvfs2_rename -- directory %s "
                    "surpassed PVFS2_LINK_MAX\n",
                    new_dentry->d_name.name);
        return -EMLINK;
    }

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!new_op)
    {
	pvfs2_error("pvfs2: pvfs2_rename -- kmem_cache_alloc failed!\n");
	return ret;
    }
    new_op->upcall.type = PVFS2_VFS_OP_RENAME;

    /*
      if no handle/fs_id is available in the parent,
      use the root handle/fs_id as specified by the
      inode's corresponding superblock
    */
    if (pvfs2_old_parent_inode->refn.handle &&
        pvfs2_old_parent_inode->refn.fs_id)
    {
        new_op->upcall.req.rename.old_parent_refn =
            pvfs2_old_parent_inode->refn;
    }
    else
    {
        sb = old_dir->i_sb;
        new_op->upcall.req.rename.old_parent_refn.handle =
	    PVFS2_SB(sb)->handle;
        new_op->upcall.req.rename.old_parent_refn.fs_id =
	    PVFS2_SB(sb)->fs_id;
    }

    /* do the same for the new parent */
    if (pvfs2_new_parent_inode->refn.handle &&
        pvfs2_new_parent_inode->refn.fs_id)
    {
        new_op->upcall.req.rename.new_parent_refn =
            pvfs2_new_parent_inode->refn;
    }
    else
    {
        sb = new_dir->i_sb;
        new_op->upcall.req.rename.new_parent_refn.handle =
	    PVFS2_SB(sb)->handle;
        new_op->upcall.req.rename.new_parent_refn.fs_id =
	    PVFS2_SB(sb)->fs_id;
    }
    strncpy(new_op->upcall.req.rename.d_old_name,
	    old_dentry->d_name.name, PVFS2_NAME_LEN);
    strncpy(new_op->upcall.req.rename.d_new_name,
	    new_dentry->d_name.name, PVFS2_NAME_LEN);

    service_operation_with_timeout_retry(
        new_op, "pvfs2_rename", retries);

    /*
      nothing's returned; just return the exit status

      NOTE: make sure the properly translated error code
      is passed down from above to distinguish between
      different types of rename errors (target dir/file
      exists, other error, etc).
    */
    ret = new_op->downcall.status;

    pvfs2_print("pvfs2: pvfs2_rename got downcall status %d\n", ret);

    if (new_dentry->d_inode)
    {
        if (are_directories && simple_empty(new_dentry))
        {
            pvfs2_print("pvfs2: pvfs2_rename target dir not empty\n");
            ret = -ENOTEMPTY;
            goto error_exit;
        }

        /*
          FIXME:
          at some point we need to get our set
          attribute rules straight
        */
        new_dentry->d_inode->i_ctime = CURRENT_TIME;
        if (are_directories)
        {
            new_dentry->d_inode->i_nlink--;
        }
    }
    else if (are_directories)
    {
        new_dir->i_nlink++;
        old_dir->i_nlink--;
    }

  error_exit:
    op_release(new_op);

    return ret;
}

struct inode_operations pvfs2_dir_inode_operations =
{
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

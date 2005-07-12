/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS namei operations.
 */

#include "pvfs2-kernel.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern int debug;

extern struct dentry_operations pvfs2_dentry_operations;

/** Get a newly allocated inode to go with a negative dentry.
 */
#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_create(
    struct inode *dir,
    struct dentry *dentry,
    int mode)
#else
static int pvfs2_create(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    struct nameidata *nd)
#endif
{
    int ret = -EINVAL;
    struct inode *inode = NULL;

    pvfs2_print("pvfs2_create: called\n");

    inode = pvfs2_create_entry(
        dir, dentry, NULL, mode, PVFS2_VFS_OP_CREATE, &ret);

    if (inode)
    {
        pvfs2_update_inode_time(dir);
        ret = 0;
    }

    pvfs2_print("pvfs2_create: returning %d\n", ret);
    return ret;
}

/** Attempt to resolve an object name (dentry->d_name), parent handle, and
 *  fsid into a handle for the object.
 */
#ifdef PVFS2_LINUX_KERNEL_2_4
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry)
#else
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd)
#endif
{
    int ret = -EINVAL, retries = PVFS2_OP_RETRY_COUNT, error_exit = 0;
    struct inode *inode = NULL;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = NULL, *found_pvfs2_inode = NULL;
    struct super_block *sb = NULL;

    /*
      in theory we could skip a lookup here (if the intent is to
      create) in order to avoid a potentially failed lookup, but
      leaving it in can skip a valid lookup and try to create a file
      that already exists (e.g. the vfs already handles checking for
      -EEXIST on O_EXCL opens, which is broken if we skip this lookup
      in the create path)
    */
    pvfs2_print("pvfs2_lookup called on %s\n", dentry->d_name.name);

    if (dentry->d_name.len > PVFS2_NAME_LEN)
    {
	return ERR_PTR(-ENAMETOOLONG);
    }

    new_op = op_alloc();
    if (!new_op)
    {
	return NULL;
    }
    new_op->upcall.type = PVFS2_VFS_OP_LOOKUP;

#ifdef PVFS2_LINUX_KERNEL_2_4
    new_op->upcall.req.lookup.sym_follow = PVFS2_LOOKUP_LINK_NO_FOLLOW;
#else
    /*
      if we're at a symlink, should we follow it? never attempt to
      follow negative dentries
    */
    new_op->upcall.req.lookup.sym_follow =
        ((nd && (nd->flags & LOOKUP_FOLLOW) &&
          (dentry->d_inode != NULL)) ?
         PVFS2_LOOKUP_LINK_FOLLOW : PVFS2_LOOKUP_LINK_NO_FOLLOW);
#endif

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
	    PVFS2_SB(sb)->root_handle;
	new_op->upcall.req.lookup.parent_refn.fs_id =
	    PVFS2_SB(sb)->fs_id;
    }
    strncpy(new_op->upcall.req.lookup.d_name,
	    dentry->d_name.name, PVFS2_NAME_LEN);

    pvfs2_print("pvfs2_lookup: doing lookup on %s\n  under %Lu,%d "
                "(follow=%s)\n", new_op->upcall.req.lookup.d_name,
                new_op->upcall.req.lookup.parent_refn.handle,
                new_op->upcall.req.lookup.parent_refn.fs_id,
                ((new_op->upcall.req.lookup.sym_follow ==
                  PVFS2_LOOKUP_LINK_FOLLOW) ? "yes" : "no"));

    service_error_exit_op_with_timeout_retry(
        new_op, "pvfs2_lookup", retries, error_exit,
        PVFS2_SB(dir->i_sb)->mnt_options.intr);

    ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

    pvfs2_print("Lookup Got %Lu, fsid %d (ret=%d)\n",
                new_op->downcall.resp.lookup.refn.handle,
                new_op->downcall.resp.lookup.refn.fs_id, ret);

    /* lookup inode matching name (or add if not there) */
    if (ret > -1)
    {
	inode = iget(sb, pvfs2_handle_to_ino(
                         new_op->downcall.resp.lookup.refn.handle));
	if (inode && !is_bad_inode(inode))
	{
	    found_pvfs2_inode = PVFS2_I(inode);

	    /* store the retrieved handle and fs_id */
	    found_pvfs2_inode->refn = new_op->downcall.resp.lookup.refn;

	    /* update dentry/inode pair into dcache */
	    dentry->d_op = &pvfs2_dentry_operations;

            pvfs2_d_splice_alias(dentry, inode);

            pvfs2_print("Lookup success (inode ct = %d)\n",
                        (int)atomic_read(&inode->i_count));
	}
	else
	{
            if (inode)
            {
                iput(inode);
            }
            op_release(new_op);
            return ERR_PTR(-EACCES);
	}
    }

  error_exit:
    op_release(new_op);

    /*
      if no inode was found, add a negative dentry to dcache anyway;
      if we don't, we don't hold expected lookup semantics and we most
      noticeably break during directory renames.

      however, if the operation failed or exited, do not add the
      dentry (e.g. in the case that a touch is issued on a file that
      already exists that was interrupted during this lookup -- no
      need to add another negative dentry for an existing file)
    */
    if (!inode && !error_exit)
    {
        /*
          make sure to set the pvfs2 specific dentry operations for
          the negative dentry that we're adding now so that a
          potential future lookup of this cached negative dentry can
          be properly revalidated.
        */
        pvfs2_print("pvfs2_lookup: Adding *negative* dentry %p\n  "
                    "for %s\n", dentry, dentry->d_name.name);

        dentry->d_op = &pvfs2_dentry_operations;
        d_add(dentry, inode);
    }
    return NULL;
}

/* return 0 on success; non-zero otherwise */
static int pvfs2_unlink(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = -ENOENT;
    struct inode *inode = dentry->d_inode;

    pvfs2_print("pvfs2_unlink: pvfs2_unlink called on %s\n",
                dentry->d_name.name);

    ret = pvfs2_remove_entry(dir, dentry);
    if (ret == 0)
    {
        inode->i_nlink--;
        pvfs2_update_inode_time(dir);
    }
    return ret;
}

/* pvfs2_link() is only implemented here to make sure that we return a
 * reasonable error code (the kernel will return a misleading EPERM
 * otherwise).  PVFS2 does not support hard links.
 */
static int pvfs2_link(
    struct dentry * old_dentry, 
    struct inode * dir,
    struct dentry *dentry)
{
    return(-EOPNOTSUPP);
}

/* pvfs2_mknod() is only implemented here to make sure that we return a
 * reasonable error code (the kernel will return a misleading EPERM
 * otherwise).  PVFS2 does not support special files such as fifos or devices.
 */
#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_mknod(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    int rdev)
#else
static int pvfs2_mknod(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    dev_t rdev)
#endif
{
    return(-EOPNOTSUPP);
}

static int pvfs2_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname)
{
    int ret = -EINVAL, mode = 755;
    struct inode *inode = NULL;

    pvfs2_print("pvfs2_symlink: called\n");

    inode = pvfs2_create_entry(
        dir, dentry, symname, mode, PVFS2_VFS_OP_SYMLINK, &ret);

    if (inode)
    {
        pvfs2_update_inode_time(dir);
        ret = 0;
    }
    return ret;
}

static int pvfs2_mkdir(
    struct inode *dir,
    struct dentry *dentry,
    int mode)
{
    int ret = -EINVAL;
    struct inode *inode = NULL;

    inode = pvfs2_create_entry(
        dir, dentry, NULL, mode, PVFS2_VFS_OP_MKDIR, &ret);

    if (inode)
    {
#if 0
        /* NOTE: we have no good way to keep nlink consistent for directories
         * across clients; keep constant at 1  -Phil
         */
	dir->i_nlink++;
#endif
        pvfs2_update_inode_time(dir);
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

    ret = pvfs2_unlink(dir, dentry);
    if (ret == 0)
    {
        inode->i_nlink--; 
#if 0
        /* NOTE: we have no good way to keep nlink consistent for directories
         * across clients; keep constant at 1  -Phil
         */
	dir->i_nlink--;
#endif
        pvfs2_update_inode_time(dir);
    }
    return ret;
}

static int pvfs2_rename(
    struct inode *old_dir,
    struct dentry *old_dentry,
    struct inode *new_dir,
    struct dentry *new_dentry)
{
    int ret = -EINVAL, retries = 5, are_directories = 0;
    pvfs2_inode_t *pvfs2_old_parent_inode = PVFS2_I(old_dir);
    pvfs2_inode_t *pvfs2_new_parent_inode = PVFS2_I(new_dir);
    pvfs2_kernel_op_t *new_op = NULL;
    struct super_block *sb = NULL;

    pvfs2_print("pvfs2_rename: called (%s/%s => %s/%s) ct=%d\n",
                old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
                new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
                atomic_read(&new_dentry->d_count));

    are_directories = S_ISDIR(old_dentry->d_inode->i_mode);
#if 0
    /* NOTE: we have no good way to keep nlink consistent for directories
     * across clients; keep constant at 1  -Phil
     */
    if (are_directories && (new_dir->i_nlink >= PVFS2_LINK_MAX))
    {
        pvfs2_error("pvfs2_rename: directory %s surpassed "
                    "PVFS2_LINK_MAX\n",
                    new_dentry->d_name.name);
        return -EMLINK;
    }
#endif

    new_op = op_alloc();
    if (!new_op)
    {
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
	    PVFS2_SB(sb)->root_handle;
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
	    PVFS2_SB(sb)->root_handle;
        new_op->upcall.req.rename.new_parent_refn.fs_id =
	    PVFS2_SB(sb)->fs_id;
    }
    strncpy(new_op->upcall.req.rename.d_old_name,
	    old_dentry->d_name.name, PVFS2_NAME_LEN);
    strncpy(new_op->upcall.req.rename.d_new_name,
	    new_dentry->d_name.name, PVFS2_NAME_LEN);

    service_operation_with_timeout_retry(
        new_op, "pvfs2_rename", retries,
        get_interruptible_flag(old_dentry->d_inode));

    ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

    pvfs2_print("pvfs2_rename: got downcall status %d\n", ret);

    if (new_dentry->d_inode)
    {
        new_dentry->d_inode->i_ctime = CURRENT_TIME;
#if 0
        /* NOTE: we have no good way to keep nlink consistent for directories
         * across clients; keep constant at 1  -Phil
         */
        if (are_directories)
        {
            new_dentry->d_inode->i_nlink--;
        }
#endif
    }
#if 0
    /* NOTE: we have no good way to keep nlink consistent for directories
     * across clients; keep constant at 1  -Phil
     */
    else if (are_directories)
    {
        new_dir->i_nlink++;
        old_dir->i_nlink--;
    }
#endif

  error_exit:
    translate_error_if_wait_failed(ret, 0, 0);
    op_release(new_op);

#ifdef PVFS2_LINUX_KERNEL_2_4
    if (ret == 0)
    {
        d_move(old_dentry, new_dentry);
    }
#endif
    return ret;
}

/** PVFS2 implementation of VFS inode operations for directories */
struct inode_operations pvfs2_dir_inode_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    create : pvfs2_create,
    lookup : pvfs2_lookup,
    link : pvfs2_link,
    unlink : pvfs2_unlink,
    symlink : pvfs2_symlink,
    mkdir : pvfs2_mkdir,
    rmdir : pvfs2_rmdir,
    mknod : pvfs2_mknod,
    rename : pvfs2_rename,
    setattr : pvfs2_setattr,
    revalidate : pvfs2_revalidate
#else
    .create = pvfs2_create,
    .lookup = pvfs2_lookup,
    .link = pvfs2_link,
    .unlink = pvfs2_unlink,
    .symlink = pvfs2_symlink,
    .mkdir = pvfs2_mkdir,
    .rmdir = pvfs2_rmdir,
    .mknod = pvfs2_mknod,
    .rename = pvfs2_rename,
    .setattr = pvfs2_setattr,
    .getattr = pvfs2_getattr
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

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
#include "pvfs2-internal.h"

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

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_create: called\n");

    inode = pvfs2_create_entry(
        dir, dentry, NULL, mode, PVFS2_VFS_OP_CREATE, &ret);

    if (inode)
    {
        pvfs2_inode_t *dir_pinode = PVFS2_I(dir);

        SetMtimeFlag(dir_pinode);
        pvfs2_update_inode_time(dir);
        mark_inode_dirty_sync(dir);
        
        ret = 0;
    }

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_create: returning %d\n", ret);
    return ret;
}

/** Attempt to resolve an object name (dentry->d_name), parent handle, and
 *  fsid into a handle for the object.
 */
#ifdef PVFS2_LINUX_KERNEL_2_4
static struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry)
#else
static struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd)
#endif
{
    int ret = -EINVAL;
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
    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_lookup called on %s\n", dentry->d_name.name);

    if (dentry->d_name.len > (PVFS2_NAME_LEN-1))
    {
	return ERR_PTR(-ENAMETOOLONG);
    }

    new_op = op_alloc(PVFS2_VFS_OP_LOOKUP);
    if (!new_op)
    {
	return ERR_PTR(-ENOMEM);
    }

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
        if (parent && parent->refn.handle != PVFS_HANDLE_NULL 
                && parent->refn.fs_id != PVFS_FS_ID_NULL)
        {
            gossip_debug(GOSSIP_NAME_DEBUG, "%s:%s:%d using parent %llu\n",
              __FILE__, __func__, __LINE__, llu(parent->refn.handle));
            new_op->upcall.req.lookup.parent_refn = parent->refn;
        }
        else
        {
#if defined(HAVE_IGET4_LOCKED) || defined(HAVE_IGET5_LOCKED)
            gossip_lerr("Critical error: i_ino cannot be relied on when using iget5/iget4\n");
            op_release(new_op);
            return ERR_PTR(-EINVAL);
#endif
            new_op->upcall.req.lookup.parent_refn.handle =
                get_handle_from_ino(dir);
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

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_lookup: doing lookup on %s\n  under %llu,%d "
                "(follow=%s)\n", new_op->upcall.req.lookup.d_name,
                llu(new_op->upcall.req.lookup.parent_refn.handle),
                new_op->upcall.req.lookup.parent_refn.fs_id,
                ((new_op->upcall.req.lookup.sym_follow ==
                  PVFS2_LOOKUP_LINK_FOLLOW) ? "yes" : "no"));

    ret = service_operation(
        new_op, "pvfs2_lookup", 
        get_interruptible_flag(dir));

    gossip_debug(GOSSIP_NAME_DEBUG, "Lookup Got %llu, fsid %d (ret=%d)\n",
                llu(new_op->downcall.resp.lookup.refn.handle),
                new_op->downcall.resp.lookup.refn.fs_id, ret);

    if(ret < 0)
    {
        if(ret == -ENOENT)
        {
            /*
             * if no inode was found, add a negative dentry to dcache anyway;
             * if we don't, we don't hold expected lookup semantics and we most
             * noticeably break during directory renames.
             *
             * however, if the operation failed or exited, do not add the
             * dentry (e.g. in the case that a touch is issued on a file that
             * already exists that was interrupted during this lookup -- no
             * need to add another negative dentry for an existing file)
             */

            gossip_debug(GOSSIP_NAME_DEBUG, 
                         "pvfs2_lookup: Adding *negative* dentry %p\n for %s\n",
                         dentry, dentry->d_name.name);

            /*
             * make sure to set the pvfs2 specific dentry operations for
             * the negative dentry that we're adding now so that a
             * potential future lookup of this cached negative dentry can
             * be properly revalidated.
             */
            dentry->d_op = &pvfs2_dentry_operations;
            d_add(dentry, inode);

            op_release(new_op);
            return NULL;
        }

        op_release(new_op);
        /* must be a non-recoverable error */
        return ERR_PTR(ret);
    }

    inode = pvfs2_iget(sb, &new_op->downcall.resp.lookup.refn);
    if (inode && !is_bad_inode(inode))
    {
        struct dentry *res;

        gossip_debug(GOSSIP_NAME_DEBUG, "%s:%s:%d Found good inode [%lu] with count [%d]\n", 
            __FILE__, __func__, __LINE__, inode->i_ino, (int)atomic_read(&inode->i_count));

        /* update dentry/inode pair into dcache */
        dentry->d_op = &pvfs2_dentry_operations;

        res = pvfs2_d_splice_alias(dentry, inode);

        gossip_debug(GOSSIP_NAME_DEBUG, "Lookup success (inode ct = %d)\n",
                     (int)atomic_read(&inode->i_count));
        if (res)
            res->d_op = &pvfs2_dentry_operations;

        op_release(new_op);
#ifdef PVFS2_LINUX_KERNEL_2_4
        return NULL;
#else
        return res;
#endif
    }
    else if (inode && is_bad_inode(inode))
    {
        gossip_debug(GOSSIP_NAME_DEBUG, "%s:%s:%d Found bad inode [%lu] with count [%d]. Returning error [%d]", 
            __FILE__, __func__, __LINE__, inode->i_ino, (int)atomic_read(&inode->i_count), ret);
        ret = -EACCES;
        found_pvfs2_inode = PVFS2_I(inode);
        /* look for an error code, possibly set by pvfs2_read_inode(),
         * otherwise we have to guess EACCES 
         */
        if(found_pvfs2_inode->error_code)
        {
            ret = found_pvfs2_inode->error_code;
        }
        iput(inode);
        op_release(new_op);
        return ERR_PTR(ret);
    }

    /* no error was returned from service_operation, but the inode
     * from pvfs2_iget was null...just return EACCESS
     */
    op_release(new_op);
    gossip_debug(GOSSIP_NAME_DEBUG, "Returning -EACCES for NULL inode\n");
    return ERR_PTR(-EACCES);
}

/* return 0 on success; non-zero otherwise */
static int pvfs2_unlink(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = -ENOENT;
    struct inode *inode = dentry->d_inode;

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_unlink: pvfs2_unlink called on %s\n",
                dentry->d_name.name);

    ret = pvfs2_remove_entry(dir, dentry);
    if (ret == 0)
    {
        pvfs2_inode_t *dir_pinode = PVFS2_I(dir);
        inode->i_nlink--;

        SetMtimeFlag(dir_pinode);
        pvfs2_update_inode_time(dir);
        mark_inode_dirty_sync(dir);
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

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_symlink: called\n");

    inode = pvfs2_create_entry(
        dir, dentry, symname, mode, PVFS2_VFS_OP_SYMLINK, &ret);

    if (inode)
    {
        pvfs2_inode_t *dir_pinode = PVFS2_I(dir);

        SetMtimeFlag(dir_pinode);
        pvfs2_update_inode_time(dir);
        mark_inode_dirty_sync(dir);

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
        pvfs2_inode_t *dir_pinode = PVFS2_I(dir);

        SetMtimeFlag(dir_pinode);
        pvfs2_update_inode_time(dir);
        mark_inode_dirty_sync(dir);

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
        pvfs2_inode_t *dir_pinode = PVFS2_I(dir);
        inode->i_nlink--; 
#if 0
        /* NOTE: we have no good way to keep nlink consistent for directories
         * across clients; keep constant at 1  -Phil
         */
	dir->i_nlink--;
#endif

        SetMtimeFlag(dir_pinode);
        pvfs2_update_inode_time(dir);
        mark_inode_dirty_sync(dir);
    }
    return ret;
}

static int pvfs2_rename(
    struct inode *old_dir,
    struct dentry *old_dentry,
    struct inode *new_dir,
    struct dentry *new_dentry)
{
    int ret = -EINVAL, are_directories = 0;
    pvfs2_inode_t *pvfs2_old_parent_inode = PVFS2_I(old_dir);
    pvfs2_inode_t *pvfs2_new_parent_inode = PVFS2_I(new_dir);
    pvfs2_kernel_op_t *new_op = NULL;
    struct super_block *sb = NULL;

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_rename: called (%s/%s => %s/%s) ct=%d\n",
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
        gossip_err("pvfs2_rename: directory %s surpassed "
                    "PVFS2_LINK_MAX\n",
                    new_dentry->d_name.name);
        return -EMLINK;
    }
#endif

    new_op = op_alloc(PVFS2_VFS_OP_RENAME);
    if (!new_op)
    {
	return ret;
    }

    /*
      if no handle/fs_id is available in the parent,
      use the root handle/fs_id as specified by the
      inode's corresponding superblock
    */
    if (pvfs2_old_parent_inode &&
            pvfs2_old_parent_inode->refn.handle != PVFS_HANDLE_NULL &&
            pvfs2_old_parent_inode->refn.fs_id != PVFS_FS_ID_NULL)
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
    if (pvfs2_new_parent_inode &&
            pvfs2_new_parent_inode->refn.handle != PVFS_HANDLE_NULL &&
            pvfs2_new_parent_inode->refn.fs_id != PVFS_FS_ID_NULL)
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

    ret = service_operation(
        new_op, "pvfs2_rename", 
        get_interruptible_flag(old_dentry->d_inode));

    gossip_debug(GOSSIP_NAME_DEBUG, "pvfs2_rename: got downcall status %d\n", ret);

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
    revalidate : pvfs2_revalidate,
#ifdef HAVE_XATTR
    getxattr: pvfs2_getxattr,
    setxattr: pvfs2_setxattr,
    removexattr: pvfs2_removexattr,
    listxattr: pvfs2_listxattr,
#endif
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
    .getattr = pvfs2_getattr,
#if defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
    .getxattr = generic_getxattr,
    .setxattr = generic_setxattr,
    .removexattr = generic_removexattr,
#else
    .setxattr = pvfs2_setxattr,
    .getxattr = pvfs2_getxattr,
    .removexattr = pvfs2_removexattr,
#endif
    .listxattr = pvfs2_listxattr,
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

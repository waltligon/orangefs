/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS directory operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-sysint.h"
#include "pvfs2-internal.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern int debug;

/* shared file/dir operations defined in file.c */
extern int pvfs2_file_open(
    struct inode *inode,
    struct file *file);
extern int pvfs2_file_release(
    struct inode *inode,
    struct file *file);

/** Read directory entries from an instance of an open directory.
 *
 * \param filldir callback function called for each entry read.
 *
 * \retval <0 on error
 * \retval 0  when directory has been completely traversed
 * \retval >0 if we don't call filldir for all entries
 *
 * \note If the filldir call-back returns non-zero, then readdir should
 *       assume that it has had enough, and should return as well.
 */
static int pvfs2_readdir(
    struct file *file,
    void *dirent,
    filldir_t filldir)
{
    int ret = 0;
    PVFS_ds_position pos = 0;
    ino_t ino = 0;
    struct dentry *dentry = file->f_dentry;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

  restart_readdir:

    pos = (PVFS_ds_position)file->f_pos;
    /* are we done? */
    if (pos == PVFS_READDIR_END)
    {
        pvfs2_print("Skipping to graceful termination path since we are done\n");
        return 0;
    }

    pvfs2_print("pvfs2_readdir called on %s (pos=%d, "
                "retry=%d, v=%llu)\n", dentry->d_name.name, (int)pos,
                (int)pvfs2_inode->num_readdir_retries,
                llu(pvfs2_inode->directory_version));

    switch (pos)
    {
	/*
	   if we're just starting, populate the "." and ".." entries
	   of the current directory; these always appear
	 */
    case 0:
        if (pvfs2_inode->directory_version == 0)
        {
            ino = dentry->d_inode->i_ino;
            pvfs2_print("calling filldir of . with pos = %d\n", pos);
            if (filldir(dirent, ".", 1, pos, ino, DT_DIR) < 0)
            {
                break;
            }
        }
        file->f_pos++;
        pos++;
	/* drop through */
    case 1:
        if (pvfs2_inode->directory_version == 0)
        {
            ino = parent_ino(dentry);
            pvfs2_print("calling filldir of .. with pos = %d\n", pos);
            if (filldir(dirent, "..", 2, pos, ino, DT_DIR) < 0)
            {
                break;
            }
        }
        file->f_pos++;
        pos++;
	/* drop through */
    default:
	/* handle the normal cases here */
	new_op = op_alloc();
	if (!new_op)
	{
	    return -ENOMEM;
	}
	new_op->upcall.type = PVFS2_VFS_OP_READDIR;

	if (pvfs2_inode && pvfs2_inode->refn.handle &&
            pvfs2_inode->refn.fs_id)
	{
	    new_op->upcall.req.readdir.refn = pvfs2_inode->refn;
	}
	else
	{
	    new_op->upcall.req.readdir.refn.handle =
		pvfs2_ino_to_handle(dentry->d_inode->i_ino);
	    new_op->upcall.req.readdir.refn.fs_id =
		PVFS2_SB(dentry->d_inode->i_sb)->fs_id;
	}
	new_op->upcall.req.readdir.max_dirent_count = MAX_DIRENT_COUNT;

	/* NOTE:
	   the position we send to the readdir upcall is out of
	   sync with file->f_pos since pvfs2 doesn't include the
	   "." and ".." entries that we added above.
        */
	new_op->upcall.req.readdir.token =
            (pos == 2 ? PVFS_READDIR_START : pos);

        ret = service_operation(
            new_op, "pvfs2_readdir", PVFS2_OP_RETRY_COUNT,
            get_interruptible_flag(dentry->d_inode));

	pvfs2_print("Readdir downcall status is %d (dirent_count "
		    "is %d)\n", new_op->downcall.status,
		    new_op->downcall.resp.readdir.dirent_count);

	if (new_op->downcall.status == 0)
	{
	    int i = 0, len = 0;
	    ino_t current_ino = 0;
	    char *current_entry = NULL;

            if (new_op->downcall.resp.readdir.dirent_count == 0)
            {
                goto graceful_termination_path;
            }

            if (pvfs2_inode->directory_version == 0)
            {
                pvfs2_inode->directory_version =
                    new_op->downcall.resp.readdir.directory_version;
            }

            if (pvfs2_inode->num_readdir_retries > -1)
            {
                if (pvfs2_inode->directory_version !=
                    new_op->downcall.resp.readdir.directory_version)
                {
                    pvfs2_print("detected directory change on listing; "
                                "starting over\n");

                    file->f_pos = 0;
                    pvfs2_inode->directory_version =
                        new_op->downcall.resp.readdir.directory_version;

                    op_release(new_op);
                    pvfs2_inode->num_readdir_retries--;
                    goto restart_readdir;
                }
            }
            else
            {
                pvfs2_print("Giving up on readdir retries to avoid "
                            "possible livelock (%d tries attempted)\n",
                            PVFS2_NUM_READDIR_RETRIES);
            }

	    for (i = 0; i < new_op->downcall.resp.readdir.dirent_count; i++)
	    {
                len = new_op->downcall.resp.readdir.d_name_len[i];
                current_entry =
                    &new_op->downcall.resp.readdir.d_name[i][0];
                current_ino =
                    pvfs2_handle_to_ino(
                        new_op->downcall.resp.readdir.refn[i].handle);

                pvfs2_print("calling filldir for %s with len %d, pos %ld\n",
                        current_entry, len, (unsigned long) pos);
                if (filldir(dirent, current_entry, len, pos,
                            current_ino, DT_UNKNOWN) < 0)
                {
                  graceful_termination_path:

                    pvfs2_inode->directory_version = 0;
                    pvfs2_inode->num_readdir_retries =
                        PVFS2_NUM_READDIR_RETRIES;

                    ret = 0;
                    break;
                }
                file->f_pos++;
                pos++;
            }
            file->f_pos = new_op->downcall.resp.readdir.token;
            pvfs2_print("pos = %d, file->f_pos should have been %ld\n", pos, 
                    (unsigned long) file->f_pos);
	}
        else
        {
            pvfs2_print("Failed to readdir (downcall status %d)\n",
                        new_op->downcall.status);
        }

	op_release(new_op);
	break;
    }
    pvfs2_print("pvfs2_readdir about to update_atime %p\n", dentry->d_inode);

#ifdef HAVE_TOUCH_ATIME
    touch_atime(file->f_vfsmnt, dentry);
#else
    update_atime(dentry->d_inode);
#endif


    pvfs2_print("pvfs2_readdir returning %d\n",ret);
    return ret;
}

#ifdef HAVE_READDIRPLUS_FILE_OPERATIONS

/* More or less identical to copy_attributes_to_inode().
 * Special version needed since we dont really have a real inode structure
 * and we are faking one
 */
static void copy_sys_attributes(struct inode *inode, PVFS_sys_attr *attrs, char *symname)
{
    int old_mode = 0, perm_mode = 0;

    inode->i_blksize = pvfs_bufmap_size_query();
    inode->i_blkbits = PAGE_CACHE_SHIFT;
    if (attrs->objtype == PVFS_TYPE_METAFILE && (attrs->mask & PVFS_ATTR_SYS_SIZE))
    {
        loff_t inode_size = 0, rounded_up_size = 0;
        inode_size = (loff_t)attrs->size;
        rounded_up_size =
            (inode_size + (4096 - (inode_size % 4096)));
        inode->i_bytes = inode_size;
        inode->i_blocks = (unsigned long) (rounded_up_size/ 512);
        inode->i_size = inode_size;
    }
    else if (attrs->objtype == PVFS_TYPE_SYMLINK &&
            symname != NULL)
    {
        inode->i_size = (loff_t) strlen(symname);
    }
    else
    {
        inode->i_bytes = PAGE_CACHE_SIZE;
        inode->i_blocks = (unsigned long) (PAGE_CACHE_SIZE/512);
        inode->i_size = PAGE_CACHE_SIZE;
    }
    inode->i_uid = attrs->owner;
    inode->i_gid = attrs->group;
    inode->i_atime.tv_sec = (time_t)attrs->atime;
    inode->i_mtime.tv_sec = (time_t)attrs->mtime;
    inode->i_ctime.tv_sec = (time_t)attrs->ctime;
    inode->i_atime.tv_nsec = 0;
    inode->i_mtime.tv_nsec = 0;
    inode->i_ctime.tv_nsec = 0;
    old_mode = inode->i_mode;
    inode->i_mode = 0;

    if (attrs->perms & PVFS_O_EXECUTE)
        perm_mode |= S_IXOTH;
    if (attrs->perms & PVFS_O_WRITE)
        perm_mode |= S_IWOTH;
    if (attrs->perms & PVFS_O_READ)
        perm_mode |= S_IROTH;

    if (attrs->perms & PVFS_G_EXECUTE)
        perm_mode |= S_IXGRP;
    if (attrs->perms & PVFS_G_WRITE)
        perm_mode |= S_IWGRP;
    if (attrs->perms & PVFS_G_READ)
        perm_mode |= S_IRGRP;

    if (attrs->perms & PVFS_U_EXECUTE)
        perm_mode |= S_IXUSR;
    if (attrs->perms & PVFS_U_WRITE)
        perm_mode |= S_IWUSR;
    if (attrs->perms & PVFS_U_READ)
        perm_mode |= S_IRUSR;

    if (attrs->perms & PVFS_G_SGID)
        perm_mode |= S_ISGID;

    inode->i_mode |= perm_mode;
    switch (attrs->objtype)
    {
        case PVFS_TYPE_METAFILE:
            inode->i_mode |= S_IFREG;
            break;
        case PVFS_TYPE_DIRECTORY:
            inode->i_mode |= S_IFDIR;
            inode->i_nlink = 1;
            break;
        case PVFS_TYPE_SYMLINK:
            inode->i_mode |= S_IFLNK;
            break;
    }
    return;
}

/** Read directory entries from an instance of an open directory 
 *  and the associated attributes for every entry in one-shot.
 *
 * \param filldir callback function called for each entry read.
 *
 * \retval <0 on error
 * \retval 0  when directory has been completely traversed
 * \retval >0 if we don't call filldir for all entries
 *
 * \note If the filldir call-back returns non-zero, then readdir should
 *       assume that it has had enough, and should return as well.
 */
static int pvfs2_readdirplus(
    struct file *file,
    void *direntplus,
    filldirplus_t filldirplus)
{
    int ret = 0;
    PVFS_ds_position pos = 0;
    ino_t ino = 0;
    struct dentry *dentry = file->f_dentry;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

  restart_readdir:

    pos = (PVFS_ds_position)file->f_pos;
    /* are we done? */
    if (pos == PVFS_READDIR_END)
    {
        pvfs2_print("Skipping to graceful termination path since we are done\n");
        return 0;
    }

    pvfs2_print("pvfs2_readdirplus called on %s (pos=%d, "
                "retry=%d, v=%llu)\n", dentry->d_name.name, (int)pos,
                (int)pvfs2_inode->num_readdir_retries,
                llu(pvfs2_inode->directory_version));

    switch (pos)
    {
        struct kstat ks;
	/*
	   if we're just starting, populate the "." and ".." entries
	   of the current directory; these always appear
	 */
    case 0:
    {
        struct inode *inode = NULL;
        if (pvfs2_inode->directory_version == 0)
        {
            ino = dentry->d_inode->i_ino;
            inode = iget(dentry->d_inode->i_sb, ino);
            if (inode)
            {
                generic_fillattr(inode, &ks);
                pvfs2_print("calling filldirplus of . with pos = %d\n", pos);
                if (filldirplus(dirent, ".", 1, pos, ino, DT_DIR, &ks) < 0)
                {
                    break;
                }
                iput(inode);
            }
        }
        file->f_pos++;
        pos++;
	/* drop through */
    }
    case 1:
    {
        struct inode *inode = NULL;
        if (pvfs2_inode->directory_version == 0)
        {
            ino = parent_ino(dentry);
            inode = iget(dentry->d_inode->i_sb, ino);
            if (inode) 
            {
                generic_fillattr(inode, &ks);
                pvfs2_print("calling filldirplus of .. with pos = %d\n", pos);
                if (filldirplus(dirent, "..", 2, pos, ino, DT_DIR, &ks) < 0)
                {
                    break;
                }
                iput(inode);
            }
        }
        file->f_pos++;
        pos++;
	/* drop through */
    }
    default:
	/* handle the normal cases here */
	new_op = op_alloc();
	if (!new_op)
	{
	    return -ENOMEM;
	}
	new_op->upcall.type = PVFS2_VFS_OP_READDIRPLUS;

	if (pvfs2_inode && pvfs2_inode->refn.handle &&
            pvfs2_inode->refn.fs_id)
	{
	    new_op->upcall.req.readdirplus.refn = pvfs2_inode->refn;
	}
	else
	{
	    new_op->upcall.req.readdirplus.refn.handle =
		pvfs2_ino_to_handle(dentry->d_inode->i_ino);
	    new_op->upcall.req.readdirplus.refn.fs_id =
		PVFS2_SB(dentry->d_inode->i_sb)->fs_id;
	}
        new_op->upcall.req.readdirplus.mask = PVFS_ATTR_SYS_ALL;
	new_op->upcall.req.readdirplus.max_dirent_count = MAX_DIRENT_COUNT;

	/* NOTE:
	   the position we send to the readdirplus upcall is out of
	   sync with file->f_pos since pvfs2 doesn't include the
	   "." and ".." entries that we added above.
        */
	new_op->upcall.req.readdirplus.token =
            (pos == 2 ? PVFS_READDIR_START : pos);

        ret = service_operation(
            new_op, "pvfs2_readdirplus", PVFS2_OP_RETRY_COUNT,
            get_interruptible_flag(dentry->d_inode));

	pvfs2_print("Readdirplus downcall status is %d (dirent_count "
		    "is %d)\n", new_op->downcall.status,
		    new_op->downcall.resp.readdirplus.dirent_count);

	if (new_op->downcall.status == 0)
	{
	    int i = 0, len = 0;
	    ino_t current_ino = 0;
	    char *current_entry = NULL;

            if (new_op->downcall.resp.readdirplus.dirent_count == 0)
            {
                goto graceful_termination_path;
            }

            if (pvfs2_inode->directory_version == 0)
            {
                pvfs2_inode->directory_version =
                    new_op->downcall.resp.readdirplus.directory_version;
            }

            if (pvfs2_inode->num_readdir_retries > -1)
            {
                if (pvfs2_inode->directory_version !=
                    new_op->downcall.resp.readdirplus.directory_version)
                {
                    pvfs2_print("detected directory change on listing; "
                                "starting over\n");

                    file->f_pos = 0;
                    pvfs2_inode->directory_version =
                        new_op->downcall.resp.readdirplus.directory_version;

                    op_release(new_op);
                    pvfs2_inode->num_readdir_retries--;
                    goto restart_readdir;
                }
            }
            else
            {
                pvfs2_print("Giving up on readdir retries to avoid "
                            "possible livelock (%d tries attempted)\n",
                            PVFS2_NUM_READDIR_RETRIES);
            }

	    for (i = 0; i < new_op->downcall.resp.readdirplus.dirent_count; i++)
	    {
                struct inode dummy_inode;
                int dt_type;
                void *ptr = NULL;

                len = new_op->downcall.resp.readdirplus.d_name_len[i];
                current_entry =
                    &new_op->downcall.resp.readdirplus.d_name[i][0];
                current_ino =
                    pvfs2_handle_to_ino(new_op->downcall.resp.readdirplus.refn[i].handle);

                pvfs2_print("calling filldirplus for %s with len %d, pos %ld\n",
                        current_entry, len, (unsigned long) pos);
                if (new_op->downcall.resp.readdirplus.stat_error[i] == 0)
                {
                    /* FIXME: Symlinks won't work right since it is not clear how to pass them through the upcall  */
                    copy_sys_attributes(&dummy_inode, &new_op->downcall.resp.readdirplus.attr_array[i], NULL);
                    generic_fillattr(&dummy_inode, &ks);
                    ptr = &ks;
                }
                else {
                    int err_num = pvfs2_normalize_to_errno(new_op->downcall.resp.readdirplus.stat_error[i]);
                    ptr = ERR_PTR(err_num);
                }
                if (new_op->downcall.resp.readdirplus.attr_array[i].objtype == PVFS_TYPE_METAFILE) {
                    dt_type = DT_REG;
                }
                else if (new_op->downcall.resp.readdirplus.attr_array[i].objtype == PVFS_TYPE_DIRECTORY) {
                    dt_type = DT_DIR;
                }
                else if (new_op->downcall.resp.readdirplus.attr_array[i].objtype == PVFS_TYPE_SYMLINK) {
                    dt_type = DT_LNK;
                }
                else {
                    dt_type = DT_UNKNOWN;
                }
                if (filldirplus(dirent, current_entry, len, pos,
                            current_ino, DT_UNKNOWN, ptr) < 0)
                {
                  graceful_termination_path:

                    pvfs2_inode->directory_version = 0;
                    pvfs2_inode->num_readdir_retries =
                        PVFS2_NUM_READDIR_RETRIES;

                    ret = 0;
                    break;
                }
                file->f_pos++;
                pos++;
            }
            file->f_pos = new_op->downcall.resp.readdirplus.token;
            pvfs2_print("pos = %d, file->f_pos should have been %ld\n", pos, 
                    (unsigned long) file->f_pos);
	}
        else
        {
            pvfs2_print("Failed to readdirplus (downcall status %d)\n",
                        new_op->downcall.status);
        }

	op_release(new_op);
	break;
    }
    pvfs2_print("pvfs2_readdirplus about to update_atime %p\n", dentry->d_inode);

#ifdef HAVE_TOUCH_ATIME
    touch_atime(file->f_vfsmnt, dentry);
#else
    update_atime(dentry->d_inode);
#endif


    pvfs2_print("pvfs2_readdirplus returning %d\n",ret);
    return ret;
}
#endif

/** PVFS2 implementation of VFS directory operations */
struct file_operations pvfs2_dir_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    read : generic_read_dir,
    readdir : pvfs2_readdir,
    open : pvfs2_file_open,
    release : pvfs2_file_release
#else
    .read = generic_read_dir,
    .readdir = pvfs2_readdir,
#ifdef HAVE_READDIRPLUS_FILE_OPERATIONS
    .readdirplus = pvfs2_readdirplus,
#endif
    .open = pvfs2_file_open,
    .release = pvfs2_file_release
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

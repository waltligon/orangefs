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
#include "pvfs2-bufmap.h"
#include "pvfs2-sysint.h"
#include "pvfs2-internal.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern int debug;
extern struct address_space_operations pvfs2_address_operations;
extern struct backing_dev_info pvfs2_backing_dev_info;

/* shared file/dir operations defined in file.c */
extern int pvfs2_file_open(
    struct inode *inode,
    struct file *file);
extern int pvfs2_file_release(
    struct inode *inode,
    struct file *file);

typedef struct 
{
    int buffer_index;
    pvfs2_readdir_response_t readdir_response;
    void *dents_buf;
} readdir_handle_t;

/* decode routine needed by kmod to make sense of the shared page for readdirs */
static long decode_dirents(char *ptr, pvfs2_readdir_response_t *readdir) 
{
    int i;
    pvfs2_readdir_response_t *rd = (pvfs2_readdir_response_t *) ptr;
    char *buf = ptr;
    char **pptr = &buf;

    readdir->token = rd->token;
    readdir->directory_version = rd->directory_version;
    readdir->pvfs_dirent_outcount = rd->pvfs_dirent_outcount;
    readdir->dirent_array = (struct pvfs2_dirent *) 
            kmalloc(readdir->pvfs_dirent_outcount * sizeof(struct pvfs2_dirent), GFP_KERNEL);
    if (readdir->dirent_array == NULL)
    {
        return -ENOMEM;
    }
    *pptr += offsetof(pvfs2_readdir_response_t, dirent_array);
    for (i = 0; i < readdir->pvfs_dirent_outcount; i++)
    {
        dec_string(pptr, &readdir->dirent_array[i].d_name, &readdir->dirent_array[i].d_length);
        readdir->dirent_array[i].handle = *(int64_t *) *pptr;
        *pptr += 8;
    }
    return ((unsigned long) *pptr - (unsigned long) ptr);
}

static long readdir_handle_ctor(readdir_handle_t *rhandle, void *buf, int buffer_index)
{
    long ret;

    if (buf == NULL)
    {
        pvfs2_error("Invalid NULL buffer specified in readdir_handle_ctor\n");
        return -ENOMEM;
    }
    if (buffer_index < 0)
    {
        pvfs2_error("Invalid buffer index specified in readdir_handle_ctor\n");
        return -EINVAL;
    }
    rhandle->buffer_index = buffer_index;
    rhandle->dents_buf = buf;
    if ((ret = decode_dirents(buf, &rhandle->readdir_response)) < 0)
    {
        pvfs2_error("Could not decode readdir from buffer %ld\n", ret);
        readdir_index_put(rhandle->buffer_index);
        rhandle->buffer_index = -1;
        pvfs2_print("vfree %p\n", buf);
        vfree(buf);
        rhandle->dents_buf = NULL;
    }
    return ret;
}

static void readdir_handle_dtor(readdir_handle_t *rhandle)
{
    if (rhandle == NULL)
    {
        return;
    }
    if (rhandle->readdir_response.dirent_array)
    {
        kfree(rhandle->readdir_response.dirent_array);
        rhandle->readdir_response.dirent_array = NULL;
    }
    if (rhandle->buffer_index >= 0)
    {
        readdir_index_put(rhandle->buffer_index);
        rhandle->buffer_index = -1;
    }
    if (rhandle->dents_buf)
    {
        pvfs2_print("vfree %p\n", rhandle->dents_buf);
        vfree(rhandle->dents_buf);
        rhandle->dents_buf = NULL;
    }
    return;
}

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
    int ret = 0, buffer_index, token_set = 0;
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
            token_set = 1;
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
            token_set = 1;
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
        {
            readdir_handle_t rhandle;

            rhandle.buffer_index = -1;
            rhandle.dents_buf = NULL;
            memset(&rhandle.readdir_response, 0, sizeof(rhandle.readdir_response));

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

            ret = readdir_index_get(&buffer_index);
            if (ret < 0)
            {
                pvfs2_error("pvfs2_readdir: readdir_index_get() failure (%d)\n", ret);
                goto err;
            }
            new_op->upcall.req.readdir.buf_index = buffer_index;

            ret = service_operation(
                new_op, "pvfs2_readdir", PVFS2_OP_RETRY_COUNT,
                get_interruptible_flag(dentry->d_inode));

            pvfs2_print("Readdir downcall status is %d\n", new_op->downcall.status);

            if (new_op->downcall.status == 0)
            {
                int i = 0, len = 0;
                ino_t current_ino = 0;
                char *current_entry = NULL;
                long bytes_decoded;

                if ((bytes_decoded = readdir_handle_ctor(&rhandle, new_op->downcall.trailer_buf,
                                buffer_index)) < 0)
                {
                    ret = bytes_decoded;
                    pvfs2_error("pvfs2_readdir: Could not decode trailer buffer "
                            " into a readdir response %d\n", ret);
                    goto err;
                }
                if (bytes_decoded != new_op->downcall.trailer_size)
                {
                    pvfs2_error("pvfs2_readdir: # bytes decoded (%ld) != trailer size (%ld)\n",
                            bytes_decoded, (long) new_op->downcall.trailer_size);
                    ret = -EINVAL;
                    goto err;
                }
                if (rhandle.readdir_response.pvfs_dirent_outcount == 0)
                {
                    goto graceful_termination_path;
                }

                if (pvfs2_inode->directory_version == 0)
                {
                    pvfs2_inode->directory_version =
                        rhandle.readdir_response.directory_version;
                }

                if (pvfs2_inode->num_readdir_retries > -1)
                {
                    if (pvfs2_inode->directory_version !=
                        rhandle.readdir_response.directory_version)
                    {
                        pvfs2_print("detected directory change on listing; "
                                    "starting over\n");

                        file->f_pos = 0;
                        pvfs2_inode->directory_version =
                            rhandle.readdir_response.directory_version;

                        readdir_handle_dtor(&rhandle);
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

                for (i = 0; i < rhandle.readdir_response.pvfs_dirent_outcount; i++)
                {
                    len = rhandle.readdir_response.dirent_array[i].d_length;
                    current_entry = rhandle.readdir_response.dirent_array[i].d_name;
                    current_ino = pvfs2_handle_to_ino(
                            rhandle.readdir_response.dirent_array[i].handle);

                    pvfs2_print("calling filldir for %s with len %d, pos %ld\n",
                            current_entry, len, (unsigned long) pos);
                    if (filldir(dirent, current_entry, len, pos,
                                current_ino, DT_UNKNOWN) < 0)
                    {
graceful_termination_path:
                        pvfs2_inode->directory_version = 0;
                        pvfs2_inode->num_readdir_retries = PVFS2_NUM_READDIR_RETRIES;

                        ret = 0;
                        break;
                    }
                    file->f_pos++;
                    pos++;
                }
                /* For the first time around, use the token returned by the readdir response */
                if (token_set == 1) {
                    file->f_pos = rhandle.readdir_response.token;
                }
                pvfs2_print("pos = %d, file->f_pos is %ld\n", pos, 
                        (unsigned long) file->f_pos);
            }
            else
            {
                readdir_index_put(buffer_index);
                pvfs2_print("Failed to readdir (downcall status %d)\n",
                            new_op->downcall.status);
            }
err:
            readdir_handle_dtor(&rhandle);
            op_release(new_op);
            break;
        }
    }
    if (ret == 0)
    {
        pvfs2_print("pvfs2_readdir about to update_atime %p\n", dentry->d_inode);
#ifdef HAVE_TOUCH_ATIME
        touch_atime(file->f_vfsmnt, dentry);
#else
        update_atime(dentry->d_inode);
#endif
    }

    pvfs2_print("pvfs2_readdir returning %d\n",ret);
    return ret;
}

#ifdef HAVE_READDIRPLUS_FILE_OPERATIONS

typedef struct 
{
    int buffer_index;
    pvfs2_readdirplus_response_t readdirplus_response;
    void *dentsplus_buf;
} readdirplus_handle_t;

static long decode_sys_attr(char *ptr, pvfs2_readdirplus_response_t *readdirplus) 
{
    int i;
    char *buf = (char *) ptr;
    char **pptr = &buf;

    readdirplus->stat_err_array = (PVFS_error *) 
        kmalloc(readdirplus->pvfs_dirent_outcount * sizeof(PVFS_error), GFP_KERNEL);
    if (readdirplus->stat_err_array == NULL)
    {
        return -ENOMEM;
    }
    memcpy(readdirplus->stat_err_array, buf, readdirplus->pvfs_dirent_outcount * sizeof(PVFS_error));
    *pptr += readdirplus->pvfs_dirent_outcount * sizeof(PVFS_error);
    if (readdirplus->pvfs_dirent_outcount % 2) 
    {
        *pptr += 4;
    }
    readdirplus->attr_array = (PVFS_sys_attr *) 
        kmalloc(readdirplus->pvfs_dirent_outcount * sizeof(PVFS_sys_attr), GFP_KERNEL);
    if (readdirplus->attr_array == NULL)
    {
        return -ENOMEM;
    }
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++)
    {
        memcpy(&readdirplus->attr_array[i], *pptr, sizeof(PVFS_sys_attr));
        *pptr += sizeof(PVFS_sys_attr);
        if (readdirplus->attr_array[i].objtype == PVFS_TYPE_SYMLINK &&
                (readdirplus->attr_array[i].mask & PVFS_ATTR_SYS_LNK_TARGET))
        {
            int *plen = NULL;
            dec_string(pptr, &readdirplus->attr_array[i].link_target, plen);
        }
        else {
            readdirplus->attr_array[i].link_target = NULL;
        }
    }
    return ((unsigned long) *pptr - (unsigned long) ptr);
}

static long decode_readdirplus_from_buffer(char *ptr, pvfs2_readdirplus_response_t *readdirplus)
{
    char *buf = ptr;
    long amt;

    amt = decode_dirents(buf, (pvfs2_readdir_response_t *) readdirplus);
    if (amt < 0)
        return amt;
    buf += amt;
    amt = decode_sys_attr(buf, readdirplus);
    if (amt < 0)
        return amt;
    buf += amt;
    return ((unsigned long) buf - (unsigned long) ptr);
}

static long readdirplus_handle_ctor(readdirplus_handle_t *rhandle, void *buf, int buffer_index)
{
    long ret;

    if (buf == NULL)
    {
        pvfs2_error("Invalid NULL buffer specified in readdirplus_handle_ctor\n");
        return -ENOMEM;
    }
    if (buffer_index < 0)
    {
        pvfs2_error("Invalid buffer index specified in readdirplus_handle_ctor\n");
        return -EINVAL;
    }
    rhandle->buffer_index = buffer_index;
    rhandle->dentsplus_buf = buf;
    if ((ret = decode_readdirplus_from_buffer(buf, &rhandle->readdirplus_response)) < 0)
    {
        pvfs2_error("Could not decode readdirplus from buffer %ld\n", ret);
        readdir_index_put(rhandle->buffer_index);
        rhandle->buffer_index = -1;
        pvfs2_print("vfree %p\n", buf);
        vfree(buf);
        rhandle->dentsplus_buf = NULL;
    }
    return ret;
}

static void readdirplus_handle_dtor(readdirplus_handle_t *rhandle)
{
    if (rhandle == NULL)
    {
        return;
    }
    if (rhandle->readdirplus_response.dirent_array)
    {
        kfree(rhandle->readdirplus_response.dirent_array);
        rhandle->readdirplus_response.dirent_array = NULL;
    }
    if (rhandle->readdirplus_response.attr_array)
    {
        kfree(rhandle->readdirplus_response.attr_array);
        rhandle->readdirplus_response.attr_array = NULL;
    }
    if (rhandle->readdirplus_response.stat_err_array)
    {
        kfree(rhandle->readdirplus_response.stat_err_array);
        rhandle->readdirplus_response.stat_err_array = NULL;
    }
    if (rhandle->buffer_index >= 0)
    {
        readdir_index_put(rhandle->buffer_index);
        rhandle->buffer_index = -1;
    }
    if (rhandle->dentsplus_buf)
    {
        pvfs2_print("vfree %p\n", rhandle->dentsplus_buf);
        vfree(rhandle->dentsplus_buf);
        rhandle->dentsplus_buf = NULL;
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
 * \note If the filldirplus call-back returns non-zero, then readdirplus should
 *       assume that it has had enough, and should return as well.
 */
static int pvfs2_readdirplus(
    struct file *file,
    void *direntplus,
    filldirplus_t filldirplus)
{
    int ret = 0, buffer_index, token_set = 0;
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
            token_set = 1;
            if (pvfs2_inode->directory_version == 0)
            {
                ino = dentry->d_inode->i_ino;
                inode = iget(dentry->d_inode->i_sb, ino);
                if (inode)
                {
                    generic_fillattr(inode, &ks);
                    iput(inode);
                    pvfs2_print("calling filldirplus of . with pos = %d\n", pos);
                    if (filldirplus(direntplus, ".", 1, pos, ino, DT_DIR, &ks) < 0)
                    {
                        break;
                    }
                }
            }
            file->f_pos++;
            pos++;
            /* drop through */
        }
        case 1:
        {
            struct inode *inode = NULL;
            token_set = 1;
            if (pvfs2_inode->directory_version == 0)
            {
                ino = parent_ino(dentry);
                inode = iget(dentry->d_inode->i_sb, ino);
                if (inode) 
                {
                    generic_fillattr(inode, &ks);
                    iput(inode);
                    pvfs2_print("calling filldirplus of .. with pos = %d\n", pos);
                    if (filldirplus(direntplus, "..", 2, pos, ino, DT_DIR, &ks) < 0)
                    {
                        break;
                    }
                }
            }
            file->f_pos++;
            pos++;
            /* drop through */
        }
        default:
        {
            readdirplus_handle_t rhandle;

            rhandle.buffer_index = -1;
            rhandle.dentsplus_buf = NULL;
            memset(&rhandle.readdirplus_response, 0, sizeof(rhandle.readdirplus_response));

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

            ret = readdir_index_get(&buffer_index);
            if (ret < 0)
            {
                pvfs2_error("pvfs2_readdirplus: readdir_index_get() failure (%d)\n", ret);
                goto err;
            }
            new_op->upcall.req.readdirplus.buf_index = buffer_index;

            ret = service_operation(
                new_op, "pvfs2_readdirplus", PVFS2_OP_RETRY_COUNT,
                get_interruptible_flag(dentry->d_inode));

            pvfs2_print("Readdirplus downcall status is %d\n",
                    new_op->downcall.status);

            if (new_op->downcall.status == 0)
            {
                int i = 0, len = 0;
                ino_t current_ino = 0;
                char *current_entry = NULL;
                long bytes_decoded;

                ret = 0;
                if ((bytes_decoded = readdirplus_handle_ctor(&rhandle,
                                new_op->downcall.trailer_buf, buffer_index)) < 0)
                {
                    ret = bytes_decoded;
                    pvfs2_error("pvfs2_readdirplus: Could not decode trailer buffer "
                            " into a readdirplus response %d\n", ret);
                    goto err;
                }
                if (bytes_decoded != new_op->downcall.trailer_size)
                {
                    pvfs2_error("pvfs2_readdirplus: # bytes decoded (%ld) != trailer size (%ld)\n",
                            bytes_decoded, (long) new_op->downcall.trailer_size);
                    ret = -EINVAL;
                    goto err;
                }

                if (rhandle.readdirplus_response.pvfs_dirent_outcount == 0)
                {
                    goto graceful_termination_path;
                }

                if (pvfs2_inode->directory_version == 0)
                {
                    pvfs2_inode->directory_version =
                        rhandle.readdirplus_response.directory_version;
                }

                if (pvfs2_inode->num_readdir_retries > -1)
                {
                    if (pvfs2_inode->directory_version !=
                        rhandle.readdirplus_response.directory_version)
                    {
                        pvfs2_print("detected directory change on listing; "
                                    "starting over\n");

                        file->f_pos = 0;
                        pvfs2_inode->directory_version =
                            rhandle.readdirplus_response.directory_version;

                        readdirplus_handle_dtor(&rhandle);
                        op_release(new_op);
                        pvfs2_inode->num_readdir_retries--;
                        goto restart_readdir;
                    }
                }
                else
                {
                    pvfs2_print("Giving up on readdirplus retries to avoid "
                                "possible livelock (%d tries attempted)\n",
                                PVFS2_NUM_READDIR_RETRIES);
                }

                for (i = 0; i < rhandle.readdirplus_response.pvfs_dirent_outcount; i++)
                {
                    struct inode *filled_inode = NULL;
                    int dt_type, stat_error;
                    void *ptr = NULL;
                    PVFS_sys_attr *attrs = NULL;
                    PVFS_handle handle;
                    PVFS_fs_id fs_id;

                    len = rhandle.readdirplus_response.dirent_array[i].d_length;
                    current_entry = rhandle.readdirplus_response.dirent_array[i].d_name;
                    handle = rhandle.readdirplus_response.dirent_array[i].handle;
                    current_ino = pvfs2_handle_to_ino(handle);
                    stat_error = rhandle.readdirplus_response.stat_err_array[i];
                    fs_id  = new_op->upcall.req.readdirplus.refn.fs_id;

                    if (stat_error == 0)
                    {
                        /* locate inode in the icache, but don't getattr() */
                        filled_inode = iget_locked(dentry->d_inode->i_sb, current_ino);
                        if (filled_inode == NULL) {
                            pvfs2_error("Could not allocate inode\n");
                            ret = -ENOMEM;
                            goto err;
                        }
                        else if (is_bad_inode(filled_inode)) {
                            iput(filled_inode);
                            pvfs2_error("bad inode obtained from iget_locked\n");
                            ret = -EINVAL;
                            goto err;
                        }
                        else {
                            attrs = &rhandle.readdirplus_response.attr_array[i];
                            if ((ret = copy_attributes_to_inode(filled_inode, attrs, attrs->link_target)) < 0) {
                                pvfs2_error("copy attributes to inode failed with err %d\n", ret);
                                iput(filled_inode);
                                goto err;
                            }
                            generic_fillattr(filled_inode, &ks);
                            if (filled_inode->i_state & I_NEW) {
                                pvfs2_inode_t *filled_pvfs2_inode = PVFS2_I(filled_inode);
                                pvfs2_inode_initialize(filled_pvfs2_inode);
                                filled_pvfs2_inode->refn.handle = handle;
                                filled_pvfs2_inode->refn.fs_id  = fs_id;
                                filled_inode->i_mapping->host = filled_inode;
                                filled_inode->i_rdev = 0;
                                filled_inode->i_bdev = NULL;
                                filled_inode->i_cdev = NULL;
                                filled_inode->i_mapping->a_ops = &pvfs2_address_operations;
#ifndef PVFS2_LINUX_KERNEL_2_4
                                filled_inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
#endif
                                /* Make sure that we unlock the inode */
                                unlock_new_inode(filled_inode);
                            }
                            iput(filled_inode);
                        }
                        ptr = &ks;
                        if (attrs->objtype == PVFS_TYPE_METAFILE) 
                        {
                            dt_type = DT_REG;
                        }
                        else if (attrs->objtype == PVFS_TYPE_DIRECTORY) 
                        {
                            dt_type = DT_DIR;
                        }
                        else if (attrs->objtype == PVFS_TYPE_SYMLINK) 
                        {
                            dt_type = DT_LNK;
                        }
                        else 
                        {
                            dt_type = DT_UNKNOWN;
                        }
                    }
                    else {
                        int err_num = pvfs2_normalize_to_errno(stat_error);
                        ptr = ERR_PTR(err_num);
                        dt_type = DT_UNKNOWN;
                    }
                    pvfs2_print("calling filldirplus for %s "
                            " (%lu) with len %d, pos %ld kstat %p\n", 
                            current_entry, (unsigned long) handle,
                            len, (unsigned long) pos, ptr);
                    
                    /* filldirplus has had enough */
                    if (filldirplus(direntplus, current_entry, len, pos,
                                current_ino, dt_type, ptr) < 0)
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
                /* For the first time around, use the token returned by the readdirplus response */
                if (token_set == 1) {
                    file->f_pos = rhandle.readdirplus_response.token;
                }
                pvfs2_print("pos = %d, file->f_pos is %ld\n", pos, 
                        (unsigned long) file->f_pos);
            }
            else
            {
                readdir_index_put(buffer_index);
                pvfs2_print("Failed to readdirplus (downcall status %d)\n",
                            new_op->downcall.status);
            }
err:
            readdirplus_handle_dtor(&rhandle);
            op_release(new_op);
            break;
        }
    }
    if (ret == 0)
    {
        pvfs2_print("pvfs2_readdirplus about to update_atime %p\n", dentry->d_inode);
#ifdef HAVE_TOUCH_ATIME
        touch_atime(file->f_vfsmnt, dentry);
#else
        update_atime(dentry->d_inode);
#endif
    }
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

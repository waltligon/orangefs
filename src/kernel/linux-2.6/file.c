/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;

extern struct address_space_operations pvfs2_address_operations;
extern struct backing_dev_info pvfs2_backing_dev_info;


#define wake_up_device_for_return(op)             \
do {                                              \
  spin_lock(&op->lock);                           \
  op->io_completed = 1;                           \
  spin_unlock(&op->lock);                         \
  wake_up_interruptible(&op->io_completion_waitq);\
} while(0)


int pvfs2_file_open(
    struct inode *inode,
    struct file *file)
{
    int ret = -EINVAL;

    pvfs2_print("pvfs2_file_open: called on %s (inode is %d)\n",
                file->f_dentry->d_name.name, (int)inode->i_ino);

    inode->i_mapping->host = inode;
    inode->i_mapping->a_ops = &pvfs2_address_operations;
#ifndef PVFS2_LINUX_KERNEL_2_4
    inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
#endif

    if (S_ISDIR(inode->i_mode))
    {
        ret = dcache_dir_open(inode, file);
    }
    else
    {
        /*
          if the file's being opened for append mode, set the file pos
          to the end of the file when we retrieve the size (which we
          must forcefully do here in this case, afaict atm)
        */
        if (file->f_flags & O_APPEND)
        {
            ret = pvfs2_inode_getattr(inode);
            if (ret == 0)
            {
                file->f_pos = inode->i_size;
            }
        }

        /*
          fs/open.c: returns 0 after enforcing large file support if
          running on a 32 bit system w/o O_LARGFILE flag
        */
        ret = generic_file_open(inode, file);
    }

    pvfs2_print("pvfs2_file_open returning normally: %d\n", ret);
    return ret;
}

ssize_t pvfs2_inode_read(
    struct inode *inode,
    char *buf,
    size_t count,
    loff_t *offset,
    int copy_to_user,
    int readahead_size)
{
    int ret = -1, error_exit = 0;
    size_t each_count = 0, amt_complete = 0;
    size_t total_count = 0;
    pvfs2_kernel_op_t *new_op = NULL;
    int buffer_index = -1;
    char *current_buf = buf;
    loff_t original_offset = *offset;
    int retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    while(total_count < count)
    {
        new_op = op_alloc();
        if (!new_op)
        {
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.readahead_size = readahead_size;
        new_op->upcall.req.io.io_type = PVFS_IO_READ;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

        ret = pvfs_bufmap_get(&buffer_index);
        if (ret < 0)
        {
            pvfs2_error("pvfs2_inode_read: pvfs_bufmap_get() "
                        "failure (%d)\n", ret);
            op_release(new_op);
            *offset = original_offset;
            return ret;
        }

        /* how much to transfer in this loop iteration */
        each_count = (((count - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count - total_count));

        new_op->upcall.req.io.buf_index = buffer_index;
        new_op->upcall.req.io.count = each_count;
        new_op->upcall.req.io.offset = *offset;

        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_inode_read", retries, error_exit,
            get_interruptible_flag(inode));

        if (new_op->downcall.status != 0)
        {
            pvfs2_error("pvfs2_inode_read: error: io downcall status.\n");

          error_exit:
            /* this macro is defined in pvfs2-kernel.h */
            handle_io_error();

            /*
              don't write an error to syslog on signaled operation
              termination unless we've got debugging turned on, as
              this can happen regularly (i.e. ctrl-c)
            */
            if ((error_exit == 1) && (ret == -EINTR))
            {
                pvfs2_print("pvfs2_inode_read: returning error %d "
                            "(error_exit=%d)\n", ret, error_exit);
            }
            else
            {
                pvfs2_error("pvfs2_inode_read: returning error %d "
                            "(error_exit=%d)\n", ret, error_exit);
            }
            return ret;
        }

        /* copy data out to destination */
        if (new_op->downcall.resp.io.amt_complete)
        {
            if (copy_to_user)
            {
                ret = pvfs_bufmap_copy_to_user(
                    current_buf, buffer_index,
                    new_op->downcall.resp.io.amt_complete);
            }
            else
            {
                ret = pvfs_bufmap_copy_to_kernel(
                    current_buf, buffer_index,
                    new_op->downcall.resp.io.amt_complete);
            }

            if (ret)
            {
                pvfs2_error("Failed to copy user buffer.  Please make "
                            "sure that the pvfs2-client is running.\n");
                goto error_exit;
            }
        }

        current_buf += new_op->downcall.resp.io.amt_complete;
        *offset += new_op->downcall.resp.io.amt_complete;
        total_count += new_op->downcall.resp.io.amt_complete;
        amt_complete = new_op->downcall.resp.io.amt_complete;

        /*
          tell the device file owner waiting on I/O that this read has
          completed and it can return now.  in this exact case, on
          wakeup the device will free the op, so we *cannot* touch it
          after this.
        */
        wake_up_device_for_return(new_op);
        pvfs_bufmap_put(buffer_index);

        /* if we got a short read, fall out and return what we
         * got so far
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }

    /*
      NOTE: for this special case, op is freed by devreq_writev and
      *not* here.
    */
    return(total_count); 
}

ssize_t pvfs2_file_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t *offset)
{
    pvfs2_print("pvfs2_file_read: called on %s\n",
                (file && file->f_dentry && file->f_dentry->d_name.name ?
                 (char *)file->f_dentry->d_name.name : "UNKNOWN"));

    return pvfs2_inode_read(
        file->f_dentry->d_inode, buf, count, offset, 1, 0);
}

static ssize_t pvfs2_file_write(
    struct file *file,
    const char *buf,
    size_t count,
    loff_t *offset)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    char *current_buf = (char*)buf;
    loff_t original_offset = *offset;
    int buffer_index = -1, error_exit = 0;
    size_t each_count = 0, total_count = 0;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    size_t amt_complete = 0;

    pvfs2_print("pvfs2_file_write: called on %s\n",
                (file && file->f_dentry && file->f_dentry->d_name.name ?
                 (char *)file->f_dentry->d_name.name : "UNKNOWN"));

    while(total_count < count)
    {
        new_op = op_alloc();
        if (!new_op)
        {
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.io_type = PVFS_IO_WRITE;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

        pvfs2_print("pvfs2_file_write: writing %d bytes.\n", count);

        ret = pvfs_bufmap_get(&buffer_index);
        if (ret < 0)
        {
            pvfs2_error("pvfs2_file_write: pvfs_bufmap_get() "
                        "failure (%d)\n", ret);
            op_release(new_op);
            *offset = original_offset;
            return ret;
        }

        /* how much to transfer in this loop iteration */
        each_count = (((count - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count - total_count));

        new_op->upcall.req.io.buf_index = buffer_index;
        new_op->upcall.req.io.count = each_count;
        new_op->upcall.req.io.offset = *offset;

        /* copy data from application */
        if (pvfs_bufmap_copy_from_user(
                buffer_index, current_buf, each_count))
        {
            pvfs2_error("Failed to copy user buffer.  Please make sure "
                        "that the pvfs2-client is running.\n");
            op_release(new_op);
            pvfs_bufmap_put(buffer_index);
            *offset = original_offset;
            return -EIO;
        }

        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_file_write", retries, error_exit,
            get_interruptible_flag(inode));

        if (new_op->downcall.status != 0)
        {
            pvfs2_error("pvfs2_file_write: io downcall status error\n");

          error_exit:
            /* this macro is defined in pvfs2-kernel.h */
            handle_io_error();

            /*
              don't write an error to syslog on signaled operation
              termination unless we've got debugging turned on, as
              this can happen regularly (i.e. ctrl-c)
            */
            if ((error_exit == 1) && (ret == -EINTR))
            {
                pvfs2_print("pvfs2_file_write: returning error %d "
                            "(error_exit=%d)\n", ret, error_exit);
            }
            else
            {
                pvfs2_error("pvfs2_file_write: returning error %d "
                            "(error_exit=%d)\n", ret, error_exit);
            }
            return ret;
        }

        current_buf += new_op->downcall.resp.io.amt_complete;
        *offset += new_op->downcall.resp.io.amt_complete;
        total_count += new_op->downcall.resp.io.amt_complete;
        amt_complete = new_op->downcall.resp.io.amt_complete;

        /* adjust inode size if applicable */
        if ((original_offset + amt_complete) > inode->i_size)
        {
            i_size_write(inode, (original_offset + amt_complete));
        }

        /*
          tell the device file owner waiting on I/O that this read has
          completed and it can return now.  in this exact case, on
          wakeup the device will free the op, so we *cannot* touch it
          after this.
        */
        wake_up_device_for_return(new_op);
        pvfs_bufmap_put(buffer_index);

        /* if we got a short write, fall out and return what we got so
         * far TODO: define semantics here- kind of depends on pvfs2
         * semantics that don't really exist yet
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }

    if (total_count)
    {
        update_atime(inode);
    }
    return(total_count);
}

int pvfs2_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int cmd,
    unsigned long arg)
{
    int ret = -ENOTTY;

    pvfs2_print("pvfs2_ioctl: called with cmd %d\n", cmd);
    return ret;
}

static int pvfs2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct inode *inode = file->f_dentry->d_inode;

    pvfs2_print("pvfs2_file_mmap: called on %s\n",
                (file ? (char *)file->f_dentry->d_name.name :
                 (char *)"Unknown"));
    /*
      for mmap on pvfs2, make sure we use pvfs2 specific address
      operations by explcitly setting the operations
    */
    inode->i_mapping->host = inode;
    inode->i_mapping->a_ops = &pvfs2_address_operations;

    /* have the kernel enforce readonly mmap support for us */
#ifdef PVFS2_LINUX_KERNEL_2_4
    vma->vm_flags &= ~VM_MAYWRITE;
    return generic_file_mmap(file, vma);
#else
    /* backing_dev_info isn't present on 2.4.x */
    inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
    return generic_file_readonly_mmap(file, vma);
#endif
}

/*
  NOTE: gets called when all files are closed.  not when
  each file is closed. (i.e. last reference to an opened file)
*/
int pvfs2_file_release(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2_file_release: called on %s\n",
                file->f_dentry->d_name.name);

    update_atime(inode);
    if (S_ISDIR(inode->i_mode))
    {
        return dcache_dir_close(inode, file);
    }

    /*
      remove all associated inode pages from the page cache and mmap
      readahead cache (if any); this forces an expensive refresh of
      data for the next caller of mmap (or 'get_block' accesses)
    */
    if (file->f_dentry->d_inode &&
        file->f_dentry->d_inode->i_mapping &&
        file->f_dentry->d_inode->i_data.nrpages)
    {
        clear_inode_mmap_ra_cache(file->f_dentry->d_inode);
        truncate_inode_pages(file->f_dentry->d_inode->i_mapping, 0);
    }
    return 0;
}

int pvfs2_fsync(
    struct file *file,
    struct dentry *dentry,
    int datasync)
{
    pvfs2_print("pvfs2_fsync: called\n");
    return 0;
}

/*
  NOTE: if .llseek is overriden, we must acquire lock as described in
  Documentation/filesystems/Locking
*/
loff_t pvfs2_file_llseek(struct file *file, loff_t offset, int origin)
{
    int ret = -EINVAL;
    struct inode *inode = file->f_dentry->d_inode;

    if (!inode)
    {
        pvfs2_error("pvfs2_file_llseek: invalid inode (NULL)\n");
        return ret;
    }

    if (origin == PVFS2_SEEK_END)
    {
        /* revalidate the inode's file size */
        ret = pvfs2_inode_getattr(inode);
        if (ret)
        {
            pvfs2_make_bad_inode(inode);
            return ret;
        }
    }

    pvfs2_print("pvfs2_file_llseek: int offset is %d | origin is %d | "
                "inode size is %lu\n", (int)offset, origin,
                (unsigned long)file->f_dentry->d_inode->i_size);

    return generic_file_llseek(file, offset, origin);
}

struct file_operations pvfs2_file_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    llseek : pvfs2_file_llseek,
    read : pvfs2_file_read,
    write : pvfs2_file_write,
    ioctl : pvfs2_ioctl,
    mmap : pvfs2_file_mmap,
    open : pvfs2_file_open,
    release : pvfs2_file_release,
    fsync : pvfs2_fsync
#else
    .llseek = pvfs2_file_llseek,
    .read = pvfs2_file_read,
    .write = pvfs2_file_write,
    .ioctl = pvfs2_ioctl,
    .mmap = pvfs2_file_mmap,
    .open = pvfs2_file_open,
    .release = pvfs2_file_release,
    .fsync = pvfs2_fsync
#endif
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

/* defined in devpvfs2-req.c */
void kill_device_owner(void);

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;

extern struct address_space_operations pvfs2_address_operations;
extern struct backing_dev_info pvfs2_backing_dev_info;


#define wake_up_device_for_return(op)           \
do {                                            \
spin_lock(&op->lock);                           \
op->io_completed = 1;                           \
spin_unlock(&op->lock);                         \
wake_up_interruptible(&op->io_completion_waitq);\
} while(0)


int pvfs2_file_open(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2: pvfs2_file_open called on %s (inode is %d)\n",
		file->f_dentry->d_name.name, (int)inode->i_ino);

    if (S_ISDIR(inode->i_mode))
    {
	return dcache_dir_open(inode, file);
    }
    /*
      fs/open.c: returns 0 after enforcing large file support
      if running on a 32 bit system w/o O_LARGFILE flag
    */
    return generic_file_open(inode, file);
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
    char* current_buf = buf;
    loff_t original_offset = *offset;
    int retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    while(total_count < count)
    {
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            pvfs2_error("pvfs2: ERROR -- pvfs2_inode_read "
                        "kmem_cache_alloc failed!\n");
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.readahead_size = readahead_size;
        new_op->upcall.req.io.io_type = PVFS_IO_READ;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

	/* get a buffer for the transfer */
	/* note that we get a new buffer each time for fairness, though
	 * it may speed things up in the common case more if we kept one
	 * buffer the whole time; need to measure performance difference
	 */
	ret = pvfs_bufmap_get(&buffer_index);
	if (ret < 0)
	{
	    pvfs2_error("pvfs2: error: pvfs_bufmap_get() failure.\n");
	    ret = new_op->downcall.status;
            op_release(new_op);
            kill_device_owner();
	    pvfs_bufmap_put(buffer_index);
	    *offset = original_offset;
	    return(ret);
	}

	/* how much to transfer in this loop iteration */
	each_count = (((count - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count - total_count));

	new_op->upcall.req.io.buf_index = buffer_index;
	new_op->upcall.req.io.count = each_count;
	new_op->upcall.req.io.offset = *offset;

        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_inode_read", retries, error_exit);

	if (new_op->downcall.status != 0)
	{
	    pvfs2_error("pvfs2_inode_read: error: io downcall status.\n");

          error_exit:
            /* this macro is defined in pvfs2-kernel.h */
            handle_io_error();

	    pvfs2_error("pvfs2_inode_read: returning error %d "
                        "(error_exit=%d)\n", ret, error_exit);
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
          tell the device file owner waiting on I/O that
          this read has completed and it can return now
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
      NOTE: for this special case, op is freed
      by devreq_writev and *not* here.
    */

    return(total_count); 
}

ssize_t pvfs2_file_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t *offset)
{
    return pvfs2_inode_read(
        file->f_dentry->d_inode, buf, count, offset, 1, 0);
}

static ssize_t pvfs2_file_write(
    struct file *file,
    const char *buf,
    size_t count,
    loff_t * offset)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    char* current_buf = (char*)buf;
    loff_t original_offset = *offset;
    int buffer_index = -1, error_exit = 0;
    size_t each_count = 0, total_count = 0;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    pvfs2_print("pvfs2: pvfs2_file_write called on %s\n",
		(file && file->f_dentry && file->f_dentry->d_name.name ?
                 (char *)file->f_dentry->d_name.name : "UNKNOWN"));

    while(total_count < count)
    {
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            pvfs2_error("pvfs2: ERROR -- pvfs2_file_write "
                        "kmem_cache_alloc failed!\n");
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.io_type = PVFS_IO_WRITE;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

	pvfs2_print("pvfs2: writing %d bytes.\n", count);

	/* get a buffer for the transfer */
	/* note that we get a new buffer each time for fairness, though
	 * it may speed things up in the common case more if we kept one
	 * buffer the whole time; need to measure performance difference
	 */
	ret = pvfs_bufmap_get(&buffer_index);
	if(ret < 0)
	{
	    pvfs2_error("pvfs2: error: pvfs_bufmap_get() failure.\n");
            goto error_exit;
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
	    ret = new_op->downcall.status;
            kill_device_owner();
            op_release(new_op);
	    pvfs_bufmap_put(buffer_index);
	    *offset = original_offset;
	    return(ret);
        }

        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_file_write", retries, error_exit);

	if (new_op->downcall.status != 0)
	{
	    pvfs2_error("pvfs2_file_write: error: io downcall status.\n");

          error_exit:
            /* this macro is defined in pvfs2-kernel.h */
            handle_io_error();

	    pvfs2_error("pvfs2_file_write: returning error %d "
                        "(error_exit=%d)\n", ret, error_exit);
	    return ret;
	}

	current_buf += new_op->downcall.resp.io.amt_complete;
	*offset += new_op->downcall.resp.io.amt_complete;
	total_count += new_op->downcall.resp.io.amt_complete;

        /* adjust inode size if applicable */
        if ((original_offset + new_op->downcall.resp.io.amt_complete) >
            inode->i_size)
        {
            i_size_write(inode, (original_offset +
                                 new_op->downcall.resp.io.amt_complete));
        }

        /*
          tell the device file owner waiting on I/O that
          this read has completed and it can return now
        */
        wake_up_device_for_return(new_op);

	pvfs_bufmap_put(buffer_index);

	/* if we got a short write, fall out and return what we
	 * got so far
	 * TODO: define semantics here- kind of depends on pvfs2
	 * semantics that don't really exist yet
	 */
	if (new_op->downcall.resp.io.amt_complete < each_count)
	{
	    break;
	}
    }

    if (total_count)
    {
        inode->i_atime = CURRENT_TIME;
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

    pvfs2_print("pvfs2: pvfs2_ioctl called with cmd %d\n", cmd);
    return ret;
}

static int pvfs2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct inode *inode = file->f_dentry->d_inode;

    pvfs2_print("pvfs2: pvfs2_mmap called on %s\n",
                (file ? (char *)file->f_dentry->d_name.name :
                 (char *)"Unknown"));

    /*
      for mmap on pvfs2, make sure we use pvfs2 specific
      address operations by explcitly setting the operations
    */
    inode->i_mapping->host = inode;
    inode->i_mapping->a_ops = &pvfs2_address_operations;
    inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;

    /* and clear any associated pages in the page cache (if any) */
    truncate_inode_pages(inode->i_mapping, 0);

    /* have the vfs enforce readonly mmap support for us */
    return generic_file_readonly_mmap(file, vma);
}

/*
  NOTE: gets called when all files are closed.  not when
  each file is closed. (i.e. last reference to an opened file)
*/
int pvfs2_file_release(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2: pvfs2_file_release called on %s\n",
                file->f_dentry->d_name.name);

    if (S_ISDIR(inode->i_mode))
    {
	return dcache_dir_close(inode, file);
    }

    /*
      remove all associated inode pages from the page cache;
      this forces an expensive refresh of data for
      the next caller of mmap (or 'get_block' accesses)
    */
    if (file->f_dentry->d_inode &&
        file->f_dentry->d_inode->i_mapping)
    {
        truncate_inode_pages(file->f_dentry->d_inode->i_mapping, 0);
        i_size_write(file->f_dentry->d_inode, 0);
    }
    return 0;
}

int pvfs2_fsync(
    struct file *file,
    struct dentry *dentry,
    int datasync)
{
    pvfs2_print("pvfs2: pvfs2_fsync called\n");

    return 0;
}

loff_t pvfs2_file_llseek(struct file *file, loff_t offset, int origin)
{
    int ret = -EINVAL;
    struct inode *inode = file->f_dentry->d_inode;

    if (!inode)
    {
        pvfs2_error("pvfs2_file_llseek: invalid inode (NULL)\n");
        return ret;
    }

    if (inode->i_size == 0)
    {
        /* revalidate the inode's file size */
        ret = pvfs2_inode_getattr(inode);
        if (ret)
        {
            make_bad_inode(inode);
            return ret;
        }
    }

    /*
      NOTE: if .llseek is overriden, we must acquire lock as
      described in Documentation/filesystems/Locking
    */
    pvfs2_print("pvfs2_file_llseek: int offset is %d| origin is %d\n",
                (int)offset, origin);

    pvfs2_print("pvfs2_file_llseek: inode thinks size is %lu\n",
                (unsigned long)file->f_dentry->d_inode->i_size);

    return generic_file_llseek(file, offset, origin);
}

struct file_operations pvfs2_file_operations =
{
    .llseek = pvfs2_file_llseek,
    .read = pvfs2_file_read,
    .write = pvfs2_file_write,
    .ioctl = pvfs2_ioctl,
    .mmap = pvfs2_file_mmap,
    .open = pvfs2_file_open,
    .release = pvfs2_file_release,
    .fsync = pvfs2_fsync};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

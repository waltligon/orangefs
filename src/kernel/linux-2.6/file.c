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

extern struct address_space_operations pvfs2_address_operations;
extern struct backing_dev_info pvfs2_backing_dev_info;


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
    int copy_to_user)
{
    size_t each_count = 0;
    size_t total_count = 0;
    pvfs2_kernel_op_t *new_op = NULL;
    struct pvfs_bufmap_desc* desc;
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    char* current_buf = buf;
    loff_t original_offset = *offset;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
    if (!new_op)
    {
	pvfs2_error("pvfs2: ERROR -- pvfs2_inode_read "
		    "kmem_cache_alloc failed!\n");
	return -ENOMEM;
    }

    new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
    new_op->upcall.req.io.io_type = PVFS_IO_READ;
    new_op->upcall.req.io.refn = pvfs2_inode->refn;

    while(total_count < count)
    {
	/* get a buffer for the transfer */
	/* note that we get a new buffer each time for fairness, though
	 * it may speed things up in the common case more if we kept one
	 * buffer the whole time; need to measure performance difference
	 */
	ret = pvfs_bufmap_get(&desc);
	if(ret < 0)
	{
	    *offset = original_offset;
	    op_release(new_op);
	    pvfs2_error("pvfs2: error: pvfs_bufmap_get() failure.\n");
	    return(ret);
	}
    
	/* how much to transfer in this loop iteration */
	if((count - total_count) > pvfs_bufmap_size_query())
	    each_count = pvfs_bufmap_size_query();
	else
	    each_count = count - total_count;

	new_op->upcall.req.io.buf = desc->uaddr;
	new_op->upcall.req.io.count = each_count;
	new_op->upcall.req.io.offset = *offset;

        service_operation_with_timeout_retry(
            new_op, "pvfs2_inode_read", retries);

	if(new_op->downcall.status != 0)
	{
          error_exit:
            kill_device_owner();
	    pvfs_bufmap_put(desc);
	    ret = new_op->downcall.status;
	    op_release(new_op);
	    *offset = original_offset;
	    pvfs2_error("pvfs2: error: read downcall status.\n");
	    return(ret);
	}

	/* copy data out to destination */
	if(new_op->downcall.resp.io.amt_complete)
	{
            if (copy_to_user)
            {
                pvfs_bufmap_copy_to_user(
                    current_buf, desc,
                    new_op->downcall.resp.io.amt_complete);
            }
            else
            {
                /*
                  NOTE: assumes size is PAGE_SIZE -- which
                  should be okay since we're being called from
                  get block if we're here
                */
                memcpy(current_buf, desc->kaddr_array[0],
                       new_op->downcall.resp.io.amt_complete);
            }
	}

	pvfs_bufmap_put(desc);

	current_buf += new_op->downcall.resp.io.amt_complete;
	*offset += new_op->downcall.resp.io.amt_complete;
	total_count += new_op->downcall.resp.io.amt_complete;

	/* if we got a short read, fall out and return what we
	 * got so far
	 */
	if(new_op->downcall.resp.io.amt_complete < each_count)
	{
	    break;
	}
    }

    op_release(new_op);

    return(total_count); 
}

ssize_t pvfs2_file_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t *offset)
{
    return pvfs2_inode_read(
        file->f_dentry->d_inode, buf, count, offset, 1);
}

static ssize_t pvfs2_file_write(
    struct file *file,
    const char *buf,
    size_t count,
    loff_t * offset)
{
    size_t each_count = 0;
    size_t total_count = 0;
    pvfs2_kernel_op_t *new_op = NULL;
    struct pvfs_bufmap_desc* desc;
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    char* current_buf = (char*)buf;
    loff_t original_offset = *offset;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    pvfs2_print("pvfs2: pvfs2_file_write called on %s\n",
		file->f_dentry->d_name.name);
    
    new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
    if (!new_op)
    {
	pvfs2_error("pvfs2: ERROR -- pvfs2_file_write "
		    "kmem_cache_alloc failed!\n");
	return -ENOMEM;
    }

    new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
    new_op->upcall.req.io.io_type = PVFS_IO_WRITE;
    new_op->upcall.req.io.refn = pvfs2_inode->refn;

    while(total_count < count)
    {
	pvfs2_print("pvfs2: write iteration %d (size of %d).\n",
                    total_count, count);

	/* get a buffer for the transfer */
	/* note that we get a new buffer each time for fairness, though
	 * it may speed things up in the common case more if we kept one
	 * buffer the whole time; need to measure performance difference
	 */
	ret = pvfs_bufmap_get(&desc);
	if(ret < 0)
	{
	    *offset = original_offset;
	    op_release(new_op);
	    pvfs2_error("pvfs2: error: pvfs_bufmap_get() failure.\n");
	    return(ret);
	}
    
	/* how much to transfer in this loop iteration */
	if((count - total_count) > pvfs_bufmap_size_query())
	    each_count = pvfs_bufmap_size_query();
	else
	    each_count = count - total_count;

	new_op->upcall.req.io.buf = desc->uaddr;
	new_op->upcall.req.io.count = each_count;
	new_op->upcall.req.io.offset = *offset;

	/* copy data from application */
	pvfs_bufmap_copy_from_user(desc, current_buf,
	    each_count);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_file_write", retries);

	if(new_op->downcall.status != 0)
	{
          error_exit:
            kill_device_owner();
	    pvfs_bufmap_put(desc);
	    ret = new_op->downcall.status;
	    op_release(new_op);
	    *offset = original_offset;
	    pvfs2_error("pvfs2: error: read downcall status.\n");
	    return(ret);
	}

	pvfs_bufmap_put(desc);

	current_buf += new_op->downcall.resp.io.amt_complete;
	*offset += new_op->downcall.resp.io.amt_complete;
	total_count += new_op->downcall.resp.io.amt_complete;

	/* if we got a short write, fall out and return what we
	 * got so far
	 * TODO: define semantics here- kind of depends on pvfs2
	 * semantics that don't really exist yet
	 */
	if(new_op->downcall.resp.io.amt_complete < each_count)
	{
	    break;
	}
    }

    op_release(new_op);

    return(total_count); 
}

int pvfs2_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int cmd,
    unsigned long arg)
{
    pvfs2_print("pvfs2: pvfs2_ioctl called\n");
    return 0;
}

static int pvfs2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct inode *inode = file->f_dentry->d_inode;

    pvfs2_print("pvfs2: pvfs2_mmap called\n");

    /*
      for mmap on pvfs2, make sure we use pvfs2 specific
      address operations by explcitly setting the operations
    */
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
    pvfs2_print("pvfs2: pvfs2_file_release called\n");
    if (S_ISDIR(inode->i_mode))
    {
	return dcache_dir_close(inode, file);
    }

    /*
      remove all associated inode pages from the page cache;
      this forces an expensive refresh of data for
      the next caller of mmap (or 'get_block' accesses)
    */
    truncate_inode_pages(file->f_dentry->d_inode->i_mapping, 0);
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
    /*
      NOTE: if .llseek is overriden, we must acquire lock as
      described in Documentation/filesystems/Locking
    */
    pvfs2_print("pvfs2_file_llseek: offset is %llu (%d)| origin is %d\n",
                (unsigned long long)offset, (int)offset, origin);

    pvfs2_print("pvfs2_file_llseek: inode thinks size is %lu\n",
                (unsigned long)file->f_dentry->d_inode->i_size);

    return generic_file_llseek(file, offset, origin);
}

struct file_operations pvfs2_file_operations =
{
    .llseek = pvfs2_file_llseek,
    .read = pvfs2_file_read,
    .aio_read = generic_file_aio_read,
    .write = pvfs2_file_write,
    .aio_write = generic_file_aio_write,
    .ioctl = pvfs2_ioctl,
    .mmap = pvfs2_file_mmap,
    .open = pvfs2_file_open,
    .release = pvfs2_file_release,
    .fsync = pvfs2_fsync,
    .readv = generic_file_readv,
    .writev = generic_file_writev,
    .sendfile = generic_file_sendfile,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

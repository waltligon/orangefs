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

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;

int pvfs2_open(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2: pvfs2_open called on %s (inode is %p)\n",
		file->f_dentry->d_name.name, inode);

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

static ssize_t pvfs2_file_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t * offset)
{
    pvfs2_kernel_op_t *new_op = (pvfs2_kernel_op_t *) 0;

    pvfs2_print("pvfs2: pvfs2_file_read called on %s\n",
		file->f_dentry->d_name.name);

    new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
    if (!new_op)
    {
	pvfs2_error("pvfs2: ERROR -- pvfs2_file_read "
		    "kmem_cache_alloc failed!\n");
	return -ENOMEM;
    }
    new_op->upcall.type = PVFS2_VFS_OP_FILE_READ;
    new_op->upcall.req.read.buf = buf;
    new_op->upcall.req.read.count = count;
    new_op->upcall.req.read.offset = offset;

    /* post req and wait for request to be serviced here */
    pvfs2_error("FIXME: SKIPPING READ: DOING NOTHING!\n");
/*     add_op_to_request_list(new_op); */
/*     wait_for_matching_downcall(new_op); */

    /* when request is serviced, free req op struct */
    op_release(new_op);

    return 0;
}

static ssize_t pvfs2_file_write(
    struct file *file,
    const char *buf,
    size_t count,
    loff_t * offset)
{
    pvfs2_print("pvfs2: pvfs2_file_write called on %s\n",
		file->f_dentry->d_name.name);
    return count;
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

/*
  NOTE: gets called when all files are closed.  not when
  each file is closed. (i.e. last reference to an opened file)
*/
int pvfs2_release(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2: pvfs2_release called\n");


    if (S_ISDIR(inode->i_mode))
    {
	return dcache_dir_close(inode, file);
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

struct file_operations pvfs2_file_operations = {
/*
  if .llseek is overriden, we must acquire lock as described
  in Documentation/filesystems/Locking
*/
    .llseek = generic_file_llseek,
    .read = pvfs2_file_read,
    .aio_read = generic_file_aio_read,
    .write = pvfs2_file_write,
    .aio_write = generic_file_aio_write,
    .ioctl = pvfs2_ioctl,
    .mmap = generic_file_mmap,
    .open = pvfs2_open,
    .release = pvfs2_release,
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

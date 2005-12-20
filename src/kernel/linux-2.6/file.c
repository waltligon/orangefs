/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS file operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-types.h"
#include "pvfs2-internal.h"
#include <linux/fs.h>
#include <linux/pagemap.h>

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern int debug;
extern int op_timeout_secs;

extern struct address_space_operations pvfs2_address_operations;
extern struct backing_dev_info pvfs2_backing_dev_info;

#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_precheck_file_write(struct file *file, struct inode *inode,
    size_t *count, loff_t *ppos);
#endif

#define wake_up_device_for_return(op)             \
do {                                              \
  spin_lock(&op->lock);                           \
  op->io_completed = 1;                           \
  spin_unlock(&op->lock);                         \
  wake_up_interruptible(&op->io_completion_waitq);\
} while(0)


/** Called when a process requests to open a file.
 */
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
            /* 
             * When we do a getattr in response to an open with O_APPEND,
             * all we are interested in is the file size. Hence we will
             * set the mask to only the size and nothing else
             * Hopefully, this will help us in reducing the number of getattr's
             */
            ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_SIZE);
            if (ret == 0)
            {
                file->f_pos = i_size_read(inode);
                pvfs2_print("f_pos = %ld\n", (unsigned long)file->f_pos);
            }
            else
            {
                pvfs2_make_bad_inode(inode);
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

/** Read data from a specified offset in a file (referenced by inode).
 *  Data may be placed either in a user or kernel buffer.
 */
ssize_t pvfs2_inode_read(
    struct inode *inode,
    char __user *buf,
    size_t count,
    loff_t *offset,
    int copy_to_user,
    loff_t readahead_size)
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
    int dc_status;

    if (copy_to_user && (!access_ok(VERIFY_WRITE, buf, count)))
        return -EFAULT;

    while(total_count < count)
    {
        new_op = op_alloc();
        if (!new_op)
        {
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.async_vfs_io = PVFS_VFS_SYNC_IO; /* synchronous I/O */
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

        dc_status = 0;  /* macro may jump to error_exit below */
        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_inode_read", retries, error_exit,
            get_interruptible_flag(inode));

        if (new_op->downcall.status != 0)
        {
            dc_status = new_op->downcall.status;

          error_exit:
            /* this macro is defined in pvfs2-kernel.h */
            handle_io_error();

            /*
              don't write an error to syslog on signaled operation
              termination unless we've got debugging turned on, as
              this can happen regularly (i.e. ctrl-c)
            */
            if ((error_exit != 0) && (ret == -EINTR))
            {
                pvfs2_print("pvfs2_inode_read: returning error %d "
                            "(error_exit=%d)\n", ret, error_exit);
            }
            else
            {
                pvfs2_error(
                    "pvfs2_inode_read: error reading from handle %llu, "
                    "\n  -- downcall status is %d, returning %d "
                    "(error_exit=%d)\n",
                    llu(pvfs2_ino_to_handle(inode->i_ino)),
                    dc_status, ret, error_exit);
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
                pvfs2_print("Failed to copy user buffer.\n");
                /* put error code in downcall so that handle_io_error()
                 * preserves properly
                 */
                new_op->downcall.status = -PVFS_EFAULT;
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

/** Read data from a specified offset in a file into a user buffer.
 */
ssize_t pvfs2_file_read(
    struct file *file,
    char __user *buf,
    size_t count,
    loff_t *offset)
{
    pvfs2_print("pvfs2_file_read: called on %s\n",
                (file && file->f_dentry && file->f_dentry->d_name.name ?
                 (char *)file->f_dentry->d_name.name : "UNKNOWN"));

    return pvfs2_inode_read(
        file->f_dentry->d_inode, buf, count, offset, 1, 0);
}

/** Write data from a contiguous user buffer into a file at a specified
 *  offset.
 */
static ssize_t pvfs2_file_write(
    struct file *file,
    const char __user *buf,
    size_t count,
    loff_t *offset)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    char __user *current_buf = (char __user *)buf;
    loff_t original_offset = *offset;
    int buffer_index = -1, error_exit = 0;
    size_t each_count = 0, total_count = 0;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    size_t amt_complete = 0;
    int dc_status;

    pvfs2_print("pvfs2_file_write: called on %s\n",
                (file && file->f_dentry && file->f_dentry->d_name.name ?
                 (char *)file->f_dentry->d_name.name : "UNKNOWN"));

    if (!access_ok(VERIFY_READ, buf, count))
        return -EFAULT;

    /* perform generic linux kernel tests for sanity of write arguments */
    /* NOTE: this is particularly helpful in handling fsize rlimit properly */
#ifdef PVFS2_LINUX_KERNEL_2_4
    ret = pvfs2_precheck_file_write(file, inode, &count, offset);
#else
    ret = generic_write_checks(file, offset, &count, S_ISBLK(inode->i_mode));
#endif
    if (ret != 0 || count == 0)
    {
        pvfs2_print("pvfs2_file_write: failed generic argument checks.\n");
        return(ret);
    }
    /* for whatever reason, count is being set to inode->i_size in
     * generic_write_checks with O_APPEND opens.  So if we want that to work,
     * we would have to do a getattr for the size which is ridiculous.
     * So we work that around here...
     */
    if (*offset != file->f_pos)
    {
        *offset = file->f_pos;
    }

    while(total_count < count)
    {
        new_op = op_alloc();
        if (!new_op)
        {
            *offset = original_offset;
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.async_vfs_io = PVFS_VFS_SYNC_IO; /* synchronous I/O */
        new_op->upcall.req.io.io_type = PVFS_IO_WRITE;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

        pvfs2_print("pvfs2_file_write: writing %d bytes at offset %lu (%lu)\n",
                (int)count, (unsigned long)file->f_pos, (unsigned long)*offset);

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
        ret = pvfs_bufmap_copy_from_user(
            buffer_index, current_buf, each_count);
        if(ret < 0)
        {
            pvfs2_print("Failed to copy user buffer.\n");
            op_release(new_op);
            pvfs_bufmap_put(buffer_index);
            *offset = original_offset;
            return ret;
        }

        dc_status = 0;
        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_file_write", retries, error_exit,
            get_interruptible_flag(inode));

        if (new_op->downcall.status != 0)
        {
            dc_status = new_op->downcall.status;

          error_exit:
            /* this macro is defined in pvfs2-kernel.h */
            handle_io_error();

            /*
              don't write an error to syslog on signaled operation
              termination unless we've got debugging turned on, as
              this can happen regularly (i.e. ctrl-c)
            */
            if ((error_exit != 0) && (ret == -EINTR))
            {
                pvfs2_print("pvfs2_file_write: returning error %d "
                            "(error_exit=%d)\n", ret, error_exit);
            }
            else
            {
                pvfs2_error(
                    "pvfs2_file_write: error writing to handle %llu, "
                    "FILE: %s\n  -- downcall status is %d, returning %d "
                    "(error_exit=%d)\n",
                    llu(pvfs2_ino_to_handle(inode->i_ino)),
                    (file && file->f_dentry && file->f_dentry->d_name.name ?
                     (char *)file->f_dentry->d_name.name : "UNKNOWN"),
                    dc_status, ret, error_exit);
            }
            *offset = original_offset;
            return ret;
        }

        current_buf += new_op->downcall.resp.io.amt_complete;
        pvfs2_print("amt_complete = %ld\n", (unsigned long) new_op->downcall.resp.io.amt_complete);
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

        /* if we got a short write, fall out and return what we got so
         * far TODO: define semantics here- kind of depends on pvfs2
         * semantics that don't really exist yet
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }
    pvfs2_print("pvfs2_file_write: pos at the end was %lu(%lu)\n", 
            (unsigned long) *offset, (unsigned long) file->f_pos);
    if (total_count)
    {
        update_atime(inode);
    }
    return total_count;
}

/*
 * The reason we need to do this is to be able to support readv() and writev()
 * of larger than PVFS_DEFAULT_DESC_SIZE (4 MB). What that means is that
 * we will create a new io vec descriptor for those memory addresses that 
 * go beyond the limit
 * Return value for this routine is -ve in case of errors
 * and 0 in case of success.
 * Further, the new_nr_segs pointer is updated to hold the new value
 * of number of iovecs, the new_vec pointer is updated to hold the pointer
 * to the new split iovec, and the size array is an array of integers holding
 * the number of iovecs that straddle PVFS_DEFAULT_DESC_SIZE.
 * The max_new_nr_segs value is computed by the caller and returned.
 * (It will be (count of all iov_len/ block_size) + 1).
 */
static int split_iovecs(unsigned long max_new_nr_segs,  /* IN */
        unsigned long nr_segs,              /* IN */
        const struct iovec *original_iovec, /* IN */
        unsigned long *new_nr_segs, struct iovec **new_vec,  /* OUT */
        unsigned int *seg_count, unsigned int **seg_array)   /* OUT */
{
    int seg, count = 0, begin_seg, tmpnew_nr_segs = 0;
    struct iovec *new_iovec = NULL, *orig_iovec;
    unsigned int *sizes = NULL, sizes_count = 0;

    if (nr_segs <= 0 || original_iovec == NULL 
            || new_nr_segs == NULL || new_vec == NULL
            || seg_count == NULL || seg_array == NULL || max_new_nr_segs <= 0)
    {
        pvfs2_error("Invalid parameters to split_iovecs\n");
        return -EINVAL;
    }
    *new_nr_segs = 0;
    *new_vec = NULL;
    *seg_count = 0;
    *seg_array = NULL;
    /* copy the passed in iovec descriptor to a temp structure */
    orig_iovec = (struct iovec *) kmalloc(nr_segs * sizeof(struct iovec),
            PVFS2_BUFMAP_GFP_FLAGS);
    if (orig_iovec == NULL)
    {
        pvfs2_error("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(nr_segs * sizeof(struct iovec)));
        return -ENOMEM;
    }
    new_iovec = (struct iovec *) kmalloc(max_new_nr_segs * sizeof(struct iovec), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (new_iovec == NULL)
    {
        kfree(orig_iovec);
        pvfs2_error("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(struct iovec)));
        return -ENOMEM;
    }
    sizes = (int *) kmalloc(max_new_nr_segs * sizeof(int), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (sizes == NULL)
    {
        kfree(new_iovec);
        kfree(orig_iovec);
        pvfs2_error("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(int)));
        return -ENOMEM;
    }
    /* copy the passed in iovec to a temp structure */
    memcpy(orig_iovec, original_iovec, nr_segs * sizeof(struct iovec));
    memset(new_iovec, 0, max_new_nr_segs * sizeof(struct iovec));
    memset(sizes, 0, max_new_nr_segs * sizeof(int));
    begin_seg = 0;
repeat:
    for (seg = begin_seg; seg < nr_segs; seg++)
    {
        if (count + orig_iovec[seg].iov_len <= pvfs_bufmap_size_query())
        {
            count += orig_iovec[seg].iov_len;
            if (tmpnew_nr_segs >= max_new_nr_segs)
            {
                kfree(sizes);
                kfree(orig_iovec);
                kfree(new_iovec);
                pvfs2_error("split_iovecs: exceeded the index limit (%d)\n", 
                        tmpnew_nr_segs);
                return -EINVAL;
            }
            memcpy(&new_iovec[tmpnew_nr_segs], &orig_iovec[seg], 
                    sizeof(struct iovec));
            tmpnew_nr_segs++;
            sizes[sizes_count]++;
        }
        else
        {
            if (tmpnew_nr_segs >= max_new_nr_segs)
            {
                kfree(sizes);
                kfree(orig_iovec);
                kfree(new_iovec);
                pvfs2_error("split_iovecs: exceeded the index limit (%d)\n", 
                        tmpnew_nr_segs);
                return -EINVAL;
            }
            new_iovec[tmpnew_nr_segs].iov_base = orig_iovec[seg].iov_base;
            new_iovec[tmpnew_nr_segs].iov_len = 
                (pvfs_bufmap_size_query() - count);
            tmpnew_nr_segs++;
            if (sizes_count >= max_new_nr_segs)
            {
                kfree(sizes);
                kfree(orig_iovec);
                kfree(new_iovec);
                pvfs2_error("split_iovecs: exceeded the size limit (%d)\n", 
                        sizes_count);
                return -EINVAL;
            }
            sizes[sizes_count]++;
            sizes_count++;
            begin_seg = seg;
            orig_iovec[seg].iov_base += (pvfs_bufmap_size_query() - count);
            orig_iovec[seg].iov_len  -= (pvfs_bufmap_size_query() - count);
            count = 0;
            break;
        }
    }
    if (seg != nr_segs) {
        goto repeat;
    }
    else
    {
        sizes_count++;
    }
    *new_nr_segs = tmpnew_nr_segs;
    /* new_iovec is freed by the caller */
    *new_vec = new_iovec;
    *seg_count = sizes_count;
    /* seg_array is also freed by the caller */
    *seg_array = sizes;
    kfree(orig_iovec);
    return 0;
}


/** Reads data to several contiguous user buffers (an iovec) from a file at a
 * specified offset.
 */
static ssize_t pvfs2_file_readv(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    loff_t *offset)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    struct iovec *iovecptr = NULL, *ptr = NULL;
    loff_t original_offset = *offset;
    int buffer_index = -1, error_exit = 0;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    size_t amt_complete = 0;
    size_t total_count = 0, count = 0, each_count = 0;
    unsigned int seg, to_free = 0;
    unsigned long new_nr_segs = 0, max_new_nr_segs = 0;
    unsigned int  seg_count, *seg_array = NULL;


    /* Calculate the total length to read by adding up the length of each io
     * segment */
    for (seg = 0; seg < nr_segs; seg++)
    {
	const struct iovec *iv = &iov[seg];
	count += iv->iov_len;
	if (unlikely((ssize_t)(count|iv->iov_len) < 0))
	    return -EINVAL;
        if (total_count + iv->iov_len < pvfs_bufmap_size_query())
        {
            total_count += iv->iov_len;
            max_new_nr_segs++;
        }
        else {
            total_count = (total_count + iv->iov_len - 
                    pvfs_bufmap_size_query());
            max_new_nr_segs+=2;
        }
    }
    total_count = 0;
    /*
     * if the total size of data transfer requested is greater than
     * the kernel-set blocksize of PVFS2, then we split the iovecs
     * such that no iovec description straddles a block size limit
     */
    if (count > pvfs_bufmap_size_query())
    {
        /*
         * Split up the given iovec description such that
         * no iovec descriptor straddles over the block-size limitation.
         * This makes us our job easier to stage the I/O.
         * In addition, this function will also compute an array with seg_count
         * entries that will store the number of segments that straddle the
         * block-size boundaries.
         */
        if ((ret = split_iovecs(max_new_nr_segs, nr_segs, iov, /* IN */
                        &new_nr_segs, &iovecptr, /* OUT */
                        &seg_count, &seg_array)  /* OUT */ ) < 0)
        {
            pvfs2_error("Failed to split iovecs to satisfy larger "
                    " than blocksize readv request %d\n", ret);
            return ret;
        }
        pvfs2_print("pvfs_file_readv: Splitting iovecs from %lu to %lu [max_new %lu]\n", 
                nr_segs, new_nr_segs, max_new_nr_segs);
        /* We must free seg_array and iovecptr */
        to_free = 1;
    }
    else {
        new_nr_segs = nr_segs;
        /* use the given iovec description */
        iovecptr = (struct iovec *) iov;
        /* There is only 1 element in the seg_array */
        seg_count = 1;
        /* and its value is the number of segments passed in */
        seg_array = (unsigned int *) &nr_segs;
        /* We dont have to free up anything */
        to_free = 0;
    }
    ptr = iovecptr;
    pvfs2_print("pvfs2_file_readv reading %d@%llu\n", (int) count, lld(*offset));
    pvfs2_print("pvfs2_file_readv: new_nr_segs: %lu, seg_count: %u\n", 
            new_nr_segs, seg_count);
    for (seg = 0; seg < new_nr_segs; seg++)
    {
        pvfs2_print("pvfs2_file_readv: %d) %p to %p [%d bytes]\n", 
                seg + 1, iovecptr[seg].iov_base, 
                iovecptr[seg].iov_base + iovecptr[seg].iov_len, 
                (int) iovecptr[seg].iov_len);
    }
    for (seg = 0; seg < seg_count; seg++)
    {
        pvfs2_print("pvfs2_file_readv: %d) %u\n", seg + 1, seg_array[seg]);
    }
    seg = 0;
    while (total_count < count)
    {
        int dc_status = 0;
        new_op = op_alloc();
        if (!new_op)
        {
            *offset = original_offset;
            if (to_free) {
                kfree(iovecptr);
                kfree(seg_array);
            }
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.async_vfs_io = PVFS_VFS_SYNC_IO; /* synchronous I/O */
        /* disable read-ahead */
        new_op->upcall.req.io.readahead_size = 0;
        new_op->upcall.req.io.io_type = PVFS_IO_READ;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

        ret = pvfs_bufmap_get(&buffer_index);
        if (ret < 0)
        {
            pvfs2_error("pvfs2_file_readv: pvfs_bufmap_get() "
                        "failure (%d)\n", ret);
            op_release(new_op);
            *offset = original_offset;
            if (to_free) {
                kfree(iovecptr);
                kfree(seg_array);
            }
            return ret;
        }

        /* how much to transfer in this loop iteration */
        each_count = (((count - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count - total_count));

        new_op->upcall.req.io.buf_index = buffer_index;
        new_op->upcall.req.io.count = each_count;
        new_op->upcall.req.io.offset = *offset;


        dc_status = 0;

        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_file_readv", retries, error_exit,
            get_interruptible_flag(inode));

        if (new_op->downcall.status != 0)
        {
            dc_status = new_op->downcall.status;

            error_exit:
              /* this macro is defined in pvfs2-kernel.h */
              handle_io_error();

              /*
                don't write an error to syslog on signaled operation
                termination unless we've got debugging turned on, as
                this can happen regularly (i.e. ctrl-c)
              */
              if ((error_exit != 0) && (ret == -EINTR))
              {
                  pvfs2_print("pvfs2_file_readv: returning error %d "
                              "(error_exit=%d)\n", ret, error_exit);
              }
              else
              {
                  pvfs2_error(
                        "pvfs2_file_readv: error writing to handle %llu, "
                        "FILE: %s\n  -- downcall status is %d, returning %d "
                        "(error_exit=%d)\n",
                        llu(pvfs2_ino_to_handle(inode->i_ino)),
                        (file && file->f_dentry && file->f_dentry->d_name.name ?
                         (char *)file->f_dentry->d_name.name : "UNKNOWN"),
                        dc_status, ret, error_exit);
              }
              *offset = original_offset;
              if (to_free) {
                  kfree(seg_array);
                  kfree(iovecptr);
              }
              return ret;
        }
        pvfs2_print("pvfs2_file_readv nr_segs %u, offset: %llu each_count:%d\n",
                (int) seg_array[seg], *offset, (int) each_count);
        /*
         * copy data to application by pushing it out to the iovec.
         * Number of segments to copy so that we don't
         * overflow the block-size is set in seg_array[], and
         * ptr points to the appropriate beginning of the
         * iovec from where data needs to be copied to, and
         * new_op->downcall.resp.io.amt_complete indicates
         * the size in bytes that needs to be pushed out
         */
        if (new_op->downcall.resp.io.amt_complete)
        {
            ret = pvfs_bufmap_copy_to_user_iovec(
                    buffer_index, ptr, seg_array[seg],
                    new_op->downcall.resp.io.amt_complete);
            if (ret < 0)
            {
                pvfs2_error("Failed to copy user buffer.  Please make sure "
                            "that the pvfs2-client is running.\n");
                /* put error codes in downcall so that handle_io_error()
                 * preserves it properly */
                new_op->downcall.status = -PVFS_EFAULT;
                goto error_exit;
            }
        }
        /* advance the iovec pointer */
        ptr += seg_array[seg];
        seg++;
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

        /* if we got a short write, fall out and return what we got so
         * far 
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }

    if (to_free) {
        kfree(iovecptr);
        kfree(seg_array);
    }
    return total_count;
}


/** Write data from a several contiguous user buffers (an iovec) into a file at
 * a specified offset.
 */
static ssize_t pvfs2_file_writev(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    loff_t *offset)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    struct iovec *iovecptr = NULL, *ptr = NULL;
    loff_t original_offset = *offset;
    int buffer_index = -1, error_exit = 0;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    size_t amt_complete = 0;
    size_t total_count = 0, count = 0, each_count = 0;
    unsigned int seg, to_free = 0;
    unsigned long new_nr_segs = 0, max_new_nr_segs = 0;
    unsigned int  seg_count, *seg_array = NULL;


    /* Calculate the total length to write by adding up the length of each io
     * segment */
    for (seg = 0; seg < nr_segs; seg++)
    {
	const struct iovec *iv = &iov[seg];
	count += iv->iov_len;
	if (unlikely((ssize_t)(count|iv->iov_len) < 0))
	    return -EINVAL;
        if (total_count + iv->iov_len < pvfs_bufmap_size_query())
        {
            total_count += iv->iov_len;
            max_new_nr_segs++;
        }
        else {
            total_count = (total_count + iv->iov_len - pvfs_bufmap_size_query());
            max_new_nr_segs+=2;
        }

    }
    /* perform generic linux kernel tests for sanity of write arguments */
    /* NOTE: this is particularly helpful in handling fsize rlimit properly */
#ifdef PVFS2_LINUX_KERNEL_2_4
    ret = pvfs2_precheck_file_write(file, inode, &count, offset);
#else
    ret = generic_write_checks(file, offset, &count, S_ISBLK(inode->i_mode));
#endif
    if (ret != 0 || count == 0)
    {
        pvfs2_print("pvfs2_file_writev: failed generic argument checks.\n");
        return(ret);
    }

    total_count = 0;
    /*
     * if the total size of data transfer requested is greater than
     * the kernel-set blocksize of PVFS2, then we split the iovecs
     * such that no iovec description straddles this block size
     * limitation.
     */
    if (count > pvfs_bufmap_size_query())
    {
        /*
         * Split up the given iovec description such that
         * no iovec descriptor straddles over the block-size limitation.
         * This makes us our job easier to stage the I/O.
         * In addition, this function will also compute an array with seg_count
         * entries that will store the number of segments that straddle the
         * block-size boundaries.
         */
        if ((ret = split_iovecs(max_new_nr_segs, nr_segs, iov, /* IN */
                        &new_nr_segs, &iovecptr, /* OUT */
                        &seg_count, &seg_array) /* OUT */ ) < 0)
        {
            pvfs2_error("Failed to split iovecs to satisfy larger than blocksize writev request %d\n", ret);
            return ret;
        }
        pvfs2_print("pvfs_file_writev: Splitting iovecs from %lu to %lu [max_new %lu]\n", 
                nr_segs, new_nr_segs, max_new_nr_segs);
        /* We must free seg_array and iovecptr */
        to_free = 1;
    }
    else {
        /* Number of segments dont change! */
        new_nr_segs = nr_segs;
         /* use the given iovec description */
        iovecptr = (struct iovec *) iov;
        /* There is only 1 element in the seg_array */
        seg_count = 1;
        /* and its value is the number of segments passed in */
        seg_array = (unsigned int *) &nr_segs;
        /* We dont have to free up anything */
        to_free = 0;
    }
    ptr = iovecptr;
    pvfs2_print("pvfs2_file_writev writing %d@%llu\n", (int) count, *offset);
    pvfs2_print("pvfs2_file_writev: new_nr_segs: %lu, seg_count: %u\n", 
            new_nr_segs, seg_count);
    for (seg = 0; seg < new_nr_segs; seg++)
    {
        pvfs2_print("pvfs2_file_writev: %d) %p to %p [%d bytes]\n", 
                seg + 1, iovecptr[seg].iov_base, 
                iovecptr[seg].iov_base + iovecptr[seg].iov_len, 
                (int) iovecptr[seg].iov_len);
    }
    for (seg = 0; seg < seg_count; seg++)
    {
        pvfs2_print("pvfs2_file_writev: %d) %u\n", seg + 1, seg_array[seg]);
    }
    seg = 0;
    while (total_count < count)
    {
        int dc_status = 0;
        new_op = op_alloc();
        if (!new_op)
        {
            *offset = original_offset;
            if (to_free) {
                kfree(iovecptr);
                kfree(seg_array);
            }
            return -ENOMEM;
        }

        new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
        new_op->upcall.req.io.async_vfs_io = PVFS_VFS_SYNC_IO; /* synchronous I/O */
        new_op->upcall.req.io.io_type = PVFS_IO_WRITE;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;

        ret = pvfs_bufmap_get(&buffer_index);
        if (ret < 0)
        {
            pvfs2_error("pvfs2_file_writev: pvfs_bufmap_get() "
                        "failure (%d)\n", ret);
            op_release(new_op);
            *offset = original_offset;
            if (to_free) {
                kfree(iovecptr);
                kfree(seg_array);
            }
            return ret;
        }

        /* how much to transfer in this loop iteration */
        each_count = (((count - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count - total_count));

        new_op->upcall.req.io.buf_index = buffer_index;
        new_op->upcall.req.io.count = each_count;
        new_op->upcall.req.io.offset = *offset;
        pvfs2_print("pvfs2_file_writev nr_segs %u, offset: %llu each_count: %d\n",
                seg_array[seg], *offset, (int) each_count);

        /* 
         * copy data from application by pulling it out  of the iovec.
         * Number of segments to copy so that we don't overflow the block-size
         * is set in seg_array[], and ptr points to the appropriate
         * beginning of the iovec from where data needs to be copied out,
         * and each_count indicates the size in bytes that needs to be pulled
         * out.  */
        ret = pvfs_bufmap_copy_iovec_from_user(
                buffer_index, ptr, seg_array[seg], each_count);
        if (ret < 0)
        {
            pvfs2_error("Failed to copy user buffer.  Please make sure "
                        "that the pvfs2-client is running. %d\n", ret);
            op_release(new_op);
            pvfs_bufmap_put(buffer_index);
            *offset = original_offset;
            if (to_free) {
                kfree(seg_array);
                kfree(iovecptr);
            }
            return ret;
        }

        dc_status = 0;
        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_file_writev", retries, error_exit,
            get_interruptible_flag(inode));

        if (new_op->downcall.status != 0)
        {
            dc_status = new_op->downcall.status;

            error_exit:
              /* this macro is defined in pvfs2-kernel.h */
              handle_io_error();

              /*
                don't write an error to syslog on signaled operation
                termination unless we've got debugging turned on, as
                this can happen regularly (i.e. ctrl-c)
              */
              if ((error_exit != 0) && (ret == -EINTR))
              {
                  pvfs2_print("pvfs2_file_writev: returning error %d "
                              "(error_exit=%d)\n", ret, error_exit);
              }
              else
              {
                  pvfs2_error(
                        "pvfs2_file_writev: error writing to handle %llu, "
                        "FILE: %s\n  -- downcall status is %d, returning %d "
                        "(error_exit=%d)\n",
                        llu(pvfs2_ino_to_handle(inode->i_ino)),
                        (file && file->f_dentry && file->f_dentry->d_name.name ?
                         (char *)file->f_dentry->d_name.name : "UNKNOWN"),
                        dc_status, ret, error_exit);
              }
              *offset = original_offset;
              if (to_free) {
                  kfree(seg_array);
                  kfree(iovecptr);
              }
              return ret;
        }
        /* advance the iovec pointer */
        ptr += seg_array[seg];
        seg++;
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

        /* if we got a short write, fall out and return what we got so
         * far TODO: define semantics here- kind of depends on pvfs2
         * semantics that don't really exist yet
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }

    if (to_free) {
        kfree(iovecptr);
        kfree(seg_array);
    }
    if (total_count)
    {
        update_atime(inode);
    }
    return total_count;
}

#ifdef HAVE_AIO_VFS_SUPPORT
/*
 * NOTES on the aio implementation.
 * Conceivably, we could just make use of the
 * generic_aio_file_read/generic_aio_file_write
 * functions that stages the read/write through 
 * the page-cache. But given that we are not
 * interested in staging anything thru the page-cache, 
 * we are going to resort to another
 * design.
 * 
 * The aio callbacks to be implemented at the f.s. level
 * are fairly straightforward. All we see at this level 
 * are individual
 * contiguous file block reads/writes. This means that 
 * we can just make use
 * of the current set of I/O upcalls without too much 
 * modifications. (All we need is an extra flag for sync/async)
 *
 * However, we do need to handle cancellations properly. 
 * What this means
 * is that the "ki_cancel" callback function must be set so 
 * that the kernel calls
 * us back with the kiocb structure for proper cancellation.
 * This way we can send appropriate upcalls
 * to cancel I/O operations if need be and copy status/results
 * back to user-space.
 */

/*
 * This is the retry routine called by the AIO core to 
 * try and see if the 
 * I/O operation submitted earlier can be completed 
 * atleast now :)
 * We can use copy_*() functions here because the kaio
 * threads do a use_mm() and assume the memory context of
 * the user-program that initiated the aio(). whew,
 * that's a big relief.
 */
static ssize_t pvfs2_aio_retry(struct kiocb *iocb)
{
    pvfs2_kiocb *x = NULL;
    pvfs2_kernel_op_t *op = NULL;
    ssize_t error = 0;

    if ((x = (pvfs2_kiocb *) iocb->private) == NULL)
    {
        pvfs2_error("pvfs2_aio_retry: could not "
                " retrieve pvfs2_kiocb!\n");
        return -EINVAL;
    }
    /* highly unlikely, but somehow paranoid need for checking */
    if (((op = x->op) == NULL) 
            || x->kiocb != iocb 
            || x->buffer_index < 0)
    {
        /*
         * Well, if this happens, we are toast!
         * What should we cleanup if such a thing happens? 
         */
        pvfs2_error("pvfs2_aio_retry: critical error "
                " x->op = %p, iocb = %p, buffer_index = %d\n",
                x->op, x->kiocb, x->buffer_index);
        return -EINVAL;
    }
    /* lock up the op */
    spin_lock(&op->lock);
    /* check the state of the op */
    if (op->op_state == PVFS2_VFS_STATE_WAITING 
            || op->op_state == PVFS2_VFS_STATE_INPROGR)
    {
        spin_unlock(&op->lock);
        return -EIOCBQUEUED;
    }
    else 
    {
        /*
         * the daemon has finished servicing this 
         * operation. It has also staged
         * the I/O to the data servers on a write
         * (if possible) and put the return value
         * of the operation in bytes_copied. 
         * Similarly, on a read the value stored in 
         * bytes_copied is the error code or the amount
         * of data that was copied to user buffers.
         */
        error = x->bytes_copied;
        op->priv = NULL;
        spin_unlock(&op->lock);
        pvfs2_print("pvfs2_aio_retry: buffer %p,"
                " size %d return %d bytes\n",
                    x->buffer, (int) x->bytes_to_be_copied, (int) error);
        if ((x->rw == PVFS_IO_WRITE) && error > 0)
        {
            struct inode *inode = iocb->ki_filp->f_mapping->host;
            /* update atime if need be */
            update_atime(inode);
        }
        /* 
         * Now we can happily free up the op,
         * and put buffer_index also away 
         */
        if (x->buffer_index >= 0)
        {
            pvfs2_print("pvfs2_aio_retry: put bufmap_index "
                    " %d\n", x->buffer_index);
            pvfs_bufmap_put(x->buffer_index);
            x->buffer_index = -1;
        }
        /* drop refcount of op and deallocate if possible */
        put_op(op);
        x->needs_cleanup = 0;
        /* x is itself deallocated when the destructor is called */
        return error;
    }
}

/*
 * Using the iocb->private->op->tag field, 
 * we should try and cancel the I/O
 * operation, and also update res->obj
 * and res->data to the values
 * at the time of cancellation.
 * This is called not only by the io_cancel() 
 * system call, but also by the exit_mm()/aio_cancel_all()
 * functions when the process that issued
 * the aio operation is about to exit.
 */
static int
pvfs2_aio_cancel(struct kiocb *iocb, struct io_event *event)
{
    pvfs2_kiocb *x = NULL;
    if (iocb == NULL || event == NULL)
    {
        pvfs2_error("pvfs2_aio_cancel: Invalid parameters "
                " %p, %p!\n", iocb, event);
        return -EINVAL;
    }
    x = (pvfs2_kiocb *) iocb->private;
    if (x == NULL)
    {
        pvfs2_error("pvfs2_aio_cancel: cannot retrieve "
                " pvfs2_kiocb structure!\n");
        return -EINVAL;
    }
    else
    {
        pvfs2_kernel_op_t *op = NULL;
        int ret;
        /*
         * Do some sanity checks
         */
        if (x->kiocb != iocb)
        {
            pvfs2_error("pvfs2_aio_cancel: kiocb structures "
                    "don't match %p %p!\n", x->kiocb, iocb);
            return -EINVAL;
        }
        if ((op = x->op) == NULL)
        {
            pvfs2_error("pvfs2_aio_cancel: cannot retreive "
                    "pvfs2_kernel_op structure!\n");
            return -EINVAL;
        }
        kiocbSetCancelled(iocb);
        get_op(op);
        /*
         * This will essentially remove it from 
         * htable_in_progress or from the req list
         * as the case may be.
         */
        clean_up_interrupted_operation(op);
        /* 
         * However, we need to make sure that 
         * the client daemon is not transferring data
         * as we speak! Thus we look at the reference
         * counter to determine if that is indeed the case.
         */
        do 
        {
            int timed_out_or_signal = 0;

            DECLARE_WAITQUEUE(wait_entry, current);
            /* add yourself to the wait queue */
            add_wait_queue_exclusive(
                    &op->io_completion_waitq, &wait_entry);

            spin_lock(&op->lock);
            while (op->io_completed == 0)
            {
                set_current_state(TASK_INTERRUPTIBLE);
                /* We don't need to wait if client-daemon did not get a reference to op */
                if (!op_wait(op))
                    break;
                /*
                 * There may be a window if the client-daemon has acquired a reference
                 * to op, but not a spin-lock on it yet before which the async
                 * canceller (i.e. this piece of code) acquires the same. 
                 * Consequently we may end up with a
                 * race. To prevent that we use the aio_ref_cnt counter. 
                 */
                spin_unlock(&op->lock);
                if (!signal_pending(current))
                {
                    int timeout = MSECS_TO_JIFFIES(1000 * op_timeout_secs);
                    if (!schedule_timeout(timeout))
                    {
                        pvfs2_print("Timed out on I/O cancellation - aborting\n");
                        timed_out_or_signal = 1;
                        spin_lock(&op->lock);
                        break;
                    }
                    spin_lock(&op->lock);
                    continue;
                }
                pvfs2_print("signal on Async I/O cancellation - aborting\n");
                timed_out_or_signal = 1;
                spin_lock(&op->lock);
                break;
            }
            set_current_state(TASK_RUNNING);
            remove_wait_queue(&op->io_completion_waitq, &wait_entry);

        } while (0);

        /* We need to fill up event->res and event->res2 if at all */
        if (op->op_state == PVFS2_VFS_STATE_SERVICED)
        {
            op->priv = NULL;
            spin_unlock(&op->lock);
            event->res = x->bytes_copied;
            event->res2 = 0;
        }
        else if (op->op_state == PVFS2_VFS_STATE_INPROGR)
        {
            op->priv = NULL;
            spin_unlock(&op->lock);
            pvfs2_print("Trying to cancel operation in "
                    " progress %ld\n", (unsigned long) op->tag);
            /* 
             * if operation is in progress we need to send 
             * a cancellation upcall for this tag 
             * The return value of that is the cancellation
             * event return value.
             */
            event->res = pvfs2_cancel_op_in_progress(op->tag);
            event->res2 = 0;
        }
        else 
        {
            op->priv = NULL;
            spin_unlock(&op->lock);
            event->res = -EINTR;
            event->res2 = 0;
        }
        /*
         * Drop the buffer pool index
         */
        if (x->buffer_index >= 0)
        {
            pvfs2_print("pvfs2_aio_cancel: put bufmap_index "
                    " %d\n", x->buffer_index);
            pvfs_bufmap_put(x->buffer_index);
            x->buffer_index = -1;
        }
        /* 
         * Put reference to op twice,
         * once for the reader/writer that initiated 
         * the op and 
         * once for the cancel 
         */
        put_op(op);
        put_op(op);
        x->needs_cleanup = 0;
        /*
         * This seems to be a weird undocumented 
         * thing, where the cancel routine is expected
         * to manually decrement ki_users field!
         * before calling aio_put_req().
         */
        iocb->ki_users--;
        ret = aio_put_req(iocb);
        /* x is itself deallocated by the destructor */
        return 0;
    }
}

/* 
 * Destructor is called when the kiocb structure is 
 * about to be deallocated by the AIO core.
 *
 * Conceivably, this could be moved onto pvfs2-cache.c
 * as the kiocb_dtor() function that can be associated
 * with the pvfs2_kiocb object. 
 */
static void pvfs2_aio_dtor(struct kiocb *iocb)
{
    pvfs2_kiocb *x = iocb->private;
    if (x && x->needs_cleanup == 1)
    {
        /* do a cleanup of the buffers and possibly op */
        if (x->buffer_index >= 0)
        {
            pvfs2_print("pvfs2_aio_dtor: put bufmap_index "
                    " %d\n", x->buffer_index);
            pvfs_bufmap_put(x->buffer_index);
            x->buffer_index = -1;
        }
        if (x->op) 
        {
            x->op->priv = NULL;
            put_op(x->op);
        }
        x->needs_cleanup = 0;
    }
    pvfs2_print("pvfs2_aio_dtor: kiocb_release %p\n", x);
    kiocb_release(x);
    iocb->private = NULL;
    return;
}

static inline void 
fill_default_kiocb(pvfs2_kiocb *x,
        struct task_struct *tsk,
        struct kiocb *iocb, int rw,
        int buffer_index, pvfs2_kernel_op_t *op, 
        void __user *buffer,
        loff_t offset, size_t count,
        int (*aio_cancel)(struct kiocb *, struct io_event *))
{
    x->tsk = tsk;
    x->kiocb = iocb;
    x->buffer_index = buffer_index;
    x->op = op;
    x->rw = rw;
    x->buffer = buffer;
    x->bytes_to_be_copied = count;
    x->offset = offset;
    x->bytes_copied = 0;
    x->needs_cleanup = 1;
    iocb->ki_cancel = aio_cancel;
    return;
}
/*
 * This function will do the following,
 * On an error, it returns a -ve error number.
 * For a synchronous iocb, we copy the data into the 
 * user buffer's before returning and
 * the count of how much was actually read.
 * For a first-time asynchronous iocb, we submit the 
 * I/O to the client-daemon and do not wait
 * for the matching downcall to be written and we 
 * return a special -EIOCBQUEUED
 * to indicate that we have queued the request.
 * NOTE: Unlike typical aio requests
 * that get completion notification from interrupt
 * context, we get completion notification from a process
 * context (i.e. the client daemon).
 */
static ssize_t 
pvfs2_file_aio_read(struct kiocb *iocb, char __user *buffer,
        size_t count, loff_t offset)
{
    struct file *filp = NULL;
    struct inode *inode = NULL;
    ssize_t error = -EINVAL;

    if (count == 0)
    {
        return 0;
    }
    if (iocb->ki_pos != offset)
    {
        return -EINVAL;
    }
    if (unlikely(((ssize_t)count)) < 0)
    {
        return -EINVAL;
    }
    if (access_ok(VERIFY_WRITE, buffer, count) == 0)
    {
        return -EFAULT;
    }
    /* Each I/O operation is not allowed to be greater than our block size */
    if (count > pvfs_bufmap_size_query())
    {
        pvfs2_error("aio_read: cannot transfer (%d) bytes"
                " (larger than block size %d)\n",
                (int) count, pvfs_bufmap_size_query());
        return -EINVAL;
    }
    filp = iocb->ki_filp;
    error = -EINVAL;
    if (filp && filp->f_mapping 
             && (inode = filp->f_mapping->host))
    {
        int dc_status = 0;
        ssize_t ret = 0;
        pvfs2_kiocb *x = NULL;

        /* First time submission */
        if ((x = (pvfs2_kiocb *) iocb->private) == NULL)
        {
            int buffer_index = -1, error_exit = 0;
            int retries = PVFS2_OP_RETRY_COUNT;
            pvfs2_kernel_op_t *new_op = NULL;
            pvfs2_kiocb pvfs_kiocb;
            char __user *current_buf = buffer;
            pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
            
            new_op = op_alloc();
            if (!new_op)
            {
                error = -ENOMEM;
                goto out_error;
            }
            /* Increase ref count */
            get_op(new_op);
            new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
            /* (A)synchronous I/O */
            new_op->upcall.req.io.async_vfs_io = 
                is_sync_kiocb(iocb) ? PVFS_VFS_SYNC_IO 
                                    : PVFS_VFS_ASYNC_IO;
            new_op->upcall.req.io.readahead_size = 0;
            new_op->upcall.req.io.io_type = PVFS_IO_READ;
            new_op->upcall.req.io.refn = pvfs2_inode->refn;
            error = pvfs_bufmap_get(&buffer_index);
            if (error < 0)
            {
                pvfs2_error("pvfs2_file_aio_read: pvfs_bufmap_get() "
                        " failure %d\n", (int) ret);
                /* drop ref count and possibly de-allocate */
                put_op(new_op);
                goto out_error;
            }
            pvfs2_print("pvfs2_file_aio_read: pvfs_bufmap_get %d\n",
                    buffer_index);
            new_op->upcall.req.io.buf_index = buffer_index;
            new_op->upcall.req.io.count = count;
            new_op->upcall.req.io.offset = offset;
            dc_status = 0;
            /*
             * if it is a synchronous operation, we
             * don't allocate anything here
             */
            if (is_sync_kiocb(iocb))
            {
                x = &pvfs_kiocb;
            }
            else /* asynchronous iocb */
            {
                x = kiocb_alloc();
                if (x == NULL)
                {
                    error = -ENOMEM;
                    /* drop the buffer index */
                    pvfs_bufmap_put(buffer_index);
                    pvfs2_print("pvfs2_file_aio_read: pvfs_bufmap_put %d\n",
                            buffer_index);
                    /* drop the reference count and deallocate */
                    put_op(new_op);
                    goto out_error;
                }
                pvfs2_print("kiocb_alloc: %p\n", x);
                /* 
                 * destructor function to make sure that we free
                 * up this allocated piece of memory 
                 */
                iocb->ki_dtor = pvfs2_aio_dtor;
            }
            /* If user requested synchronous type of operation */
            if (is_sync_kiocb(iocb))
            {
                /* 
                 * Stage the operation!
                 * On an error, macro jumps to error_exit 
                 */
                service_error_exit_op_with_timeout_retry(
                        new_op, "pvfs2_file_aio_read", retries, error_exit, 
                        get_interruptible_flag(inode));
                if (new_op->downcall.status != 0)
                {
                    dc_status = new_op->downcall.status;
            error_exit:
                    handle_sync_aio_error();
                    /*
                      don't write an error to syslog on signaled operation
                      termination unless we've got debugging turned on, as
                      this can happen regularly (i.e. ctrl-c)
                    */
                    if ((error_exit != 0) && (ret == -EINTR))
                    {
                        pvfs2_print("pvfs2_file_aio_read: returning error %d " 
                                "(error_exit=%d)\n", (int) ret, error_exit);
                    }
                    else
                    {
                        pvfs2_error(
                            "pvfs2_file_aio_read: error reading from "
                            " handle %llu, "
                            "\n  -- downcall status is %d, returning %d "
                            "(error_exit=%d)\n",
                            llu(pvfs2_ino_to_handle(inode->i_ino)),
                            dc_status, (int) ret, error_exit);
                    }
                    error = ret;
                    goto out_error;
                }
                /* copy data out to destination */
                if (new_op->downcall.resp.io.amt_complete)
                {
                    ret = pvfs_bufmap_copy_to_user(
                            current_buf, buffer_index, 
                            new_op->downcall.resp.io.amt_complete);
                }
                if (ret)
                {
                    pvfs2_print("Failed to copy user buffer %d\n", (int) ret);
                    new_op->downcall.status = -PVFS_EFAULT;
                    /* error is set in the goto target */
                    goto error_exit;
                }
                error = new_op->downcall.resp.io.amt_complete;
                wake_up_device_for_return(new_op);
                pvfs_bufmap_put(buffer_index);
                pvfs2_print("pvfs2_file_aio_read: pvfs_bufmap_put %d\n",
                        buffer_index);
                /* new_op is freed by the client-daemon */
                goto out_error;
            }
            else
            {
                /* 
                 * We need to set the cancellation callbacks + 
                 * other state information
                 * here if the asynchronous request is going to
                 * be successfully submitted 
                 */
                fill_default_kiocb(x, current, iocb, PVFS_IO_READ,
                        buffer_index, new_op, current_buf,
                        offset, count,
                        &pvfs2_aio_cancel);
                /*
                 * We need to be able to retrieve this structure from
                 * the op structure as well, since the client-daemon
                 * needs to send notifications upon aio_completion.
                 */
                new_op->priv = x;
                /* and stash it away in the kiocb structure as well */
                iocb->private = x;
                /*
                 * Add it to the list of ops to be serviced
                 * but don't wait for it to be serviced. 
                 * Return immediately 
                 */
                service_async_vfs_op(new_op);
                pvfs2_print("pvfs2_file_aio_read: queued "
                        " read operation [%ld for %d]\n",
                            (unsigned long) offset, (int) count);
                error = -EIOCBQUEUED;
                /*
                 * All cleanups done upon completion
                 * (OR) cancellation!
                 */
            }
        }
        /* I don't think this path will ever be taken */
        else { /* retry and see what is the status! */
            error = pvfs2_aio_retry(iocb);
        }
    }
out_error:
    return error;
}

/*
 * This function will do the following,
 * On an error, it returns a -ve error number.
 * For a synchronous iocb, we copy the user's data into the 
 * buffers before returning and
 * the count of how much was actually written.
 * For a first-time asynchronous iocb, we copy the user's
 * data into the buffers before submitting the 
 * I/O to the client-daemon and do not wait
 * for the matching downcall to be written and we 
 * return a special -EIOCBQUEUED
 * to indicate that we have queued the request.
 * NOTE: Unlike typical aio requests
 * that get completion notification from interrupt
 * context, we get completion notification from a process
 * context (i.e. the client daemon). 
 */
static ssize_t 
pvfs2_file_aio_write(struct kiocb *iocb, const char __user *buffer,
        size_t count, loff_t offset)
{
    struct file *filp = NULL;
    struct inode *inode = NULL;
    ssize_t error = -EINVAL;

    if (count == 0)
    {
        return 0;
    }
    if (iocb->ki_pos != offset)
    {
        return -EINVAL;
    }
    if (unlikely(((ssize_t)count)) < 0)
    {
        return -EINVAL;
    }
    if (access_ok(VERIFY_READ, buffer, count) == 0)
    {
        return -EFAULT;
    }
    filp = iocb->ki_filp;
    if (filp && filp->f_mapping 
             && (inode = filp->f_mapping->host))
    {
        int ret;
        /* perform generic linux kernel tests for 
         * sanity of write arguments 
         * NOTE: this is particularly helpful in 
         * handling fsize rlimit properly 
         */
#ifdef PVFS2_LINUX_KERNEL_2_4
        ret = pvfs2_precheck_file_write(filp, inode, &count, offset);
#else
        ret = generic_write_checks(filp, &offset, &count,
                S_ISBLK(inode->i_mode));
#endif
        if (ret != 0 || count == 0)
        {
            pvfs2_error("pvfs2_file_aio_write: failed generic "
                    " argument checks.\n");
            return(ret);
        }
    }
    /* Each I/O operation is not allowed to be greater than our block size */
    if (count > pvfs_bufmap_size_query())
    {
        pvfs2_error("aio_write: cannot transfer (%d) bytes"
                " (larger than block size %d)\n",
                (int) count, pvfs_bufmap_size_query());
        return -EINVAL;
    }
    error = -EINVAL;
    if (filp && inode)
    {
        int dc_status = 0;
        ssize_t ret = 0;
        pvfs2_kiocb *x = NULL;

        /* First time submission */
        if ((x = (pvfs2_kiocb *) iocb->private) == NULL)
        {
            int buffer_index = -1;
            int retries = PVFS2_OP_RETRY_COUNT, error_exit = 0;
            pvfs2_kernel_op_t *new_op = NULL;
            pvfs2_kiocb pvfs_kiocb;
            char __user *current_buf = (char *) buffer;
            pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
            
            new_op = op_alloc();
            if (!new_op)
            {
                error = -ENOMEM;
                goto out_error;
            }
            /* Increase ref count */
            get_op(new_op);
            new_op->upcall.type = PVFS2_VFS_OP_FILE_IO;
            /* (A)synchronous I/O */
            new_op->upcall.req.io.async_vfs_io = 
                is_sync_kiocb(iocb) ? PVFS_VFS_SYNC_IO 
                                    : PVFS_VFS_ASYNC_IO;
            new_op->upcall.req.io.io_type = PVFS_IO_WRITE;
            new_op->upcall.req.io.refn = pvfs2_inode->refn;
            error = pvfs_bufmap_get(&buffer_index);
            if (error < 0)
            {
                pvfs2_error("pvfs2_file_aio_write: pvfs_bufmap_get()"
                        " failure %d\n", (int) ret);
                /* drop ref count and possibly de-allocate */
                put_op(new_op);
                goto out_error;
            }
            pvfs2_print("pvfs2_file_aio_write: pvfs_bufmap_put %d\n",
                    buffer_index);
            new_op->upcall.req.io.buf_index = buffer_index;
            new_op->upcall.req.io.count = count;
            new_op->upcall.req.io.offset = offset;
            /* 
             * copy the data from the application. 
             * Should this be done here even for async I/O? 
             * We could return -EIOCBRETRY here and have 
             * the data copied in the pvfs2_aio_retry routine,
             * I think. But I dont see the point in doing that...
             */
            error = pvfs_bufmap_copy_from_user(
                    buffer_index, current_buf, count);
            if (error < 0)
            {
                pvfs2_print("Failed to copy user buffer %d\n", (int) ret);
                /* drop the buffer index */
                pvfs_bufmap_put(buffer_index);
                pvfs2_print("pvfs2_file_aio_read: pvfs_bufmap_put %d\n",
                        buffer_index);
                /* drop the reference count and deallocate */
                put_op(new_op);
                goto out_error;
            }

            dc_status = 0;
            /* 
             * if it is a synchronous operation, we
             * don't allocate anything here 
             */
            if (is_sync_kiocb(iocb))
            {
                x = &pvfs_kiocb;
            }
            else /* asynchronous iocb */
            {
                x = kiocb_alloc();
                if (x == NULL)
                {
                    error = -ENOMEM;
                    /* drop the buffer index */
                    pvfs_bufmap_put(buffer_index);
                    pvfs2_print("pvfs2_file_aio_read: pvfs_bufmap_put %d\n",
                            buffer_index);
                    /* drop the reference count and deallocate */
                    put_op(new_op);
                    goto out_error;
                }
                pvfs2_print("kiocb_alloc: %p\n", x);
                /* 
                 * destructor function to make sure that we free 
                 * up this allocated piece of memory 
                 */
                iocb->ki_dtor = pvfs2_aio_dtor;
            }
            /* If user requested synchronous type of operation */
            if (is_sync_kiocb(iocb))
            {
                /*
                 * Stage the operation!
                 * On an error, macro jumps to error_exit 
                 */
                service_error_exit_op_with_timeout_retry(
                        new_op, "pvfs2_file_aio_write", retries, error_exit, 
                        get_interruptible_flag(inode));
                if (new_op->downcall.status != 0)
                {
                    dc_status = new_op->downcall.status;
            error_exit:
                    handle_sync_aio_error();
                    /*
                      don't write an error to syslog on signaled operation
                      termination unless we've got debugging turned on, as
                      this can happen regularly (i.e. ctrl-c)
                    */
                    if ((error_exit != 0) && (ret == -EINTR))
                    {
                        pvfs2_print("pvfs2_file_aio_write: returning error %d " 
                                "(error_exit=%d)\n", (int) ret, error_exit);
                    }
                    else
                    {
                        pvfs2_error(
                            "pvfs2_file_aio_write: error writing to "
                            " handle %llu, "
                            "FILE: %s\n  -- "
                            "downcall status is %d, returning %d "
                            "(error_exit=%d)\n",
                            llu(pvfs2_ino_to_handle(inode->i_ino)),
                            (filp && filp->f_dentry 
                             && filp->f_dentry->d_name.name ?
                             (char *)filp->f_dentry->d_name.name : "UNKNOWN"),
                            dc_status, (int) ret, error_exit);
                    }
                    error = ret;
                    goto out_error;
                }
                error = new_op->downcall.resp.io.amt_complete;
                wake_up_device_for_return(new_op);
                pvfs_bufmap_put(buffer_index);
                pvfs2_print("pvfs2_file_aio_read: pvfs_bufmap_put %d\n",
                        (int) buffer_index);
                if (error > 0)
                {
                    update_atime(inode);
                }
                /* new_op is freed by the client-daemon */
                goto out_error;
            }
            else
            {
                /* 
                 * We need to set the cancellation callbacks + 
                 * other state information
                 * here if the asynchronous request is going to
                 * be successfully submitted 
                 */
                fill_default_kiocb(x, current, iocb, PVFS_IO_WRITE,
                        buffer_index, new_op, current_buf,
                        offset, count,
                        &pvfs2_aio_cancel);
                /*
                 * We need to be able to retrieve this structure from
                 * the op structure as well, since the client-daemon
                 * needs to send notifications upon aio_completion.
                 */
                new_op->priv = x;
                /* and stash it away in the kiocb structure as well */
                iocb->private = x;
                /*
                 * Add it to the list of ops to be serviced
                 * but don't wait for it to be serviced. 
                 * Return immediately 
                 */
                service_async_vfs_op(new_op);
                pvfs2_print("pvfs2_file_aio_write: queued "
                        " write operation [%ld for %d]\n",
                            (unsigned long) offset, (int) count);
                error = -EIOCBQUEUED;
                /*
                 * All cleanups done upon completion
                 * (OR) cancellation!
                 */
            }
        }
        /* I don't think this path is ever taken */
        else { /* retry and see what is the status! */
            error = pvfs2_aio_retry(iocb);
        }
    }
out_error:
    return error;
}

#endif

/** Perform a miscellaneous operation on a file.
 */
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

/** Memory map a region of a file.
 */
static int pvfs2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct inode *inode = file->f_dentry->d_inode;

    pvfs2_print("pvfs2_file_mmap: called on %s\n",
                (file ? (char *)file->f_dentry->d_name.name :
                 (char *)"Unknown"));

    /* we don't support mmap writes, or SHARED mmaps at all */
    if ((vma->vm_flags & VM_SHARED) || (vma->vm_flags & VM_MAYSHARE))
    {
        return -EINVAL;
    }

    /*
      for mmap on pvfs2, make sure we use pvfs2 specific address
      operations by explcitly setting the operations
    */
    inode->i_mapping->host = inode;
    inode->i_mapping->a_ops = &pvfs2_address_operations;

    /* set the sequential readahead hint */
    vma->vm_flags |= VM_SEQ_READ;
    vma->vm_flags &= ~VM_RAND_READ;

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

/** Called to notify the module that there are no more references to
 *  this file (i.e. no processes have it open).
 *
 *  \note Not called when each file is closed.
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

/** Push all data for a specific file onto permanent storage.
 */
int pvfs2_fsync(
    struct file *file,
    struct dentry *dentry,
    int datasync)
{
    int ret = -EINVAL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(file->f_dentry->d_inode);
    pvfs2_kernel_op_t *new_op = NULL;

    new_op = op_alloc();
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.type = PVFS2_VFS_OP_FSYNC;
    new_op->upcall.req.fsync.refn = pvfs2_inode->refn;

    service_operation(new_op, "pvfs2_fsync",
                      get_interruptible_flag(file->f_dentry->d_inode));

    ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

    pvfs2_print("pvfs2_fsync got return value of %d\n",ret);

  error_exit:
    translate_error_if_wait_failed(ret, 0, 0);
    op_release(new_op);

    return ret;
}

/** Change the file pointer position for an instance of an open file.
 *
 *  \note If .llseek is overriden, we must acquire lock as described in
 *        Documentation/filesystems/Locking.
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
        /* revalidate the inode's file size. 
         * NOTE: We are only interested in file size here, so we set mask accordingly 
         */
        ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_SIZE);
        if (ret)
        {
            pvfs2_make_bad_inode(inode);
            return ret;
        }
    }

    pvfs2_print("pvfs2_file_llseek: offset is %ld | origin is %d | "
                "inode size is %lu\n", (long)offset, origin,
                (unsigned long)file->f_dentry->d_inode->i_size);

    return generic_file_llseek(file, offset, origin);
}

/*
 * Apache uses the sendfile system call to stuff page-sized file data to 
 * a socket. Unfortunately, the generic_sendfile function exported by
 * the kernel uses the page-cache and does I/O in pagesize granularities
 * and this leads to undesirable consistency problems not to mention performance
 * limitations.
 * Consequently, we chose to override the default callback by bypassing the page-cache.
 * Although, we could read larger than page-sized buffers from the file,
 * the actor routine does not know how to handle > 1 page buffer at a time.
 * So we still end up breaking things down. darn...
 */
#ifdef HAVE_SENDFILE_VFS_SUPPORT

static void do_bypass_page_cache_read(struct file *filp, loff_t *ppos, 
        read_descriptor_t *desc, read_actor_t actor)
{
    struct inode *inode = NULL;
    struct address_space *mapping = NULL;
    struct page *uncached_page = NULL;
    unsigned long kaddr = 0;
    unsigned long offset;
    loff_t isize;
    unsigned long begin_index, end_index;
    long prev_index;
    int to_free = 0;

    mapping = filp->f_mapping;
    inode   = mapping->host;
    /* offset in file in terms of page_cache_size */
    begin_index = *ppos >> PAGE_CACHE_SHIFT; 
    offset = *ppos & ~PAGE_CACHE_MASK;

    isize = i_size_read(inode);
    if (!isize)
    {
        return;
    }
    end_index = (isize - 1) >> PAGE_CACHE_SHIFT;
    prev_index = -1;
    /* copy page-sized units at a time using the actor routine */
    for (;;)
    {
        unsigned long nr, ret, error;

        /* Are we reading beyond what exists */
        if (begin_index > end_index)
        {
            break;
        }
        /* issue a file-system read call to fill this buffer which is in kernel space */
        if (prev_index != begin_index)
        {
            loff_t file_offset;
            file_offset = (begin_index << PAGE_CACHE_SHIFT);
            /* Allocate a page, but don't add it to the pagecache proper */
            kaddr = __get_free_page(mapping_gfp_mask(mapping));
            if (kaddr == 0UL)
            {
                desc->error = -ENOMEM;
                break;
            }
            to_free = 1;
            uncached_page = virt_to_page(kaddr);
            pvfs2_print("begin_index = %lu offset = %lu file_offset = %ld\n",
                    (unsigned long) begin_index, (unsigned long) offset, (unsigned long)file_offset);

            error = pvfs2_inode_read(inode, (void *) kaddr, PAGE_CACHE_SIZE, &file_offset, 0, 0);
            prev_index = begin_index;
        }
        else {
            error = 0;
        }
        /*
         * In the unlikely event of an error, bail out 
         */
        if (unlikely(error < 0))
        {
            desc->error = error;
            break;
        }
        /* nr is the maximum amount of bytes to be copied from this page */
        nr = PAGE_CACHE_SIZE;
        if (begin_index >= end_index)
        {
            if (begin_index > end_index)
            {
                break;
            }
            /* Adjust the number of bytes on the last page */
            nr = ((isize - 1) & ~PAGE_CACHE_MASK) + 1;
            /* Do we have fewer valid bytes in the file than what was requested? */
            if (nr <= offset)
            {
                break;
            }
        }
        nr = nr - offset;

        ret = actor(desc, uncached_page, offset, nr);
        pvfs2_print("actor with offset %lu nr %lu return %lu desc->count %lu\n", 
                (unsigned long) offset, (unsigned long) nr, (unsigned long) ret, (unsigned long) desc->count);

        offset += ret;
        begin_index += (offset >> PAGE_CACHE_SHIFT);
        offset &= ~PAGE_CACHE_MASK;
        if (to_free == 1)
        {
            free_page(kaddr);
            to_free = 0;
        }
        if (ret == nr && desc->count)
            continue;
        break;
    }
    if (to_free == 1)
    {
        free_page(kaddr);
        to_free = 0;
    }
    *ppos = (begin_index << PAGE_CACHE_SHIFT) + offset;
    file_accessed(filp);
    return;
}

static ssize_t pvfs2_sendfile(struct file *filp, loff_t *ppos,
        size_t count, read_actor_t actor, void *target)
{
    int error;
    read_descriptor_t desc;
    desc.written = 0;
    desc.count = count;
    desc.arg.data = target;
    desc.error = 0;

    /*
     * Revalidate the inode so that i_size_read will 
     * return the appropriate size 
     */
    if ((error = pvfs2_inode_getattr(filp->f_mapping->host, PVFS_ATTR_SYS_SIZE)) < 0)
    {
        return error;
    }

    /* Do a blocking read from the file and invoke the actor appropriately */
    do_bypass_page_cache_read(filp, ppos, &desc, actor);
    if (desc.written)
        return desc.written;
    return desc.error;
}

#endif

/** PVFS2 implementation of VFS file operations */
struct file_operations pvfs2_file_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    llseek : pvfs2_file_llseek,
    read : pvfs2_file_read,
    write : pvfs2_file_write,
    readv : pvfs2_file_readv,
    writev : pvfs2_file_writev,
    ioctl : pvfs2_ioctl,
    mmap : pvfs2_file_mmap,
    open : pvfs2_file_open,
    release : pvfs2_file_release,
    fsync : pvfs2_fsync
#else
    .llseek = pvfs2_file_llseek,
    .read = pvfs2_file_read,
    .write = pvfs2_file_write,
    .readv = pvfs2_file_readv,
    .writev = pvfs2_file_writev,
#ifdef HAVE_AIO_VFS_SUPPORT
    .aio_read = pvfs2_file_aio_read,
    .aio_write = pvfs2_file_aio_write,
#endif
    .ioctl = pvfs2_ioctl,
    .mmap = pvfs2_file_mmap,
    .open = pvfs2_file_open,
    .release = pvfs2_file_release,
    .fsync = pvfs2_fsync,
#ifdef HAVE_SENDFILE_VFS_SUPPORT
    .sendfile = pvfs2_sendfile,
#endif
#endif
};

#ifdef PVFS2_LINUX_KERNEL_2_4
/*
 * pvfs2_precheck_file_write():
 * Check the conditions on a file descriptor prior to beginning a write
 * on it.  Contains the common precheck code for both buffered and direct
 * IO.
 *
 * NOTE: this function is a modified version of precheck_file_write() from
 * 2.4.x.  precheck_file_write() is not exported so we are forced to
 * duplicate it here.
 */
static int pvfs2_precheck_file_write(struct file *file, struct inode *inode,
    size_t *count, loff_t *ppos)
{
    ssize_t       err;
    unsigned long limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
    loff_t        pos = *ppos;
    
    err = -EINVAL;
    if (pos < 0)
        goto out;

    err = file->f_error;
    if (err) {
        file->f_error = 0;
        goto out;
    }

    /* FIXME: this is for backwards compatibility with 2.4 */
    if (!S_ISBLK(inode->i_mode) && (file->f_flags & O_APPEND))
        *ppos = pos = inode->i_size;

    /*
     * Check whether we've reached the file size limit.
     */
    err = -EFBIG;
    
    if (!S_ISBLK(inode->i_mode) && limit != RLIM_INFINITY) {
        if (pos >= limit) {
            send_sig(SIGXFSZ, current, 0);
            goto out;
        }
        if (pos > 0xFFFFFFFFULL || *count > limit - (u32)pos) {
            /* send_sig(SIGXFSZ, current, 0); */
            *count = limit - (u32)pos;
        }
    }

    /*
     *    LFS rule 
     */
    if ( pos + *count > MAX_NON_LFS && !(file->f_flags&O_LARGEFILE)) {
        if (pos >= MAX_NON_LFS) {
            send_sig(SIGXFSZ, current, 0);
            goto out;
        }
        if (*count > MAX_NON_LFS - (u32)pos) {
            /* send_sig(SIGXFSZ, current, 0); */
            *count = MAX_NON_LFS - (u32)pos;
        }
    }

    /*
     *    Are we about to exceed the fs block limit ?
     *
     *    If we have written data it becomes a short write
     *    If we have exceeded without writing data we send
     *    a signal and give them an EFBIG.
     *
     *    Linus frestrict idea will clean these up nicely..
     */
     
    if (!S_ISBLK(inode->i_mode)) {
        if (pos >= inode->i_sb->s_maxbytes)
        {
            if (*count || pos > inode->i_sb->s_maxbytes) {
                send_sig(SIGXFSZ, current, 0);
                err = -EFBIG;
                goto out;
            }
            /* zero-length writes at ->s_maxbytes are OK */
        }

        if (pos + *count > inode->i_sb->s_maxbytes)
            *count = inode->i_sb->s_maxbytes - pos;
    } else {
        if (is_read_only(inode->i_rdev)) {
            err = -EPERM;
            goto out;
        }
        if (pos >= inode->i_size) {
            if (*count || pos > inode->i_size) {
                err = -ENOSPC;
                goto out;
            }
        }

        if (pos + *count > inode->i_size)
            *count = inode->i_size - pos;
    }

    err = 0;
out:
    return err;
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

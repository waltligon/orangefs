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

enum io_type {
    IO_READ = 0,
    IO_WRITE = 1,
    IO_READV = 0,
    IO_WRITEV = 1,
    IO_READX = 0,
    IO_WRITEX = 1,
};

#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_precheck_file_write(struct file *file, struct inode *inode,
    size_t *count, loff_t *ppos);
#endif

#define wake_up_daemon_for_return(op)             \
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

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_open: called on %s (inode is %llu)\n",
                file->f_dentry->d_name.name, llu(get_handle_from_ino(inode)));

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
                gossip_debug(GOSSIP_FILE_DEBUG, "f_pos = %ld\n", (unsigned long)file->f_pos);
            }
            else
            {
                pvfs2_make_bad_inode(inode);
                gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_open returning error: %d\n", ret);
                return(ret);
            }
        }

        /*
          fs/open.c: returns 0 after enforcing large file support if
          running on a 32 bit system w/o O_LARGFILE flag
        */
        ret = generic_file_open(inode, file);
    }

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_open returning normally: %d\n", ret);
    return ret;
}

enum dest_type {
    COPY_TO_ADDRESSES = 0,
    COPY_TO_PAGES     = 1
};

struct rw_options {
    /* whether or not it is a synchronous I/O operation */
    int            async;
    /* whether it is a READ/WRITE operation */
    enum io_type   type; 
    /* whether we are copying to addresses/pages */
    enum dest_type copy_dest_type; 
    loff_t        *offset;
    struct file   *file;
    struct inode  *inode;
    pvfs2_inode_t *pvfs2_inode;
    loff_t readahead_size;
    /* whether the destination addresses are in user/kernel */
    int copy_to_user;
    const char *fnstr;
    /* Asynch I/O control block */
    struct kiocb *iocb;
    union {
        struct {
            struct iovec *iov;
            unsigned long nr_segs;
        } address;
        struct {
            struct page  **pages;
            unsigned long nr_pages;
        } pages;
    } dest;
};

/*
 * Post and wait for the I/O upcall to finish
 * @rw - contains state information to initiate the I/O operation
 * @vec- contains the memory vector regions 
 * @nr_segs - number of memory vector regions
 * @total_size - total expected size of the I/O operation
 */
static ssize_t wait_for_io(struct rw_options *rw, struct iovec *vec,
        int nr_segs, size_t total_size)
{
    pvfs2_kernel_op_t *new_op = NULL;
    int buffer_index = -1;
    ssize_t ret;

    if (!rw || !vec || nr_segs < 0 || total_size <= 0 
            || !rw->pvfs2_inode || !rw->inode || !rw->fnstr)
    {
        gossip_lerr("invalid parameters (rw: %p, vec: %p, nr_segs: %d, "
                "total_size: %zd)\n", rw, vec, nr_segs, total_size);
        ret = -EINVAL;
        goto out;
    }
    new_op = op_alloc(PVFS2_VFS_OP_FILE_IO);
    if (!new_op)
    {
        ret = -ENOMEM;
        goto out;
    }
    /* synchronous I/O */
    new_op->upcall.req.io.async_vfs_io = PVFS_VFS_SYNC_IO; 
    new_op->upcall.req.io.readahead_size = (int32_t) rw->readahead_size;
    new_op->upcall.req.io.io_type = 
        (rw->type == IO_READV) ? PVFS_IO_READ : PVFS_IO_WRITE;
    new_op->upcall.req.io.refn = rw->pvfs2_inode->refn;
    /* get a shared buffer index */
    ret = pvfs_bufmap_get(&buffer_index);
    if (ret < 0)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_get failure (%ld)\n",
                rw->fnstr, (long) ret);
        goto out;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "GET op %p -> buffer_index %d\n", new_op, buffer_index);

    new_op->upcall.req.io.buf_index = buffer_index;
    new_op->upcall.req.io.count = total_size;
    new_op->upcall.req.io.offset = *(rw->offset);
    if (rw->type == IO_WRITEV)
    {
        /* 
         * copy data from application/kernel by pulling it out 
         * of the iovec.
         */
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: copy_to_user %d nr_segs %u, "
                "offset: %llu total_size: %zd\n", rw->fnstr, rw->copy_to_user, 
                nr_segs, llu(*(rw->offset)), total_size);
        if (rw->copy_to_user)
        {
            ret = pvfs_bufmap_copy_iovec_from_user(
                    buffer_index, vec, nr_segs, total_size);
        }
        else {
            ret = pvfs_bufmap_copy_iovec_from_kernel(
                    buffer_index, vec, nr_segs, total_size);
        }
        if (ret < 0)
        {
            gossip_lerr("Failed to copy-in buffers. Please make sure "
                        "that the pvfs2-client is running. %ld\n", 
                        (long) ret);
            goto out;
        }
    }
    ret = service_operation(new_op, rw->fnstr,
         get_interruptible_flag(rw->inode));

    if (ret < 0)
    {
          /* this macro is defined in pvfs2-kernel.h */
          handle_io_error();

          /*
            don't write an error to syslog on signaled operation
            termination unless we've got debugging turned on, as
            this can happen regularly (i.e. ctrl-c)
          */
          if (ret == -EINTR)
          {
              gossip_debug(GOSSIP_FILE_DEBUG, "%s: returning error %ld\n", 
                      rw->fnstr, (long) ret);
          }
          else
          {
              gossip_err(
                    "%s: error in %s handle %llu, "
                    "FILE: %s\n  -- returning %ld\n",
                    rw->fnstr, 
                    rw->type == IO_READV ? "vectored read from" : "vectored write to",
                    llu(get_handle_from_ino(rw->inode)),
                    (rw->file && rw->file->f_dentry && rw->file->f_dentry->d_name.name ?
                     (char *)rw->file->f_dentry->d_name.name : "UNKNOWN"),
                    (long) ret);
          }
          goto out;
    }

    if (rw->type == IO_READV)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: nr_segs %u, offset: %llu total_size:%zd\n",
            rw->fnstr, nr_segs, *(rw->offset), total_size);
        /*
         * copy data to application/kernel by pushing it out to the iovec.
         */
        if (new_op->downcall.resp.io.amt_complete)
        {
            if (rw->copy_to_user)
            {
                ret = pvfs_bufmap_copy_to_user_iovec(buffer_index, vec, 
                        nr_segs, new_op->downcall.resp.io.amt_complete);
            }
            else
            {
                ret = pvfs_bufmap_copy_to_kernel_iovec(buffer_index, vec,
                        nr_segs, new_op->downcall.resp.io.amt_complete);
            }
            if (ret < 0)
            {
                gossip_lerr("Failed to copy-out buffers.  Please make sure "
                            "that the pvfs2-client is running (%ld)\n", (long) ret);
                /* put error codes in downcall so that handle_io_error()
                 * preserves it properly */
                new_op->downcall.status = ret;
                handle_io_error();
                goto out;
            }
        }
    }
    ret = new_op->downcall.resp.io.amt_complete;
    /*
      tell the device file owner waiting on I/O that this read has
      completed and it can return now.  in this exact case, on
      wakeup the daemon will free the op, so we *cannot* touch it
      after this.
    */
    wake_up_daemon_for_return(new_op);
    new_op = NULL;
out:
    if (buffer_index >= 0)
    {
        pvfs_bufmap_put(buffer_index);
        gossip_debug(GOSSIP_FILE_DEBUG, "PUT buffer_index %d\n", buffer_index);
        buffer_index = -1;
    }
    if (new_op) 
    {
        op_release(new_op);
        new_op = NULL;
    }
    return ret;
}

/*
 * The reason we need to do this is to be able to support 
 * readv and writev that are
 * larger than PVFS_DEFAULT_DESC_SIZE (4 MB). What that means is that
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
        gossip_err("Invalid parameters to split_iovecs\n");
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
        gossip_err("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(nr_segs * sizeof(struct iovec)));
        return -ENOMEM;
    }
    new_iovec = (struct iovec *) kmalloc(max_new_nr_segs * sizeof(struct iovec), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (new_iovec == NULL)
    {
        kfree(orig_iovec);
        gossip_err("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(struct iovec)));
        return -ENOMEM;
    }
    sizes = (int *) kmalloc(max_new_nr_segs * sizeof(int), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (sizes == NULL)
    {
        kfree(new_iovec);
        kfree(orig_iovec);
        gossip_err("split_iovecs: Could not allocate memory for %lu bytes!\n", 
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
        if (tmpnew_nr_segs >= max_new_nr_segs || sizes_count >= max_new_nr_segs)
        {
            kfree(sizes);
            kfree(orig_iovec);
            kfree(new_iovec);
            gossip_err("split_iovecs: exceeded the index limit (%d)\n", 
                    tmpnew_nr_segs);
            return -EINVAL;
        }
        if (count + orig_iovec[seg].iov_len < pvfs_bufmap_size_query())
        {
            count += orig_iovec[seg].iov_len;
            
            memcpy(&new_iovec[tmpnew_nr_segs], &orig_iovec[seg], 
                    sizeof(struct iovec));
            tmpnew_nr_segs++;
            sizes[sizes_count]++;
        }
        else
        {
            new_iovec[tmpnew_nr_segs].iov_base = orig_iovec[seg].iov_base;
            new_iovec[tmpnew_nr_segs].iov_len = 
                (pvfs_bufmap_size_query() - count);
            tmpnew_nr_segs++;
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

static long estimate_max_iovecs(const struct iovec *curr, unsigned long nr_segs, ssize_t *total_count)
{
    unsigned long i;
    long max_nr_iovecs;
    ssize_t total, count;

    total = 0;
    count = 0;
    max_nr_iovecs = 0;
    for (i = 0; i < nr_segs; i++) 
    {
        const struct iovec *iv = &curr[i];
        count += iv->iov_len;
	if (unlikely((ssize_t)(count|iv->iov_len) < 0))
            return -EINVAL;
        if (total + iv->iov_len < pvfs_bufmap_size_query())
        {
            total += iv->iov_len;
            max_nr_iovecs++;
        }
        else 
        {
            total = (total + iv->iov_len - pvfs_bufmap_size_query());
            max_nr_iovecs += (total / pvfs_bufmap_size_query() + 2);
        }
    }
    *total_count = count;
    return max_nr_iovecs;
}

/*
 * Common entry point for read/write/readv/writev
 */
static ssize_t do_direct_readv_writev(struct rw_options *rw)
{
    ssize_t ret, total_count;
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct file *file = NULL;
    unsigned int to_free;
    size_t count;
    struct iovec *iov;
    unsigned long nr_segs, seg, new_nr_segs = 0;
    long max_new_nr_segs = 0;
    unsigned int  seg_count = 0, *seg_array = NULL;
    struct iovec *iovecptr = NULL, *ptr = NULL;
    loff_t *offset;

    total_count = 0;
    ret = -EINVAL;
    file = NULL;
    inode = NULL;
    count =  0;
    to_free = 0;
    if (!rw || !rw->fnstr)
    {
        gossip_lerr("Invalid parameters\n");
        goto out;
    }
    offset = rw->offset;
    if (!offset)
    {
        gossip_err("%s: Invalid offset\n", rw->fnstr);
        goto out;
    }
    inode = rw->inode;
    if (!inode)
    {
        gossip_err("%s: Invalid inode\n", rw->fnstr);
        goto out;
    }
    pvfs2_inode = rw->pvfs2_inode;
    if (!pvfs2_inode)
    {
        gossip_err("%s: Invalid pvfs2 inode\n", rw->fnstr);
        goto out;
    }
    file  = rw->file;
    iov = rw->dest.address.iov;
    nr_segs = rw->dest.address.nr_segs;
    if (iov == NULL || nr_segs < 0)
    {
        gossip_err("%s: Invalid iovec %p or nr_segs %ld\n",
                rw->fnstr, iov, nr_segs);
        goto out;
    }
    /* Compute total and max number of segments after split */
    if ((max_new_nr_segs = estimate_max_iovecs(iov, nr_segs, &count)) < 0)
    {
        gossip_lerr("%s: could not estimate iovec %ld\n", rw->fnstr, max_new_nr_segs);
        goto out;
    }
    if (rw->type == IO_WRITEV)
    {
        if (!file)
        {
            gossip_err("%s: Invalid file pointer\n", rw->fnstr);
            goto out;
        }
        if (file->f_pos > i_size_read(inode))
        {
            i_size_write(inode, file->f_pos);
        }
        /* perform generic linux kernel tests for sanity of write 
         * arguments 
         */
#ifdef PVFS2_LINUX_KERNEL_2_4
        ret = pvfs2_precheck_file_write(file, inode, &count, offset);
#else
        ret = generic_write_checks(file, offset, &count, S_ISBLK(inode->i_mode));
#endif
        if (ret != 0)
        {
            gossip_err("%s: failed generic argument checks.\n", rw->fnstr);
            goto out;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: proceeding with offset : %llu, size %zd\n",
                rw->fnstr, llu(*offset), count);
    }
    if (count == 0)
    {
        ret = 0;
        goto out;
    }
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
            gossip_err("%s: Failed to split iovecs to satisfy larger "
                    " than blocksize readv/writev request %zd\n", rw->fnstr, ret);
            goto out;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: Splitting iovecs from %lu to %lu [max_new %lu]\n", 
                rw->fnstr, nr_segs, new_nr_segs, max_new_nr_segs);
        /* We must free seg_array and iovecptr */
        to_free = 1;
    }
    else 
    {
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

    gossip_debug(GOSSIP_FILE_DEBUG, "%s %zd@%llu\n", 
            rw->fnstr, count, llu(*offset));
    gossip_debug(GOSSIP_FILE_DEBUG, "%s: new_nr_segs: %lu, seg_count: %u\n", 
            rw->fnstr, new_nr_segs, seg_count);
#ifdef PVFS2_KERNEL_DEBUG
    for (seg = 0; seg < new_nr_segs; seg++)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: %d) %p to %p [%d bytes]\n", 
                rw->fnstr,
                seg + 1, iovecptr[seg].iov_base, 
                iovecptr[seg].iov_base + iovecptr[seg].iov_len, 
                (int) iovecptr[seg].iov_len);
    }
    for (seg = 0; seg < seg_count; seg++)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: %d) %u\n",
                rw->fnstr, seg + 1, seg_array[seg]);
    }
#endif
    seg = 0;
    while (total_count < count)
    {
        size_t each_count, amt_complete;

        /* how much to transfer in this loop iteration */
        each_count = (((count - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count - total_count));
        /* and push the I/O through */
        ret = wait_for_io(rw, ptr, seg_array[seg], each_count);
        if (ret < 0)
        {
            goto out;
        }
        /* advance the iovec pointer */
        ptr += seg_array[seg];
        seg++;
        *offset += ret;
        total_count += ret;
        amt_complete = ret;

        /* if we got a short I/O operations,
         * fall out and return what we got so far 
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }
    if (total_count > 0)
    {
        ret = total_count;
    }
out:
    if (to_free) 
    {
        kfree(iovecptr);
        kfree(seg_array);
    }
    if (ret > 0 && inode != NULL && pvfs2_inode != NULL)
    {
        if (rw->type == IO_READV)
        {
            SetAtimeFlag(pvfs2_inode);
            inode->i_atime = CURRENT_TIME;
        }
        else 
        {
            SetMtimeFlag(pvfs2_inode);
            inode->i_mtime = CURRENT_TIME;
        }
        mark_inode_dirty_sync(inode);
    }
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
    struct rw_options rw;
    struct iovec vec;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_READ;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.offset = offset;
    rw.readahead_size = readahead_size;
    rw.copy_to_user = copy_to_user;
    rw.fnstr = __FUNCTION__;
    vec.iov_base = buf;
    vec.iov_len  = count;
    rw.inode = inode;
    rw.pvfs2_inode = PVFS2_I(inode);
    rw.file = NULL;
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;
    return do_direct_readv_writev(&rw); 
}

/** Read data from a specified offset in a file into a user buffer.
 */
ssize_t pvfs2_file_read(
    struct file *file,
    char __user *buf,
    size_t count,
    loff_t *offset)
{
    struct rw_options rw;
    struct iovec vec;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_READ;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.offset = offset;
    rw.copy_to_user = 1;
    rw.fnstr = __FUNCTION__;
    vec.iov_base = buf;
    vec.iov_len  = count;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.file = file;
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;

    if (IS_IMMUTABLE(rw.inode)) 
    {
        rw.readahead_size = (rw.inode)->i_size;
        return generic_file_read(file, buf, count, offset);
    }
    else 
    {
        rw.readahead_size = 0;
        return do_direct_readv_writev(&rw);
    }
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
    struct rw_options rw;
    struct iovec vec;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_WRITE;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.offset = offset;
    rw.readahead_size = 0;
    rw.copy_to_user = 1;
    rw.fnstr = __FUNCTION__;
    vec.iov_base  = (char *) buf;
    vec.iov_len   = count;
    rw.file = file;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;
    return do_direct_readv_writev(&rw);
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
    struct rw_options rw;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_READV;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.offset = offset;
    rw.copy_to_user = 1;
    rw.fnstr = __FUNCTION__;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.file  = file;
    rw.dest.address.iov = (struct iovec *) iov;
    rw.dest.address.nr_segs = nr_segs;

    if (IS_IMMUTABLE(rw.inode))
    {
        rw.readahead_size = (rw.inode)->i_size;
        return do_direct_readv_writev(&rw);
    }
    else 
    {
        rw.readahead_size = 0;
        return do_direct_readv_writev(&rw);
    }
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
    struct rw_options rw;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_WRITEV;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.offset = offset;
    rw.readahead_size = 0;
    rw.copy_to_user = 1;
    rw.fnstr = __FUNCTION__;
    rw.file = file;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = (struct iovec *) iov;
    rw.dest.address.nr_segs = nr_segs;
    return do_direct_readv_writev(&rw);
}

#if defined(HAVE_READX_FILE_OPERATIONS) || defined(HAVE_WRITEX_FILE_OPERATIONS)

/* Construct a trailer of <file offsets, length pairs> in a buffer that we
 * pass in as an upcall trailer to client-core. This is used by clientcore
 * to construct a Request_hindexed type to stage the non-contiguous I/O
 * to file
 */
static int construct_file_offset_trailer(char **trailer, 
        PVFS_size *trailer_size, int seg_count, struct xtvec *xptr)
{
    int i;
    struct read_write_x *rwx;

    *trailer_size = seg_count * sizeof(struct read_write_x);
    *trailer = (char *) vmalloc(*trailer_size);
    if (*trailer == NULL)
    {
        *trailer_size = 0;
        return -ENOMEM;
    }
    rwx = (struct read_write_x *) *trailer;
    for (i = 0; i < seg_count; i++) 
    {
        rwx->off = xptr[i].xtv_off;
        rwx->len = xptr[i].xtv_len;
        rwx++;
    }
    return 0;
}

/*
 * The reason we need to do this is to be able to support readx() and writex()
 * of larger than PVFS_DEFAULT_DESC_SIZE (4 MB). What that means is that
 * we will create a new xtvec descriptor for those file offsets that 
 * go beyond the limit
 * Return value for this routine is -ve in case of errors
 * and 0 in case of success.
 * Further, the new_nr_segs pointer is updated to hold the new value
 * of number of xtvecs, the new_xtvec pointer is updated to hold the pointer
 * to the new split xtvec, and the size array is an array of integers holding
 * the number of xtvecs that straddle PVFS_DEFAULT_DESC_SIZE.
 * The max_new_nr_segs value is computed by the caller and passed in.
 * (It will be (count of all xtv_len/ block_size) + 1).
 */
static int split_xtvecs(unsigned long max_new_nr_segs,  /* IN */
        unsigned long nr_segs,              /* IN */
        const struct xtvec *original_xtvec, /* IN */
        unsigned long *new_nr_segs, struct xtvec **new_vec,  /* OUT */
        unsigned int *seg_count, unsigned int **seg_array)   /* OUT */
{
    int seg, count, begin_seg, tmpnew_nr_segs;
    struct xtvec *new_xtvec = NULL, *orig_xtvec;
    unsigned int *sizes = NULL, sizes_count = 0;

    if (nr_segs <= 0 || original_xtvec == NULL 
            || new_nr_segs == NULL || new_vec == NULL
            || seg_count == NULL || seg_array == NULL || max_new_nr_segs <= 0)
    {
        gossip_err("Invalid parameters to split_xtvecs\n");
        return -EINVAL;
    }
    *new_nr_segs = 0;
    *new_vec = NULL;
    *seg_count = 0;
    *seg_array = NULL;
    /* copy the passed in xtvec descriptor to a temp structure */
    orig_xtvec = (struct xtvec *) kmalloc(nr_segs * sizeof(struct xtvec),
            PVFS2_BUFMAP_GFP_FLAGS);
    if (orig_xtvec == NULL)
    {
        gossip_err("split_xtvecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(nr_segs * sizeof(struct xtvec)));
        return -ENOMEM;
    }
    new_xtvec = (struct xtvec *) kmalloc(max_new_nr_segs * sizeof(struct xtvec), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (new_xtvec == NULL)
    {
        kfree(orig_xtvec);
        gossip_err("split_xtvecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(struct xtvec)));
        return -ENOMEM;
    }
    sizes = (unsigned int *) kmalloc(max_new_nr_segs * sizeof(unsigned int), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (sizes == NULL)
    {
        kfree(new_xtvec);
        kfree(orig_xtvec);
        gossip_err("split_xtvecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(int)));
        return -ENOMEM;
    }
    /* copy the passed in xtvec to a temp structure */
    memcpy(orig_xtvec, original_xtvec, nr_segs * sizeof(struct xtvec));
    memset(new_xtvec, 0, max_new_nr_segs * sizeof(struct xtvec));
    memset(sizes, 0, max_new_nr_segs * sizeof(int));
    begin_seg = 0;
    count = 0;
    tmpnew_nr_segs = 0;
repeat:
    for (seg = begin_seg; seg < nr_segs; seg++)
    {
        if (tmpnew_nr_segs >= max_new_nr_segs || sizes_count >= max_new_nr_segs)
        {
            kfree(sizes);
            kfree(orig_xtvec);
            kfree(new_xtvec);
            gossip_err("split_xtvecs: exceeded the index limit (%d)\n", 
                    tmpnew_nr_segs);
            return -EINVAL;
        }
        if (count + orig_xtvec[seg].xtv_len < pvfs_bufmap_size_query())
        {
            count += orig_xtvec[seg].xtv_len;
            
            memcpy(&new_xtvec[tmpnew_nr_segs], &orig_xtvec[seg], 
                    sizeof(struct xtvec));
            tmpnew_nr_segs++;
            sizes[sizes_count]++;
        }
        else
        {
            new_xtvec[tmpnew_nr_segs].xtv_off = orig_xtvec[seg].xtv_off;
            new_xtvec[tmpnew_nr_segs].xtv_len = 
                (pvfs_bufmap_size_query() - count);
            tmpnew_nr_segs++;
            sizes[sizes_count]++;
            sizes_count++;
            begin_seg = seg;
            orig_xtvec[seg].xtv_off += (pvfs_bufmap_size_query() - count);
            orig_xtvec[seg].xtv_len -= (pvfs_bufmap_size_query() - count);
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
    /* new_xtvec is freed by the caller */
    *new_vec = new_xtvec;
    *seg_count = sizes_count;
    /* seg_array is also freed by the caller */
    *seg_array = sizes;
    kfree(orig_xtvec);
    return 0;
}

static long 
estimate_max_xtvecs(const struct xtvec *curr, unsigned long nr_segs, ssize_t *total_count)
{
    unsigned long i;
    long max_nr_xtvecs;
    ssize_t total, count;

    total = 0;
    count = 0;
    max_nr_xtvecs = 0;
    for (i = 0; i < nr_segs; i++) 
    {
        const struct xtvec *xv = &curr[i];
        count += xv->xtv_len;
	if (unlikely((ssize_t)(count|xv->xtv_len) < 0))
            return -EINVAL;
        if (total + xv->xtv_len < pvfs_bufmap_size_query())
        {
            total += xv->xtv_len;
            max_nr_xtvecs++;
        }
        else 
        {
            total = (total + xv->xtv_len - pvfs_bufmap_size_query());
            max_nr_xtvecs += (total / pvfs_bufmap_size_query() + 2);
        }
    }
    *total_count = count;
    return max_nr_xtvecs;
}


static ssize_t do_direct_readx_writex(int type, struct file *file,
        const struct iovec *iov, unsigned long nr_segs,
        const struct xtvec *xtvec, unsigned long xtnr_segs)
{
    ssize_t ret;
    unsigned int to_free;
    unsigned long seg;
    ssize_t total_count, count_mem, count_stream;
    size_t  each_count;
    long max_new_nr_segs_mem, max_new_nr_segs_stream;
    unsigned long new_nr_segs_mem = 0, new_nr_segs_stream = 0;
    unsigned int seg_count_mem, *seg_array_mem = NULL;
    unsigned int seg_count_stream, *seg_array_stream = NULL;
    struct inode *inode = file->f_dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    struct iovec *iovecptr = NULL, *ptr = NULL;
    struct xtvec *xtvecptr = NULL, *xptr = NULL;
    pvfs2_kernel_op_t *new_op = NULL;
    int buffer_index = -1;
    size_t amt_complete = 0;
    char *fnstr = (type == IO_READX) ? "pvfs2_file_readx" : "pvfs2_file_writex";

    ret = -EINVAL;
    to_free = 0;

    /* Calculate the total memory length to read by adding up the length of each io
     * segment */
    count_mem = 0;
    max_new_nr_segs_mem = 0;

    /* Compute total and max number of segments after split of the memory vector */
    if ((max_new_nr_segs_mem = estimate_max_iovecs(iov, nr_segs, &count_mem)) < 0)
    {
        return -EINVAL;
    }
    if (count_mem == 0)
    {
        return 0;
    }
    /* Calculate the total stream length to read and max number of segments after split of the stream vector */
    count_stream = 0;
    max_new_nr_segs_stream = 0;
    if ((max_new_nr_segs_stream = estimate_max_xtvecs(xtvec, xtnr_segs, &count_stream)) < 0)
    {
        return -EINVAL;
    }
    if (count_mem != count_stream) 
    {
        gossip_err("%s: mem count %ld != stream count %ld\n",
                fnstr, (long) count_mem, (long) count_stream);
        goto out;
    }
    total_count = 0;
    /*
     * if the total size of data transfer requested is greater than
     * the kernel-set blocksize of PVFS2, then we split the iovecs
     * such that no iovec description straddles a block size limit
     */
    if (count_mem > pvfs_bufmap_size_query())
    {
        /*
         * Split up the given iovec description such that
         * no iovec descriptor straddles over the block-size limitation.
         * This makes us our job easier to stage the I/O.
         * In addition, this function will also compute an array with seg_count
         * entries that will store the number of segments that straddle the
         * block-size boundaries.
         */
        if ((ret = split_iovecs(max_new_nr_segs_mem, nr_segs, iov, /* IN */
                        &new_nr_segs_mem, &iovecptr, /* OUT */
                        &seg_count_mem, &seg_array_mem)  /* OUT */ ) < 0)
        {
            gossip_err("%s: Failed to split iovecs to satisfy larger "
                    " than blocksize readx request %ld\n", fnstr, (long) ret);
            goto out;
        }
        /* We must free seg_array_mem and iovecptr, xtvecptr and seg_array_stream */
        to_free = 1;
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: Splitting iovecs from %lu to %lu [max_new %lu]\n", 
                fnstr, nr_segs, new_nr_segs_mem, max_new_nr_segs_mem);
        /* 
         * Split up the given xtvec description such that
         * no xtvec descriptor straddles over the block-size limitation.
         */
        if ((ret = split_xtvecs(max_new_nr_segs_stream, xtnr_segs, xtvec, /* IN */
                        &new_nr_segs_stream, &xtvecptr, /* OUT */
                        &seg_count_stream, &seg_array_stream) /* OUT */) < 0)
        {
            gossip_err("Failed to split iovecs to satisfy larger "
                    " than blocksize readx request %ld\n", (long) ret);
            goto out;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: Splitting xtvecs from %lu to %lu [max_new %lu]\n", 
                fnstr, xtnr_segs, new_nr_segs_stream, max_new_nr_segs_stream);
    }
    else {
        new_nr_segs_mem = nr_segs;
        /* use the given iovec description */
        iovecptr = (struct iovec *) iov;
        /* There is only 1 element in the seg_array_mem */
        seg_count_mem = 1;
        /* and its value is the number of segments passed in */
        seg_array_mem = (unsigned int *) &nr_segs;
        
        new_nr_segs_stream = xtnr_segs;
        /* use the given file description */
        xtvecptr = (struct xtvec *) xtvec;
        /* There is only 1 element in the seg_array_stream */
        seg_count_stream = 1;
        /* and its value is the number of segments passed in */
        seg_array_stream = (unsigned int *) &xtnr_segs;
        /* We dont have to free up anything */
        to_free = 0;
    }
#ifdef PVFS2_KERNEL_DEBUG
    for (seg = 0; seg < new_nr_segs_mem; seg++)
    {
        pvfs2_print("%s: %d) %p to %p [%ld bytes]\n",
                fnstr,
                seg + 1, iovecptr[seg].iov_base,
                iovecptr[seg].iov_base + iovecptr[seg].iov_len,
                (long) iovecptr[seg].iov_len);
    }
    for (seg = 0; seg < new_nr_segs_stream; seg++)
    {
        pvfs2_print("%s: %d) %ld to %ld [%ld bytes]\n",
                fnstr,
                seg + 1, (long) xtvecptr[seg].xtv_off,
                (long) xtvecptr[seg].xtv_off + xtvecptr[seg].xtv_len,
                (long) xtvecptr[seg].xtv_len);
    }
#endif
    seg = 0;
    ptr = iovecptr;
    xptr = xtvecptr;

    while (total_count < count_mem)
    {
        new_op = op_alloc_trailer(PVFS2_VFS_OP_FILE_IOX);
        if (!new_op)
        {
            ret = -ENOMEM;
            goto out;
        }
        new_op->upcall.req.iox.io_type = 
            (type == IO_READX) ? PVFS_IO_READ : PVFS_IO_WRITE;
        new_op->upcall.req.iox.refn = pvfs2_inode->refn;

        /* get a shared buffer index */
        ret = pvfs_bufmap_get(&buffer_index);
        if (ret < 0)
        {
            gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_get() "
                        "failure (%ld)\n", fnstr, (long) ret);
            goto out;
        }

        /* how much to transfer in this loop iteration */
        each_count = (((count_mem - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count_mem - total_count));

        new_op->upcall.req.iox.buf_index = buffer_index;
        new_op->upcall.req.iox.count     = each_count;

        /* construct the upcall trailer buffer */
        if ((ret = construct_file_offset_trailer(&new_op->upcall.trailer_buf, 
                        &new_op->upcall.trailer_size, seg_array_stream[seg], xptr)) < 0)
        {
            gossip_err("%s: construct_file_offset_trailer() "
                    "failure (%ld)\n", fnstr, (long) ret);
            goto out;
        }
        if (type == IO_WRITEX)
        {
            /* copy data from application by pulling it out of the iovec.
             * Number of segments to copy so that we don't overflow the
             * block-size is set in seg_array_mem[] and ptr points to the
             * appropriate iovec from where data needs to be copied out,
             * and each_count indicates size in bytes that needs to be
             * pulled out
             */
            ret = pvfs_bufmap_copy_iovec_from_user(
                    buffer_index, ptr, seg_array_mem[seg], each_count);
            if (ret < 0)
            {
                gossip_err("%s: failed to copy user buffer. Please make sure "
                        " that the pvfs2-client is running. %ld\n", fnstr, (long) ret);
                goto out;
            }
        }
        /* whew! finally service this operation */
        ret = service_operation(new_op, fnstr,
                get_interruptible_flag(inode));
        if (ret < 0)
        {
              /* this macro is defined in pvfs2-kernel.h */
              handle_io_error();

              /*
                don't write an error to syslog on signaled operation
                termination unless we've got debugging turned on, as
                this can happen regularly (i.e. ctrl-c)
              */
              if (ret == -EINTR)
              {
                  gossip_debug(GOSSIP_FILE_DEBUG, "%s: returning error %ld\n", fnstr, (long) ret);
              }
              else
              {
                  gossip_err(
                        "%s: error in %s handle %llu, "
                        "FILE: %s\n  -- returning %ld\n",
                        fnstr, 
                        type == IO_READX ? "noncontig read from" : "noncontig write to",
                        llu(get_handle_from_ino(inode)),
                        (file && file->f_dentry && file->f_dentry->d_name.name ?
                         (char *)file->f_dentry->d_name.name : "UNKNOWN"),
                        (long) ret);
              }
              goto out;
        }
        if (type == IO_READX)
        {
            /* copy data to application by pushing it out to the iovec.
             * Number of segments to copy so that we don't overflow the
             * block-size is set in seg_array_mem[] and ptr points to
             * the appropriate iovec from where data needs to be copied
             * in, and each count indicates size in bytes that needs to be
             * pulled in
             */
            if (new_op->downcall.resp.iox.amt_complete)
            {
                ret = pvfs_bufmap_copy_to_user_iovec(
                        buffer_index, ptr, seg_array_mem[seg], new_op->downcall.resp.iox.amt_complete);
                if (ret < 0)
                {
                    gossip_err("%s: failed to copy user buffer. Please make sure "
                            " that the pvfs2-client is running. %ld\n", fnstr, (long) ret);
                    /* put error codes in downcall so that handle_io_error()
                     * preserves it properly */
                    new_op->downcall.status = ret;
                    handle_io_error();
                    goto out;
                }
            }
        }
        /* Advance the iovec pointer */
        ptr += seg_array_mem[seg];
        /* Advance the xtvec pointer */
        xptr += seg_array_stream[seg];
        seg++;
        amt_complete = new_op->downcall.resp.iox.amt_complete;
        total_count += amt_complete;

        /*
          tell the device file owner waiting on I/O that this I/O has
          completed and it can return now.  in this exact case, on
          wakeup the device will free the op, so we *cannot* touch it
          after this.
        */
        wake_up_daemon_for_return(new_op);
        new_op = NULL;
        pvfs_bufmap_put(buffer_index);
        buffer_index = -1;

        /* if we got a short I/O operations,
         * fall out and return what we got so far 
         */
        if (amt_complete < each_count)
        {
            break;
        }
    }
    if (total_count > 0)
    {
        ret = total_count;
    }
out:
    if (buffer_index >= 0) {
        pvfs_bufmap_put(buffer_index);
        gossip_debug(GOSSIP_FILE_DEBUG, "PUT buffer_index %d\n", buffer_index);
    }
    if (new_op)
    {
        if (new_op->upcall.trailer_buf)
            vfree(new_op->upcall.trailer_buf);
        op_release(new_op);
    }
    if (to_free)
    {
        kfree(iovecptr);
        kfree(seg_array_mem);
        kfree(xtvecptr);
        kfree(seg_array_stream);
    }
    if (ret > 0 && inode != NULL && pvfs2_inode != NULL)
    {
        if (type == IO_READX)
        {
            SetAtimeFlag(pvfs2_inode);
            inode->i_atime = CURRENT_TIME;
        }
        else 
        {
            SetMtimeFlag(pvfs2_inode);
            inode->i_mtime = CURRENT_TIME;
        }
        mark_inode_dirty_sync(inode);
    }
    return ret;
}

#endif

#ifdef HAVE_READX_FILE_OPERATIONS

static ssize_t pvfs2_file_readx(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    const struct xtvec *xtvec,
    unsigned long xtnr_segs)
{
    return do_direct_readx_writex(IO_READX, file, iov, nr_segs, xtvec, xtnr_segs);
}

#endif

#ifdef HAVE_WRITEX_FILE_OPERATIONS

static ssize_t pvfs2_file_writex(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    const struct xtvec *xtvec,
    unsigned long xtnr_segs)
{
    return do_direct_readx_writex(IO_WRITEX, file, iov, nr_segs, xtvec, xtnr_segs);
}

#endif

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
        gossip_err("pvfs2_aio_retry: could not "
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
        gossip_err("pvfs2_aio_retry: critical error "
                " x->op = %p, iocb = %p, buffer_index = %d\n",
                x->op, x->kiocb, x->buffer_index);
        return -EINVAL;
    }
    /* lock up the op */
    spin_lock(&op->lock);
    /* check the state of the op */
    if (op_state_waiting(op) || op_state_in_progress(op))
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
        gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_retry: buffer %p,"
                " size %d return %d bytes\n",
                    x->buffer, (int) x->bytes_to_be_copied, (int) error);
        if (error > 0)
        {
            struct inode *inode = iocb->ki_filp->f_mapping->host;
            pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
            if (x->rw == PVFS_IO_READ)
            {
                SetAtimeFlag(pvfs2_inode);
                inode->i_atime = CURRENT_TIME;
            }
            else 
            {
                SetMtimeFlag(pvfs2_inode);
                inode->i_mtime = CURRENT_TIME;
            }
            mark_inode_dirty_sync(inode);
        }
        /* 
         * Now we can happily free up the op,
         * and put buffer_index also away 
         */
        if (x->buffer_index >= 0)
        {
            gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_retry: put bufmap_index "
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
        gossip_err("pvfs2_aio_cancel: Invalid parameters "
                " %p, %p!\n", iocb, event);
        return -EINVAL;
    }
    x = (pvfs2_kiocb *) iocb->private;
    if (x == NULL)
    {
        gossip_err("pvfs2_aio_cancel: cannot retrieve "
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
            gossip_err("pvfs2_aio_cancel: kiocb structures "
                    "don't match %p %p!\n", x->kiocb, iocb);
            return -EINVAL;
        }
        if ((op = x->op) == NULL)
        {
            gossip_err("pvfs2_aio_cancel: cannot retreive "
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
                        gossip_debug(GOSSIP_FILE_DEBUG, "Timed out on I/O cancellation - aborting\n");
                        timed_out_or_signal = 1;
                        spin_lock(&op->lock);
                        break;
                    }
                    spin_lock(&op->lock);
                    continue;
                }
                gossip_debug(GOSSIP_FILE_DEBUG, "signal on Async I/O cancellation - aborting\n");
                timed_out_or_signal = 1;
                spin_lock(&op->lock);
                break;
            }
            set_current_state(TASK_RUNNING);
            remove_wait_queue(&op->io_completion_waitq, &wait_entry);

        } while (0);

        /* We need to fill up event->res and event->res2 if at all */
        if (op_state_serviced(op))
        {
            op->priv = NULL;
            spin_unlock(&op->lock);
            event->res = x->bytes_copied;
            event->res2 = 0;
        }
        else if (op_state_in_progress(op))
        {
            op->priv = NULL;
            spin_unlock(&op->lock);
            gossip_debug(GOSSIP_FILE_DEBUG, "Trying to cancel operation in "
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
            gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_cancel: put bufmap_index "
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
            gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_dtor: put bufmap_index "
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
    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_dtor: kiocb_release %p\n", x);
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
 * TODO: We do not handle vectored aio requests yet
 */
static ssize_t do_direct_aio_read_write(struct rw_options *rw)
{
    struct file *filp;
    struct inode *inode;
    ssize_t error;
    pvfs2_inode_t *pvfs2_inode;
    struct iovec *iov;
    unsigned long nr_segs, max_new_nr_segs;
    size_t count;
    struct kiocb *iocb;
    loff_t *offset;
    pvfs2_kiocb *x;

    error = -EINVAL;
    if (!rw || !rw->fnstr || !rw->offset)
    {
        gossip_lerr("Invalid parameters (rw %p)\n", rw);
        goto out_error;
    }
    inode = rw->inode;
    filp  = rw->file;
    iocb  = rw->iocb;
    pvfs2_inode = rw->pvfs2_inode;
    offset = rw->offset;
    if (!inode || !filp || !pvfs2_inode || !iocb || !offset)
    {
        gossip_lerr("Invalid parameters\n");
        goto out_error;
    }
    if (iocb->ki_pos != *offset)
    {
        gossip_lerr("iocb offsets don't match (%llu %llu)\n",
                llu(iocb->ki_pos), llu(*offset));
        goto out_error;
    }
    iov = rw->dest.address.iov;
    nr_segs = rw->dest.address.nr_segs;
    if (iov == NULL || nr_segs < 0)
    {
        gossip_lerr("Invalid iovector (%p) or invalid iovec count (%ld)\n",
                iov, nr_segs);
        goto out_error;
    }
    if (nr_segs > 1)
    {
        gossip_lerr("%s: not implemented yet (aio with %ld segments)\n",
                rw->fnstr, nr_segs);
        goto out_error;
    }
    count = 0;
    /* Compute total and max number of segments after split */
    if ((max_new_nr_segs = estimate_max_iovecs(iov, nr_segs, &count)) < 0)
    {
        gossip_lerr("%s: could not estimate iovecs %ld\n", rw->fnstr, max_new_nr_segs);
        goto out_error;
    }
    if (unlikely(((ssize_t)count)) < 0)
    {
        gossip_lerr("%s: count overflow\n", rw->fnstr);
        goto out_error;
    }
    /* synchronous I/O */
    if (!rw->async)
    {
        error = do_direct_readv_writev(rw);
        goto out_error;
    }
    /* Asynchronous I/O */
    if (rw->type == IO_WRITE)
    {
        int ret;
        /* perform generic tests for sanity of write arguments */
#ifdef PVFS2_LINUX_KERNEL_2_4
        ret = pvfs2_precheck_file_write(filp, inode, &count, offset);
#else
        ret = generic_write_checks(filp, offset, &count, S_ISBLK(inode->i_mode));
#endif
        if (ret != 0)
        {
            gossip_err("%s: failed generic "
                    " argument checks.\n", rw->fnstr);
            return ret;
        }
    }
    if (count == 0)
    {
        error = 0;
        goto out_error;
    }
    else if (count > pvfs_bufmap_size_query())
    {
        /* TODO: Asynchronous I/O operation is not allowed to 
         * be greater than our block size 
         */
        gossip_lerr("%s: cannot transfer (%zd) bytes"
                " (larger than block size %d)\n",
                rw->fnstr, count, pvfs_bufmap_size_query());
        goto out_error;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "Posting asynchronous I/O operation\n");
    /* First time submission */
    if ((x = (pvfs2_kiocb *) iocb->private) == NULL)
    {
        int buffer_index = -1;
        pvfs2_kernel_op_t *new_op = NULL;
        char __user *current_buf = (char *) rw->dest.address.iov[0].iov_base;
        pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
        
        new_op = op_alloc(PVFS2_VFS_OP_FILE_IO);
        if (!new_op)
        {
            error = -ENOMEM;
            goto out_error;
        }
        /* Increase ref count */
        get_op(new_op);
        /* Asynchronous I/O */
        new_op->upcall.req.io.async_vfs_io = PVFS_VFS_ASYNC_IO;
        new_op->upcall.req.io.io_type = (rw->type == IO_READ) ?
                                        PVFS_IO_READ : PVFS_IO_WRITE;
        new_op->upcall.req.io.refn = pvfs2_inode->refn;
        error = pvfs_bufmap_get(&buffer_index);
        if (error < 0)
        {
            gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_get()"
                    " failure %ld\n", rw->fnstr, (long) error);
            /* drop ref count and possibly de-allocate */
            put_op(new_op);
            goto out_error;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_get %d\n",
                rw->fnstr, buffer_index);
        new_op->upcall.req.io.buf_index = buffer_index;
        new_op->upcall.req.io.count = count;
        new_op->upcall.req.io.offset = *offset;
        if (rw->type == IO_WRITE)
        {
            /* 
             * copy the data from the application for writes 
             * Should this be done here even for async I/O? 
             * We could return -EIOCBRETRY here and have 
             * the data copied in the pvfs2_aio_retry routine,
             * I think. But I dont see the point in doing that...
             */
            error = pvfs_bufmap_copy_from_user(
                    buffer_index, current_buf, count);
            if (error < 0)
            {
                gossip_err("%s: Failed to copy user buffer %ld. Make sure that pvfs2-client-core"
                        " is still running \n", rw->fnstr, (long) error);
                /* drop the buffer index */
                pvfs_bufmap_put(buffer_index);
                gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_put %d\n",
                        rw->fnstr, buffer_index);
                /* drop the reference count and deallocate */
                put_op(new_op);
                goto out_error;
            }
        }
        x = kiocb_alloc();
        if (x == NULL)
        {
            error = -ENOMEM;
            /* drop the buffer index */
            pvfs_bufmap_put(buffer_index);
            gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_put %d\n",
                    rw->fnstr, buffer_index);
            /* drop the reference count and deallocate */
            put_op(new_op);
            goto out_error;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "kiocb_alloc: %p\n", x);
        /* 
         * destructor function to make sure that we free
         * up this allocated piece of memory 
         */
        iocb->ki_dtor = pvfs2_aio_dtor;
        /* 
         * We need to set the cancellation callbacks + 
         * other state information
         * here if the asynchronous request is going to
         * be successfully submitted 
         */
        fill_default_kiocb(x, current, iocb, 
                (rw->type == IO_READ) ? PVFS_IO_READ : PVFS_IO_WRITE,
                buffer_index, new_op, current_buf,
                *offset, count,
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
        service_operation(new_op, rw->fnstr, 
                PVFS2_OP_ASYNC);
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: queued "
                " operation [%llu for %zd]\n",
                rw->fnstr, llu(*offset), count);
        error = -EIOCBQUEUED;
        /*
         * All cleanups done upon completion
         * (OR) cancellation!
         */
    }
    /* I don't think this path will ever be taken */
    else { /* retry and see what is the status! */
        error = pvfs2_aio_retry(iocb);
    }
out_error:
    return error;
}

static ssize_t 
pvfs2_file_aio_read(struct kiocb *iocb, char __user *buffer,
        size_t count, loff_t offset)
{
    struct rw_options rw;
    struct iovec vec;
    memset(&rw, 0, sizeof(rw));
    rw.async = !is_sync_kiocb(iocb);
    rw.type = IO_READ;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.offset = &offset;
    rw.copy_to_user = 1;
    rw.fnstr = __FUNCTION__;
    rw.iocb = iocb;
    vec.iov_base = (char __user *) buffer;
    vec.iov_len  = count;
    rw.file = iocb->ki_filp;
    if (!rw.file || !(rw.file)->f_mapping)
    {
        return -EINVAL;
    }
    rw.inode = (rw.file)->f_mapping->host;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;

    if (IS_IMMUTABLE(rw.inode)) 
    {
        rw.readahead_size = (rw.inode)->i_size;
        return generic_file_aio_read(iocb, buffer, count, offset);
    }
    else 
    {
        rw.readahead_size = 0;
        return do_direct_aio_read_write(&rw);
    }
}

static ssize_t 
pvfs2_file_aio_write(struct kiocb *iocb, const char __user *buffer,
        size_t count, loff_t offset)
{
    struct rw_options rw;
    struct iovec vec;

    memset(&rw, 0, sizeof(rw));
    rw.async = !is_sync_kiocb(iocb);
    rw.type = IO_WRITE;
    rw.copy_dest_type = COPY_TO_ADDRESSES;
    rw.readahead_size = 0;
    rw.offset = &offset;
    rw.copy_to_user = 1;
    rw.fnstr = __FUNCTION__;
    rw.iocb = iocb;
    vec.iov_base = (char __user *) buffer;
    vec.iov_len  = count;
    rw.file = iocb->ki_filp;
    if (!rw.file || !(rw.file)->f_mapping)
    {
        return -EINVAL;
    }
    rw.inode = (rw.file)->f_mapping->host;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;
    return do_direct_aio_read_write(&rw);
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

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_ioctl: called with cmd %d\n", cmd);
    return ret;
}

/** Memory map a region of a file.
 */
static int pvfs2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct inode *inode = file->f_dentry->d_inode;

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_mmap: called on %s\n",
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
    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_release: called on %s\n",
                file->f_dentry->d_name.name);

    pvfs2_flush_inode(inode);
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

    new_op = op_alloc(PVFS2_VFS_OP_FSYNC);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.fsync.refn = pvfs2_inode->refn;

    ret = service_operation(new_op, "pvfs2_fsync", 
            get_interruptible_flag(file->f_dentry->d_inode));

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_fsync got return value of %d\n",ret);

    op_release(new_op);

    pvfs2_flush_inode(file->f_dentry->d_inode);
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
        gossip_err("pvfs2_file_llseek: invalid inode (NULL)\n");
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

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_llseek: offset is %ld | origin is %d | "
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
            gossip_debug(GOSSIP_FILE_DEBUG, "begin_index = %lu offset = %lu file_offset = %ld\n",
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
        gossip_debug(GOSSIP_FILE_DEBUG, "actor with offset %lu nr %lu return %lu desc->count %lu\n", 
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
#ifdef HAVE_ARG_IN_READ_DESCRIPTOR_T
    desc.arg.data = target;
#else
    desc.buf = target;
#endif
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
#ifdef HAVE_READX_FILE_OPERATIONS
    .readx = pvfs2_file_readx,
#endif
#ifdef HAVE_WRITEX_FILE_OPERATIONS
    .writex = pvfs2_file_writex,
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
            gossip_err("Operation not permitted on read only file system\n");
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

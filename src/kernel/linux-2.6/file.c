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

struct rw_options;

#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_precheck_file_write(struct file *file, struct inode *inode,
    size_t *count, loff_t *ppos);
#endif

static ssize_t wait_for_direct_io(struct rw_options *rw,
                                  struct iovec *vec,
                                  unsigned long  nr_segs,
                                  size_t total_size);

static ssize_t wait_for_iox(struct rw_options *rw, 
                            struct iovec *vec,
                            unsigned long  nr_segs,
                            struct xtvec *xtvec,
                            unsigned long xtnr_segs,
                            size_t total_size);

#define wake_up_daemon_for_return(op)             \
do {                                              \
  spin_lock(&op->lock);                           \
  op->io_completed = 1;                           \
  spin_unlock(&op->lock);                         \
  wake_up_interruptible(&op->io_completion_waitq);\
} while(0)

#ifndef HAVE_COMBINED_AIO_AND_VECTOR
/* <2.6.19 called it this instead */
#define do_sync_read generic_file_read
#endif

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
                file->f_pos = pvfs2_i_size_read(inode);
                gossip_debug(GOSSIP_FILE_DEBUG, "f_pos = %ld\n", (unsigned long)file->f_pos);
            }
            else
            {
                gossip_debug(GOSSIP_FILE_DEBUG, "%s:%s:%d calling make bad inode\n", __FILE__,  __func__, __LINE__);
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
    /* Destination type can be addresses (user or kernel va) */
    COPY_DEST_ADDRESSES = 0,
    /* or can be pointers to struct pages */
    COPY_DEST_PAGES     = 1
};

struct rw_options {
    /* whether or not it is a synchronous I/O operation */
    int            async;
    /* whether it is a READ/WRITE operation */
    enum io_type   type; 
    /* whether we are copying to addresses/pages */
    enum dest_type copy_dest_type; 
    struct file   *file;
    struct inode  *inode;
    pvfs2_inode_t *pvfs2_inode;
    loff_t readahead_size;
    /* whether the destination addresses are in user/kernel */
    int copy_to_user_addresses;
    const char *fnstr;
    ssize_t count;
    /* Asynch I/O control block */
    struct kiocb *iocb;
    union {
        struct {
            const struct iovec *iov;
            unsigned long nr_segs;
        } address;
        struct {
            /* byte-map of which pages are locked down for I/o */
            unsigned char *pg_byte_map;
            /* All pages spanning a given I/O operation */
            struct page  **pages;
            /* count of such pages */
            unsigned long nr_pages;
            /* Only those pages that need to be fetched */
            struct page  **issue_pages;
            /* and the count of such pages */
            unsigned long nr_issue_pages;
            /* list of pages for which I/O needs to be 
             * done as dictated by read_cache_pages 
             */
            struct list_head page_list;
        } pages;
    } dest;
    union {
        /* Contiguous file I/O operations use a single offset */
        struct {
            loff_t        *offset;
        } io;
        /* Non-contiguous file I/O operations use a vector of offsets */
        struct {
            struct xtvec  *xtvec;
            unsigned long  xtnr_segs;
        } iox;
    } off;
};

/*
 * Copy to client-core's address space from the buffers specified
 * by the iovec upto total_size bytes.
 * NOTE: the iovector can either contain addresses which
 *       can futher be kernel-space or user-space addresses.
 *       or it can pointers to struct page's
 * @buffer_index: index used by client-core's buffers
 * @rw: operation context (read/write) holding the state of the I/O
 * @vec: iovector
 * @nr_segs: number of segments in the iovector
 * @total_size: Total size in bytes to be copied into client-core.
 */
static int precopy_buffers(int buffer_index, 
                           struct rw_options *rw, 
                           const struct iovec *vec,
                           unsigned long nr_segs,
                           size_t total_size)
{
    int ret = 0;

    if (rw->type == IO_WRITEV)
    {
        /* 
         * copy data from application/kernel by pulling it out 
         * of the iovec. NOTE: target buffers can be addresses
         * or struct page pointers
         */
        if (rw->copy_dest_type == COPY_DEST_ADDRESSES) {
            /* Are we copying from User Virtual Addresses? */
            if (rw->copy_to_user_addresses)
            {
                ret = pvfs_bufmap_copy_iovec_from_user(buffer_index,
                                                       vec,
                                                       nr_segs,
                                                       total_size);
            }
            /* Are we copying from Kernel Virtual Addresses? */
            else {
                ret = pvfs_bufmap_copy_iovec_from_kernel(buffer_index,
                                                         vec,
                                                         nr_segs,
                                                         total_size);
            }
        }
        else {
            /* We must be copying from struct page pointers */
            ret = pvfs_bufmap_copy_from_pages(buffer_index,
                                              vec,
                                              nr_segs,
                                              total_size);
        }
        if (ret < 0)
        {
            gossip_err("%s: Failed to copy-in buffers. Please make sure "
                        "that the pvfs2-client is running. %ld\n", 
                        rw->fnstr, (long) ret);
        }
    }
    return ret;
}

/*
 * Copy from client-core's address space to the buffers specified
 * by the iovec upto total_size bytes.
 * NOTE: the iovector can either contain addresses which
 *       can futher be kernel-space or user-space addresses.
 *       or it can pointers to struct page's
 * @buffer_index: index used by client-core's buffers
 * @rw: operation context (read/write) holding the state of the I/O
 * @vec: iovector
 * @nr_segs: number of segments in the iovector
 * @total_size: Total size in bytes to be copied from client-core.
 */
static int postcopy_buffers(int buffer_index, struct rw_options *rw,
        const struct iovec *vec, int nr_segs, size_t total_size)
{
    int ret = 0;

    if (rw->type == IO_READV)
    {
        /*
         * copy data to application/kernel by pushing it out to the iovec.
         * NOTE; target buffers can be addresses or struct page pointers
         */
        if (total_size)
        {
            if (rw->copy_dest_type == COPY_DEST_ADDRESSES) {
                /* Are we copying to User Virtual Addresses? */
                if (rw->copy_to_user_addresses)
                {
                    ret = pvfs_bufmap_copy_to_user_iovec(buffer_index, vec, 
                            nr_segs, total_size);

                }
                /* Are we copying to Kernel Virtual Addresses? */
                else
                {
                    ret = pvfs_bufmap_copy_to_kernel_iovec(buffer_index, vec,
                            nr_segs, total_size);
                }
            }
            else {
                /* We must be copying to struct page pointers */
                ret = pvfs_bufmap_copy_to_pages(buffer_index, vec,
                            nr_segs, total_size);
            }
            if (ret < 0)
            {
                gossip_err("%s: Failed to copy-out buffers.  Please make sure "
                            "that the pvfs2-client is running (%ld)\n",
                            rw->fnstr, (long) ret);
            }
        }
    }
    return ret;
}

#ifndef PVFS2_LINUX_KERNEL_2_4

/* Copy from page-cache to application address space 
 * @rw - operation context, contains information about the I/O operation
 *       and holds the pointers to the page-cache page array from which
 *       the copies are to be initiated.
 * @vec - iovec describing the layout of buffers in user-space
 * @nr_segs - number of segments in the iovec
 * @total_actual_io - total size of the buffers to be copied.
 */
static int copy_from_pagecache(struct rw_options *rw,
                               const struct iovec *vec, 
                               unsigned long nr_segs,
                               size_t total_actual_io)
{
    struct iovec *copied_iovec = NULL;
    size_t amt_copied = 0, cur_copy_size = 0;
    int ret = 0;
    unsigned long seg, page_offset = 0;
    int index = 0;
    void __user *to_addr = NULL;

    gossip_debug(GOSSIP_FILE_DEBUG, "copy_from_pagecache: "
                 "nr_segs %ld, total_actual_io: %zd, total pages %ld\n",
                 nr_segs, total_actual_io, rw->dest.pages.nr_pages);
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = kmalloc(nr_segs * sizeof(*copied_iovec), 
                           PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        gossip_err("copy_from_pagecache: failed allocating memory\n");
        return -ENOMEM;
    }
    memcpy(copied_iovec, vec, nr_segs * sizeof(*copied_iovec));
    /*
     * Go through each segment in the iovec and make sure that
     * the summation of iov_len is greater than the given size.
     */
    for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
    {
        amt_copied += copied_iovec[seg].iov_len;
    }
    if (amt_copied < total_actual_io)
    {
        gossip_err("copy_from_pagecache: user buffer size (%zd) "
                   "is less than I/O size (%zd)\n",
                    amt_copied, total_actual_io);
        kfree(copied_iovec);
        return -EINVAL;
    }
    index = 0;
    amt_copied = 0;
    seg = 0;
    page_offset = 0;
    /* 
     * Go through each segment in the iovec and copy from the page-cache,
     * but make sure that we do so one page at a time.
     */
    while (amt_copied < total_actual_io)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_index = 0;
        void *from_kaddr;

        if (index >= rw->dest.pages.nr_pages) {
            gossip_err("index cannot exceed number of allocated pages %ld\n", 
                    (long) rw->dest.pages.nr_pages);
            kfree(copied_iovec);
            return -EINVAL;
        }

        if (iv->iov_len < (PAGE_CACHE_SIZE - page_offset))
        {
            cur_copy_size = iv->iov_len;
            seg++;
            to_addr = iv->iov_base;
            inc_index = 0;
        }
        else if (iv->iov_len == (PAGE_CACHE_SIZE - page_offset))
        {
            cur_copy_size = iv->iov_len;
            seg++;
            to_addr = iv->iov_base;
            inc_index = 1;
        }
        else 
        {
            cur_copy_size = (PAGE_CACHE_SIZE - page_offset);
            to_addr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len  -= cur_copy_size;
            inc_index = 1;
        }
#if 0
        gossip_debug(GOSSIP_FILE_DEBUG, "copy_from_pagecache: copying to "
                "user %p, kernel page %p\n", to_addr, rw->dest.pages.pages[index]);
#endif
        from_kaddr = pvfs2_kmap(rw->dest.pages.pages[index]);
        ret = copy_to_user(to_addr, from_kaddr + page_offset, cur_copy_size);
        pvfs2_kunmap(rw->dest.pages.pages[index]);
#if 0
        gossip_debug(GOSSIP_FILE_DEBUG, "copy_from_pagecache: copying to user %p from "
                "kernel %p %d bytes (from_kaddr:%p, page_offset:%d)\n",
                to_addr, from_kaddr + page_offset, cur_copy_size, from_kaddr, page_offset); 
#endif
        if (ret)
        {
            gossip_err("Failed to copy data to user space\n");
            kfree(copied_iovec);
            return -EFAULT;
        }

        amt_copied += cur_copy_size;
        if (inc_index) {
            page_offset = 0;
            index++;
        }
        else {
            page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    return 0;
}

#endif /* #ifndef PVFS2_LINUX_KERNEL_2_4 */

/*
 * Post and wait for the I/O upcall to finish
 * @rw - contains state information to initiate the I/O operation
 * @vec- contains the memory vector regions 
 * @nr_segs - number of memory vector regions
 * @total_size - total expected size of the I/O operation
 */
static ssize_t wait_for_direct_io(struct rw_options *rw,
                                  struct iovec *vec,
                                  unsigned long nr_segs,
                                  size_t total_size)
{
    pvfs2_kernel_op_t *new_op = NULL;
    int buffer_index = -1;
    ssize_t ret;

    if (!rw || !vec || nr_segs < 0 || total_size <= 0 
            || !rw->pvfs2_inode || !rw->inode || !rw->fnstr)
    {
        gossip_lerr("invalid parameters (rw: %p, vec: %p, nr_segs: %lu, "
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
    new_op->upcall.req.io.io_type = (rw->type == IO_READV) ?
                                     PVFS_IO_READ : PVFS_IO_WRITE;
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
    new_op->upcall.req.io.offset = *(rw->off.io.offset);

    gossip_debug(GOSSIP_FILE_DEBUG, "%s: copy_to_user %d nr_segs %lu, "
            "offset: %llu total_size: %zd\n", rw->fnstr, rw->copy_to_user_addresses, 
            nr_segs, llu(*(rw->off.io.offset)), total_size);
    /* Stage 1: copy the buffers into client-core's address space */
    if ((ret = precopy_buffers(buffer_index, rw, vec, nr_segs, total_size)) < 0) 
    {
        goto out;
    }
    /* Stage 2: Service the I/O operation */
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
    /* Stage 3: Post copy buffers from client-core's address space */
    if ((ret = postcopy_buffers(buffer_index, rw, vec, nr_segs, 
                    new_op->downcall.resp.io.amt_complete)) < 0) {
        /* put error codes in downcall so that handle_io_error()
         * preserves it properly 
         */
        new_op->downcall.status = ret;
        handle_io_error();
        goto out;
    }
    ret = new_op->downcall.resp.io.amt_complete;
    gossip_debug(GOSSIP_FILE_DEBUG, "wait_for_io returning %ld\n", (long) ret);
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
 * larger than (pvfs_bufmap_size_query())
 * Default is PVFS2_BUFMAP_DEFAULT_DESC_SIZE MB.
 * What that means is that
 * we will create a new io vec descriptor for those memory addresses that 
 * go beyond the limit
 * Return value for this routine is -ve in case of errors
 * and 0 in case of success.
 * Further, the new_nr_segs pointer is updated to hold the new value
 * of number of iovecs, the new_vec pointer is updated to hold the pointer
 * to the new split iovec, and the size array is an array of integers holding
 * the number of iovecs that straddle pvfs_bufmap_size_query().
 * The max_new_nr_segs value is computed by the caller and returned.
 * (It will be (count of all iov_len/ block_size) + 1).
 */
static int split_iovecs(
	unsigned long max_new_nr_segs,      /* IN */
        unsigned long nr_segs,              /* IN */
        const struct iovec *original_iovec, /* IN */
        unsigned long *new_nr_segs, 	    /* OUT */
	struct iovec **new_vec,  	    /* OUT */
        unsigned long *seg_count,           /* OUT */
	unsigned long **seg_array)   	    /* OUT */
{
    unsigned long seg, count = 0, begin_seg, tmpnew_nr_segs = 0;
    struct iovec *new_iovec = NULL, *orig_iovec;
    unsigned long *sizes = NULL, sizes_count = 0;

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
    orig_iovec = kmalloc(nr_segs * sizeof(*orig_iovec),
                         PVFS2_BUFMAP_GFP_FLAGS);
    if (orig_iovec == NULL)
    {
        gossip_err("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(nr_segs * sizeof(*orig_iovec)));
        return -ENOMEM;
    }
    new_iovec = kzalloc(max_new_nr_segs * sizeof(*new_iovec), 
                        PVFS2_BUFMAP_GFP_FLAGS);
    if (new_iovec == NULL)
    {
        kfree(orig_iovec);
        gossip_err("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(*new_iovec)));
        return -ENOMEM;
    }
    sizes = kzalloc(max_new_nr_segs * sizeof(*sizes),
                    PVFS2_BUFMAP_GFP_FLAGS);
    if (sizes == NULL)
    {
        kfree(new_iovec);
        kfree(orig_iovec);
        gossip_err("split_iovecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(*sizes)));
        return -ENOMEM;
    }
    /* copy the passed in iovec to a temp structure */
    memcpy(orig_iovec, original_iovec, nr_segs * sizeof(*orig_iovec));
    begin_seg = 0;
repeat:
    for (seg = begin_seg; seg < nr_segs; seg++)
    {
        if (tmpnew_nr_segs >= max_new_nr_segs || sizes_count >= max_new_nr_segs)
        {
            kfree(sizes);
            kfree(orig_iovec);
            kfree(new_iovec);
            gossip_err("split_iovecs: exceeded the index limit (%lu)\n", 
                    tmpnew_nr_segs);
            return -EINVAL;
        }
        if (count + orig_iovec[seg].iov_len < pvfs_bufmap_size_query())
        {
            count += orig_iovec[seg].iov_len;
            
            memcpy(&new_iovec[tmpnew_nr_segs], &orig_iovec[seg], 
                    sizeof(*new_iovec));
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

static long bound_max_iovecs(const struct iovec *curr, unsigned long nr_segs, ssize_t *total_count)
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

#ifndef PVFS2_LINUX_KERNEL_2_4

#ifdef HAVE_OBSOLETE_STRUCT_PAGE_COUNT_NO_UNDERSCORE
#define pg_ref_count(pg) atomic_read(&(pg)->count)
#else
#define pg_ref_count(pg) atomic_read(&(pg)->_count)
#endif

/*
 * Cleaning up pages in the cache involves dropping the reference count
 * while cleaning up pages that were newly allocated involves unlocking
 * the page after indicating if there was an error in the page.
 */
static void cleanup_cache_pages(unsigned long page_idx,
                                struct rw_options *rw,
                                int error)
{
    unsigned long j;

    gossip_debug(GOSSIP_FILE_DEBUG, "cleaning up %ld memory pages\n", page_idx);
    /* and pinned existing ones as well */
    for (j = 0; j < page_idx; j++) {
        if (rw->dest.pages.pages[j]) {
            /* if the page was locked for I/O unlock it */
            if (rw->dest.pages.pg_byte_map[j]) {
                /* Mark if the page had errors */
                if (error < 0) {
                    gossip_lerr("Marking page %ld with error %d\n", j, error);
                    SetPageError(rw->dest.pages.pages[j]);
                }
                /* or if it is indeed uptodate */
                else {
                    gossip_debug(GOSSIP_FILE_DEBUG, "Marking page %ld uptodate\n", j);
                    SetPageUptodate(rw->dest.pages.pages[j]);
                }
                unlock_page(rw->dest.pages.pages[j]);
            } else {
                /* if it was already cached, decrement its use count */
                page_cache_release(rw->dest.pages.pages[j]);
            }
            gossip_debug(GOSSIP_FILE_DEBUG, "Releasing page %p (refcount %d)\n",
                    rw->dest.pages.pages[j], pg_ref_count(rw->dest.pages.pages[j]));
        }
    }
    kfree(rw->dest.pages.pages);
    rw->dest.pages.pages = NULL;
    rw->dest.pages.nr_pages = 0;
    kfree(rw->dest.pages.issue_pages);
    rw->dest.pages.issue_pages = NULL;
    kfree(rw->dest.pages.pg_byte_map);
    rw->dest.pages.pg_byte_map = NULL;
    rw->dest.pages.nr_issue_pages = 0;
    return;
}

/* callback from read_cache_pages.
 * What we are doing is aggregating all the pages in the cache
 * on which I/O needs to be issued against.
 * nr_issue_pages is a counter that keeps track of how many such
 * pages are there and issue_pages is the array that keeps track
 * of all the pointers to such pages.
 * All such pages are locked until the I/O completes or an error
 * happens.
 */
static int pvfs2_readpages_fill_cb(void *_data, struct page *page)
{
    struct rw_options *rw = (struct rw_options *) _data;

    gossip_debug(GOSSIP_FILE_DEBUG, "nr_issue: %ld page %p\n",
                 rw->dest.pages.nr_issue_pages, page);
    rw->dest.pages.issue_pages[rw->dest.pages.nr_issue_pages++] = page;
    return 0;
}


#if defined(HAVE_SPIN_LOCK_PAGE_ADDR_SPACE_STRUCT)
#define lock_mapping_tree(mapping) spin_lock(&mapping->page_lock)
#define unlock_mapping_tree(mapping) spin_unlock(&mapping->page_lock)
#elif defined(HAVE_RW_LOCK_TREE_ADDR_SPACE_STRUCT)
#define lock_mapping_tree(mapping) read_lock(&mapping->tree_lock)
#define unlock_mapping_tree(mapping) read_unlock(&mapping->tree_lock)
#elif defined(HAVE_SPIN_LOCK_TREE_ADDR_SPACE_STRUCT)
#define lock_mapping_tree(mapping) spin_lock(&mapping->tree_lock)
#define unlock_mapping_tree(mapping) spin_unlock(&mapping->tree_lock)
#elif defined(HAVE_RT_PRIV_LOCK_ADDR_SPACE_STRUCT)
#define lock_mapping_tree(mapping) spin_lock(&mapping->priv_lock)
#define unlock_mapping_tree(mapping) spin_unlock(&mapping->priv_lock)
#else
#define lock_mapping_tree(mapping) read_lock_irq(&mapping->tree_lock)
#define unlock_mapping_tree(mapping) read_unlock_irq(&mapping->tree_lock)
#endif

/* A debugging function to check the contents of a
 *  mapping's address space/radix tree
 */
static int check_mapping_tree(struct address_space *mapping,
                              size_t file_size) __attribute__((unused));
static int check_mapping_tree(struct address_space *mapping,
                              size_t file_size)
{
    unsigned long page_idx, begin_index, end_index, nr_to_read;

    begin_index = 0;
    end_index = (file_size - 1) >> PAGE_CACHE_SHIFT;
    nr_to_read = end_index - begin_index + 1;
    lock_mapping_tree(mapping);
    for (page_idx = 0; page_idx < nr_to_read; page_idx++) {
        struct page *page;
        pgoff_t page_offset = begin_index + page_idx;

        if (page_offset > end_index) {
            break;
        }
        page = radix_tree_lookup(&mapping->page_tree, page_offset);
        if (page) {
            gossip_debug(GOSSIP_FILE_DEBUG, "check:(%ld) HIT page %p (refcount %d)"
                                            "(page_offset %ld)\n",
                                            page_idx, page,
                                            pg_ref_count(page),
                                            page_offset);
        } else {
            gossip_debug(GOSSIP_FILE_DEBUG, "check: (%ld) MISS (page_offset %ld)\n",
                                            page_idx, page_offset);
        }
    }
    unlock_mapping_tree(mapping);
    return 0;
}
                            

/* Locate the pages of the file blocks from the page-cache and 
 * store them in the rw_options control block.
 * Note: if we don't locate, we allocate them.
 * After that we increment their ref count so that we know for sure that
 * they won't get swapped out.
 */
static int locate_file_pages(struct rw_options *rw, size_t total_size)
{
    struct address_space *mapping;
    loff_t offset, isize;
    unsigned long page_idx, begin_index, end_index, nr_to_read;
    int ret = 0;
    struct page *page;
    
    if (!rw ||  !rw->inode || !rw->off.io.offset || 
        !rw->inode->i_mapping) {
        gossip_lerr("invalid options\n");
        return -EINVAL;
    }
    isize = pvfs2_i_size_read(rw->inode);
    rw->copy_dest_type = COPY_DEST_PAGES;
    /* start with an empty page list */
    INIT_LIST_HEAD(&rw->dest.pages.page_list);
    mapping = rw->inode->i_mapping;
    offset = *(rw->off.io.offset);
    /* Return if the file size was 0 */
    if (isize == 0) {
        rw->dest.pages.nr_pages = 0;
        rw->dest.pages.pages = NULL;
        rw->dest.pages.nr_issue_pages = 0;
        rw->dest.pages.issue_pages = NULL;
        return 0;
    }
    begin_index = offset >> PAGE_CACHE_SHIFT;
    end_index = (unsigned long) (PVFS_util_min(isize - 1, (offset + total_size - 1))) >> PAGE_CACHE_SHIFT;
    gossip_debug(GOSSIP_FILE_DEBUG, "filp: %p, inode: %p, mapping: %p\n",
                                     rw->file, rw->inode, rw->inode->i_mapping);
    gossip_debug(GOSSIP_FILE_DEBUG, "isize: %ld, offset (%ld) + total_size (%ld): %ld\n",
                                     (long) isize, 
                                     (long) offset,
                                     (long) total_size, 
                                     (long) offset + total_size);
    gossip_debug(GOSSIP_FILE_DEBUG, "offset %lld, begin_index: %ld "
                                    "end_index: %ld requested total_size: %zd\n",
                                     offset, begin_index,
                                     end_index, total_size);
    nr_to_read = end_index - begin_index + 1;
    rw->dest.pages.nr_pages = nr_to_read;
    /* Allocate a byte map for all the pages */
    rw->dest.pages.pg_byte_map = kzalloc(nr_to_read *
                                         sizeof(*rw->dest.pages.pg_byte_map),
                                         PVFS2_BUFMAP_GFP_FLAGS);
    if (!rw->dest.pages.pg_byte_map) {
        gossip_err("could not allocate memory\n");
        return -ENOMEM;
    }
    /* and the array to hold the page pointers */
    rw->dest.pages.pages = kzalloc(nr_to_read * sizeof(*rw->dest.pages.pages), 
                                   PVFS2_BUFMAP_GFP_FLAGS);
    if (!rw->dest.pages.pages) {
        gossip_err("could not allocate memory\n");
        kfree(rw->dest.pages.pg_byte_map);
        return -ENOMEM;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "read %ld pages\n",
            nr_to_read);

    lock_mapping_tree(mapping);
    /* Preallocate all pages, increase their ref counts if they are in cache */
    for (page_idx = 0; page_idx < nr_to_read; page_idx++) {
        pgoff_t page_offset = begin_index + page_idx;

        if (page_offset > end_index) {
            break;
        }
        page = radix_tree_lookup(&mapping->page_tree, page_offset);
        if (page) {
            page_cache_get(page);
            gossip_debug(GOSSIP_FILE_DEBUG, "(%ld) HIT page %p (refcount %d)"
                                            "(page_offset %ld)\n",
                                            page_idx, page,
                                            pg_ref_count(page),
                                            page_offset);
            rw->dest.pages.pages[page_idx] = page;
            g_pvfs2_stats.cache_hits++;
            continue;
        }
        g_pvfs2_stats.cache_misses++;
        unlock_mapping_tree(mapping);
        /* Allocate, but don't add it to the LRU list yet */
        page = page_cache_alloc_cold(mapping);
        lock_mapping_tree(mapping);
        if (!page) {
            ret = -ENOMEM;
            gossip_err("could not allocate page cache\n");
            break;
        }
        page_cache_get(page);
        gossip_debug(GOSSIP_FILE_DEBUG, "(%ld) MISS page %p (refcount %d)"
                                        "(page_offset %ld)\n",
                                        page_idx, page,
                                        pg_ref_count(page),
                                        page_offset);
        page->index = page_offset;
        /* Add it to our internal private list */
        list_add(&page->lru, &rw->dest.pages.page_list);
        rw->dest.pages.pages[page_idx] = page;
        /* mark in the byte map */
        rw->dest.pages.pg_byte_map[page_idx] = 1;
        ret++;
    }
    unlock_mapping_tree(mapping);
    /* cleanup in case of error */
    if (ret < 0) {
        gossip_err("could not page_cache_alloc_cold\n");
        goto cleanup;
    }
    rw->dest.pages.nr_issue_pages = 0;
    /* if there is any need to issue I/O */
    if (ret > 0)
    {
        /* Allocate memory for the pages against which I/O needs to be issued */
        rw->dest.pages.issue_pages = kzalloc(ret * 
                                             sizeof(*rw->dest.pages.issue_pages), 
                                             PVFS2_BUFMAP_GFP_FLAGS);
        if (!rw->dest.pages.issue_pages) {
            gossip_err("could not allocate memory for issue_pages\n");
            ret = -ENOMEM;
            goto cleanup;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "issue %d I/O\n", ret);
        /* read_cache_pages can now be called on the list of pages */
        read_cache_pages(mapping, &rw->dest.pages.page_list, 
                               pvfs2_readpages_fill_cb, rw);
        BUG_ON(!list_empty(&rw->dest.pages.page_list));
        /* 
         * A failed read_cache_pages will be
         * indicated if
         * rw->dest.pages.nr_issues_pages != ret
         */
        if (rw->dest.pages.nr_issue_pages != ret) {
            gossip_err("read_cache_pages failed (%ld != %d)\n",
                 rw->dest.pages.nr_issue_pages, ret);
            ret = -ENOMEM;
            goto cleanup;
        }
    }
out:
    return ret;
cleanup:
    /* cleanup any of the allocated pagecache pages */
    cleanup_cache_pages(page_idx, rw, ret);
    goto out;
}

/*
 * Given an array of pages and a count of such pages, this function
 * returns
 * an error if the parameters/pages are invalid/similar
 * 0 if the pages are not contiguous on the file
 * 1 if the pages are contiguous on file
 */
static int are_contiguous(int nr_pages, struct page **page_array)
{
    int i;
    pgoff_t fpoffset;
    if (!page_array || nr_pages <= 0) {
        gossip_err("Bogus parameters %d, page_array: %p\n", nr_pages, page_array);
        return -EINVAL;
    }
    if (!page_array[0]) {
        gossip_err("Bogus parameters %p\n", page_array[0]);
        return -EINVAL;
    }
    fpoffset = page_array[0]->index;
    for (i = 1; i < nr_pages; i++) {
        if (!page_array[i]) {
            return -EINVAL;
        }
        if (page_array[i]->index == fpoffset) {
            gossip_err("2 pages have the same file offset (index 0 and %d)\n",
                    i);
            return -EINVAL;
        }
        /* not contiguous on file */
        if (page_array[i]->index != fpoffset + i) {
            gossip_debug(GOSSIP_FILE_DEBUG, "offset at index %d is non-contiguous\n", i);
            return 0;
        }
    }
    /* Cool. they are all contiguous */
    return 1;
}

/* Issue any I/O for regions not found in the cache 
 * NOTE: Try to be smart about whether to issue non-contiguous I/O
 * or contiguous I/O.
 */
static ssize_t wait_for_missing_io(struct rw_options *rw)
{
    ssize_t err = 0;

    if (rw->dest.pages.nr_issue_pages) {
        int contig_on_file = 0;

        gossip_debug(GOSSIP_FILE_DEBUG, "Number of pages for I/O issue %ld,"
                                        " total_size: %ld\n", 
                rw->dest.pages.nr_issue_pages
              , (rw->dest.pages.nr_issue_pages << PAGE_CACHE_SHIFT));
        /* scan through the issue pages array and see if we can submit a direct
         * contiguous request first.
         */
        contig_on_file = are_contiguous(rw->dest.pages.nr_issue_pages,
                rw->dest.pages.issue_pages);
        /* Any errors? */
        if (contig_on_file < 0) {
            err = contig_on_file;
            goto out;
        }
        /* contiguous or non-contiguous on file */
        else {
            struct iovec *uncached_vec = NULL;
            struct xtvec *uncached_xtvec = NULL;
            int i;
            size_t total_requested_io;

            total_requested_io = (rw->dest.pages.nr_issue_pages << PAGE_CACHE_SHIFT);
            uncached_vec = kzalloc(rw->dest.pages.nr_issue_pages *
                                   sizeof(*uncached_vec), PVFS2_BUFMAP_GFP_FLAGS);
            if (!uncached_vec) {
                gossip_err("out of memory allocating uncached_vec\n");
                err = -ENOMEM;
                goto out;
            }
            if (!contig_on_file)
            {
                uncached_xtvec = kzalloc(rw->dest.pages.nr_issue_pages * 
                                         sizeof(*uncached_xtvec), PVFS2_BUFMAP_GFP_FLAGS);
                if (!uncached_xtvec) {
                    gossip_err("out of memory allocating uncached_xtvec\n");
                    kfree(uncached_vec);
                    err = -ENOMEM;
                    goto out;
                }
            }
            for (i = 0; i < rw->dest.pages.nr_issue_pages; i++) {
                uncached_vec[i].iov_base = rw->dest.pages.issue_pages[i];
                uncached_vec[i].iov_len = PAGE_CACHE_SIZE;
#if 0
                gossip_debug(GOSSIP_FILE_DEBUG, "ISSUE: (%d) "
                        "iov_base: %p, iov_len: %zd \n",
                        i, uncached_vec[i].iov_base, 
                        uncached_vec[i].iov_len);
#endif
                if (!contig_on_file)
                {
                    uncached_xtvec[i].xtv_off = 
                        (rw->dest.pages.issue_pages[i]->index << PAGE_CACHE_SHIFT);
                    uncached_xtvec[i].xtv_len = PAGE_CACHE_SIZE;
                    gossip_debug(GOSSIP_FILE_DEBUG, 
                            "(%d) xtv_off = %zd, xtv_len = %zd\n",
                            i, (size_t) uncached_xtvec[i].xtv_off, 
                            uncached_xtvec[i].xtv_len);
                }
            }
            /* if all page cache pages are contiguous on file */
            if (contig_on_file) {
                /* issue a simple direct contiguous I/O call */
                err = wait_for_direct_io(rw,
                                         uncached_vec,
                                         rw->dest.pages.nr_issue_pages,
                                         total_requested_io);
            }
            else {
                /* else issue a complicated non-contig I/O call */
                err = wait_for_iox(rw, 
                                   uncached_vec, 
                                   rw->dest.pages.nr_issue_pages, 
                                   uncached_xtvec,
                                   rw->dest.pages.nr_issue_pages, 
                                   total_requested_io);
                kfree(uncached_xtvec);
            }
            kfree(uncached_vec);
            if (err < 0) {
                gossip_err("failed with error %zd\n",
                        (size_t) err);
                goto out;
            }
            gossip_debug(GOSSIP_FILE_DEBUG, "wait_for_missing_io: "
                    "transferred %zd, requested %zd\n",
                     (size_t) err, total_requested_io);
        }
    }
out:
    return err;
}

/*
 * NOTE: Currently only immutable files pass their I/O
 * through the cache.
 * Preparation for cached I/O requires that we locate all the file block
 * in the page-cache and stashing those pointers.
 * Returns the actual size of completed I/O.
 */
static ssize_t wait_for_cached_io(struct rw_options *old_rw, struct iovec *vec, 
        int nr_segs, size_t total_size)
{
    ssize_t err = 0, total_actual_io = 0;
    ssize_t ret = 0;
    struct rw_options rw;
    loff_t isize, offset;

    memcpy(&rw, old_rw, sizeof(rw));
    if (rw.type != IO_READV) {
        gossip_err("writes are not handled yet!\n");
        return -EOPNOTSUPP;
    }
    offset = *(rw.off.io.offset);
    isize = pvfs2_i_size_read(rw.inode);
    /* If our file offset was greater than file size, we should return 0 */
    if (offset >= isize) {
        return 0;
    }
    /* (Al)locate all the pages in the pagecache first */
    if ((err = locate_file_pages(&rw, total_size)) < 0) {
        gossip_err("error in locating pages %ld\n", (long) err);
        return err;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "total_size %zd, total # of pages %ld\n",
            total_size, rw.dest.pages.nr_pages);
    /* Issue and wait for I/O only for pages that are not uptodate 
     * or are not found in the cache 
     */
    if ((ret = wait_for_missing_io(&rw)) < 0) {
       gossip_err("wait_for_missing_io: error in waiting for missing I/O %ld\n"
                 ,(long)err);
        goto cleanup;
    }
    /* return value is basically file size minus current file offset */
    /* total_actual_io = isize - offset; */

    /* number of bytes to retrieve from the pagecache should be based on
     * the number of bytes returned from wait_for_missing_io, which executes
     * the io call with the number of bytes requested and returns the number
     * of bytes actually transferred.
    */
    total_actual_io = ret;

    gossip_debug(GOSSIP_FILE_DEBUG, "total_actual_io to be staged from "
                                    "page-cache %zd\n", total_actual_io);
    /* Copy the data from the page-cache to the application's address space */
    err = copy_from_pagecache(&rw, vec, nr_segs, total_actual_io);
    err = 0;
cleanup:
    cleanup_cache_pages(rw.dest.pages.nr_pages, &rw, err);
    return err == 0 ? total_actual_io : err;
}
#endif /* #ifndef PVFS2_LINUX_KERNEL_2_4 */

/*
 * Common entry point for read/write/readv/writev
 * This function will dispatch it to either the direct I/O
 * or buffered I/O path depending on the mount options and/or
 * augmented/extended metadata attached to the file.
 * Note: File extended attributes override any mount options.
 */
static ssize_t do_readv_writev(struct rw_options *rw)
{
    ssize_t ret, total_count;
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct file *file;
    unsigned int to_free;
    size_t count;
    const struct iovec *iov;
    unsigned long nr_segs, seg, new_nr_segs = 0;
    unsigned long max_new_nr_segs = 0;
    unsigned long  seg_count = 0;
    unsigned long *seg_array = NULL;
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
    offset = rw->off.io.offset;
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
        gossip_err("%s: Invalid iovec %p or nr_segs %lu\n",
                rw->fnstr, iov, nr_segs);
        goto out;
    }
    /* Compute total and max number of segments after split */
    if ((max_new_nr_segs = bound_max_iovecs(iov, nr_segs, &count)) < 0)
    {
        gossip_lerr("%s: could not bound iovec %lu\n", rw->fnstr
                                                     , max_new_nr_segs);
        goto out;
    }
    if (rw->type == IO_WRITEV)
    {
        if (!file)
        {
            gossip_err("%s: Invalid file pointer\n", rw->fnstr);
            goto out;
        }
        if (file->f_pos > pvfs2_i_size_read(inode))
        {
            pvfs2_i_size_write(inode, file->f_pos);
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
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: proceeding with offset : %llu, "
                                        "size %zd\n",
                                        rw->fnstr, llu(*offset), count);
    }
    if (count == 0)
    {
        ret = 0;
        goto out;
    }

    rw->count = count;
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
        ret = split_iovecs(max_new_nr_segs, /* IN */
			   nr_segs,         /* IN */
			   iov,             /* IN */
			   &new_nr_segs,    /* OUT */
			   &iovecptr,       /* OUT */
			   &seg_count,      /* OUT */
			   &seg_array);     /* OUT */ 
	if(ret < 0)
        {
            gossip_err("%s: Failed to split iovecs to satisfy larger "
                       " than blocksize readv/writev request %zd\n", rw->fnstr
                                                                   , ret);
            goto out;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: Splitting iovecs from %lu to %lu"
                                        " [max_new %lu]\n", 
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
        seg_array = &nr_segs;
        /* We dont have to free up anything */
        to_free = 0;
    }
    ptr = iovecptr;

    gossip_debug(GOSSIP_FILE_DEBUG, "%s %zd@%llu\n", 
            rw->fnstr, count, llu(*offset));
    gossip_debug(GOSSIP_FILE_DEBUG, "%s: new_nr_segs: %lu, seg_count: %lu\n", 
            rw->fnstr, new_nr_segs, seg_count);
#ifdef PVFS2_KERNEL_DEBUG
    for (seg = 0; seg < new_nr_segs; seg++)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: %d) %p to %p [%d bytes]\n", 
                rw->fnstr,
                (int)seg + 1, iovecptr[seg].iov_base, 
                iovecptr[seg].iov_base + iovecptr[seg].iov_len, 
                (int) iovecptr[seg].iov_len);
    }
    for (seg = 0; seg < seg_count; seg++)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: %zd) %lu\n",
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
#ifndef PVFS2_LINUX_KERNEL_2_4
        /* if a file is immutable, stage its I/O 
         * through the cache */
        if (IS_IMMUTABLE(rw->inode)) {
            /* Stage the I/O through the kernel's pagecache */
            ret = wait_for_cached_io(rw, ptr, seg_array[seg], each_count);
        }
        else 
#endif /* PVFS2_LINUX_KERNEL_2_4 */
        {
            /* push the I/O directly through to storage */
            ret = wait_for_direct_io(rw, ptr, seg_array[seg], each_count);
        }
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
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.readahead_size = readahead_size;
    rw.copy_to_user_addresses = copy_to_user;
    rw.fnstr = __FUNCTION__;
    vec.iov_base = buf;
    vec.iov_len  = count;
    rw.inode = inode;
    rw.pvfs2_inode = PVFS2_I(inode);
    rw.file = NULL;
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;
    rw.off.io.offset = offset;
    g_pvfs2_stats.reads++;
    return do_readv_writev(&rw); 
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
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    vec.iov_base = buf;
    vec.iov_len  = count;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.file = file;
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;
    rw.off.io.offset = offset;

    if (IS_IMMUTABLE(rw.inode)) 
    {
        rw.readahead_size = (rw.inode)->i_size;
    }
    else 
    {
        rw.readahead_size = 0;
    }
    g_pvfs2_stats.reads++;


    return do_readv_writev(&rw);
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
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.readahead_size = 0;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    vec.iov_base  = (char *) buf;
    vec.iov_len   = count;
    rw.file = file;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = &vec;
    rw.dest.address.nr_segs = 1;
    rw.off.io.offset = offset;
    g_pvfs2_stats.writes++;
    return do_readv_writev(&rw);
}

/* compat code, < 2.6.19 */
#ifndef HAVE_COMBINED_AIO_AND_VECTOR
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
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.file  = file;
    rw.dest.address.iov = (struct iovec *) iov;
    rw.dest.address.nr_segs = nr_segs;
    rw.off.io.offset = offset;
    rw.readahead_size = 0;
    g_pvfs2_stats.reads++;
    return do_readv_writev(&rw);
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
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.readahead_size = 0;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    rw.file = file;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = (struct iovec *) iov;
    rw.dest.address.nr_segs = nr_segs;
    rw.off.io.offset = offset;

    g_pvfs2_stats.writes++;
    return do_readv_writev(&rw);
}
#endif


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

    *trailer_size = seg_count * sizeof(*rwx);
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
 * of larger than (pvfs_bufmap_size_query()) 
 * (default is PVFS2_BUFMAP_DEFAULT_DESC_SIZE MB).
 * What that means is that
 * we will create a new xtvec descriptor for those file offsets that 
 * go beyond the limit
 * Return value for this routine is -ve in case of errors
 * and 0 in case of success.
 * Further, the new_nr_segs pointer is updated to hold the new value
 * of number of xtvecs, the new_xtvec pointer is updated to hold the pointer
 * to the new split xtvec, and the size array is an array of integers holding
 * the number of xtvecs that straddle (pvfs_bufmap_size_query()).
 * The max_new_nr_segs value is computed by the caller and passed in.
 * (It will be (count of all xtv_len/ block_size) + 1).
 */
static int split_xtvecs(
		unsigned long max_new_nr_segs,      /* IN */
		unsigned long nr_segs,              /* IN */
		const struct xtvec *original_xtvec, /* IN */
		unsigned long *new_nr_segs,         /* OUT */
		struct xtvec **new_vec,  	    /* OUT */
		unsigned long *seg_count,           /* OUT */
		unsigned long **seg_array)          /* OUT */
{
    unsigned long seg, count, begin_seg, tmpnew_nr_segs;
    struct xtvec *new_xtvec = NULL, *orig_xtvec;
    unsigned long *sizes = NULL, sizes_count = 0;

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
    orig_xtvec = kmalloc(nr_segs * sizeof(*orig_xtvec), PVFS2_BUFMAP_GFP_FLAGS);
    if (orig_xtvec == NULL)
    {
        gossip_err("split_xtvecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(nr_segs * sizeof(*orig_xtvec)));
        return -ENOMEM;
    }
    new_xtvec = kzalloc(max_new_nr_segs * sizeof(*new_xtvec), 
            PVFS2_BUFMAP_GFP_FLAGS);
    if (new_xtvec == NULL)
    {
        kfree(orig_xtvec);
        gossip_err("split_xtvecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(*new_xtvec)));
        return -ENOMEM;
    }
    sizes = kzalloc(max_new_nr_segs * sizeof(*sizes), PVFS2_BUFMAP_GFP_FLAGS);
    if (sizes == NULL)
    {
        kfree(new_xtvec);
        kfree(orig_xtvec);
        gossip_err("split_xtvecs: Could not allocate memory for %lu bytes!\n", 
                (unsigned long)(max_new_nr_segs * sizeof(*sizes)));
        return -ENOMEM;
    }
    /* copy the passed in xtvec to a temp structure */
    memcpy(orig_xtvec, original_xtvec, nr_segs * sizeof(*orig_xtvec));
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
            gossip_err("split_xtvecs: exceeded the index limit (%lu)\n", 
			    tmpnew_nr_segs);
            return -EINVAL;
        }
        if (count + orig_xtvec[seg].xtv_len < pvfs_bufmap_size_query())
        {
            count += orig_xtvec[seg].xtv_len;
            
            memcpy(&new_xtvec[tmpnew_nr_segs], &orig_xtvec[seg], 
                    sizeof(*new_xtvec));
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
bound_max_xtvecs(const struct xtvec *curr, unsigned long nr_segs, size_t *total_count)
{
    unsigned long i;
    long max_nr_xtvecs;
    size_t total, count;

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

/*
 * Post and wait for the I/O upcall to finish.
 * @rw  - contains state information to initiate the I/O operation
 * @vec - contains the memory regions
 * @nr_segs - number of memory vector regions
 * @xtvec - contains the file regions
 * @xtnr_segs - number of file vector regions
 */
static ssize_t wait_for_iox(struct rw_options *rw, 
			    struct iovec *vec, 
			    unsigned long nr_segs,
			    struct xtvec *xtvec, 
		            unsigned long xtnr_segs, 
                            size_t total_size)
{
    pvfs2_kernel_op_t *new_op = NULL;
    int buffer_index = -1;
    ssize_t ret;

    if (!rw || !vec || nr_segs < 0 || total_size <= 0
            || !xtvec || xtnr_segs < 0)
    {
        gossip_lerr("invalid parameters (rw: %p, vec: %p, nr_segs: %lu, "
                "xtvec %p, xtnr_segs %lu, total_size: %zd\n", rw, vec, nr_segs,
                xtvec, xtnr_segs, total_size);
        ret = -EINVAL;
        goto out;
    }
    if (!rw->pvfs2_inode || !rw->inode || !rw->fnstr)
    {
        gossip_lerr("invalid parameters (pvfs2_inode: %p, inode: %p, fnstr: %p\n",
                rw->pvfs2_inode, rw->inode, rw->fnstr);
        ret = -EINVAL;
        goto out;
    }
    new_op = op_alloc_trailer(PVFS2_VFS_OP_FILE_IOX);
    if (!new_op)
    {
        ret = -ENOMEM;
        goto out;
    }
    new_op->upcall.req.iox.io_type = 
        (rw->type == IO_READX) ? PVFS_IO_READ : PVFS_IO_WRITE;
    new_op->upcall.req.iox.refn = rw->pvfs2_inode->refn;

    /* get a shared buffer index */
    ret = pvfs_bufmap_get(&buffer_index);
    if (ret < 0)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_get() "
                    "failure (%ld)\n", rw->fnstr, (long) ret);
        goto out;
    }
    new_op->upcall.req.iox.buf_index = buffer_index;
    new_op->upcall.req.iox.count     = total_size;
    /* construct the upcall trailer buffer */
    if ((ret = construct_file_offset_trailer(&new_op->upcall.trailer_buf, 
                    &new_op->upcall.trailer_size, xtnr_segs, xtvec)) < 0)
    {
        gossip_err("%s: construct_file_offset_trailer "
                "failure (%ld)\n", rw->fnstr, (long) ret);
        goto out;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "%s: copy_to_user %d nr_segs %lu, "
            "xtnr_segs: %lu "
            "total_size: %zd "
            "copy_dst_type %d\n",
            rw->fnstr, rw->copy_to_user_addresses, 
            nr_segs, xtnr_segs,
            total_size, rw->copy_dest_type);

    /* Stage 1: Copy in buffers */
    if ((ret = precopy_buffers(buffer_index, rw, vec, nr_segs, total_size)) < 0) {
        goto out;
    }
    /* Stage 2: whew! finally service this operation */
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
                rw->type == IO_READX ? "noncontig read from" : "noncontig write to",
                llu(get_handle_from_ino(rw->inode)),
                (rw->file && rw->file->f_dentry && rw->file->f_dentry->d_name.name ?
                     (char *) rw->file->f_dentry->d_name.name : "UNKNOWN"),
                    (long) ret);
          }
          goto out;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "downcall returned %lld\n",
            llu(new_op->downcall.resp.iox.amt_complete));
    /* Stage 3: Post copy buffers */
    if ((ret = postcopy_buffers(buffer_index, rw, vec, nr_segs, 
                    new_op->downcall.resp.iox.amt_complete)) < 0) {
        /* put error codes in downcall so that handle_io_error()
         * preserves it properly */
        new_op->downcall.status = ret;
        handle_io_error();
        goto out;
    }
    ret = new_op->downcall.resp.iox.amt_complete;
    gossip_debug(GOSSIP_FILE_DEBUG, "wait_for_iox returning %ld\n", (long) ret);
     /*
      tell the device file owner waiting on I/O that this I/O has
      completed and it can return now.  in this exact case, on
      wakeup the device will free the op, so we *cannot* touch it
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
        if (new_op->upcall.trailer_buf)
            vfree(new_op->upcall.trailer_buf);
        op_release(new_op);
        new_op = NULL;
    }
    return ret;
}

static ssize_t do_readx_writex(struct rw_options *rw)
{
    ssize_t ret, total_count;
    size_t count_mem, count_stream;
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;
    unsigned int to_free;
    const struct iovec *iov; 
    unsigned long seg, nr_segs, xtnr_segs;
    struct xtvec *xtvec; 
    unsigned long max_new_nr_segs_mem, max_new_nr_segs_stream;
    unsigned long new_nr_segs_mem = 0, new_nr_segs_stream = 0;
    unsigned long seg_count_mem, *seg_array_mem = NULL;
    unsigned long seg_count_stream, *seg_array_stream = NULL;
    struct iovec *iovecptr = NULL, *ptr = NULL;
    struct xtvec *xtvecptr = NULL, *xptr = NULL;

    total_count = 0;
    ret = -EINVAL;
    to_free = 0;
    inode = NULL;
    count_mem = 0;
    max_new_nr_segs_mem = 0;
    count_stream = 0;
    max_new_nr_segs_stream = 0;

    if (!rw || !rw->fnstr)
    {
        gossip_lerr("Invalid parameters\n");
        goto out;
    }
    inode = rw->inode;
    if (!inode)
    {
        gossip_err("%s: invalid inode\n", rw->fnstr);
        goto out;
    }
    pvfs2_inode = rw->pvfs2_inode;
    if (!pvfs2_inode)
    {
        gossip_err("%s: Invalid pvfs2 inode\n", rw->fnstr);
        goto out;
    }
    iov  = rw->dest.address.iov;
    nr_segs = rw->dest.address.nr_segs;
    if (iov == NULL || nr_segs < 0)
    {
        gossip_err("%s: Invalid iovec %p or nr_segs %lu\n",
                rw->fnstr, iov, nr_segs);
        goto out;
    }
    /* Compute total and max number of segments after split of the memory vector */
    if ((max_new_nr_segs_mem = bound_max_iovecs(iov, nr_segs, &count_mem)) < 0)
    {
        gossip_lerr("%s: could not bound iovec %lu\n", rw->fnstr, max_new_nr_segs_mem);
        goto out;
    }
    xtvec = rw->off.iox.xtvec;
    xtnr_segs = rw->off.iox.xtnr_segs;
    if (xtvec == NULL || xtnr_segs < 0)
    {
        gossip_err("%s: Invalid xtvec %p or xtnr_segs %lu\n",
                rw->fnstr, xtvec, xtnr_segs);
        goto out;
    }
    /* Calculate the total stream length amd max segments after split of the stream vector */
    if ((max_new_nr_segs_stream = bound_max_xtvecs(xtvec, xtnr_segs, &count_stream)) < 0)
    {
        gossip_lerr("%s: could not bound xtvec %lu\n", rw->fnstr, max_new_nr_segs_stream);
        goto out;
    }
    if (count_mem == 0)
    {
        return 0;
    }
    if (count_mem != count_stream) 
    {
        gossip_err("%s: mem count %ld != stream count %ld\n",
                rw->fnstr, (long) count_mem, (long) count_stream);
        goto out;
    }
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
        ret = split_iovecs(max_new_nr_segs_mem, /* IN */
			   nr_segs, 		/* IN */
			   iov, 		/* IN */
			   &new_nr_segs_mem,    /* OUT */
			   &iovecptr, 		/* OUT */
			   &seg_count_mem, 	/* OUT */
			   &seg_array_mem);     /* OUT */
        if(ret < 0)
        {
            gossip_err("%s: Failed to split iovecs to satisfy larger "
                    " than blocksize readx request %ld\n", rw->fnstr, (long) ret);
            goto out;
        }
        /* We must free seg_array_mem and iovecptr, xtvecptr and seg_array_stream */
        to_free = 1;
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: Splitting iovecs from %lu to %lu [max_new %lu]\n", 
                rw->fnstr, nr_segs, new_nr_segs_mem, max_new_nr_segs_mem);
        /* 
         * Split up the given xtvec description such that
         * no xtvec descriptor straddles over the block-size limitation.
         */
        ret = split_xtvecs(max_new_nr_segs_stream, /* IN */
			   xtnr_segs,              /* IN */
			   xtvec,  		   /* IN */
                           &new_nr_segs_stream,    /* OUT */
			   &xtvecptr, 		   /* OUT */
                           &seg_count_stream,      /* OUT */
			   &seg_array_stream);     /* OUT */
        if(ret < 0)
        {
            gossip_err("Failed to split iovecs to satisfy larger "
                    " than blocksize readx request %ld\n", (long) ret);
            goto out;
        }
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: Splitting xtvecs from %lu to %lu [max_new %lu]\n", 
                rw->fnstr, xtnr_segs, new_nr_segs_stream, max_new_nr_segs_stream);
    }
    else 
    {
        new_nr_segs_mem = nr_segs;
        /* use the given iovec description */
        iovecptr = (struct iovec *) iov;
        /* There is only 1 element in the seg_array_mem */
        seg_count_mem = 1;
        /* and its value is the number of segments passed in */
        seg_array_mem = &nr_segs;
        
        new_nr_segs_stream = xtnr_segs;
        /* use the given file description */
        xtvecptr = (struct xtvec *) xtvec;
        /* There is only 1 element in the seg_array_stream */
        seg_count_stream = 1;
        /* and its value is the number of segments passed in */
        seg_array_stream = &xtnr_segs;
        /* We dont have to free up anything */
        to_free = 0;
    }
#ifdef PVFS2_KERNEL_DEBUG
    for (seg = 0; seg < new_nr_segs_mem; seg++)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: %d) %p to %p [%ld bytes]\n",
                rw->fnstr,
                seg + 1, iovecptr[seg].iov_base,
                iovecptr[seg].iov_base + iovecptr[seg].iov_len,
                (long) iovecptr[seg].iov_len);
    }
    for (seg = 0; seg < new_nr_segs_stream; seg++)
    {
        gossip_debug(GOSSIP_FILE_DEBUG, "%s: %d) %ld to %ld [%ld bytes]\n",
                rw->fnstr,
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
        size_t  each_count, amt_complete;

        /* how much to transfer in this loop iteration */
        each_count = (((count_mem - total_count) > pvfs_bufmap_size_query()) ?
                      pvfs_bufmap_size_query() : (count_mem - total_count));
        /* and push the I/O directly through to the servers */
        ret = wait_for_iox(rw, ptr, seg_array_mem[seg],
                xptr, seg_array_stream[seg], each_count);
        if (ret < 0)
        {
            goto out;
        }
        /* Advance the iovec pointer */
        ptr += seg_array_mem[seg];
        /* Advance the xtvec pointer */
        xptr += seg_array_stream[seg];
        seg++;
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
        kfree(seg_array_mem);
        kfree(xtvecptr);
        kfree(seg_array_stream);
    }
    if (ret > 0 && inode != NULL && pvfs2_inode != NULL)
    {
        if (rw->type == IO_READX)
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

#ifndef HAVE_READX_FILE_OPERATIONS
static ssize_t pvfs2_file_readx(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    const struct xtvec *xtvec,
    unsigned long xtnr_segs) __attribute__((unused));
#endif
static ssize_t pvfs2_file_readx(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    const struct xtvec *xtvec,
    unsigned long xtnr_segs)
{
    struct rw_options rw;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_READX;
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.file  = file;
    rw.dest.address.iov = (struct iovec *) iov;
    rw.dest.address.nr_segs = nr_segs;
    rw.off.iox.xtvec = (struct xtvec *) xtvec;
    rw.off.iox.xtnr_segs = xtnr_segs;
    g_pvfs2_stats.reads++;
    return do_readx_writex(&rw);
}

#ifndef HAVE_WRITEX_FILE_OPERATIONS
static ssize_t pvfs2_file_writex(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    const struct xtvec *xtvec,
    unsigned long xtnr_segs) __attribute__((unused));
#endif
static ssize_t pvfs2_file_writex(
    struct file *file,
    const struct iovec *iov,
    unsigned long nr_segs,
    const struct xtvec *xtvec,
    unsigned long xtnr_segs)
{
    struct rw_options rw;

    memset(&rw, 0, sizeof(rw));
    rw.async = 0;
    rw.type = IO_WRITEX;
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    rw.inode = file->f_dentry->d_inode;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.file  = file;
    rw.dest.address.iov = (struct iovec *) iov;
    rw.dest.address.nr_segs = nr_segs;
    rw.off.iox.xtvec = (struct xtvec *) xtvec;
    rw.off.iox.xtnr_segs = xtnr_segs;
    g_pvfs2_stats.writes++;
    return do_readx_writex(&rw);
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
        gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_retry: iov %p,"
                " size %d return %d bytes\n",
                    x->iov, (int) x->bytes_to_be_copied, (int) error);
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
        if (x->iov) 
        {
            kfree(x->iov);
            x->iov = NULL;
        }
        x->needs_cleanup = 0;
    }
    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_aio_dtor: kiocb_release %p\n", x);
    kiocb_release(x);
    iocb->private = NULL;
    return;
}

static inline int 
fill_default_kiocb(pvfs2_kiocb *x,
        struct task_struct *tsk,
        struct kiocb *iocb, int rw,
        int buffer_index, pvfs2_kernel_op_t *op, 
        const struct iovec *iovec, unsigned long nr_segs,
        loff_t offset, size_t count,
        int (*aio_cancel)(struct kiocb *, struct io_event *))
{
    x->tsk = tsk;
    x->kiocb = iocb;
    x->buffer_index = buffer_index;
    x->op = op;
    x->rw = rw;
    x->bytes_to_be_copied = count;
    x->offset = offset;
    x->bytes_copied = 0;
    x->needs_cleanup = 1;
    iocb->ki_cancel = aio_cancel;
    /* Allocate a private pointer to store the
     * iovector since the caller could pass in a
     * local variable for the iovector.
     */
    x->iov = kmalloc(nr_segs * sizeof(*x->iov), PVFS2_BUFMAP_GFP_FLAGS);
    if (x->iov == NULL) 
    {
        return -ENOMEM;
    }
    memcpy(x->iov, iovec, nr_segs * sizeof(*x->iov));
    x->nr_segs = nr_segs;
    return 0;
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
 * TODO: We handle vectored aio requests now but we do
 * not handle the case where the total size of IO is
 * larger than our FS transfer block size (4 MB
 * default).
 */
static ssize_t do_aio_read_write(struct rw_options *rw)
{
    struct file *filp;
    struct inode *inode;
    ssize_t error;
    pvfs2_inode_t *pvfs2_inode;
    const struct iovec *iov;
    unsigned long nr_segs, max_new_nr_segs;
    size_t count;
    struct kiocb *iocb;
    loff_t *offset;
    pvfs2_kiocb *x;

    error = -EINVAL;
    if (!rw || !rw->fnstr || !rw->off.io.offset)
    {
        gossip_lerr("Invalid parameters (rw %p)\n", rw);
        goto out_error;
    }
    inode = rw->inode;
    filp  = rw->file;
    iocb  = rw->iocb;
    pvfs2_inode = rw->pvfs2_inode;
    offset = rw->off.io.offset;
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
    count = 0;
    /* Compute total and max number of segments after split */
    if ((max_new_nr_segs = bound_max_iovecs(iov, nr_segs, &count)) < 0)
    {
        gossip_lerr("%s: could not bound iovecs %ld\n", rw->fnstr, max_new_nr_segs);
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
        error = do_readv_writev(rw);
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
             * copy the data from the application for writes.
             * We could return -EIOCBRETRY here and have 
             * the data copied in the pvfs2_aio_retry routine,
             * I dont see too much point in doing that
             * since the app would have touched the
             * memory pages prior to the write and
             * hence accesses to the page won't block.
             */
            if (rw->copy_to_user_addresses) 
            {
                error = pvfs_bufmap_copy_iovec_from_user(
                        buffer_index,
                        iov,
                        nr_segs,
                        count);
            } 
            else 
            {
                error = pvfs_bufmap_copy_iovec_from_kernel(
                        buffer_index,
                        iov,
                        nr_segs,
                        count);
            }
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
         * We need to set the cancellation callbacks + 
         * other state information
         * here if the asynchronous request is going to
         * be successfully submitted 
         */
        error = fill_default_kiocb(x, current, iocb, 
                                   (rw->type == IO_READ) ? PVFS_IO_READ : PVFS_IO_WRITE,
                                   buffer_index,
                                   new_op, iov, nr_segs,
                                   *offset, count,
                                   &pvfs2_aio_cancel);
        if (error != 0) 
        {
            kiocb_release(x);
            /* drop the buffer index */
            pvfs_bufmap_put(buffer_index);
            gossip_debug(GOSSIP_FILE_DEBUG, "%s: pvfs_bufmap_put %d\n",
                    rw->fnstr, buffer_index);
            /* drop the reference count and deallocate */
            put_op(new_op);
            goto out_error;
        }
        /* 
         * destructor function to make sure that we free
         * up this allocated piece of memory 
         */
        iocb->ki_dtor = pvfs2_aio_dtor;
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

static ssize_t pvfs2_file_aio_read_iovec(struct kiocb *iocb,
                                         const struct iovec *iov,
                                         unsigned long nr_segs, loff_t offset)
{
    struct rw_options rw;
    memset(&rw, 0, sizeof(rw));
    rw.async = !is_sync_kiocb(iocb);
    rw.type = IO_READ;
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.off.io.offset = &offset;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    rw.iocb = iocb;
    rw.file = iocb->ki_filp;
    if (!rw.file || !(rw.file)->f_mapping)
    {
        return -EINVAL;
    }
    rw.inode = (rw.file)->f_mapping->host;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = iov;
    rw.dest.address.nr_segs = nr_segs;
    rw.readahead_size = 0;
    g_pvfs2_stats.reads++;
    return do_aio_read_write(&rw);
}

static ssize_t pvfs2_file_aio_write_iovec(struct kiocb *iocb,
                                          const struct iovec *iov,
                                          unsigned long nr_segs, loff_t offset)
{
    struct rw_options rw;

    memset(&rw, 0, sizeof(rw));
    rw.async = !is_sync_kiocb(iocb);
    rw.type = IO_WRITE;
    rw.copy_dest_type = COPY_DEST_ADDRESSES;
    rw.readahead_size = 0;
    rw.off.io.offset = &offset;
    rw.copy_to_user_addresses = 1;
    rw.fnstr = __FUNCTION__;
    rw.iocb = iocb;
    rw.file = iocb->ki_filp;
    if (!rw.file || !(rw.file)->f_mapping)
    {
        return -EINVAL;
    }
    rw.inode = (rw.file)->f_mapping->host;
    rw.pvfs2_inode = PVFS2_I(rw.inode);
    rw.dest.address.iov = iov;
    rw.dest.address.nr_segs = nr_segs;
    g_pvfs2_stats.writes++;
    return do_aio_read_write(&rw);
}

/* compat functions for < 2.6.19 */
#ifndef HAVE_COMBINED_AIO_AND_VECTOR
static ssize_t 
pvfs2_file_aio_read(struct kiocb *iocb, char __user *buffer,
        size_t count, loff_t offset)

{
    struct iovec iov = {
        .iov_base = buffer,
        .iov_len = count,
    };
    return pvfs2_file_aio_read_iovec(iocb, &iov, 1, offset);
}

static ssize_t 
pvfs2_file_aio_write(struct kiocb *iocb, const char __user *buffer,
        size_t count, loff_t offset)
{
    struct iovec iov = {
        .iov_base = (void __user *) buffer,  /* discard const so it fits */
        .iov_len = count,
    };
    return pvfs2_file_aio_write_iovec(iocb, &iov, 1, offset);
}
#endif
#endif  /* HAVE_AIO_VFS_SUPPORT */

/** Perform a miscellaneous operation on a file.
 */

#ifdef HAVE_NO_FS_IOC_FLAGS
int pvfs2_ioctl(
        struct inode *inode,
        struct file *file,
        unsigned int cmd,
        unsigned long arg)
{
    return -ENOTTY;
}
#else

int pvfs2_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int cmd,
    unsigned long arg)
{
    int ret = -ENOTTY;
    uint64_t val = 0;
    unsigned long uval;

    gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_ioctl: called with cmd %d\n", cmd);

    /* we understand some general ioctls on files, such as the immutable
     * and append flags
     */
    if(cmd == FS_IOC_GETFLAGS)
    {
        val = 0;
        ret = pvfs2_xattr_get_default(inode,
                                      "user.pvfs2.meta_hint",
                                      &val, sizeof(val));
        if(ret < 0 && ret != -ENODATA)
        {
            return ret;
        }
        else if(ret == -ENODATA)
        {
            val = 0;
        }
        uval = val;
        gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_ioctl: FS_IOC_GETFLAGS: %llu\n",
                     (unsigned long long)uval);
        return put_user(uval, (int __user *)arg);
    }
    else if(cmd == FS_IOC_SETFLAGS)
    {
        ret = 0;
        if(get_user(uval, (int __user *)arg))
        {
            return -EFAULT;
        }
        if(uval & (~(FS_IMMUTABLE_FL|FS_APPEND_FL|FS_NOATIME_FL)))
        {
            gossip_err("pvfs2_ioctl: the FS_IOC_SETFLAGS only supports setting "
                       "one of FS_IMMUTABLE_FL|FS_APPEND_FL|FS_NOATIME_FL\n");
            return -EINVAL;
        }
        val = uval;
        gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_ioctl: FS_IOC_SETFLAGS: %llu\n",
                     (unsigned long long)val);
        ret = pvfs2_xattr_set_default(inode,
                                      "user.pvfs2.meta_hint",
                                      &val, sizeof(val), 0);
    }

    return ret;
}
#endif

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

#ifndef HAVE_MAPPING_NRPAGES_MACRO
#define mapping_nrpages(idata) (idata)->nrpages
#endif

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
        mapping_nrpages(&file->f_dentry->d_inode->i_data))
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
            gossip_debug(GOSSIP_FILE_DEBUG, "%s:%s:%d calling make bad inode\n", __FILE__,  __func__, __LINE__);
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

    isize = pvfs2_i_size_read(inode);
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

int pvfs2_lock(struct file *f, int flags, struct file_lock *lock)
{
    return -ENOSYS;
}

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
#ifdef HAVE_COMBINED_AIO_AND_VECTOR
    /* for >= 2.6.19 */
#ifdef HAVE_AIO_VFS_SUPPORT
    .aio_read = pvfs2_file_aio_read_iovec,
    .aio_write = pvfs2_file_aio_write_iovec,
#endif
    .lock = pvfs2_lock,
#else
    .readv = pvfs2_file_readv,
    .writev = pvfs2_file_writev,
#  ifdef HAVE_AIO_VFS_SUPPORT
    .aio_read = pvfs2_file_aio_read,
    .aio_write = pvfs2_file_aio_write,
#  endif
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
    .lock = pvfs2_lock,
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS inode operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

/* defined in file.c */
extern ssize_t pvfs2_inode_read(
    struct inode *inode,
    char *buf,
    size_t count,
    loff_t *offset,
    int copy_to_user,
    loff_t readahead_size);

extern struct file_operations pvfs2_file_operations;
extern struct inode_operations pvfs2_symlink_inode_operations;
extern struct inode_operations pvfs2_dir_inode_operations;
extern struct file_operations pvfs2_dir_operations;


/** Read page-sized blocks from file.  This code is only used in the mmap
 *  path.
 *
 *  \note Current implementation ignores max_blocks parameter and always
 *        reads exactly one block.  Because the only time this function
 *        seems to be called is from pvfs2_get_block(), this appears to be
 *        ok.
 */
static int pvfs2_get_blocks(
    struct inode *inode,
    sector_t lblock,
    unsigned long max_blocks,
    struct buffer_head *bh_result,
    int create)
{
    int ret = -EIO;
    uint32_t max_block = 0;
    ssize_t bytes_read = 0;
    struct page *page = NULL;
    void *page_data = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    /*
      We're faking our inode block size to be PAGE_CACHE_SIZE
      to play nicely with the page cache.

      In some reality, inode->i_blksize != PAGE_CACHE_SIZE and
      inode->i_blkbits != PAGE_CACHE_SHIFT
    */
    const uint32_t blocksize = PAGE_CACHE_SIZE;  /* inode->i_blksize */
    const uint32_t blockbits = PAGE_CACHE_SHIFT; /* inode->i_blkbits */

    /* check assumption before continuing */
    if (max_blocks != 1) {
	pvfs2_error("pvfs2_get_blocks called with invalid max_blocks (%lu)\n",
		    max_blocks);
    }

    pvfs2_print("pvfs2_get_blocks called for lblock %lu\n",
                (unsigned long)lblock);

    /*
      this is a quick workaround for the case when a sequence
      of sequential blocks are needed to be read, but the
      operation that issued it was cancelled by a signal.
      if we detect a sequential pattern of failures, don't
      even bother issuing the upcall since that's way more
      expensive to do and it will also fail due to the signal
      raised.

      the first failure will have the failed block as lblock-1,
      but the mpage code (the caller) tries twice in a row, so the
      second time, the failed block index will be == to lblock.

      NOTE: the easiest way to test this is to run a program
      from a pvfs2 volume and ctrl-c it before it's fully
      read into memory.
    */
    if ((pvfs2_inode->last_failed_block_index_read > 0) &&
        ((pvfs2_inode->last_failed_block_index_read == (lblock - 1)) ||
         (pvfs2_inode->last_failed_block_index_read == lblock)))
    {
        pvfs2_print("pvfs2: skipping get_block on index %d due "
                    "to previous failure.\n", (int)lblock);
        pvfs2_inode->last_failed_block_index_read = lblock;
        /*
          NOTE: we don't need to worry about cleaning up the
          mmap_ra_cache in userspace from here because on I/O
          failure, the pvfs2-client-core will be restarted
        */
        return -EIO;
    }

    page = bh_result->b_page;
    page_data = pvfs2_kmap(page);

    /*
      NOTE: this unsafely *assumes* that the size stored in the inode
      is accurate.

      make sure we're not looking for a page block past the end of
      this file;
    */
    max_block = ((inode->i_size / blocksize) + 1);
    if (page->index < max_block)
    {
        loff_t blockptr_offset =
            (((loff_t)page->index) << blockbits);

        /*
          NOTE: This is conceptually backwards.  we could be
          implementing the file_read as generic_file_read and doing
          the actual i/o here (via readpage).  There are *no* plans to
          do this.

          The main reason it's not like that now is because of the
          mismatch of page cache size and the inode blocksize that
          we're using.  It's more efficient in the general case to use
          the larger blocksize (~4M rather than ~4K) for reading &
          writing (via pvfs2_inode_read/pvfs2_file_write).  For now it
          seems that this call can *only* handle reads of
          PAGE_CACHE_SIZE blocks, which is terribly slow for us.

          set the readahead size to be the entire file size so that
          subsequent calls have the opportunity to be userspace read
          cache hits; any readahead data the client pulls in is
          flushed (both from userspace and the page cache) on vfs file
          close

	  ALSO NOTE: This call actually only reads one PAGE_CACHE_SIZE
	  block (blocksize), even though we might have been asked to
	  read more than that. -- RobR
        */
        bytes_read = pvfs2_inode_read(
            inode, page_data, blocksize, &blockptr_offset, 0,
            inode->i_size);

        if (bytes_read < 0)
        {
            pvfs2_print("pvfs_get_blocks: failed to read page block %d\n",
                        (int)page->index);
            pvfs2_inode->last_failed_block_index_read = page->index;
        }
        else
        {
            pvfs2_print("pvfs2_get_blocks: read %d bytes | offset is %d | "
                        "cur_block is %d | max_block is %d\n",
                        (int)bytes_read, (int)blockptr_offset,
                        (int)page->index, (int)max_block);
            pvfs2_inode->last_failed_block_index_read = 0;
        }
    }

    /* only zero remaining unread portions of the page data */
    if (bytes_read > 0)
    {
        memset(page_data + bytes_read, 0, blocksize - bytes_read);
    }
    else
    {
        memset(page_data, 0, blocksize);
    }

    /* takes care of potential aliasing */
    flush_dcache_page(page);
    pvfs2_kunmap(page);

    if (bytes_read < 0)
    {
        ret = bytes_read;
        SetPageError(page);
    }
    else
    {
        SetPageUptodate(page);
        if (PageError(page))
        {
            ClearPageError(page);
        }

        bh_result->b_data = page_data;
        bh_result->b_size = bytes_read;

#ifdef PVFS2_LINUX_KERNEL_2_4
        set_bit(BH_Mapped, &(bh_result)->b_state);
        mark_buffer_uptodate(bh_result, 1);
        bh_result->b_blocknr = lblock;
#else
        map_bh(bh_result, inode->i_sb, lblock);
        set_buffer_uptodate(bh_result);
#endif
        ret = 0;
    }
    return ret;
}

/** Passes request for a block on to pvfs2_get_blocks()
 */
static int pvfs2_get_block(
    struct inode *ip,
    get_block_block_type lblock,
    struct buffer_head *bh_result,
    int create)
{
    pvfs2_print("pvfs2_get_block called with block %lu\n",
                (unsigned long)lblock);
    return pvfs2_get_blocks(ip, lblock, 1, bh_result, create);
}

/** Passes request for a page on to pvfs2_get_block()
 */
static int pvfs2_readpage(
    struct file *file,
    struct page *page)
{
    pvfs2_print("pvfs2_readpage called with page %p\n",page);
    return pvfs2_kernel_readpage(page, pvfs2_get_block);
}

#ifndef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_readpages(
    struct file *file,
    struct address_space *mapping,
    struct list_head *pages,
    unsigned nr_pages)
{
    pvfs2_print("pvfs2_readpages called\n");
    return mpage_readpages(mapping, pages, nr_pages, pvfs2_get_block);
}

static int pvfs2_invalidatepage(struct page *page, unsigned long offset)
{
    pvfs2_print("pvfs2_invalidatepage called on page %p "
                "(offset is %lu)\n", page, offset);

    ClearPageUptodate(page);
    ClearPageMappedToDisk(page);
    return 0;
}

static int pvfs2_releasepage(struct page *page, int foo)
{
    pvfs2_print("pvfs2_releasepage called on page %p\n", page);
    try_to_free_buffers(page);
    return 0;
}

struct backing_dev_info pvfs2_backing_dev_info =
{
    .ra_pages = 1024,
#ifdef HAVE_BDI_MEMORY_BACKED
    /* old interface, up through 2.6.11 */
    .memory_backed = 1 /* does not contribute to dirty memory */
#else
    .capabilities = BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
#endif
};
#endif /* !PVFS2_LINUX_KERNEL_2_4 */

/** PVFS2 implementation of address space operations */
struct address_space_operations pvfs2_address_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    readpage : pvfs2_readpage
#else
    .readpage = pvfs2_readpage,
    .readpages = pvfs2_readpages,
    .invalidatepage = pvfs2_invalidatepage,
    .releasepage = pvfs2_releasepage
#endif
};

/** Change size of an object referenced by inode
 */
void pvfs2_truncate(struct inode *inode)
{
    pvfs2_print("pvfs2: pvfs2_truncate called on inode %d "
                "with size %d\n",(int)inode->i_ino,(int)inode->i_size);

    pvfs2_truncate_inode(inode, inode->i_size);
}

/** Change attributes of an object referenced by dentry.
 */
int pvfs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
    int ret = -EINVAL;
    struct inode *inode = dentry->d_inode;

    pvfs2_print("pvfs2_setattr: called on %s\n", dentry->d_name.name);

    ret = inode_change_ok(inode, iattr);
    if (ret == 0)
    {
        ret = inode_setattr(inode, iattr);
        pvfs2_print("pvfs2_setattr: inode_setattr returned %d\n", ret);

        if (ret == 0)
        {
            ret = pvfs2_inode_setattr(inode, iattr);
        }
    }
    pvfs2_print("pvfs2_setattr: returning %d\n", ret);
    return ret;
}

#ifdef PVFS2_LINUX_KERNEL_2_4
/** Linux 2.4 only equivalent of getattr
 */
int pvfs2_revalidate(struct dentry *dentry)
{
    int ret = 0;
    struct inode *inode = (dentry ? dentry->d_inode : NULL);

    pvfs2_print("pvfs2_revalidate: called on %s\n", dentry->d_name.name);

    ret = pvfs2_inode_getattr(inode);
    if (ret)
    {
        /* assume an I/O error and flag inode as bad */
        pvfs2_make_bad_inode(inode);
    }
    return ret;
}
#else
/** Obtain attributes of an object given a dentry
 */
int pvfs2_getattr(
    struct vfsmount *mnt,
    struct dentry *dentry,
    struct kstat *kstat)
{
    int ret = -ENOENT;
    struct inode *inode = dentry->d_inode;

    pvfs2_print("pvfs2_getattr: called on %s\n", dentry->d_name.name);

    ret = pvfs2_inode_getattr(inode);
    if (ret == 0)
    {
        generic_fillattr(inode, kstat);
    }
    else
    {
        /* assume an I/O error and flag inode as bad */
        pvfs2_make_bad_inode(inode);
    }
    return ret;
}
#endif /* PVFS2_LINUX_KERNEL_2_4 */

/** PVFS2 implementation of VFS inode operations for files */
struct inode_operations pvfs2_file_inode_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    truncate : pvfs2_truncate,
    setattr : pvfs2_setattr,
    revalidate : pvfs2_revalidate
#else
    .truncate = pvfs2_truncate,
    .setattr = pvfs2_setattr,
    .getattr = pvfs2_getattr
#endif
};

/** Allocates a Linux inode structure with additional PVFS2-specific
 *  private data (I think -- RobR).
 */
struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    int mode,
    dev_t dev,
    unsigned long ino)
{
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    pvfs2_print("pvfs2_get_custom_inode: called\n  (sb is %p | "
                "MAJOR(dev)=%u | MINOR(dev)=%u)\n", sb, MAJOR(dev),
                MINOR(dev));

    inode = iget(sb, ino);
    if (inode)
    {
	/* initialize pvfs2 specific private data */
	pvfs2_inode = PVFS2_I(inode);
	if (!pvfs2_inode)
        {
            iput(inode);
            pvfs2_panic("pvfs2_get_custom_inode: PRIVATE "
                        "DATA NOT ALLOCATED\n");
            return NULL;
        }

        inode->i_mode = mode;
        inode->i_mapping->host = inode;
        inode->i_uid = current->fsuid;
        inode->i_gid = current->fsgid;
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
        inode->i_size = PAGE_CACHE_SIZE;
        inode->i_blksize = PAGE_CACHE_SIZE;
        inode->i_blkbits = PAGE_CACHE_SHIFT;
        inode->i_blocks = 0;
        inode->i_rdev = dev;
        inode->i_bdev = NULL;
        inode->i_cdev = NULL;
        inode->i_mapping->a_ops = &pvfs2_address_operations;
#ifndef PVFS2_LINUX_KERNEL_2_4
        inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
#endif

	pvfs2_print("pvfs2_get_custom_inode: inode %p allocated\n  "
		    "(pvfs2_inode is %p | sb is %p)\n", inode,
		    pvfs2_inode, inode->i_sb);

        if ((mode & S_IFMT) == S_IFREG)
        {
	    inode->i_op = &pvfs2_file_inode_operations;
	    inode->i_fop = &pvfs2_file_operations;

            inode->i_blksize = pvfs_bufmap_size_query();
            inode->i_blkbits = PAGE_CACHE_SHIFT;
        }
        else if ((mode & S_IFMT) == S_IFLNK)
        {
            inode->i_op = &pvfs2_symlink_inode_operations;
            inode->i_fop = NULL;
        }
        else if ((mode & S_IFMT) == S_IFDIR)
        {
	    inode->i_op = &pvfs2_dir_inode_operations;
	    inode->i_fop = &pvfs2_dir_operations;

	    /* dir inodes start with i_nlink == 2 (for "." entry) */
	    inode->i_nlink++;
        }
        else
        {
	    pvfs2_print("pvfs2_get_custom_inode: unsupported mode\n");
	}
    }
    return inode;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

/* defined in devpvfs2-req.c */
void kill_device_owner(void);

/*defined in file.c */
extern ssize_t pvfs2_inode_read(
    struct inode *inode,
    char *buf,
    size_t count,
    loff_t * offset,
    int copy_to_user);

extern struct file_operations pvfs2_file_operations;
extern struct inode_operations pvfs2_symlink_inode_operations;
extern struct inode_operations pvfs2_dir_inode_operations;
extern struct file_operations pvfs2_dir_operations;

/*
  FIXME:
  we're completely ignoring the create argument for now
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
    void *page_data = NULL;
    struct page *page = NULL;
    /*
      FIXME:
      We're faking our inode block size to be PAGE_CACHE_SIZE
      to play nicely with the page cache.

      In some reality, inode->i_blksize != PAGE_CACHE_SIZE and
      inode->i_blkbits != PAGE_CACHE_SHIFT
    */
    const uint32_t blocksize = PAGE_CACHE_SIZE;  /* inode->i_blksize */
    const uint32_t blockbits = PAGE_CACHE_SHIFT; /* inode->i_blkbits */

    pvfs2_print("pvfs2: pvfs2_get_blocks called for lblock %d "
                "(create flag is %s)\n", (int)lblock,
                (create ? "set" : "clear"));

    page = bh_result->b_page;
    page_data = kmap(page);

    /*
      NOTE: this unsafely *assumes* that the size stored in
      the inode is accurate.

      make sure we're not looking for a page block past the
      end of this file;
    */
    max_block = ((inode->i_size / blocksize) + 1);
    if (page->index < max_block)
    {
        loff_t blockptr_offset =
            (((loff_t)page->index) << blockbits);

        /*
          NOTE: This is conceptually backwards.  we could be
          implementing the file_read as generic_file_read and
          doing the actual i/o here (via readpage).

          The main reason it's not like that now is because
          of the mismatch of page cache size and the inode
          blocksize that we're using.  It's more efficient in
          the general case to use the larger blocksize for
          reading/writing.  For now it seems that this call
          can *only* handle reads of PAGE_CACHE_SIZE blocks.
        */
        bytes_read = pvfs2_inode_read(
            inode, page_data, blocksize, &blockptr_offset, 0);

        if (bytes_read < 0)
        {
            pvfs2_error("pvfs_get_blocks: failed to read page block %d\n",
                        (int)page->index);
        }
        else
        {
            pvfs2_print("pvfs2_get_blocks: read %d bytes | offset is %d | "
                        "cur_block is %d | max_block is %d\n",
                        (int)bytes_read, (int)blockptr_offset,
                        (int)page->index, (int)max_block);
        }
    }

    /* only zero remaining unread portions of the page data */
    memset(page_data + bytes_read, 0, blocksize - bytes_read);

    flush_dcache_page(page);
    kunmap(page);

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
        bh_result->b_size = blocksize;

        map_bh(bh_result, inode->i_sb, lblock);
        set_buffer_uptodate(bh_result);

        ret = 0;
    }
    return ret;
}

static int pvfs2_get_block(
    struct inode *ip,
    sector_t lblock,
    struct buffer_head *bh_result,
    int create)
{
    pvfs2_print("pvfs2: pvfs2_get_block called\n");
    return pvfs2_get_blocks(ip, lblock, 1, bh_result, create);
}

static int pvfs2_writepage(
    struct page *page,
    struct writeback_control *wbc)
{
    pvfs2_print("pvfs2: pvfs2_writepage called\n");
    return block_write_full_page(page, pvfs2_get_block, wbc);
}

static int pvfs2_writepages(
    struct address_space *mapping,
    struct writeback_control *wbc)
{
    pvfs2_print("pvfs2: pvfs2_writepages called\n");
    return mpage_writepages(mapping, wbc, pvfs2_get_block);
}

static int pvfs2_sync_page(struct page *page)
{
    pvfs2_print("pvfs2: pvfs2_sync_page called on page %p\n", page);
    return 0;
}

static int pvfs2_readpage(
    struct file *file,
    struct page *page)
{
    pvfs2_print("pvfs2: pvfs2_readpage called with page %p\n",page);


/*     block_invalidatepage(page, 0); */
/*     truncate_inode_pages(file->f_dentry->d_inode->i_mapping, 0); */


    /*
      NOTE: if not using mpage support, we need to drop
      the page lock here before returning
    */
    return mpage_readpage(page, pvfs2_get_block);
}

static int pvfs2_readpages(
    struct file *file,
    struct address_space *mapping,
    struct list_head *pages,
    unsigned nr_pages)
{
    pvfs2_print("pvfs2: pvfs2_readpages called\n");
    return mpage_readpages(mapping, pages, nr_pages, pvfs2_get_block);
}

static int pvfs2_prepare_write(
    struct file *file,
    struct page *page,
    unsigned from,
    unsigned to)
{
    pvfs2_print("pvfs2: pvfs2_prepare_write called\n");
    return nobh_prepare_write(page, from, to, pvfs2_get_block);
}

static int pvfs2_set_page_dirty(struct page *page)
{
    pvfs2_print("pvfs2: pvfs2_set_page_dirty called\n");

    set_page_dirty(page);
    return 0;
}

static int pvfs2_commit_write(
    struct file *file,
    struct page *page,
    unsigned offset,
    unsigned to)
{
    struct inode *inode = page->mapping->host;

    /* FIXME: inode->i_blkbits != PAGE_CACHE_SHIFT */
    loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

    pvfs2_print("pvfs2: pvfs2_commit_write called\n");

    if (pos > inode->i_size)
    {
        i_size_write(inode, pos);
    }
    set_page_dirty(page);
    return 0;
}

static sector_t pvfs2_bmap(
    struct address_space *mapping,
    sector_t block)
{
    pvfs2_print("pvfs2: pvfs2_bmap called\n");
    return generic_block_bmap(mapping, block, pvfs2_get_block);
}

static int pvfs2_invalidatepage(struct page *page, unsigned long offset)
{
    pvfs2_print("pvfs2: pvfs2_invalidatepage called on page %p "
                "(offset is %lu)\n", page, offset);

/*     ClearPageUptodate(page); */
/*     ClearPageMappedToDisk(page); */
    return 0;
}

static int pvfs2_releasepage(struct page *page, int foo)
{
    pvfs2_print("pvfs2: pvfs2_releasepage called on page %p\n", page);
    try_to_free_buffers(page);
    return 0;
}

static int pvfs2_direct_IO(
    int rw,
    struct kiocb *iocb,
    const struct iovec *iov,
    loff_t offset,
    unsigned long nr_segs)
{
    struct file *file = iocb->ki_filp;
    struct inode *inode = file->f_dentry->d_inode->i_mapping->host;

    pvfs2_print("pvfs2: pvfs2_direct_IO called\n");
    return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
			      offset, nr_segs, pvfs2_get_blocks, NULL);
}

struct address_space_operations pvfs2_address_operations =
{
    .readpage = pvfs2_readpage,
    .readpages = pvfs2_readpages,
    .writepage = pvfs2_writepage,
    .writepages = pvfs2_writepages,
    .sync_page = pvfs2_sync_page,
    .prepare_write = pvfs2_prepare_write,
    .commit_write = pvfs2_commit_write,
    .set_page_dirty = pvfs2_set_page_dirty,
    .bmap = pvfs2_bmap,
    .invalidatepage = pvfs2_invalidatepage,
    .releasepage = pvfs2_releasepage,
    .direct_IO = pvfs2_direct_IO
};

struct backing_dev_info pvfs2_backing_dev_info =
{
    .ra_pages = 8,     /* no readahead for now */
    .memory_backed = 0
};

void pvfs2_truncate(struct inode *inode)
{
    pvfs2_print("pvfs2: pvfs2_truncate called on inode %d\n",
                (int)inode->i_ino);
    block_truncate_page(
        inode->i_mapping, inode->i_size, pvfs2_get_block);
}

int pvfs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
    int ret = -EINVAL;
    struct inode *inode = dentry->d_inode;

    pvfs2_print("pvfs2: pvfs2_setattr called on %s\n",
                dentry->d_name.name);

    ret = inode_change_ok(inode, iattr);
    if (ret == 0)
    {
        ret = pvfs2_inode_setattr(inode, iattr);
    }
    else
    {
        pvfs2_error("pvfs2: inode_change_ok failed with %d\n",ret);
    }
    return ret;
}

struct inode_operations pvfs2_file_inode_operations =
{
    .truncate = pvfs2_truncate,
/*     .setxattr = pvfs2_setxattr, */
/*     .getxattr = pvfs2_getxattr, */
/*     .listxattr = pvfs2_listxattr, */
/*     .removexattr = pvfs2_removexattr, */
/*     .permission = pvfs2_permission, */
    .setattr = pvfs2_setattr
};

struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    int mode,
    dev_t dev)
{
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    pvfs2_print("pvfs2_get_custom_inode: called (sb is %p)\n", sb);

    inode = new_inode(sb);
    if (inode)
    {
	/* initialize pvfs2 specific private data */
	pvfs2_inode = PVFS2_I(inode);
	if (!pvfs2_inode)
	{
	    panic("pvfs2_get_custom_inode: PRIVATE DATA NOT ALLOCATED\n");
	    return NULL;
	}
	else
	{
	    pvfs2_inode->refn.handle = 0;
	    pvfs2_inode->refn.fs_id = 0;
	}
	pvfs2_print("pvfs2_get_custom_inode: inode %p allocated "
		    "(pvfs2_inode is %p | sb is %p)\n", inode,
		    pvfs2_inode, inode->i_sb);

	inode->i_mode = mode;
	inode->i_mapping->a_ops = &pvfs2_address_operations;
	inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
	inode->i_uid = current->uid;
	inode->i_gid = current->gid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;

        inode->i_size = 0;
        inode->i_blksize = PAGE_CACHE_SIZE;
        inode->i_blkbits = PAGE_CACHE_SHIFT;
        inode->i_blocks = 1;

        if (mode & S_IFREG)
        {
	    inode->i_op = &pvfs2_file_inode_operations;
	    inode->i_fop = &pvfs2_file_operations;

            inode->i_blksize = pvfs_bufmap_size_query();
            inode->i_blkbits = PAGE_CACHE_SHIFT;
        }
        else if (mode & S_IFLNK)
        {
            inode->i_op = &pvfs2_symlink_inode_operations;
            inode->i_fop = NULL;
        }
        else if (mode & S_IFDIR)
        {
	    inode->i_op = &pvfs2_dir_inode_operations;
	    inode->i_fop = &pvfs2_dir_operations;

	    /* dir inodes start with i_nlink == 2 (for "." entry) */
	    inode->i_nlink++;
        }
        else
        {
	    pvfs2_print("pvfs2_get_custom_inode -- unsupported mode\n");
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

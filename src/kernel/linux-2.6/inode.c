#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include "pvfs2-kernel.h"

extern struct file_operations pvfs2_file_operations;
extern struct inode_operations pvfs2_dir_inode_operations;
extern struct file_operations pvfs2_dir_operations;

static int pvfs2_get_blocks(struct inode *ip, sector_t lblock,
                            unsigned long max_blocks,
                            struct buffer_head *bh_result, int create)
{
    pvfs2_print("pvfs2: pvfs2_get_blocks called\n");
    return 0;
}

static int pvfs2_get_block(struct inode *ip, sector_t lblock,
                           struct buffer_head *bh_result, int create)
{
    pvfs2_print("pvfs2: pvfs2_get_block called\n");
    return pvfs2_get_blocks(ip,lblock,1,bh_result,create);
}

static int pvfs2_writepage(struct page *page,
                           struct writeback_control *wbc)
{
    pvfs2_print("pvfs2: pvfs2_writepage called\n");
    return block_write_full_page(page,pvfs2_get_block,wbc);
}

static int pvfs2_writepages(struct address_space *mapping,
                            struct writeback_control *wbc)
{
    pvfs2_print("pvfs2: pvfs2_writepages called\n");
    return mpage_writepages(mapping,wbc,pvfs2_get_block);
}

static int pvfs2_readpage(struct file *file, struct page *page)
{
    pvfs2_print("pvfs2: pvfs2_readpage called\n");
    return mpage_readpage(page,pvfs2_get_block);
}

static int pvfs2_readpages(struct file *file, struct address_space *mapping,
                           struct list_head *pages, unsigned nr_pages)
{
    pvfs2_print("pvfs2: pvfs2_readpages called\n");
    return mpage_readpages(mapping,pages,nr_pages,pvfs2_get_block);
}

static int pvfs2_prepare_write(struct file *file, struct page *page,
                               unsigned from, unsigned to)
{
    pvfs2_print("pvfs2: pvfs2_prepare_write called\n");
    return nobh_prepare_write(page,from,to,pvfs2_get_block);
}

static sector_t pvfs2_bmap(struct address_space *mapping,
                           sector_t block)
{
    pvfs2_print("pvfs2: pvfs2_bmap called\n");
    return generic_block_bmap(mapping,block,pvfs2_get_block);
}

static int pvfs2_direct_IO(int rw, struct kiocb *iocb,
                           const struct iovec *iov,
                           loff_t offset, unsigned long nr_segs)
{
    struct file *file = iocb->ki_filp;
    struct inode *inode = file->f_dentry->d_inode->i_mapping->host;

    pvfs2_print("pvfs2: pvfs2_direct_IO called\n");
    return blockdev_direct_IO(rw,iocb,inode,inode->i_sb->s_bdev,iov,
                              offset,nr_segs,pvfs2_get_blocks);
}

struct address_space_operations pvfs2_aops =
{
    .readpage = pvfs2_readpage,
    .readpages = pvfs2_readpages,
    .writepage = pvfs2_writepage,
    .writepages = pvfs2_writepages,
    .sync_page = block_sync_page,
    .prepare_write = pvfs2_prepare_write,
    .commit_write = nobh_commit_write,
    .bmap = pvfs2_bmap,
    .direct_IO = pvfs2_direct_IO,
};

static struct backing_dev_info pvfs2_backing_dev_info =
{
    .ra_pages = 0,       /* no readahead */
    .memory_backed = 1   /* does not contribute to dirty memory */ 
};


static int pvfs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
    struct inode *inode = dentry->d_inode;

    pvfs2_print("pvfs2: pvfs2_setattr called on %s\n",
                dentry->d_name.name);

    return pvfs2_inode_setattr(inode, iattr);
}

struct inode_operations pvfs2_file_inode_operations =
{
/*     .truncate = pvfs2_truncate, */
/*     .setxattr = pvfs2_setxattr, */
/*     .getxattr = pvfs2_getxattr, */
/*     .listxattr = pvfs2_listxattr, */
/*     .removexattr = pvfs2_removexattr, */
/*     .permission = pvfs2_permission */
    .setattr = pvfs2_setattr
};

struct inode *pvfs2_get_custom_inode(struct super_block *sb,
                                     int mode, dev_t dev)
{
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    pvfs2_print("pvfs2_get_custom_inode: called (sb is %p)\n",sb);

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
                    "(pvfs2_inode is %p | sb is %p)\n",inode,
                    pvfs2_inode,inode->i_sb);

        inode->i_mode = mode;
        inode->i_rdev = NODEV;
        inode->i_mapping->a_ops = &pvfs2_aops;
        inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
        inode->i_uid = current->uid;
        inode->i_gid = current->gid;
        inode->i_blksize = PAGE_CACHE_SIZE;
        inode->i_blocks = 1;
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
        switch(mode & S_IFMT)
        {
            case S_IFREG:
                inode->i_op = &pvfs2_file_inode_operations;
                inode->i_fop = &pvfs2_file_operations;
                break;
            case S_IFDIR:
                inode->i_op = &pvfs2_dir_inode_operations;
                inode->i_fop = &pvfs2_dir_operations;

                /* dir inodes start with i_nlink == 2 (for "." entry) */
                inode->i_nlink++;
                break;
            default:
                pvfs2_print("pvfs2_get_custom_inode -- unsupported mode\n");
        }
    }
    return inode;
}

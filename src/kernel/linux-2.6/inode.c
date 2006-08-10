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
#include "pvfs2-types.h"

static int read_one_page(struct page *page)
{
    void *page_data;
    int ret, max_block;
    ssize_t bytes_read = 0;
    struct inode *inode = page->mapping->host;
    const uint32_t blocksize = PAGE_CACHE_SIZE;  /* inode->i_blksize */
    const uint32_t blockbits = PAGE_CACHE_SHIFT; /* inode->i_blkbits */

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_readpage called with page %p\n",page);
    page_data = pvfs2_kmap(page);

    max_block = ((inode->i_size / blocksize) + 1);

    if (page->index < max_block)
    {
        loff_t blockptr_offset =
            (((loff_t)page->index) << blockbits);
        bytes_read = pvfs2_inode_read(
            inode, page_data, blocksize, &blockptr_offset, 0, inode->i_size);
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
        ret = 0;
    }
    pvfs2_kunmap(page);
    /* unlock the page after the ->readpage() routine completes */
    unlock_page(page);
    return ret;
}

static int pvfs2_readpage(
    struct file *file,
    struct page *page)
{
    return read_one_page(page);
}

#ifndef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_readpages(
    struct file *file,
    struct address_space *mapping,
    struct list_head *pages,
    unsigned nr_pages)
{
    int page_idx;
    int ret;

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_readpages called\n");

    for (page_idx = 0; page_idx < nr_pages; page_idx++)
    {
        struct page *page;
        page = list_entry(pages->prev, struct page, lru);
        list_del(&page->lru);
        if (!add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
            ret = read_one_page(page);
        }
        else {
            page_cache_release(page);
        }
    }
    BUG_ON(!list_empty(pages));
    return 0;
}

#ifdef HAVE_INT_RETURN_ADDRESS_SPACE_OPERATIONS_INVALIDATEPAGE
static int pvfs2_invalidatepage(struct page *page, unsigned long offset)
#else
static void pvfs2_invalidatepage(struct page *page, unsigned long offset)
#endif
{
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_invalidatepage called on page %p "
                "(offset is %lu)\n", page, offset);

    ClearPageUptodate(page);
    ClearPageMappedToDisk(page);
#ifdef HAVE_INT_RETURN_ADDRESS_SPACE_OPERATIONS_INVALIDATEPAGE
    return 0;
#else
    return;
#endif

}

#ifdef HAVE_INT_ARG2_ADDRESS_SPACE_OPERATIONS_RELEASEPAGE
static int pvfs2_releasepage(struct page *page, int foo)
#else
static int pvfs2_releasepage(struct page *page, gfp_t foo)
#endif
{
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_releasepage called on page %p\n", page);
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
    loff_t orig_size = i_size_read(inode);
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2: pvfs2_truncate called on inode %d "
                "with size %ld\n",(int)inode->i_ino, (long) orig_size);

    /* successful truncate when size changes also requires mtime updates 
     * although the mtime updates are propagated lazily!
     */
    if (pvfs2_truncate_inode(inode, inode->i_size) == 0
            && (orig_size != i_size_read(inode)))
    {
        pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
        SetMtimeFlag(pvfs2_inode);
        inode->i_mtime = CURRENT_TIME;
        mark_inode_dirty_sync(inode);
    }
}

/** Change attributes of an object referenced by dentry.
 */
int pvfs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
    int ret = -EINVAL;
    struct inode *inode = dentry->d_inode;

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_setattr: called on %s\n", dentry->d_name.name);

    ret = inode_change_ok(inode, iattr);
    if (ret == 0)
    {
        ret = inode_setattr(inode, iattr);
        gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_setattr: inode_setattr returned %d\n", ret);

        if (ret == 0)
        {
            ret = pvfs2_inode_setattr(inode, iattr);
#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
            if (!ret && (iattr->ia_valid & ATTR_MODE))
            {
                /* change mod on a file that has ACLs */
                ret = pvfs2_acl_chmod(inode);
            }
#endif
        }
    }
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_setattr: returning %d\n", ret);
    return ret;
}

#ifdef PVFS2_LINUX_KERNEL_2_4
/** Linux 2.4 only equivalent of getattr
 */
int pvfs2_revalidate(struct dentry *dentry)
{
    int ret = 0;
    struct inode *inode = (dentry ? dentry->d_inode : NULL);

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_revalidate: called on %s\n", dentry->d_name.name);

    /*
     * A revalidate expects that all fields of the inode would be refreshed
     * So we have no choice but to refresh all attributes.
     */
    ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
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

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_getattr: called on %s\n", dentry->d_name.name);

    /*
     * Similar to the above comment, a getattr also expects that all fields/attributes
     * of the inode would be refreshed. So again, we dont have too much of a choice
     * but refresh all the attributes.
     */
    ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
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
    revalidate : pvfs2_revalidate,
#ifdef HAVE_XATTR
    setxattr : pvfs2_setxattr, 
    getxattr : pvfs2_getxattr,
    removexattr: pvfs2_removexattr,
    listxattr: pvfs2_listxattr,
#endif
#else
    .truncate = pvfs2_truncate,
    .setattr = pvfs2_setattr,
    .getattr = pvfs2_getattr,
#if defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
    .setxattr = generic_setxattr,
    .getxattr = generic_getxattr,
    .removexattr = generic_removexattr,
#else
    .setxattr = pvfs2_setxattr,
    .getxattr = pvfs2_getxattr,
    .removexattr = pvfs2_removexattr,
#endif
    .listxattr = pvfs2_listxattr,
#if defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
    .permission = pvfs2_permission,
#endif
#endif
};

/** Allocates a Linux inode structure with additional PVFS2-specific
 *  private data (I think -- RobR).
 */
struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    struct inode *dir,
    int mode,
    dev_t dev,
    unsigned long ino)
{
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_get_custom_inode: called\n  (sb is %p | "
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
            gossip_err("pvfs2_get_custom_inode: PRIVATE "
                        "DATA NOT ALLOCATED\n");
            return NULL;
        }

        if (inode->i_ino != PVFS2_SB(inode->i_sb)->root_handle)
        {
            inode->i_mode = mode;
        }
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

	gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_get_custom_inode: inode %p allocated\n  "
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
	    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_get_custom_inode: unsupported mode\n");
            goto error;
	}
#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
        /* Initialize the ACLs of the new inode */
        pvfs2_init_acl(inode, dir);
#endif
    }
error:
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

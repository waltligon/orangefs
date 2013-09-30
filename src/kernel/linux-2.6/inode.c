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
#include "pvfs2-internal.h"

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
            gossip_debug(GOSSIP_INODE_DEBUG, "failure adding page to cache, "
                         "read_one_page returned: %d\n", ret);
        }
        else {
            page_cache_release(page);
        }
    }
    BUG_ON(!list_empty(pages));
    return 0;
}

static void pvfs2_invalidatepage(struct page *page, 
                                 unsigned int offset,
                                 unsigned int length)
{
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_invalidatepage called on page %p "
                "(offset is %u)\n", page, offset);

    ClearPageUptodate(page);
    ClearPageMappedToDisk(page);
    return;

}

static int pvfs2_releasepage(struct page *page, gfp_t foo)
{
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_releasepage called on page %p\n", page);
    return 0;
}

struct backing_dev_info pvfs2_backing_dev_info =
{
    .name = "pvfs2",
    .ra_pages = 0,
    .capabilities = BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

/** PVFS2 implementation of address space operations */
struct address_space_operations pvfs2_address_operations =
{
    .readpage = pvfs2_readpage,
    .readpages = pvfs2_readpages,
    .invalidatepage = pvfs2_invalidatepage,
    .releasepage = pvfs2_releasepage
};

/** Change size of an object referenced by inode
 */
void pvfs2_truncate(struct inode *inode)
{
    loff_t orig_size = pvfs2_i_size_read(inode);

    if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
        return;
    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2: pvfs2_truncate called on inode %llu "
                "with size %ld\n", llu(get_handle_from_ino(inode)), (long) orig_size);

    /* successful truncate when size changes also requires mtime updates 
     * although the mtime updates are propagated lazily!
     */
    if (pvfs2_truncate_inode(inode, inode->i_size) == 0
            && (orig_size != pvfs2_i_size_read(inode)))
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

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_setattr: called on %s\n", 
                 dentry->d_name.name);

    ret = inode_change_ok(inode, iattr);
    if (ret == 0)
    {

        if ((iattr->ia_valid & ATTR_SIZE) &&
           iattr->ia_size != i_size_read(inode)) 
        {
            ret = inode_newsize_ok(inode, iattr->ia_size);
            if (!ret) {
              truncate_setsize(inode,iattr->ia_size);
              pvfs2_truncate(inode);
            }
            if (ret)
                return ret;
        }

        setattr_copy(inode, iattr);
        mark_inode_dirty(inode);
        ret = 0;
    
        gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_setattr: inode_setattr returned %d\n", ret);

        if (ret == 0)
        {
            ret = pvfs2_inode_setattr(inode, iattr);

/* CONFIG_FS_POSIX_ACL is in .config */
#if defined(CONFIG_FS_POSIX_ACL)
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

/** Obtain attributes of an object given a dentry
 */
int pvfs2_getattr(
    struct vfsmount *mnt,
    struct dentry *dentry,
    struct kstat *kstat)
{
    int ret = -ENOENT;
    struct inode *inode = dentry->d_inode;
    pvfs2_inode_t *pvfs2_inode = NULL;

    gossip_debug(GOSSIP_INODE_DEBUG, 
        "pvfs2_getattr: called on %s\n", dentry->d_name.name);

    /* This seems to be the only place to reliably detect mount options
     * parsed by the VFS layer.  Propigate them to our internal sb structure so
     * that we can handle lazy time updates properly.
     */
    if(mnt->mnt_flags && MNT_NOATIME) 
    { 
        inode->i_sb->s_flags |= MS_NOATIME; 
    } 
    if(mnt->mnt_flags && MNT_NODIRATIME) 
    { 
        inode->i_sb->s_flags |= MS_NODIRATIME; 
    } 

    /*
     * Similar to the above comment, a getattr also expects that all fields/attributes
     * of the inode would be refreshed. So again, we dont have too much of a choice
     * but refresh all the attributes.
     */
    ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
    if (ret == 0)
    {
        generic_fillattr(inode, kstat);
        /* override block size reported to stat */
        pvfs2_inode = PVFS2_I(inode);
        kstat->blksize = pvfs2_inode->blksize;
    }
    else
    {
        /* assume an I/O error and flag inode as bad */
        gossip_debug(GOSSIP_INODE_DEBUG, "%s:%s:%d calling make bad inode\n", __FILE__,  __func__, __LINE__);
        pvfs2_make_bad_inode(inode);
    }
    return ret;
}

/** PVFS2 implementation of VFS inode operations for files */
struct inode_operations pvfs2_file_inode_operations =
{
    .setattr = pvfs2_setattr,
    .getattr = pvfs2_getattr,
/* CONFIG_FS_POSIX_ACL is in .config */
#if defined(CONFIG_FS_POSIX_ACL)
    .setxattr = generic_setxattr,
    .getxattr = generic_getxattr,
    .removexattr = generic_removexattr,
#endif
    .listxattr = pvfs2_listxattr,
/* CONFIG_FS_POSIX_ACL is in .config */
#if defined(CONFIG_FS_POSIX_ACL)
    .permission = pvfs2_permission,
    .get_acl = pvfs2_get_acl,
#endif
};

/*
 * Given a PVFS2 object identifier (fsid, handle), convert it into a ino_t type
 * that will be used as a hash-index from where the handle will
 * be searched for in the VFS hash table of inodes.
 */
static inline ino_t pvfs2_handle_hash(PVFS_object_ref *ref)
{
    if (!ref)
        return 0;
    return pvfs2_handle_to_ino(ref->handle);
}

/* the ->set callback of iget5_locked and friends. Sorta equivalent to the ->read_inode()
 * callback if we are using iget and friends 
 */
int pvfs2_set_inode(struct inode *inode, void *data)
{
    /* callbacks to set inode number handle */
    PVFS_object_ref *ref = (PVFS_object_ref *) data;
    pvfs2_inode_t *pvfs2_inode = NULL;

    /* Make sure that we have sane parameters */
    if (!data || !inode)
        return 0;
    pvfs2_inode = PVFS2_I(inode);
    if (!pvfs2_inode)
        return 0;
    pvfs2_inode_initialize(pvfs2_inode);
    pvfs2_inode->refn.fs_id  = ref->fs_id;
    pvfs2_inode->refn.handle = ref->handle;
    return 0;
}

static int
pvfs2_test_inode(struct inode *inode, void *data)
{
    /* callbacks to determine if handles match */
    PVFS_object_ref *ref = (PVFS_object_ref *) data;
    pvfs2_inode_t *pvfs2_inode = NULL;

    pvfs2_inode = PVFS2_I(inode);
    return (pvfs2_inode->refn.handle == ref->handle && pvfs2_inode->refn.fs_id == ref->fs_id);
}

/*
 * Front-end to lookup the inode-cache maintained by the VFS using the PVFS2
 * file handle instead of the inode number.
 * Problem with iget() is well-documented in that it can lead to possible
 * collissions especially for a file-system with 64 bit handles since inode->i_ino
 * is only a scalar field (32 bits). So the trick now is to use iget4_locked (OR) iget5_locked
 * if the kernel defines one and set inode number to be just a hash for the
 * handle
 * @sb: the file system super block instance
 * @ref: The PVFS2 object for which we are trying to locate an inode structure
 * @keep_locked : indicates whether the inode must be simply allocated and not filled
 * in with the results from a ->getattr. i.e. if keep_locked is set to 0, we do a getattr() and
 * unlock the inode and if set to 1, we do not issue a getattr() and keep it locked
 * 
 * Boy, this function is so ugly with all these macros. I wish I could find a better
 * way to reduce the macro clutter.
 */
struct inode *pvfs2_iget_common(struct super_block *sb, PVFS_object_ref *ref, int keep_locked)
{
    struct inode *inode = NULL;
    unsigned long hash;

    hash = pvfs2_handle_hash(ref);
    inode = iget5_locked(sb, hash, pvfs2_test_inode, pvfs2_set_inode, ref);
    if (!keep_locked)
    {
        if (inode && (inode->i_state & I_NEW))
        {
            inode->i_ino = hash; /* needed for stat etc */
            if (PVFS2_I(inode)) {
                pvfs2_set_inode(inode, ref);
            } 
            /* issue a call to read the inode */
            pvfs2_read_inode(inode);
            unlock_new_inode(inode);
        }
    }
    gossip_debug(GOSSIP_INODE_DEBUG, "iget handle %llu, fsid %d hash %ld i_ino %lu\n",
                 ref->handle, ref->fs_id, hash, inode->i_ino);
    return inode;
}

/** Allocates a Linux inode structure with additional PVFS2-specific
 *  private data (I think -- RobR).
 */
struct inode *pvfs2_get_custom_inode_common(
    struct super_block *sb,
    struct inode *dir,
    int mode,
    dev_t dev,
    PVFS_object_ref object,
    int from_create)
{
    struct inode *inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_get_custom_inode_common: called\n  (sb is %p | "
                "MAJOR(dev)=%u | MINOR(dev)=%u)\n", sb, MAJOR(dev),
                MINOR(dev));

    inode = pvfs2_iget(sb, &object);
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

        /*
         * Since we are using the same function to create a new on-disk object
         * as well as to create an in-memory object, the mode of the object
         * needs to be set carefully. If we are called from a function that is
         * creating a new on-disk object, set its mode here since the caller is
         * providing it. Else let it be since the getattr should fill it up
         * properly.
         */
        if (from_create)
        {
            /* the exception is when we are creating a directory that needs
             * to inherit the setgid bit.  That much we need to preserve from
             * the getattr's view of the mode.
             */
            if(inode->i_mode & S_ISGID)
            {
                gossip_debug(GOSSIP_INODE_DEBUG,
                    "pvfs2_get_custom_inode_commmon: setting SGID bit.\n");
                inode->i_mode = mode | S_ISGID;
            }
            else
            {
                inode->i_mode = mode;
            }
        }
        gossip_debug(GOSSIP_INODE_DEBUG, 
                "pvfs2_get_custom_inode_common: inode: %p, inode->i_mode %o\n",
                inode, inode->i_mode);
        inode->i_mapping->host = inode;
        inode->i_uid = current_fsuid();
        inode->i_gid = current_fsgid();
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
        inode->i_size = PAGE_CACHE_SIZE;
        inode->i_blkbits = PAGE_CACHE_SHIFT;
        inode->i_blocks = 0;
        inode->i_rdev = dev;
        inode->i_bdev = NULL;
        inode->i_cdev = NULL;
        inode->i_mapping->a_ops = &pvfs2_address_operations;
        inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;

	gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_get_custom_inode: inode %p allocated\n  "
		    "(pvfs2_inode is %p | sb is %p)\n", inode,
		    pvfs2_inode, inode->i_sb);

        if ((mode & S_IFMT) == S_IFREG)
        {
	    inode->i_op = &pvfs2_file_inode_operations;
	    inode->i_fop = &pvfs2_file_operations;

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
            pvfs2_i_inc_nlink(inode);
        }
        else
        {
	    gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_get_custom_inode: unsupported mode\n");
            goto error;
	}
/* CONFIG_FS_POSIX_ACL is in .config */
#if defined(CONFIG_FS_POSIX_ACL)
        gossip_debug(GOSSIP_ACL_DEBUG, "Initializing ACL's for inode %llu\n", 
                llu(get_handle_from_ino(inode)));
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

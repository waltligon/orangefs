/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/pagemap.h>
#include <linux/statfs.h>
#include "pvfs2-kernel.h"

extern struct file_system_type pvfs2_fs_type;
extern struct dentry_operations pvfs2_dentry_operations;
extern struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    int mode,
    dev_t dev);

extern kmem_cache_t *pvfs2_inode_cache;

static struct inode *pvfs2_alloc_inode(
    struct super_block *sb)
{
    struct inode *new_inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    /*
       this allocator has an associated constructor that fills in the
       internal vfs inode structure.  this initialization is
       extremely important and is required since we're allocating
       the inodes ourselves (rather than letting the system inode
       allocator initialize them for us); see inode.c/inode_init_once()
     */
    pvfs2_inode = kmem_cache_alloc(pvfs2_inode_cache, SLAB_KERNEL);
    if (pvfs2_inode)
    {
	new_inode = &pvfs2_inode->vfs_inode;
    }
    return new_inode;
}

static void pvfs2_destroy_inode(
    struct inode *inode)
{
    /* free pvfs2 specific (private) inode data */
    kmem_cache_free(pvfs2_inode_cache, PVFS2_I(inode));
}

static void pvfs2_read_inode(
    struct inode *inode)
{
    pvfs2_print("pvfs2: pvfs2_read_inode called (inode = %d)\n",
		(int) inode->i_ino);

    /*
       need to populate the freshly allocated (passed in)
       inode here.  this gets called if the vfs can't find
       this inode in the inode cache.  we need to getattr here
       because d_revalidate isn't called after a successful
       dentry lookup if the inode is not present in the inode
       cache already.  so this is our chance.
     */
    pvfs2_inode_getattr(inode);
}

/* called on sync ; make sure data is safe */
static void pvfs2_write_inode(
    struct inode *inode,
    int force_sync)
{
    pvfs2_print("pvfs2: pvfs2_write_inode called (inode = %d)\n",
		(int) inode->i_ino);

    /*
       the force_sync flag was added to tell us that we
       *really* need to sync the specified inode to disk
     */
    if (!force_sync)
    {
	return;
    }
    /* force real sync here */
}

/* called when the VFS removes this inode from the inode cache */
static void pvfs2_put_inode(
    struct inode *inode)
{
    pvfs2_print("pvfs2: pvfs2_put_inode called (ino %d)\n", (int) inode->i_ino);

    if (atomic_read(&inode->i_count) == 0)
    {
	/* kill dentries associated with this inode */
	d_prune_aliases(inode);
    }
}

/* information put here is reflected in the output of 'df' */
static int pvfs2_statfs(
    struct super_block *sb,
    struct kstatfs *buf)
{
    buf->f_type = sb->s_magic;
    buf->f_bsize = sb->s_blocksize;
    buf->f_namelen = PVFS2_NAME_LEN;
    buf->f_blocks = buf->f_bfree = buf->f_bavail = 100000;
    buf->f_files = buf->f_ffree = 100;
    return 0;
}

static int pvfs2_remount(
    struct super_block *sb,
    int *flags,
    char *data)
{
    pvfs2_print("pvfs2: pvfs2_remount called\n");
    return 0;
}

struct super_operations pvfs2_s_ops = {
    .drop_inode = generic_delete_inode,
    .alloc_inode = pvfs2_alloc_inode,
    .destroy_inode = pvfs2_destroy_inode,
    .read_inode = pvfs2_read_inode,
    .write_inode = pvfs2_write_inode,
    .put_inode = pvfs2_put_inode,
    .statfs = pvfs2_statfs,
    .remount_fs = pvfs2_remount
/*     .delete_inode   = pvfs2_delete_inode */
/*     .put_super      = pvfs2_put_super, */
/*     .write_super    = pvfs2_write_super, */
/*     .clear_inode    = pvfs2_clear_inode, */
};

int pvfs2_fill_sb(
    struct super_block *sb,
    void *data,
    int silent)
{
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;

    if (!silent)
    {
	pvfs2_print("pvfs2: pvfs2_fill_sb called (sb = %p)\n", sb);
    }

    sb->s_blocksize = PAGE_CACHE_SIZE;
    sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
    sb->s_magic = PVFS2_MAGIC;
    sb->s_op = &pvfs2_s_ops;
    sb->s_type = &pvfs2_fs_type;

    /* alloc and initialize our root directory inode */
    root = pvfs2_get_custom_inode(sb, (S_IFDIR | 0755), 0);
    if (!root)
    {
	return -ENOMEM;
    }

    /* alloc and init our private pvfs2 sb info */
    sb->s_fs_info = kmalloc(sizeof(pvfs2_sb_info), GFP_KERNEL);
    if (!PVFS2_SB(sb))
    {
	iput(root);
	return -ENOMEM;
    }

    /* FIXME: this is a hack...but we need this info from somewhere */
    root->i_ino = (ino_t) PVFS2_ROOT_INODE_NUMBER;
    PVFS2_SB(sb)->fs_id = (PVFS_fs_id) 9;
    PVFS2_SB(sb)->handle = (PVFS_handle)PVFS2_ROOT_INODE_NUMBER;

    /* allocates and places root dentry in dcache */
    root_dentry = d_alloc_root(root);
    if (!root_dentry)
    {
	iput(root);
	kfree(PVFS2_SB(sb));
	return -ENOMEM;
    }
    root_dentry->d_op = &pvfs2_dentry_operations;

    sb->s_root = root_dentry;
    return 0;
}

struct super_block *pvfs2_get_sb(
    struct file_system_type *fst,
    int flags,
    const char *devname,
    void *data)
{
    return get_sb_single(fst, flags, data, pvfs2_fill_sb);
}

void pvfs2_kill_sb(
    struct super_block *sb)
{
    /* prune dcache based on sb */
    shrink_dcache_sb(sb);

    /* provided sb cleanup */
    kill_litter_super(sb);

    /* release the allocated root dentry */
    dput(sb->s_root);

    /* free the pvfs2 superblock private data */
    kfree(PVFS2_SB(sb));
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

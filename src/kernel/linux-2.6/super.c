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
#include <linux/buffer_head.h>
#include "pvfs2-kernel.h"

/* defined in linux/fs/block_dev.c */
extern struct super_block *blockdev_superblock;

extern struct file_system_type pvfs2_fs_type;
extern struct dentry_operations pvfs2_dentry_operations;
extern struct inode *pvfs2_get_custom_inode(
    struct super_block *sb, int mode, dev_t dev);

extern kmem_cache_t *pvfs2_inode_cache;

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;

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
    pvfs2_inode = kmem_cache_alloc(pvfs2_inode_cache,
                                   PVFS2_CACHE_ALLOC_FLAGS);
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
    pvfs2_print("pvfs2_destroy_inode: destroying inode %d\n",
                (int)inode->i_ino);
    kmem_cache_free(pvfs2_inode_cache, PVFS2_I(inode));
}

static void pvfs2_read_inode(
    struct inode *inode)
{
    pvfs2_print("pvfs2: pvfs2_read_inode called (inode = %d | "
                "ct = %d)\n", (int)inode->i_ino,
                (int)atomic_read(&inode->i_count));

    /*
       need to populate the freshly allocated (passed in)
       inode here.  this gets called if the vfs can't find
       this inode in the inode cache.  we need to getattr here
       because d_revalidate isn't called after a successful
       dentry lookup if the inode is not present in the inode
       cache already.  so this is our chance.
    */
    if (pvfs2_inode_getattr(inode) != 0)
    {
        /* assume an I/O error and flag inode as bad */
        make_bad_inode(inode);
    }
}

/* called on sync ; make sure data is safe */
static void pvfs2_write_inode(
    struct inode *inode,
    int do_sync)
{
    pvfs2_print("pvfs2: pvfs2_write_inode called (inode = %d)\n",
		(int)inode->i_ino);
}

/* called when the VFS removes this inode from the inode cache */
static void pvfs2_put_inode(
    struct inode *inode)
{
    pvfs2_print("pvfs2: pvfs2_put_inode called (ino %d | ct=%d | "
                "nlink=%d)\n", (int) inode->i_ino,
                (int)atomic_read(&inode->i_count),
                (int)inode->i_nlink);

    /* set nlink to 0 to allow the inode to be freed */
/*     inode->i_nlink = 0; */

    if (atomic_read(&inode->i_count) == 1)
    {
	/* kill dentries associated with this inode */
	d_prune_aliases(inode);
    }
}

/*
  NOTE: information filled in here is typically
  reflected in the output of 'df'
*/
static int pvfs2_statfs(
    struct super_block *sb,
    struct kstatfs *buf)
{
    int ret = -1, retries = 5;
    pvfs2_kernel_op_t *new_op = NULL;

    pvfs2_print("pvfs2_: pvfs2_statfs called on sb %p "
                "(fs_id is %d)\n", sb, (int)(PVFS2_SB(sb)->fs_id));

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!new_op)
    {
	pvfs2_error("pvfs2: pvfs2_statfs -- kmem_cache_alloc failed!\n");
	return ret;
    }
    new_op->upcall.type = PVFS2_VFS_OP_STATFS;
    new_op->upcall.req.statfs.fs_id = PVFS2_SB(sb)->fs_id;

    service_operation_with_timeout_retry(
        new_op, "pvfs2_statfs", retries);

    if (new_op->downcall.status > -1)
    {
        pvfs2_print("pvfs2_statfs got %Ld blocks available | "
                    "%Ld blocks total\n",
                    Ld(new_op->downcall.resp.statfs.blocks_avail),
                    Ld(new_op->downcall.resp.statfs.blocks_total));

        /*
          re-assign superblock blocksize based on statfs blocksize:

          NOTE: it seems okay that the superblock blocksize doesn't
          match what we're using as the inode blocksize.  keep an
          eye out to be sure.
        */
/*         sb->s_blocksize = pvfs_bufmap_size_query(); */
        sb->s_blocksize = new_op->downcall.resp.statfs.block_size;

        buf->f_type = sb->s_magic;
        buf->f_bsize = sb->s_blocksize;
        buf->f_namelen = PVFS2_NAME_LEN;

        buf->f_blocks = le32_to_cpu(
            new_op->downcall.resp.statfs.blocks_total);
        buf->f_bfree = le32_to_cpu(
            new_op->downcall.resp.statfs.blocks_avail);
        buf->f_bavail = le32_to_cpu(
            new_op->downcall.resp.statfs.blocks_avail);
        buf->f_files = le32_to_cpu(
            new_op->downcall.resp.statfs.files_total);
        buf->f_ffree = le32_to_cpu(
            new_op->downcall.resp.statfs.files_avail);

        ret = new_op->downcall.status;
    }

  error_exit:
    op_release(new_op);

    pvfs2_print("pvfs2_statfs returning %d\n", ret);
    return ret;
}

static int pvfs2_remount(
    struct super_block *sb,
    int *flags,
    char *data)
{
    pvfs2_print("pvfs2: pvfs2_remount called\n");
    return 0;
}

/* static int pvfs2_sync_fs(struct super_block *sb, int wait) */

struct super_operations pvfs2_s_ops = {
    .drop_inode = generic_delete_inode,
    .alloc_inode = pvfs2_alloc_inode,
    .destroy_inode = pvfs2_destroy_inode,
    .read_inode = pvfs2_read_inode,
    .write_inode = pvfs2_write_inode,
    .put_inode = pvfs2_put_inode,
    .statfs = pvfs2_statfs,
    .remount_fs = pvfs2_remount,
/*     .sync_fs = pvfs2_sync_fs */
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
    int shift_val = 0;
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;

    if (!silent)
    {
	pvfs2_print("pvfs2: pvfs2_fill_sb called (sb = %p)\n", sb);
    }

    sb->s_magic = PVFS2_MAGIC;
    sb->s_op = &pvfs2_s_ops;
    sb->s_type = &pvfs2_fs_type;

    sb->s_blocksize = PVFS2_BUFMAP_DEFAULT_DESC_SIZE;
    shift_val = ((sizeof(sb->s_blocksize_bits) * 8) - 1);
    sb->s_blocksize_bits = (1 << shift_val);
    sb->s_blocksize_bits =
        ((sb->s_blocksize > sb->s_blocksize_bits) ?
         sb->s_blocksize_bits : PVFS2_BUFMAP_DEFAULT_DESC_SIZE);

    shift_val = ((sizeof(sb->s_maxbytes) * 8) - 1);
    sb->s_maxbytes = (1 << shift_val);

    if (!silent)
    {
        pvfs2_print("pvfs2: pvfs2_fill_sb -- sb max bytes is %llu (%d)\n",
                    (unsigned long long)sb->s_maxbytes,
                    (int)sb->s_maxbytes);
    }

    /* alloc and initialize our root directory inode */
    root = pvfs2_get_custom_inode(sb, (S_IFDIR | 0755), 0);
    if (!root)
    {
	return -ENOMEM;
    }

    /* alloc and init our private pvfs2 sb info */
    sb->s_fs_info = kmalloc(sizeof(pvfs2_sb_info), PVFS2_GFP_FLAGS);
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
    return get_sb_nodev(fst, flags, data, pvfs2_fill_sb);
}

void pvfs2_kill_sb(
    struct super_block *sb)
{
    pvfs2_print("pvfs2_kill_sb: called\n");

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

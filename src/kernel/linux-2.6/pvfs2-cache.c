#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include "pvfs2-kernel.h"

/* a cache for pvfs2 upcall/downcall operations */
extern kmem_cache_t *op_cache;
static atomic_t next_tag_value;

/* a cache for device (/dev/pvfs2-req) communication */
extern kmem_cache_t *dev_req_cache;

/* a cache for pvfs2-inode objects (i.e. pvfs2 inode private data) */
extern kmem_cache_t *pvfs2_inode_cache;

static void op_cache_ctor(
    void *kernel_op, kmem_cache_t *cachep, unsigned long flags)
{
    pvfs2_kernel_op_t *op = (pvfs2_kernel_op_t *)kernel_op;

    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
        memset(op,0,sizeof(*op));

        INIT_LIST_HEAD(&op->list);
        spin_lock_init(&op->lock);
        init_waitqueue_head(&op->waitq);

        op->upcall.type = PVFS2_VFS_OP_INVALID;
        op->downcall.type = PVFS2_VFS_OP_INVALID;

        op->op_state = PVFS2_VFS_STATE_UNKNOWN;
        op->tag = (unsigned long)atomic_read(&next_tag_value);
        atomic_inc(&next_tag_value);
    }
}

void op_cache_initialize()
{
    /*
      NOTE: the SLAB_POISION and SLAB_RED_ZONE flags
      are for debugging only and should be removed
    */
    op_cache = kmem_cache_create("pvfs2_op_cache",
                                 sizeof(pvfs2_kernel_op_t),
                                 0,
                                 SLAB_POISON | SLAB_RED_ZONE,
                                 op_cache_ctor,
                                 NULL);
    if (!op_cache)
    {
        panic("Cannot create pvfs2_op_cache");
    }

    /* initialize our atomic tag counter */
    atomic_set(&next_tag_value, 100);
}

void op_cache_finalize()
{
    if (kmem_cache_destroy(op_cache) != 0)
    {
        panic("Failed to destroy pvfs2_op_cache");
    }
}

void op_release(void *op)
{
    pvfs2_kernel_op_t *pvfs2_op = (pvfs2_kernel_op_t *)op;

    /* need to free specific upcall/downcall fields here */
    printk("Freeing OP with tag %lu\n",pvfs2_op->tag);

    kmem_cache_free(op_cache, op);
}


static void dev_req_cache_ctor(
    void *req, kmem_cache_t *cachep, unsigned long flags)
{
    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
        memset(req,0,sizeof(MAX_DEV_REQ_DOWNSIZE));
    }
}

void dev_req_cache_initialize()
{
    /*
      NOTE: the SLAB_POISION and SLAB_RED_ZONE flags
      are for debugging only and should be removed
    */
    dev_req_cache = kmem_cache_create("pvfs2_dev_req_cache",
                                      MAX_DEV_REQ_DOWNSIZE,
                                      0,
                                      SLAB_POISON | SLAB_RED_ZONE,
                                      dev_req_cache_ctor,
                                      NULL);
    if (!dev_req_cache)
    {
        panic("Cannot create pvfs2_dev_req_cache");
    }
}

void dev_req_cache_finalize()
{
    if (kmem_cache_destroy(dev_req_cache) != 0)
    {
        panic("Failed to destroy pvfs2_dev_req_cache");
    }
}

static void pvfs2_inode_cache_ctor(
    void *new_pvfs2_inode, kmem_cache_t *cachep, unsigned long flags)
{
    pvfs2_inode_t *pvfs2_inode = (pvfs2_inode_t *)new_pvfs2_inode;

    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
        memset(pvfs2_inode,0,sizeof(pvfs2_inode_t));

        pvfs2_inode->refn.handle = 0;
        pvfs2_inode->refn.fs_id = 0;

        /*
          inode_init_once is from inode.c;
          it's normally run when an inode is allocated by the
          system's inode slab allocator.  we call it here since
          we're overloading the system's inode allocation with
          this routine, thus we have to init vfs inodes manually
        */
        inode_init_once(&pvfs2_inode->vfs_inode);
        pvfs2_inode->vfs_inode.i_version = 1;
    }
}

void pvfs2_inode_cache_initialize(void)
{
    /*
      NOTE: the SLAB_POISION and SLAB_RED_ZONE flags
      are for debugging only and should be removed
    */
    pvfs2_inode_cache = kmem_cache_create("pvfs2_inode_cache",
                                          sizeof(pvfs2_inode_t),
                                          0,
                                          SLAB_POISON | SLAB_RED_ZONE,
                                          pvfs2_inode_cache_ctor,
                                          NULL);
    if (!pvfs2_inode_cache)
    {
        panic("Cannot create pvfs2_inode_cache");
    }
}

void pvfs2_inode_cache_finalize()
{
    if (kmem_cache_destroy(pvfs2_inode_cache) != 0)
    {
        panic("Failed to destroy pvfs2_inode_cache");
    }
}

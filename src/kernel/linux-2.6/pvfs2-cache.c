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
#include <linux/wait.h>
#include "pvfs2-kernel.h"

/* a cache for pvfs2 upcall/downcall operations */
extern kmem_cache_t *op_cache;

static atomic_t next_tag_value;

/* a cache for device (/dev/pvfs2-req) communication */
extern kmem_cache_t *dev_req_cache;

/* a cache for pvfs2-inode objects (i.e. pvfs2 inode private data) */
extern kmem_cache_t *pvfs2_inode_cache;

extern int pvfs2_gen_credentials(
    PVFS_credentials *credentials);


static void op_cache_ctor(
    void *kernel_op,
    kmem_cache_t *cachep,
    unsigned long flags)
{
    pvfs2_kernel_op_t *op = (pvfs2_kernel_op_t *) kernel_op;

    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
	memset(op, 0, sizeof(*op));

	INIT_LIST_HEAD(&op->list);
	spin_lock_init(&op->lock);
	init_waitqueue_head(&op->waitq);

        op->io_completed = 0;
        init_waitqueue_head(&op->io_completion_waitq);

	op->upcall.type = PVFS2_VFS_OP_INVALID;
	op->downcall.type = PVFS2_VFS_OP_INVALID;
        op->downcall.status = -1;

	op->op_state = PVFS2_VFS_STATE_UNKNOWN;

        op->tag = (unsigned long)atomic_read(&next_tag_value);
        atomic_inc(&next_tag_value);

        /* preemptively fill in the upcall credentials */
        pvfs2_gen_credentials(&op->upcall.credentials);
    }
}

void op_cache_initialize(void)
{
    op_cache = kmem_cache_create("pvfs2_op_cache",
				 sizeof(pvfs2_kernel_op_t),
				 0,
                                 PVFS2_CACHE_CREATE_FLAGS,
				 op_cache_ctor, NULL);
    if (!op_cache)
    {
	panic("Cannot create pvfs2_op_cache");
    }

    /* initialize our atomic tag counter */
    atomic_set(&next_tag_value, 100);
}

void op_cache_finalize(void)
{
    if (kmem_cache_destroy(op_cache) != 0)
    {
#ifdef PVFS2_KERNEL_DEBUG
	panic("Failed to destroy pvfs2_op_cache");
#else
        pvfs2_error("Failed to destroy pvfs2_op_cache");
#endif
    }
}

void op_release(void *op)
{
/*     pvfs2_kernel_op_t *pvfs2_op = (pvfs2_kernel_op_t *) op; */

    /* need to free specific upcall/downcall fields here (if any) */
/*     pvfs2_print("Freeing OP with tag %lu\n", pvfs2_op->tag); */

    kmem_cache_free(op_cache, op);
}


static void dev_req_cache_ctor(
    void *req,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
	memset(req, 0, sizeof(MAX_DEV_REQ_DOWNSIZE));
    }
}

void dev_req_cache_initialize(void)
{
    dev_req_cache = kmem_cache_create("pvfs2_dev_req_cache",
				      MAX_DEV_REQ_DOWNSIZE,
				      0,
                                      PVFS2_CACHE_CREATE_FLAGS,
				      dev_req_cache_ctor, NULL);
    if (!dev_req_cache)
    {
	panic("Cannot create pvfs2_dev_req_cache");
    }
}

void dev_req_cache_finalize(void)
{
    if (kmem_cache_destroy(dev_req_cache) != 0)
    {
#ifdef PVFS2_KERNEL_DEBUG
	panic("Failed to destroy pvfs2_dev_req_cache");
#else
	pvfs2_error("Failed to destroy pvfs2_dev_req_cache");
#endif
    }
}

static void pvfs2_inode_cache_ctor(
    void *new_pvfs2_inode,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    pvfs2_inode_t *pvfs2_inode = (pvfs2_inode_t *) new_pvfs2_inode;

    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
	memset(pvfs2_inode, 0, sizeof(pvfs2_inode_t));

	pvfs2_inode->refn.handle = 0;
	pvfs2_inode->refn.fs_id = 0;
        pvfs2_inode->link_target = NULL;
        pvfs2_inode->last_failed_block_index_read = 0;

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

static void pvfs2_inode_cache_dtor(
    void *old_pvfs2_inode,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    pvfs2_inode_t *pvfs2_inode = (pvfs2_inode_t *)old_pvfs2_inode;

    if (pvfs2_inode && pvfs2_inode->link_target)
    {
        kfree(pvfs2_inode->link_target);
        pvfs2_inode->link_target = NULL;
    }
}

void pvfs2_inode_cache_initialize(void)
{
    pvfs2_inode_cache = kmem_cache_create("pvfs2_inode_cache",
					  sizeof(pvfs2_inode_t),
					  0,
                                          PVFS2_CACHE_CREATE_FLAGS,
					  pvfs2_inode_cache_ctor,
                                          pvfs2_inode_cache_dtor);
    if (!pvfs2_inode_cache)
    {
	panic("Cannot create pvfs2_inode_cache");
    }
}

void pvfs2_inode_cache_finalize(void)
{
    if (kmem_cache_destroy(pvfs2_inode_cache) != 0)
    {
#ifdef PVFS2_KERNEL_DEBUG
	panic("Failed to destroy pvfs2_inode_cache");
#else
	pvfs2_error("Failed to destroy pvfs2_inode_cache");
#endif
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

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

        init_waitqueue_head(&op->io_completion_waitq);

        pvfs2_op_initialize(op);

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
	pvfs2_panic("Cannot create pvfs2_op_cache\n");
    }

    /* initialize our atomic tag counter */
    atomic_set(&next_tag_value, 100);
}

void op_cache_finalize(void)
{
    if (kmem_cache_destroy(op_cache) != 0)
    {
	pvfs2_panic("Failed to destroy pvfs2_op_cache\n");
    }
}

void op_release(void *op)
{
    pvfs2_kernel_op_t *pvfs2_op = (pvfs2_kernel_op_t *)op;

    pvfs2_op_initialize(pvfs2_op);
    kmem_cache_free(op_cache, op);
}


static void dev_req_cache_ctor(
    void *req,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
	memset(req, 0, sizeof(MAX_ALIGNED_DEV_REQ_DOWNSIZE));
    }
    else
    {
        pvfs2_error("WARNING!! devreq_ctor called without ctor flag\n");
    }
}

void dev_req_cache_initialize(void)
{
    dev_req_cache = kmem_cache_create("pvfs2_devreqcache",
				      MAX_ALIGNED_DEV_REQ_DOWNSIZE,
				      0,
                                      PVFS2_CACHE_CREATE_FLAGS,
				      dev_req_cache_ctor, NULL);
    if (!dev_req_cache)
    {
	pvfs2_panic("Cannot create pvfs2_dev_req_cache\n");
    }
}

void dev_req_cache_finalize(void)
{
    if (kmem_cache_destroy(dev_req_cache) != 0)
    {
	pvfs2_panic("Failed to destroy pvfs2_devreqcache\n");
    }
}

static void pvfs2_inode_cache_ctor(
    void *new_pvfs2_inode,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    pvfs2_inode_t *pvfs2_inode = (pvfs2_inode_t *)new_pvfs2_inode;

    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
	memset(pvfs2_inode, 0, sizeof(pvfs2_inode_t));

        pvfs2_inode_initialize(pvfs2_inode);

#ifndef PVFS2_LINUX_KERNEL_2_4
	/*
	   inode_init_once is from 2.6.x's inode.c; it's normally run
	   when an inode is allocated by the system's inode slab
	   allocator.  we call it here since we're overloading the
	   system's inode allocation with this routine, thus we have
	   to init vfs inodes manually
        */
	inode_init_once(&pvfs2_inode->vfs_inode);
	pvfs2_inode->vfs_inode.i_version = 1;
#endif
    }
    else
    {
        pvfs2_error("WARNING!! inode_ctor called without ctor flag\n");
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
	pvfs2_panic("Cannot create pvfs2_inode_cache\n");
    }
}

void pvfs2_inode_cache_finalize(void)
{
    if (kmem_cache_destroy(pvfs2_inode_cache) != 0)
    {
	pvfs2_panic("Failed to destroy pvfs2_inode_cache\n");
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

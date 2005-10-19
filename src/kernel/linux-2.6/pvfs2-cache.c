/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"

/* a cache for pvfs2 upcall/downcall operations */
static kmem_cache_t *op_cache = NULL;

static uint64_t next_tag_value;
static spinlock_t next_tag_value_lock = SPIN_LOCK_UNLOCKED;

/* a cache for device (/dev/pvfs2-req) communication */
extern kmem_cache_t *dev_req_cache;

/* a cache for pvfs2-inode objects (i.e. pvfs2 inode private data) */
extern kmem_cache_t *pvfs2_inode_cache;

extern int debug;
extern int pvfs2_gen_credentials(
    PVFS_credentials *credentials);

#ifdef HAVE_AIO_VFS_SUPPORT
/* a cache for pvfs2_kiocb objects (i.e pvfs2 iocb structures ) */
static kmem_cache_t *pvfs2_kiocb_cache;
#endif

void op_cache_initialize(void)
{
    op_cache = kmem_cache_create(
        "pvfs2_op_cache", sizeof(pvfs2_kernel_op_t),
        0, PVFS2_CACHE_CREATE_FLAGS, NULL, NULL);

    if (!op_cache)
    {
        pvfs2_panic("Cannot create pvfs2_op_cache\n");
    }

    /* initialize our atomic tag counter */
    spin_lock(&next_tag_value_lock);
    next_tag_value = 100;
    spin_unlock(&next_tag_value_lock);
}

void op_cache_finalize(void)
{
    if (kmem_cache_destroy(op_cache) != 0)
    {
        pvfs2_panic("Failed to destroy pvfs2_op_cache\n");
    }
}

pvfs2_kernel_op_t *op_alloc(void)
{
    pvfs2_kernel_op_t *new_op = NULL;

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (new_op)
    {
        memset(new_op, 0, sizeof(pvfs2_kernel_op_t));

        INIT_LIST_HEAD(&new_op->list);
        spin_lock_init(&new_op->lock);
        init_waitqueue_head(&new_op->waitq);

        init_waitqueue_head(&new_op->io_completion_waitq);
        atomic_set(&new_op->aio_ref_count, 0);

        pvfs2_op_initialize(new_op);

        /* initialize the op specific tag and upcall credentials */
        spin_lock(&next_tag_value_lock);
        new_op->tag = next_tag_value++;
        if (next_tag_value == 0)
        {
            next_tag_value = 100;
        }
        spin_unlock(&next_tag_value_lock);

        pvfs2_gen_credentials(&new_op->upcall.credentials);
    }
    else
    {
        pvfs2_panic("op_alloc: kmem_cache_alloc failed!\n");
    }
    return new_op;
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
    dev_req_cache = kmem_cache_create(
        "pvfs2_devreqcache", MAX_ALIGNED_DEV_REQ_DOWNSIZE, 0,
        PVFS2_CACHE_CREATE_FLAGS, dev_req_cache_ctor, NULL);

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
        /* Initialize the reader/writer semaphore */
        init_rwsem(&pvfs2_inode->xattr_sem);
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
    pvfs2_inode_cache = kmem_cache_create(
        "pvfs2_inode_cache", sizeof(pvfs2_inode_t), 0,
        PVFS2_CACHE_CREATE_FLAGS, pvfs2_inode_cache_ctor,
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

#ifdef HAVE_AIO_VFS_SUPPORT

static void kiocb_ctor(
    void *req,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
        memset(req, 0, sizeof(pvfs2_kiocb));
    }
    else
    {
        pvfs2_error("WARNING!! kiocb_ctor called without ctor flag\n");
    }
}


void kiocb_cache_initialize(void)
{
    pvfs2_kiocb_cache = kmem_cache_create(
        "pvfs2_kiocbcache", sizeof(pvfs2_kiocb), 0,
        PVFS2_CACHE_CREATE_FLAGS, kiocb_ctor, NULL);

    if (!pvfs2_kiocb_cache)
    {
        pvfs2_panic("Cannot create pvfs2_kiocb_cache!\n");
    }
}

void kiocb_cache_finalize(void)
{
    if (kmem_cache_destroy(pvfs2_kiocb_cache) != 0)
    {
        pvfs2_panic("Failed to destroy pvfs2_devreqcache\n");
    }
}

pvfs2_kiocb* kiocb_alloc(void)
{
    pvfs2_kiocb *x = NULL;

    x = kmem_cache_alloc(pvfs2_kiocb_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (x == NULL)
    {
        pvfs2_panic("kiocb_alloc: kmem_cache_alloc failed!\n");
    }
    return x;
}

void kiocb_release(pvfs2_kiocb *x)
{
    if (x)
    {
        kmem_cache_free(pvfs2_kiocb_cache, x);
    }
    else 
    {
        pvfs2_panic("kiocb_release: kmem_cache_free NULL pointer!\n");
    }
}

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

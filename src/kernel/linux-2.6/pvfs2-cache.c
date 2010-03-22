/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"

/* A list of all allocated pvfs2 inode objects */
static spinlock_t pvfs2_inode_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(pvfs2_inode_list);

/* tags assigned to kernel upcall operations */
static uint64_t next_tag_value;
static spinlock_t next_tag_value_lock = SPIN_LOCK_UNLOCKED;

/* the pvfs2 memory caches */

#ifdef HAVE_STRUCT_KMEM_CACHE
typedef struct kmem_cache pvfs2_kmem_cache_t;
#else
typedef kmem_cache_t pvfs2_kmem_cache_t;
#endif

/* a cache for pvfs2 upcall/downcall operations */
static pvfs2_kmem_cache_t *op_cache = NULL;
/* a cache for device (/dev/pvfs2-req) communication */
static pvfs2_kmem_cache_t *dev_req_cache = NULL;
/* a cache for pvfs2-inode objects (i.e. pvfs2 inode private data) */
static pvfs2_kmem_cache_t *pvfs2_inode_cache = NULL;
#ifdef HAVE_AIO_VFS_SUPPORT
/* a cache for pvfs2_kiocb objects (i.e pvfs2 iocb structures ) */
static pvfs2_kmem_cache_t *pvfs2_kiocb_cache = NULL;
#endif

#ifdef HAVE_KMEM_CACHE_DESTROY_INT_RETURN
#define pvfs_kmem_cache_destroy kmem_cache_destroy
#else
/* recent kernels do not return a value */
static int pvfs_kmem_cache_destroy(void *x)
{
    kmem_cache_destroy(x);
    return 0;
}
#endif

int op_cache_initialize(void)
{
    op_cache = kmem_cache_create(
        "pvfs2_op_cache", sizeof(pvfs2_kernel_op_t),
        0, PVFS2_CACHE_CREATE_FLAGS, NULL
#ifdef HAVE_KMEM_CACHE_CREATE_DESTRUCTOR_PARAM
        , NULL
#endif
        );

    if (!op_cache)
    {
        gossip_err("Cannot create pvfs2_op_cache\n");
        return -ENOMEM;
    }

    /* initialize our atomic tag counter */
    spin_lock(&next_tag_value_lock);
    next_tag_value = 100;
    spin_unlock(&next_tag_value_lock);
    return 0;
}

int op_cache_finalize(void)
{
    if (pvfs_kmem_cache_destroy(op_cache) != 0)
    {
        gossip_err("Failed to destroy pvfs2_op_cache\n");
        return -EINVAL;
    }
    return 0;
}

char *get_opname_string(pvfs2_kernel_op_t *new_op)
{
    if (new_op)
    {
        int32_t type = new_op->upcall.type;
        if (type == PVFS2_VFS_OP_FILE_IO)
            return "OP_FILE_IO";
        else if (type == PVFS2_VFS_OP_LOOKUP)
            return "OP_LOOKUP";
        else if (type == PVFS2_VFS_OP_CREATE)
            return "OP_CREATE";
        else if (type == PVFS2_VFS_OP_GETATTR)
            return "OP_GETATTR";
        else if (type == PVFS2_VFS_OP_REMOVE)
            return "OP_REMOVE";
        else if (type == PVFS2_VFS_OP_MKDIR)
            return "OP_MKDIR";
        else if (type == PVFS2_VFS_OP_READDIR)
            return "OP_READDIR";
        else if (type == PVFS2_VFS_OP_READDIRPLUS)
            return "OP_READDIRPLUS";
        else if (type == PVFS2_VFS_OP_SETATTR)
            return "OP_SETATTR";
        else if (type == PVFS2_VFS_OP_SYMLINK)
            return "OP_SYMLINK";
        else if (type == PVFS2_VFS_OP_RENAME)
            return "OP_RENAME";
        else if (type == PVFS2_VFS_OP_STATFS)
            return "OP_STATFS";
        else if (type == PVFS2_VFS_OP_TRUNCATE)
            return "OP_TRUNCATE";
        else if (type == PVFS2_VFS_OP_MMAP_RA_FLUSH)
            return "OP_MMAP_RA_FLUSH";
        else if (type == PVFS2_VFS_OP_FS_MOUNT)
            return "OP_FS_MOUNT";
        else if (type == PVFS2_VFS_OP_FS_UMOUNT)
            return "OP_FS_UMOUNT";
        else if (type == PVFS2_VFS_OP_GETXATTR)
            return "OP_GETXATTR";
        else if (type == PVFS2_VFS_OP_SETXATTR)
            return "OP_SETXATTR";
        else if (type == PVFS2_VFS_OP_LISTXATTR)
            return "OP_LISTXATTR";
        else if (type == PVFS2_VFS_OP_REMOVEXATTR)
            return "OP_REMOVEXATTR";
        else if (type == PVFS2_VFS_OP_PARAM)
            return "OP_PARAM";
        else if (type == PVFS2_VFS_OP_PERF_COUNT)
            return "OP_PERF_COUNT";
        else if (type == PVFS2_VFS_OP_CANCEL)
            return "OP_CANCEL";
        else if (type == PVFS2_VFS_OP_FSYNC)
            return "OP_FSYNC";
        else if (type == PVFS2_VFS_OP_FSKEY)
            return "OP_FSKEY";
        else if (type == PVFS2_VFS_OP_FILE_IOX)
            return "OP_FILE_IOX";
    }
    return "OP_UNKNOWN?";
}

static pvfs2_kernel_op_t *op_alloc_common(int32_t op_linger, int32_t type)
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
        new_op->upcall.type = type;
        new_op->attempts = 0;
        gossip_debug(GOSSIP_CACHE_DEBUG, "Alloced OP (%p: %ld %s)\n", new_op, (unsigned long) new_op->tag, get_opname_string(new_op));

        pvfs2_gen_credentials(&new_op->upcall.credentials);
        new_op->op_linger = new_op->op_linger_tmp = op_linger;
    }
    else
    {
        gossip_err("op_alloc: kmem_cache_alloc failed!\n");
    }
    return new_op;
}

pvfs2_kernel_op_t *op_alloc(int32_t type)
{
    return op_alloc_common(1, type);
}

pvfs2_kernel_op_t *op_alloc_trailer(int32_t type)
{
    return op_alloc_common(2, type);
}

void op_release(pvfs2_kernel_op_t *pvfs2_op)
{
    if (pvfs2_op)
    {
        gossip_debug(GOSSIP_CACHE_DEBUG, "Releasing OP (%p: %ld)\n", pvfs2_op, (unsigned long) pvfs2_op->tag);
        pvfs2_op_initialize(pvfs2_op);
        kmem_cache_free(op_cache, pvfs2_op);
    }
    else 
    {
        gossip_err("NULL pointer in op_release\n");
    }
}


static void dev_req_cache_ctor(
#if defined(HAVE_KMEM_CACHE_CREATE_CTOR_ONE_PARAM)
    void *req
#elif defined(HAVE_KMEM_CACHE_CREATE_CTOR_TWO_PARAM)
    struct kmem_cache *cachep,
    void *req
#else
    void *req,
    pvfs2_kmem_cache_t * cachep,
    unsigned long flags
#endif
)
{
    memset(req, 0, sizeof(MAX_ALIGNED_DEV_REQ_DOWNSIZE));
}

int dev_req_cache_initialize(void)
{
    dev_req_cache = kmem_cache_create(
        "pvfs2_devreqcache", MAX_ALIGNED_DEV_REQ_DOWNSIZE, 0,
        PVFS2_CACHE_CREATE_FLAGS, dev_req_cache_ctor
#ifdef HAVE_KMEM_CACHE_CREATE_DESTRUCTOR_PARAM
        , NULL
#endif
        );

    if (!dev_req_cache)
    {
        gossip_err("Cannot create pvfs2_dev_req_cache\n");
        return -ENOMEM;
    }
    return 0;
}

int dev_req_cache_finalize(void)
{
    if (pvfs_kmem_cache_destroy(dev_req_cache) != 0)
    {
        gossip_err("Failed to destroy pvfs2_devreqcache\n");
        return -EINVAL;
    }
    return 0;
}

void *dev_req_alloc(void)
{
    void *buffer;

    buffer = kmem_cache_alloc(dev_req_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (buffer == NULL)
    {
        gossip_err("Failed to allocate from dev_req_cache\n"); 
    }
    return buffer;
}

void dev_req_release(void *buffer)
{
    if (buffer)
    {
        kmem_cache_free(dev_req_cache, buffer);
    }
    else 
    {
        gossip_err("NULL pointer passed to dev_req_release\n");
    }
    return;
}

static void pvfs2_inode_cache_ctor(
#if defined(HAVE_KMEM_CACHE_CREATE_CTOR_ONE_PARAM)
    void *req
#elif defined(HAVE_KMEM_CACHE_CREATE_CTOR_TWO_PARAM)
    struct kmem_cache *cachep,
    void *req
#else
    void *req,
    pvfs2_kmem_cache_t * cachep,
    unsigned long flags
#endif
)
{
    pvfs2_inode_t *pvfs2_inode = req;

    memset(pvfs2_inode, 0, sizeof(pvfs2_inode_t));
    ClearInitFlag(pvfs2_inode);

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

static inline void add_to_pinode_list(pvfs2_inode_t *pvfs2_inode)
{
    spin_lock(&pvfs2_inode_list_lock);
    list_add_tail(&pvfs2_inode->list, &pvfs2_inode_list);
    spin_unlock(&pvfs2_inode_list_lock);
    return;
}

static inline void del_from_pinode_list(pvfs2_inode_t *pvfs2_inode)
{
    spin_lock(&pvfs2_inode_list_lock);
    list_del_init(&pvfs2_inode->list);
    spin_unlock(&pvfs2_inode_list_lock);
    return;
}

int pvfs2_inode_cache_initialize(void)
{
    pvfs2_inode_cache = kmem_cache_create(
        "pvfs2_inode_cache", sizeof(pvfs2_inode_t), 0,
        PVFS2_CACHE_CREATE_FLAGS, pvfs2_inode_cache_ctor
#ifdef HAVE_KMEM_CACHE_CREATE_DESTRUCTOR_PARAM
        , NULL
#endif
        );

    if (!pvfs2_inode_cache)
    {
        gossip_err("Cannot create pvfs2_inode_cache\n");
        return -ENOMEM;
    }
    return 0;
}

int pvfs2_inode_cache_finalize(void)
{
    if (!list_empty(&pvfs2_inode_list))
    {
        gossip_err("pvfs2_inode_cache_finalize: WARNING: releasing unreleased pvfs2 inode objects!\n");
        while (pvfs2_inode_list.next != &pvfs2_inode_list)
        {
            pvfs2_inode_t *pinode = list_entry(pvfs2_inode_list.next, pvfs2_inode_t, list);
            list_del(pvfs2_inode_list.next);
            kmem_cache_free(pvfs2_inode_cache, pinode);
        }
    }
    if (pvfs_kmem_cache_destroy(pvfs2_inode_cache) != 0)
    {
        gossip_err("Failed to destroy pvfs2_inode_cache\n");
        return -EINVAL;
    }
    return 0;
}

pvfs2_inode_t* pvfs2_inode_alloc(void)
{
    pvfs2_inode_t *pvfs2_inode = NULL;
    /*
        this allocator has an associated constructor that fills in the
        internal vfs inode structure.  this initialization is extremely
        important and is required since we're allocating the inodes
        ourselves (rather than letting the system inode allocator
        initialize them for us); see inode.c/inode_init_once()
    */
    pvfs2_inode = kmem_cache_alloc(pvfs2_inode_cache,
                                   PVFS2_CACHE_ALLOC_FLAGS);
    if (pvfs2_inode == NULL) 
    {
        gossip_err("Failed to allocate pvfs2_inode\n");
    }
    else {
        add_to_pinode_list(pvfs2_inode);
    }
    return pvfs2_inode;
}

void pvfs2_inode_release(pvfs2_inode_t *pinode)
{
    if (pinode)
    {
        del_from_pinode_list(pinode);
        kmem_cache_free(pvfs2_inode_cache, pinode);
    }
    else
    {
        gossip_err("NULL pointer in pvfs2_inode_release\n");
    }
}

#ifdef HAVE_AIO_VFS_SUPPORT

static void kiocb_ctor(
#if defined(HAVE_KMEM_CACHE_CREATE_CTOR_ONE_PARAM)
    void *req
#elif defined(HAVE_KMEM_CACHE_CREATE_CTOR_TWO_PARAM)
    struct kmem_cache *cachep,
    void *req
#else
    void *req,
    pvfs2_kmem_cache_t * cachep,
    unsigned long flags
#endif
)
{
    memset(req, 0, sizeof(pvfs2_kiocb));
}


int kiocb_cache_initialize(void)
{
    pvfs2_kiocb_cache = kmem_cache_create(
        "pvfs2_kiocbcache", sizeof(pvfs2_kiocb), 0,
        PVFS2_CACHE_CREATE_FLAGS, kiocb_ctor
#ifdef HAVE_KMEM_CACHE_CREATE_DESTRUCTOR_PARAM
        , NULL
#endif
        );

    if (!pvfs2_kiocb_cache)
    {
        gossip_err("Cannot create pvfs2_kiocb_cache!\n");
        return -ENOMEM;
    }
    return 0;
}

int kiocb_cache_finalize(void)
{
    if (pvfs_kmem_cache_destroy(pvfs2_kiocb_cache) != 0)
    {
        gossip_err("Failed to destroy pvfs2_devreqcache\n");
        return -EINVAL;
    }
    return 0;
}

pvfs2_kiocb* kiocb_alloc(void)
{
    pvfs2_kiocb *x = NULL;

    x = kmem_cache_alloc(pvfs2_kiocb_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (x == NULL)
    {
        gossip_err("kiocb_alloc: kmem_cache_alloc failed!\n");
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
        gossip_err("kiocb_release: kmem_cache_free NULL pointer!\n");
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

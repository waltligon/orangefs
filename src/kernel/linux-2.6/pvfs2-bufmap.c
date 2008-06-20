/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pint-dev-shared.h"


static int bufmap_page_count, pages_per_desc;

static int32_t pvfs2_bufmap_desc_size, pvfs2_bufmap_desc_shift,
               pvfs2_bufmap_desc_count, pvfs2_bufmap_total_size;

inline int pvfs_bufmap_size_query(void)
{
    return pvfs2_bufmap_desc_size;
}

inline int pvfs_bufmap_shift_query(void)
{
    return pvfs2_bufmap_desc_shift;
}

static int bufmap_init = 0;
DECLARE_RWSEM(bufmap_init_sem);
static struct page **bufmap_page_array = NULL;

/* array to track usage of buffer descriptors */
static int *buffer_index_array = NULL;
static spinlock_t buffer_index_lock = SPIN_LOCK_UNLOCKED;

/* array to track usage of buffer descriptors for readdir/readdirplus */
static int readdir_index_array[PVFS2_READDIR_DEFAULT_DESC_COUNT] = {0};
static spinlock_t readdir_index_lock = SPIN_LOCK_UNLOCKED;

static struct pvfs_bufmap_desc *desc_array = NULL;

static DECLARE_WAIT_QUEUE_HEAD(bufmap_waitq);
static DECLARE_WAIT_QUEUE_HEAD(readdir_waitq);

static int initialize_bufmap_descriptors(int ndescs)
{
    int err;

    err = -EINVAL;
    if (ndescs < 0)
    {
        gossip_err("pvfs2: ndescs (%d) cannot be < 0.\n", ndescs);
        goto out;
    }
    err = -ENOMEM;
    buffer_index_array = kzalloc(ndescs * sizeof(*buffer_index_array), 
                                 PVFS2_BUFMAP_GFP_FLAGS);
    if (buffer_index_array == NULL) 
    {
        gossip_err("pvfs2: could not allocate %d bytes\n",
                (int) (ndescs * sizeof(*buffer_index_array)));
        goto out;
    }

    desc_array = kmalloc(ndescs * sizeof(*desc_array),
                         PVFS2_BUFMAP_GFP_FLAGS);
    if (desc_array == NULL)
    {
        gossip_err("pvfs2: could not allocate %d bytes\n",
		(int) (ndescs * sizeof(*desc_array)));
        goto out1;
    }
    err = 0;
    goto out;

out1:
    kfree(buffer_index_array);
    buffer_index_array = NULL;
out:
    return err;
}

static void finalize_bufmap_descriptors(void)
{
    if (buffer_index_array != NULL) 
    {
        kfree(buffer_index_array);
        buffer_index_array = NULL;
    }
    if (desc_array != NULL)
    {
        kfree(desc_array);
        desc_array = NULL;
    }
    return;
}

/* pvfs_bufmap_initialize()
 *
 * initializes the mapped buffer interface
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_initialize(struct PVFS_dev_map_desc *user_desc)
{
    int ret = -EINVAL;
    int i = 0;
    int offset = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_initialize: called "
                 "(ptr (%p) sz (%d) cnt(%d).\n",
                 user_desc->ptr, user_desc->size, user_desc->count);

    down_write(&bufmap_init_sem);
    if (bufmap_init == 1)
    {
        gossip_err("pvfs2: error: bufmap already initialized.\n");
        ret = -EALREADY;
        goto init_failure;
    }

    /* sanity check alignment and size of buffer that caller wants to
     * work with
     */
    if (PAGE_ALIGN((unsigned long)user_desc->ptr) != 
        (unsigned long)user_desc->ptr)
    {
        gossip_err("pvfs2 error: memory alignment (front). %p\n",
                user_desc->ptr);
        goto init_failure;
    }

    if (PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->total_size)) != 
        (unsigned long)(user_desc->ptr + user_desc->total_size))
    {
        gossip_err("pvfs2 error: memory alignment (back).(%p + %d)\n",
                user_desc->ptr, user_desc->total_size);
        goto init_failure;
    }

    if (user_desc->total_size != (user_desc->size * user_desc->count))
    {
        gossip_err("pvfs2 error: user provided an oddly "
                    "sized buffer...(%d, %d, %d)\n",
                    user_desc->total_size, user_desc->size, user_desc->count);
        goto init_failure;
    }

    if ((user_desc->size % PAGE_SIZE) != 0)
    {
        gossip_err("pvfs2 error: bufmap size not page size "
                    "divisible (%d).\n", user_desc->size);
        goto init_failure;
    }
    /* Initialize critical variables */
    pvfs2_bufmap_total_size = user_desc->total_size;
    pvfs2_bufmap_desc_count = user_desc->count;
    pvfs2_bufmap_desc_size  = user_desc->size;
    pvfs2_bufmap_desc_shift = LOG2(pvfs2_bufmap_desc_size);
    bufmap_page_count = pvfs2_bufmap_total_size / PAGE_SIZE;
    pages_per_desc    = pvfs2_bufmap_desc_size / PAGE_SIZE;
    /* Initialize descriptor arrays */
    if ((ret = initialize_bufmap_descriptors(pvfs2_bufmap_desc_count)) < 0)
    {
        goto init_failure;
    }

    /* allocate storage to track our page mappings */
    bufmap_page_array = kmalloc(bufmap_page_count * sizeof(*bufmap_page_array), 
                                PVFS2_BUFMAP_GFP_FLAGS);
    if (!bufmap_page_array)
    {
        ret = -ENOMEM;
        goto init_failure;
    }

    /* map the pages */
    down_write(&current->mm->mmap_sem);

    ret = get_user_pages(
        current, current->mm, (unsigned long)user_desc->ptr,
        bufmap_page_count, 1, 0, bufmap_page_array, NULL);

    up_write(&current->mm->mmap_sem);

    if (ret < 0)
    {
        kfree(bufmap_page_array);
        goto init_failure;
    }

    /*
      in theory we could run with what we got, but I will just treat
      it as an error for simplicity's sake right now
    */
    if (ret != bufmap_page_count)
    {
        gossip_err("pvfs2 error: asked for %d pages, only got %d.\n",
                    (int) bufmap_page_count, ret);

        for(i = 0; i < ret; i++)
        {
            SetPageError(bufmap_page_array[i]);
            page_cache_release(bufmap_page_array[i]);
        }
        kfree(bufmap_page_array);
        ret = -ENOMEM;
        goto init_failure;
    }

    /*
      ideally we want to get kernel space pointers for each page, but
      we can't kmap that many pages at once if highmem is being used.
      so instead, we just kmap/kunmap the page address each time the
      kaddr is needed.  this loop used to kmap every page, but now it
      only ensures every page is marked reserved (non-pageable) NOTE:
      setting PageReserved in 2.6.x seems to cause more trouble than
      it's worth.  in 2.4.x, marking the pages does what's expected
      and doesn't try to swap out our pages
    */
    for(i = 0; i < bufmap_page_count; i++)
    {
        flush_dcache_page(bufmap_page_array[i]);
        pvfs2_set_page_reserved(bufmap_page_array[i]);
    }

    /* build a list of available descriptors */
    for(offset = 0, i = 0; i < pvfs2_bufmap_desc_count; i++)
    {
        desc_array[i].page_array = &bufmap_page_array[offset];
        desc_array[i].array_count = pages_per_desc;
        desc_array[i].uaddr =
            (user_desc->ptr + (i * pages_per_desc * PAGE_SIZE));
        offset += pages_per_desc;
    }

    /* clear any previously used buffer indices */
    spin_lock(&buffer_index_lock);
    for(i = 0; i < pvfs2_bufmap_desc_count; i++)
    {
        buffer_index_array[i] = 0;
    }
    spin_unlock(&buffer_index_lock);
    spin_lock(&readdir_index_lock);
    for (i = 0; i < PVFS2_READDIR_DEFAULT_DESC_COUNT; i++)
    {
        readdir_index_array[i] = 0;
    }
    spin_unlock(&readdir_index_lock);

    bufmap_init = 1;
    up_write(&bufmap_init_sem);

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_initialize: exiting normally\n");
    return 0;

init_failure:
    finalize_bufmap_descriptors();
    up_write(&bufmap_init_sem);
    return ret;
}

/* pvfs_bufmap_finalize()
 *
 * shuts down the mapped buffer interface and releases any resources
 * associated with it
 *
 * no return value
 */
void pvfs_bufmap_finalize(void)
{
    int i = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_finalize: called\n");

    down_write(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_finalize: not yet "
                    "initialized; returning\n");
        up_write(&bufmap_init_sem);
        return;
    }

    for(i = 0; i < bufmap_page_count; i++)
    {
        pvfs2_clear_page_reserved(bufmap_page_array[i]);
        page_cache_release(bufmap_page_array[i]);
    }
    kfree(bufmap_page_array);

    bufmap_init = 0;

    finalize_bufmap_descriptors();
    up_write(&bufmap_init_sem);
    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_finalize: exiting normally\n");
}

struct slot_args {
    int         slot_count;
    int        *slot_array;
    spinlock_t *slot_lock;
    wait_queue_head_t *slot_wq;
};

static int wait_for_a_slot(struct slot_args *slargs, int *buffer_index)
{
    int ret = -1, i = 0;
    DECLARE_WAITQUEUE(my_wait, current);

    add_wait_queue_exclusive(slargs->slot_wq, &my_wait);

    while(1)
    {
        set_current_state(TASK_INTERRUPTIBLE);

        /* check for available desc */
        spin_lock(slargs->slot_lock);
        for(i = 0; i < slargs->slot_count; i++)
        {
            if (slargs->slot_array[i] == 0)
            {
                slargs->slot_array[i] = 1;
                *buffer_index = i;
                ret = 0;
                break;
            }
        }
        spin_unlock(slargs->slot_lock);

        /* if we acquired a buffer, then break out of while */
        if (ret == 0)
        {
            break;
        }

        if (!signal_pending(current))
        {
            int timeout = MSECS_TO_JIFFIES(1000 * slot_timeout_secs);
            gossip_debug(GOSSIP_BUFMAP_DEBUG,
                         "[BUFMAP]: waiting %d seconds for a slot\n",
                         slot_timeout_secs);
            if (!schedule_timeout(timeout))
            {
                gossip_debug(GOSSIP_BUFMAP_DEBUG, "*** wait_for_a_slot timed out\n");
                ret = -ETIMEDOUT;
                break;
            }
            gossip_debug(GOSSIP_BUFMAP_DEBUG,
                         "[BUFMAP]: acquired slot\n");
            continue;
        }

        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2: wait_for_a_slot() interrupted.\n");
        ret = -EINTR;
        break;
    }

    set_current_state(TASK_RUNNING);
    remove_wait_queue(slargs->slot_wq, &my_wait);

    return ret;
}

static void put_back_slot(struct slot_args *slargs, int buffer_index)
{
    if (buffer_index < 0 || buffer_index >= slargs->slot_count)
    {
        return;
    }
   /* put the desc back on the queue */
    spin_lock(slargs->slot_lock);
    slargs->slot_array[buffer_index] = 0;
    spin_unlock(slargs->slot_lock);

    /* wake up anyone who may be sleeping on the queue */
    wake_up_interruptible(slargs->slot_wq);
}

/* pvfs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_get(int *buffer_index)
{
    struct slot_args slargs;
    int ret;

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_get: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    slargs.slot_count = pvfs2_bufmap_desc_count;
    slargs.slot_array = buffer_index_array;
    slargs.slot_lock  = &buffer_index_lock;
    slargs.slot_wq    = &bufmap_waitq;
    ret = wait_for_a_slot(&slargs, buffer_index);
    up_read(&bufmap_init_sem);
    return(ret);
}

/* pvfs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void pvfs_bufmap_put(int buffer_index)
{
    struct slot_args slargs;

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_put: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return;
    }

    slargs.slot_count = pvfs2_bufmap_desc_count;
    slargs.slot_array = buffer_index_array;
    slargs.slot_lock  = &buffer_index_lock;
    slargs.slot_wq    = &bufmap_waitq;
    put_back_slot(&slargs, buffer_index);
    up_read(&bufmap_init_sem);
    return;
}

/* readdir_index_get()
 *
 * gets a free descriptor, will sleep until one becomes
 * available if necessary.
 * Although the readdir buffers are not mapped into kernel space
 * we could do that at a later point of time. Regardless, these
 * indices are used by the client-core.
 *
 * returns 0 on success, -errno on failure
 */
int readdir_index_get(int *buffer_index)
{
    struct slot_args slargs;
    int ret;

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_get: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    slargs.slot_count = PVFS2_READDIR_DEFAULT_DESC_COUNT;
    slargs.slot_array = readdir_index_array;
    slargs.slot_lock  = &readdir_index_lock;
    slargs.slot_wq    = &readdir_waitq;
    ret = wait_for_a_slot(&slargs, buffer_index);
    up_read(&bufmap_init_sem);
    return(ret);
}

void readdir_index_put(int buffer_index)
{
    struct slot_args slargs;

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_get: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return;
    }


    slargs.slot_count = PVFS2_READDIR_DEFAULT_DESC_COUNT;
    slargs.slot_array = readdir_index_array;
    slargs.slot_lock  = &readdir_index_lock;
    slargs.slot_wq    = &readdir_waitq;
    put_back_slot(&slargs, buffer_index);
    up_read(&bufmap_init_sem);
    return;
}

/* pvfs_bufmap_copy_to_user()
 *
 * copies data out of a mapped buffer to a user space address
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user(void __user *to, int buffer_index, size_t size)
{
    size_t ret = 0, amt_copied = 0, amt_remaining = 0, cur_copy_size = 0;
    int from_page_index = 0;
    void *from_kaddr = NULL;
    void __user *to_kaddr = to;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_user: to %p, from %p, index %d, "
                "size %zd\n", to, from, buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_copy_to_user: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    while(amt_copied < size)
    {
        amt_remaining = (size - amt_copied);
        cur_copy_size =
            ((amt_remaining > PAGE_SIZE) ? PAGE_SIZE : amt_remaining);

        from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
        ret = copy_to_user(to_kaddr, from_kaddr, cur_copy_size);
        pvfs2_kunmap(from->page_array[from_page_index]);

        if (ret)
        {
            gossip_debug(GOSSIP_BUFMAP_DEBUG, "Failed to copy data to user space %zd\n", ret);
            up_read(&bufmap_init_sem);
            return -EFAULT;
        }

        to_kaddr += cur_copy_size;
        amt_copied += cur_copy_size;
        from_page_index++;
    }
    up_read(&bufmap_init_sem);
    return 0;
}

/* pvfs_bufmap_copy_to_kernel()
 *
 * copies data out of a mapped buffer to a kernel space address
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_kernel(void *to, int buffer_index, size_t size)
{
    size_t amt_copied = 0, amt_remaining = 0, cur_copy_size = 0;
    int from_page_index = 0;
    void *to_kaddr = to, *from_kaddr = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_kernel: to %p, index %d, size %zd\n",
                to, buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_copy_to_kernel: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    while(amt_copied < size)
    {
        amt_remaining = (size - amt_copied);
        cur_copy_size =
            ((amt_remaining > PAGE_SIZE) ? PAGE_SIZE : amt_remaining);

        from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
        memcpy(to_kaddr, from_kaddr, cur_copy_size);
        pvfs2_kunmap(from->page_array[from_page_index]);

        to_kaddr += cur_copy_size;
        amt_copied += cur_copy_size;
        from_page_index++;
    }
    up_read(&bufmap_init_sem);
    return 0;
}

/* pvfs_bufmap_copy_from_user()
 *
 * copies data from a user space address to a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_from_user(int buffer_index, void __user *from, size_t size)
{
    size_t ret = 0, amt_copied = 0, amt_remaining = 0, cur_copy_size = 0;
    void __user *from_kaddr = from;
    void *to_kaddr = NULL;
    int to_page_index = 0;
    struct pvfs_bufmap_desc *to = &desc_array[buffer_index];
    char* tmp_printer = NULL;
    int tmp_int = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_from_user: from %p, index %d, "
                "size %zd\n", from, buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_copy_from_user: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    while(amt_copied < size)
    {
        amt_remaining = (size - amt_copied);
        cur_copy_size =
            ((amt_remaining > PAGE_SIZE) ? PAGE_SIZE : amt_remaining);

        to_kaddr = pvfs2_kmap(to->page_array[to_page_index]);
        ret = copy_from_user(to_kaddr, from_kaddr, cur_copy_size);
        if (!tmp_printer)
        {
            tmp_printer = (char*)(to_kaddr);
            tmp_int += tmp_printer[0];
            gossip_debug(GOSSIP_BUFMAP_DEBUG, "First character (integer value) in pvfs_bufmap_copy_from_user: %d\n", tmp_int);
        }

        pvfs2_kunmap(to->page_array[to_page_index]);

        if (ret)
        {
            gossip_debug(GOSSIP_BUFMAP_DEBUG, "Failed to copy data from user space\n");
            up_read(&bufmap_init_sem);
            return -EFAULT;
        }

        from_kaddr += cur_copy_size;
        amt_copied += cur_copy_size;
        to_page_index++;
    }
    up_read(&bufmap_init_sem);
    return 0;
}

/*
 * pvfs_bufmap_copy_to_pages() 
 *
 * Copies data from the mapped buffer to the specified set of
 * kernel pages (typically page-cache pages) for a specified size and 
 * number of pages.
 * NOTE: iovec is expected to store pointers to struct page
 *
 * Returns 0 on success, -errno on failure.
 */
int pvfs_bufmap_copy_to_pages(int buffer_index,
                              const struct iovec *vec, 
                              unsigned long nr_segs,
                              size_t size)
{
    size_t amt_copied = 0, amt_remaining = 0, cur_copy_size = 0;
    int from_page_index = 0;
    void *from_kaddr = NULL, *to_kaddr = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];
    struct page *page;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_pages: nr_pages %lu,"
                 "index %d, size %zd\n", nr_segs, buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_copy_to_pages: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    while (amt_copied < size)
    {
        if (from_page_index >= nr_segs) 
        {
            gossip_err("pvfs_bufmap_copy_to_pages: count cannot exceed "
                       "number of pages(%lu)\n", nr_segs);
            up_read(&bufmap_init_sem);
            return -EIO;
        }
        page = (struct page *) vec[from_page_index].iov_base;
        if (page == NULL) {
            gossip_err("pvfs_bufmap_copy_to_pages: invalid page pointer %d\n",
                        from_page_index);
            up_read(&bufmap_init_sem);
            return -EIO;
        }
        amt_remaining = (size - amt_copied);
        cur_copy_size = ((amt_remaining > PAGE_SIZE) ? 
                          PAGE_SIZE : amt_remaining);
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_pages:"
                                          "from_page: %p, to_page: %p\n",
                                          from->page_array[from_page_index], page);

        from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
        to_kaddr = pvfs2_kmap(page);
#if 0
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_pages -> "
                     "to_kaddr: %p, from_kaddr:%p, cur_copy_size: %d\n",
                     to_kaddr, from_kaddr, cur_copy_size);
#endif
        memcpy(to_kaddr, from_kaddr, cur_copy_size);
        /* zero out remaining page */
        if (cur_copy_size < PAGE_SIZE) {
            memset(to_kaddr + cur_copy_size, 0, PAGE_SIZE - cur_copy_size);
        }
        pvfs2_kunmap(page);
        pvfs2_kunmap(from->page_array[from_page_index]);

        amt_copied += cur_copy_size;
        from_page_index++;
    }
    up_read(&bufmap_init_sem);
    return 0;
}

/*
 * pvfs_bufmap_copy_from_pages() 
 *
 * Copies data to the mapped buffer from the specified set of target 
 * pages (typically the kernel's page-cache)
 * for a given size and number of pages.
 * NOTE: iovec is expected to store pointers to struct page.
 *
 * Returns 0 on success and -errno on failure.
 */
int pvfs_bufmap_copy_from_pages(int buffer_index, 
                                const struct iovec *vec,
                                unsigned long nr_segs,
                                size_t size)
{
    size_t amt_copied = 0, amt_remaining = 0, cur_copy_size = 0;
    int to_page_index = 0;
    void *from_kaddr = NULL, *to_kaddr = NULL;
    struct pvfs_bufmap_desc *to = &desc_array[buffer_index];
    struct page *page;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_from_pages: nr_pages %lu "
            "index %d, size %zd\n", nr_segs, buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs_bufmap_copy_from_pages: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }

    while (amt_copied < size)
    {
        if (to_page_index >= nr_segs) {
            gossip_err("pvfs_bufmap_copy_from_pages: count cannot exceed number of"
                       "pages(%lu)\n", nr_segs);
            up_read(&bufmap_init_sem);
            return -EIO;
        }
        page = (struct page *) vec[to_page_index].iov_base;
        if (page == NULL) {
            gossip_err("pvfs_bufmap_copy_from_pages: invalid page pointer\n");
            up_read(&bufmap_init_sem);
            return -EIO;
        }
        amt_remaining = (size - amt_copied);
        cur_copy_size = ((amt_remaining > PAGE_SIZE) ?
                          PAGE_SIZE : amt_remaining);
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_from_pages:"
                "from_page: %p, to_page: %p\n", page, to->page_array[to_page_index]);
        to_kaddr = pvfs2_kmap(to->page_array[to_page_index]);
        from_kaddr = pvfs2_kmap(page);
#if 0
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_from_pages -> "
                     "to_kaddr: %p, from_kaddr:%p, cur_copy_size: %d\n",
                     to_kaddr, from_kaddr, cur_copy_size);
#endif
        memcpy(to_kaddr, from_kaddr, cur_copy_size);
        pvfs2_kunmap(page);
        pvfs2_kunmap(to->page_array[to_page_index]);
        amt_copied += cur_copy_size;
        to_page_index++;
    }
    up_read(&bufmap_init_sem);
    return 0;
}


/* pvfs_bufmap_copy_iovec_from_user()
 *
 * copies data from several user space address's in an iovec
 * to a mapped buffer
 *
 * Note that the mapped buffer is a series of pages and therefore
 * the copies have to be split by PAGE_SIZE bytes at a time.
 * Note that this routine checks that summation of iov_len
 * across all the elements of iov is equal to size.
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_iovec_from_user(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size)
{
    size_t ret = 0, amt_copied = 0, cur_copy_size = 0;
    unsigned int to_page_offset = 0, to_page_index = 0;
    void *to_kaddr = NULL;
    void __user *from_addr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *to = &desc_array[buffer_index];
    unsigned int seg;
    char* tmp_printer = NULL;
    int tmp_int = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_iovec_from_user: index %d, "
                "size %zd\n", buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_iovec_from_user: not yet "
                    "initialized; returning\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = kmalloc(nr_segs * sizeof(*copied_iovec), 
                           PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        gossip_err("pvfs2_bufmap_copy_iovec_from_user: failed allocating memory\n");
        up_read(&bufmap_init_sem);
        return -ENOMEM;
    }
    memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
    /*
     * Go through each segment in the iovec and make sure that
     * the summation of iov_len matches the given size.
     */
    for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
    {
        amt_copied += copied_iovec[seg].iov_len;
    }
    if (amt_copied != size)
    {
        gossip_err("pvfs2_bufmap_copy_iovec_from_user: computed total (%zd) is not equal to (%zd)\n",
                amt_copied, size);
        kfree(copied_iovec);
        up_read(&bufmap_init_sem);
        return -EINVAL;
    }

    to_page_index = 0;
    to_page_offset = 0;
    amt_copied = 0;
    seg = 0;
    /* Go through each segment in the iovec and copy its
     * buffer into the mapped buffer one page at a time though
     */
    while (amt_copied < size)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_to_page_index;

        if (iv->iov_len < (PAGE_SIZE - to_page_offset)) 
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            from_addr = iv->iov_base;
            inc_to_page_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - to_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            from_addr = iv->iov_base;
            inc_to_page_index = 1;
        }
        else 
        {
            cur_copy_size = PVFS_util_min(PAGE_SIZE - to_page_offset, size - amt_copied);
            from_addr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len -= cur_copy_size;
            inc_to_page_index = 1;
        }
        to_kaddr = pvfs2_kmap(to->page_array[to_page_index]);
        ret = copy_from_user(to_kaddr + to_page_offset, from_addr, cur_copy_size);
        if (!tmp_printer)
        {
            tmp_printer = (char*)(to_kaddr + to_page_offset);
            tmp_int += tmp_printer[0];
            gossip_debug(GOSSIP_BUFMAP_DEBUG, "First character (integer value) in pvfs_bufmap_copy_from_user: %d\n", tmp_int);
        }


        pvfs2_kunmap(to->page_array[to_page_index]);
#if 0
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_copy_iovec_from_user: copying from user %p to kernel %p %zd bytes (to_kddr: %p,page_offset: %d)\n",
                from_addr, to_kaddr + to_page_offset, cur_copy_size, to_kaddr, to_page_offset); 
#endif
        if (ret)
        {
            gossip_err("Failed to copy data from user space\n");
            kfree(copied_iovec);
            up_read(&bufmap_init_sem);
            return -EFAULT;
        }

        amt_copied += cur_copy_size;
        if (inc_to_page_index) 
        {
            to_page_offset = 0;
            to_page_index++;
        }
        else 
        {
            to_page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    up_read(&bufmap_init_sem);
    return 0;
}

/* pvfs_bufmap_copy_iovec_from_kernel()
 *
 * copies data from several kernel space address's in an iovec
 * to a mapped buffer
 *
 * Note that the mapped buffer is a series of pages and therefore
 * the copies have to be split by PAGE_SIZE bytes at a time.
 * Note that this routine checks that summation of iov_len
 * across all the elements of iov is equal to size.
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_iovec_from_kernel(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size)
{
    size_t amt_copied = 0, cur_copy_size = 0;
    int to_page_index = 0;
    void *to_kaddr = NULL;
    void *from_kaddr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *to = &desc_array[buffer_index];
    unsigned int seg, to_page_offset = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_iovec_from_kernel: index %d, "
                "size %zd\n", buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_iovec_from_kernel: not yet "
                    "initialized; returning\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = kmalloc(nr_segs * sizeof(*copied_iovec),
                           PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        gossip_err("pvfs2_bufmap_copy_iovec_from_kernel: failed allocating memory\n");
        up_read(&bufmap_init_sem);
        return -ENOMEM;
    }
    memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
    /*
     * Go through each segment in the iovec and make sure that
     * the summation of iov_len matches the given size.
     */
    for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
    {
        amt_copied += copied_iovec[seg].iov_len;
    }
    if (amt_copied != size)
    {
        gossip_err("pvfs2_bufmap_copy_iovec_from_kernel: computed total (%zd) is not equal to (%zd)\n",
                amt_copied, size);
        kfree(copied_iovec);
        up_read(&bufmap_init_sem);
        return -EINVAL;
    }

    to_page_index = 0;
    amt_copied = 0;
    seg = 0;
    to_page_offset = 0;
    /* Go through each segment in the iovec and copy its
     * buffer into the mapped buffer one page at a time though
     */
    while (amt_copied < size)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_to_page_index;

        if (iv->iov_len < (PAGE_SIZE - to_page_offset)) 
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            from_kaddr = iv->iov_base;
            inc_to_page_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - to_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            from_kaddr = iv->iov_base;
            inc_to_page_index = 1;
        }
        else 
        {
            cur_copy_size = PVFS_util_min(PAGE_SIZE - to_page_offset, size - amt_copied);
            from_kaddr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len -= cur_copy_size;
            inc_to_page_index = 1;
        }
        to_kaddr = pvfs2_kmap(to->page_array[to_page_index]);
        memcpy(to_kaddr + to_page_offset, from_kaddr, cur_copy_size);
        pvfs2_kunmap(to->page_array[to_page_index]);
#if 0
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_copy_iovec_from_kernel: copying from kernel %p to kernel %p %zd bytes (to_kddr: %p,page_offset: %d)\n",
                from_kaddr, to_kaddr + page_offset, cur_copy_size, to_kaddr, page_offset); 
#endif
        amt_copied += cur_copy_size;
        if (inc_to_page_index) 
        {
            to_page_offset = 0;
            to_page_index++;
        }
        else 
        {
            to_page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    up_read(&bufmap_init_sem);
    return 0;
}

/* pvfs_bufmap_copy_to_user_iovec()
 *
 * copies data to several user space address's in an iovec
 * from a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user_iovec(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size)
{
    size_t ret = 0, amt_copied = 0;
    size_t cur_copy_size = 0;
    int from_page_index = 0;
    void *from_kaddr = NULL;
    void __user *to_addr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];
    unsigned int seg, from_page_offset = 0;
    char* tmp_printer = NULL;
    int tmp_int = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_user_iovec: index %d, "
                "size %zd\n", buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_copy_to_user_iovec: not yet "
                    "initialized; returning\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = kmalloc(nr_segs * sizeof(*copied_iovec),
                           PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        gossip_err("pvfs2_bufmap_copy_to_user_iovec: failed allocating memory\n");
        up_read(&bufmap_init_sem);
        return -ENOMEM;
    }
    memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
    /*
     * Go through each segment in the iovec and make sure that
     * the summation of iov_len is greater than the given size.
     */
    for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
    {
        amt_copied += copied_iovec[seg].iov_len;
    }
    if (amt_copied < size)
    {
        gossip_err("pvfs2_bufmap_copy_to_user_iovec: computed total (%zd) is less than (%zd)\n",
                amt_copied, size);
        kfree(copied_iovec);
        up_read(&bufmap_init_sem);
        return -EINVAL;
    }

    from_page_index = 0;
    amt_copied = 0;
    seg = 0;
    from_page_offset = 0;
    /* 
     * Go through each segment in the iovec and copy from the mapper buffer,
     * but make sure that we do so one page at a time.
     */
    while (amt_copied < size)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_from_page_index;

        if (iv->iov_len < (PAGE_SIZE - from_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            to_addr = iv->iov_base;
            inc_from_page_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - from_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            to_addr = iv->iov_base;
            inc_from_page_index = 1;
        }
        else 
        {
            cur_copy_size = PVFS_util_min(PAGE_SIZE - from_page_offset, size - amt_copied);
            to_addr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len  -= cur_copy_size;
            inc_from_page_index = 1;
        }
        from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
        if (!tmp_printer)
        {
            tmp_printer = (char*)(from_kaddr + from_page_offset);
            tmp_int += tmp_printer[0];
            gossip_debug(GOSSIP_BUFMAP_DEBUG, "First character (integer value) in pvfs_bufmap_copy_to_user_iovec: %d\n", tmp_int);
        }
        ret = copy_to_user(to_addr, from_kaddr + from_page_offset, cur_copy_size);
        pvfs2_kunmap(from->page_array[from_page_index]);
#if 0
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_copy_to_user_iovec: copying to user %p from kernel %p %d bytes (from_kaddr:%p, page_offset:%d)\n",
                to_addr, from_kaddr + from_page_offset, cur_copy_size, from_kaddr, from_page_offset); 
#endif
        if (ret)
        {
            gossip_err("Failed to copy data to user space\n");
            kfree(copied_iovec);
            up_read(&bufmap_init_sem);
            return -EFAULT;
        }

        amt_copied += cur_copy_size;
        if (inc_from_page_index) 
        {
            from_page_offset = 0;
            from_page_index++;
        }
        else 
        {
            from_page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    up_read(&bufmap_init_sem);
    return 0;
}

/* pvfs_bufmap_copy_to_kernel_iovec()
 *
 * copies data to several kernel space address's in an iovec
 * from a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_kernel_iovec(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size)
{
    size_t amt_copied = 0;
    size_t cur_copy_size = 0;
    int from_page_index = 0;
    void *from_kaddr = NULL;
    void *to_kaddr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];
    unsigned int seg, from_page_offset = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_kernel_iovec: index %d, "
                "size %zd\n", buffer_index, size);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_copy_to_kernel_iovec: not yet "
                    "initialized; returning\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = kmalloc(nr_segs * sizeof(*copied_iovec),
                           PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        gossip_err("pvfs2_bufmap_copy_to_kernel_iovec: failed allocating memory\n");
        up_read(&bufmap_init_sem);
        return -ENOMEM;
    }
    memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
    /*
     * Go through each segment in the iovec and make sure that
     * the summation of iov_len is greater than the given size.
     */
    for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
    {
        amt_copied += copied_iovec[seg].iov_len;
    }
    if (amt_copied < size)
    {
        gossip_err("pvfs2_bufmap_copy_to_kernel_iovec: computed total (%zd) is less than (%zd)\n",
                amt_copied, size);
        kfree(copied_iovec);
        up_read(&bufmap_init_sem);
        return -EINVAL;
    }

    from_page_index = 0;
    amt_copied = 0;
    seg = 0;
    from_page_offset = 0;
    /* 
     * Go through each segment in the iovec and copy from the mapper buffer,
     * but make sure that we do so one page at a time.
     */
    while (amt_copied < size)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_from_page_index;

        if (iv->iov_len < (PAGE_SIZE - from_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            to_kaddr = iv->iov_base;
            inc_from_page_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - from_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size - amt_copied);
            seg++;
            to_kaddr = iv->iov_base;
            inc_from_page_index = 1;
        }
        else 
        {
            cur_copy_size = PVFS_util_min(PAGE_SIZE - from_page_offset, size - amt_copied);
            to_kaddr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len  -= cur_copy_size;
            inc_from_page_index = 1;
        }
        from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
        memcpy(to_kaddr, from_kaddr + from_page_offset, cur_copy_size);
        pvfs2_kunmap(from->page_array[from_page_index]);
#if 0
        gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_kernel_iovec: copying to kernel %p from kernel %p %d bytes (from_kaddr:%p, page_offset:%d)\n",
                to_kaddr, from_kaddr + page_offset, cur_copy_size, from_kaddr, page_offset); 
#endif
        amt_copied += cur_copy_size;
        if (inc_from_page_index) 
        {
            from_page_offset = 0;
            from_page_index++;
        }
        else 
        {
            from_page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    up_read(&bufmap_init_sem);
    return 0;
}

#ifdef HAVE_AIO_VFS_SUPPORT

/* pvfs_bufmap_copy_to_user_task_iovec()
 *
 * copies data out of a mapped buffer to a vector of user space address
 * of a given task specified by the task structure argument (tsk)
 * This is used by the client-daemon for completing an aio
 * operation that was issued by an arbitrary user program.
 * Unfortunately, we cannot use a copy_to_user
 * in that case and need to map in the user pages before
 * attempting the copy!
 *
 * NOTE: There is no need for an analogous copy from user task since
 * the data buffers get copied in the context of the process initiating
 * the write system call!
 *
 * Returns number of bytes copied on success, -errno on failure.
 */
size_t pvfs_bufmap_copy_to_user_task_iovec(
        struct task_struct *tsk,
        struct iovec *iovec, unsigned long nr_segs,
        int buffer_index,
        size_t size_to_be_copied)
{
    size_t ret = 0, amt_copied = 0, cur_copy_size = 0;
    int from_page_index = 0;
    void *from_kaddr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];

    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    struct page *page = NULL;
    unsigned long to_addr = 0;
    void *maddr = NULL;
    unsigned int to_offset = 0;
    unsigned int seg, from_page_offset = 0;

    gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs_bufmap_copy_to_user_task_iovec: "
            " PID: %d, iovec %p, from %p, index %d, "
            " size %zd\n", tsk->pid, iovec, from, buffer_index, size_to_be_copied);

    down_read(&bufmap_init_sem);
    if (bufmap_init == 0)
    {
        gossip_err("pvfs2_bufmap_copy_to_user: not yet "
                    "initialized.\n");
        gossip_err("pvfs2: please confirm that pvfs2-client "
                "daemon is running.\n");
        up_read(&bufmap_init_sem);
        return -EIO;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = kmalloc(nr_segs * sizeof(*copied_iovec),
                           PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        gossip_err("pvfs_bufmap_copy_to_user_iovec: failed allocating memory\n");
        up_read(&bufmap_init_sem);
        return -ENOMEM;
    }
    memcpy(copied_iovec, iovec, nr_segs * sizeof(*copied_iovec));
    /*
     * Go through each segment in the iovec and make sure that
     * the summation of iov_len is greater than the given size.
     */
    for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
    {
        amt_copied += copied_iovec[seg].iov_len;
    }
    if (amt_copied < size_to_be_copied)
    {
        gossip_err("pvfs_bufmap_copy_to_user_task_iovec: computed total (%zd) "
                "is less than (%zd)\n", amt_copied, size_to_be_copied);
        kfree(copied_iovec);
        up_read(&bufmap_init_sem);
        return -EINVAL;
    }
    mm = get_task_mm(tsk);
    if (!mm) 
    {
        kfree(copied_iovec);
        up_read(&bufmap_init_sem);
        return -EIO;
    }
    from_page_index = 0;
    amt_copied = 0;
    seg = 0;
    from_page_offset = 0;
    /* 
     * Go through each of the page in the specified process
     * address space and copy from the mapped
     * buffer, and make sure to do this one page at a time!
     */
    down_read(&mm->mmap_sem);
    while (amt_copied < size_to_be_copied)
    {
        int inc_from_page_index = 0;
	struct iovec *iv = &copied_iovec[seg];

        if (iv->iov_len < (PAGE_SIZE - from_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size_to_be_copied - amt_copied);
            seg++;
            to_addr = (unsigned long) iv->iov_base;
            inc_from_page_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - from_page_offset))
        {
            cur_copy_size = PVFS_util_min(iv->iov_len, size_to_be_copied - amt_copied);
            seg++;
            to_addr = (unsigned long) iv->iov_base;
            inc_from_page_index = 1;
        }
        else 
        {
            cur_copy_size = PVFS_util_min(PAGE_SIZE - from_page_offset, size_to_be_copied - amt_copied);
            to_addr = (unsigned long) iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len  -= cur_copy_size;
            inc_from_page_index = 1;
        }
        ret = get_user_pages(tsk, mm, to_addr, 
                1,/* count */
                1,/* write */
                1,/* force */
                &page, &vma);
        if (ret <= 0)
            break;
        to_offset = to_addr & (PAGE_SIZE - 1);
        maddr = pvfs2_kmap(page);
        from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
        copy_to_user_page(vma, page, to_addr,
             maddr + to_offset /* dst */, 
             from_kaddr + from_page_offset, /* src */
             cur_copy_size /* len */);
        set_page_dirty_lock(page);
        pvfs2_kunmap(from->page_array[from_page_index]);
        pvfs2_kunmap(page);
        page_cache_release(page);

        amt_copied += cur_copy_size;
        if (inc_from_page_index)
        {
            from_page_offset = 0;
            from_page_index++;
        }
        else 
        {
            from_page_offset += cur_copy_size;
        }
    }
    up_read(&mm->mmap_sem);
    mmput(mm);
    up_read(&bufmap_init_sem);
    kfree(copied_iovec);
    return (amt_copied < size_to_be_copied) ? -EFAULT: amt_copied;
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pint-dev-shared.h"

#define BUFMAP_PAGE_COUNT (PVFS2_BUFMAP_TOTAL_SIZE/PAGE_SIZE)
#define PAGES_PER_DESC (PVFS2_BUFMAP_DEFAULT_DESC_SIZE/PAGE_SIZE)

extern int debug;
extern int op_timeout_secs;
static int bufmap_init = 0;

static struct page **bufmap_page_array = NULL;

/* array to track usage of buffer descriptors */
int buffer_index_array[PVFS2_BUFMAP_DESC_COUNT] = {0};
static spinlock_t buffer_index_lock = SPIN_LOCK_UNLOCKED;

static struct pvfs_bufmap_desc desc_array[PVFS2_BUFMAP_DESC_COUNT];
static DECLARE_WAIT_QUEUE_HEAD(bufmap_waitq);

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

    pvfs2_print("pvfs2_bufmap_initialize: called\n");

    if (bufmap_init == 1)
    {
        pvfs2_error("pvfs2: error: bufmap already initialized.\n");
        ret = -EALREADY;
        goto init_failure;
    }

    /* sanity check alignment and size of buffer that caller wants to
     * work with
     */
    if (PAGE_ALIGN((unsigned long)user_desc->ptr) != 
        (unsigned long)user_desc->ptr)
    {
        pvfs2_error("pvfs2 error: memory alignment (front).\n");
        goto init_failure;
    }

    if (PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->size)) != 
        (unsigned long)(user_desc->ptr + user_desc->size))
    {
        pvfs2_error("pvfs2 error: memory alignment (back).\n");
        goto init_failure;
    }

    if (user_desc->size != PVFS2_BUFMAP_TOTAL_SIZE)
    {
        pvfs2_error("pvfs2 error: user provided an oddly "
                    "sized buffer...\n");
        goto init_failure;
    }

    if ((PVFS2_BUFMAP_DEFAULT_DESC_SIZE % PAGE_SIZE) != 0)
    {
        pvfs2_error("pvfs2 error: bufmap size not page size "
                    "divisible.\n");
        goto init_failure;
    }

    /* allocate storage to track our page mappings */
    bufmap_page_array = (struct page **)kmalloc(
        BUFMAP_PAGE_COUNT*sizeof(struct page *), PVFS2_BUFMAP_GFP_FLAGS);
    if (!bufmap_page_array)
    {
        ret = -ENOMEM;
        goto init_failure;
    }

    /* map the pages */
    down_read(&current->mm->mmap_sem);

    ret = get_user_pages(
        current, current->mm, (unsigned long)user_desc->ptr,
        BUFMAP_PAGE_COUNT, 1, 0, bufmap_page_array, NULL);

    up_read(&current->mm->mmap_sem);

    if (ret < 0)
    {
        kfree(bufmap_page_array);
        goto init_failure;
    }

    /*
      in theory we could run with what we got, but I will just treat
      it as an error for simplicity's sake right now
    */
    if (ret != BUFMAP_PAGE_COUNT)
    {
        pvfs2_error("pvfs2 error: asked for %d pages, only got %d.\n",
                    (int)BUFMAP_PAGE_COUNT, ret);

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
    for(i = 0; i < BUFMAP_PAGE_COUNT; i++)
    {
        flush_dcache_page(bufmap_page_array[i]);
        pvfs2_set_page_reserved(bufmap_page_array[i]);
    }

    /* build a list of available descriptors */
    for(offset = 0, i = 0; i < PVFS2_BUFMAP_DESC_COUNT; i++)
    {
        desc_array[i].page_array = &bufmap_page_array[offset];
        desc_array[i].array_count = PAGES_PER_DESC;
        desc_array[i].uaddr =
            (user_desc->ptr + (i * PAGES_PER_DESC * PAGE_SIZE));
        offset += PAGES_PER_DESC;
    }

    /* clear any previously used buffer indices */
    spin_lock(&buffer_index_lock);
    for(i = 0; i < PVFS2_BUFMAP_DESC_COUNT; i++)
    {
        buffer_index_array[i] = 0;
    }
    spin_unlock(&buffer_index_lock);

    bufmap_init = 1;

    pvfs2_print("pvfs2_bufmap_initialize: exiting normally\n");
    return 0;

  init_failure:

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

    pvfs2_print("pvfs2_bufmap_finalize: called\n");

    if (bufmap_init == 0)
    {
        pvfs2_print("pvfs2_bufmap_finalize: not yet "
                    "initialized; returning\n");
        return;
    }

    for(i = 0; i < BUFMAP_PAGE_COUNT; i++)
    {
        pvfs2_clear_page_reserved(bufmap_page_array[i]);
        page_cache_release(bufmap_page_array[i]);
    }
    kfree(bufmap_page_array);

    bufmap_init = 0;

    pvfs2_print("pvfs2_bufmap_finalize: exiting normally\n");
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
    int ret = -1, i = 0;
    DECLARE_WAITQUEUE(my_wait, current);

    add_wait_queue_exclusive(&bufmap_waitq, &my_wait);

    while(1)
    {
        set_current_state(TASK_INTERRUPTIBLE);

        /* check for available desc */
        spin_lock(&buffer_index_lock);
        for(i = 0; i < PVFS2_BUFMAP_DESC_COUNT; i++)
        {
            if (buffer_index_array[i] == 0)
            {
                buffer_index_array[i] = 1;
                *buffer_index = i;
                ret = 0;
                break;
            }
        }
        spin_unlock(&buffer_index_lock);

        /* if we acquired a buffer, then break out of while */
        if (ret == 0)
        {
            break;
        }

        if (!signal_pending(current))
        {
            int timeout = MSECS_TO_JIFFIES(
                1000 * op_timeout_secs);
            if (!schedule_timeout(timeout))
            {
                pvfs2_print("*** pvfs_bufmap_get timed out\n");
                break;
            }
            continue;
        }

        pvfs2_print("pvfs2: bufmap_get() interrupted.\n");
        ret = -EINTR;
        break;
    }

    set_current_state(TASK_RUNNING);
    remove_wait_queue(&bufmap_waitq, &my_wait);

    return ret;
}

/* pvfs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void pvfs_bufmap_put(int buffer_index)
{
    /* put the desc back on the queue */
    spin_lock(&buffer_index_lock);
    buffer_index_array[buffer_index] = 0;
    spin_unlock(&buffer_index_lock);

    /* wake up anyone who may be sleeping on the queue */
    wake_up_interruptible(&bufmap_waitq);
}

/* pvfs_bufmap_copy_to_user()
 *
 * copies data out of a mapped buffer to a user space address
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user(void __user *to, int buffer_index, int size)
{
    int ret = 0, amt_copied = 0, amt_remaining = 0;
    int cur_copy_size = 0, index = 0;
    void __user *offset = to;
    void *from_kaddr = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];

    pvfs2_print("pvfs_bufmap_copy_to_user: to %p, from %p, index %d, "
                "size %d\n", to, from, buffer_index, size);

    if (bufmap_init == 0)
    {
        pvfs2_error("pvfs2_bufmap_copy_to_user: not yet "
                    "initialized.\n");
        pvfs2_error("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        return -EIO;
    }

    while(amt_copied < size)
    {
        amt_remaining = (size - amt_copied);
        cur_copy_size =
            ((amt_remaining > PAGE_SIZE) ? PAGE_SIZE : amt_remaining);

        from_kaddr = pvfs2_kmap(from->page_array[index]);
        ret = copy_to_user(offset, from_kaddr, cur_copy_size);
        pvfs2_kunmap(from->page_array[index]);

        if (ret)
        {
            pvfs2_print("Failed to copy data to user space %d\n", ret);
            return -EFAULT;
        }

        offset += cur_copy_size;
        amt_copied += cur_copy_size;
        index++;
    }
    return 0;
}

int pvfs_bufmap_copy_to_kernel(
    void *to, int buffer_index, int size)
{
    int amt_copied = 0, amt_remaining = 0;
    int cur_copy_size = 0, index = 0;
    void *offset = to, *from_kaddr = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];

    pvfs2_print("pvfs_bufmap_copy_to_kernel: to %p, index %d, size %d\n",
                to, buffer_index, size);

    if (bufmap_init == 0)
    {
        pvfs2_error("pvfs2_bufmap_copy_to_kernel: not yet "
                    "initialized.\n");
        pvfs2_error("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        return -EIO;
    }

    while(amt_copied < size)
    {
        amt_remaining = (size - amt_copied);
        cur_copy_size =
            ((amt_remaining > PAGE_SIZE) ? PAGE_SIZE : amt_remaining);

        from_kaddr = pvfs2_kmap(from->page_array[index]);
        memcpy(offset, from_kaddr, cur_copy_size);
        pvfs2_kunmap(from->page_array[index]);

        offset += cur_copy_size;
        amt_copied += cur_copy_size;
        index++;
    }
    return 0;
}

/* pvfs_bufmap_copy_from_user()
 *
 * copies data from a user space address to a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_from_user(
    int buffer_index, void __user *from, int size)
{
    int ret = 0, amt_copied = 0, amt_remaining = 0;
    int cur_copy_size = 0, index = 0;
    void __user *offset = from;
    void *to_kaddr = NULL;
    struct pvfs_bufmap_desc *to = &desc_array[buffer_index];

    pvfs2_print("pvfs_bufmap_copy_from_user: from %p, index %d, "
                "size %d\n", from, buffer_index, size);

    if (bufmap_init == 0)
    {
        pvfs2_error("pvfs2_bufmap_copy_from_user: not yet "
                    "initialized.\n");
        pvfs2_error("pvfs2: please confirm that pvfs2-client daemon is running.\n");
        return -EIO;
    }

    while(amt_copied < size)
    {
        amt_remaining = (size - amt_copied);
        cur_copy_size =
            ((amt_remaining > PAGE_SIZE) ? PAGE_SIZE : amt_remaining);

        to_kaddr = pvfs2_kmap(to->page_array[index]);
        ret = copy_from_user(to_kaddr, offset, cur_copy_size);
        pvfs2_kunmap(to->page_array[index]);

        if (ret)
        {
            pvfs2_print("Failed to copy data from user space\n");
            return -EFAULT;
        }

        offset += cur_copy_size;
        amt_copied += cur_copy_size;
        index++;
    }
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
    int size)
{
    int ret = 0, amt_copied = 0; 
    int cur_copy_size = 0, index = 0;
    void *to_kaddr = NULL;
    void __user *from_addr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *to = &desc_array[buffer_index];
    unsigned int seg, page_offset = 0;

    pvfs2_print("pvfs_bufmap_copy_iovec_from_user: index %d, "
                "size %d\n", buffer_index, size);

    if (bufmap_init == 0)
    {
        pvfs2_print("pvfs2_bufmap_copy_iovec_from_user: not yet "
                    "initialized; returning\n");
        return 1;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = (struct iovec *) kmalloc(nr_segs * sizeof(struct iovec),
            PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        pvfs2_error("pvfs2_bufmap_copy_iovec_from_user: failed allocating memory\n");
        return -ENOMEM;
    }
    memcpy(copied_iovec, iov, nr_segs * sizeof(struct iovec));
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
        pvfs2_error("pvfs2_bufmap_copy_iovec_from_user: computed total (%d) is not equal to (%d)\n",
                amt_copied, size);
        kfree(copied_iovec);
        return -EINVAL;
    }

    index = 0;
    amt_copied = 0;
    seg = 0;
    page_offset = 0;
    /* Go through each segment in the iovec and copy its
     * buffer into the mapped buffer one page at a time though
     */
    while (amt_copied < size)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_index = 0;

        if (iv->iov_len < (PAGE_SIZE - page_offset)) 
        {
            cur_copy_size = iv->iov_len;
            seg++;
            from_addr = iv->iov_base;
            inc_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - page_offset))
        {
            cur_copy_size = iv->iov_len;
            seg++;
            from_addr = iv->iov_base;
            inc_index = 1;
        }
        else 
        {
            cur_copy_size = (PAGE_SIZE - page_offset);
            from_addr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len -= cur_copy_size;
            inc_index = 1;
        }
        to_kaddr = pvfs2_kmap(to->page_array[index]);
        ret = copy_from_user(to_kaddr + page_offset, from_addr, cur_copy_size);
        pvfs2_kunmap(to->page_array[index]);
#if 0
        pvfs2_print("pvfs2_bufmap_copy_iovec_from_user: copying from user %p to kernel %p %d bytes (to_kddr: %p,page_offset: %d)\n",
                from_addr, to_kaddr + page_offset, cur_copy_size, to_kaddr, page_offset); 
#endif
        if (ret)
        {
            pvfs2_error("Failed to copy data from user space\n");
            kfree(copied_iovec);
            return -EFAULT;
        }

        amt_copied += cur_copy_size;
        if (inc_index) {
            page_offset = 0;
            index++;
        }
        else {
            page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    if (amt_copied != size)
    {
	pvfs2_error("Failed to copy all the data from user space [%d instead of %d]\n",
                amt_copied, size);
	return -EIO;
    }
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
    int size)
{
    int ret = 0, amt_copied = 0;
    int cur_copy_size = 0, index = 0;
    void *from_kaddr = NULL;
    void __user *to_addr = NULL;
    struct iovec *copied_iovec = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];
    unsigned int seg, page_offset = 0;

    pvfs2_print("pvfs_bufmap_copy_to_user_iovec: index %d, "
                "size %d\n", buffer_index, size);

    if (bufmap_init == 0)
    {
        pvfs2_print("pvfs2_bufmap_copy_to_user_iovec: not yet "
                    "initialized; returning\n");
        return 1;
    }
    /*
     * copy the passed in iovec so that we can change some of its fields
     */
    copied_iovec = (struct iovec *) kmalloc(nr_segs * sizeof(struct iovec),
            PVFS2_BUFMAP_GFP_FLAGS);
    if (copied_iovec == NULL)
    {
        pvfs2_error("pvfs2_bufmap_copy_to_user_iovec: failed allocating memory\n");
        return -ENOMEM;
    }
    memcpy(copied_iovec, iov, nr_segs * sizeof(struct iovec));
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
        pvfs2_error("pvfs2_bufmap_copy_to_user_iovec: computed total (%d) is less than (%d)\n",
                amt_copied, size);
        kfree(copied_iovec);
        return -EINVAL;
    }

    index = 0;
    amt_copied = 0;
    seg = 0;
    page_offset = 0;
    /* 
     * Go through each segment in the iovec and copy from the mapper buffer,
     * but make sure that we do so one page at a time.
     */
    while (amt_copied < size)
    {
	struct iovec *iv = &copied_iovec[seg];
        int inc_index = 0;

        if (iv->iov_len < (PAGE_SIZE - page_offset))
        {
            cur_copy_size = iv->iov_len;
            seg++;
            to_addr = iv->iov_base;
            inc_index = 0;
        }
        else if (iv->iov_len == (PAGE_SIZE - page_offset))
        {
            cur_copy_size = iv->iov_len;
            seg++;
            to_addr = iv->iov_base;
            inc_index = 1;
        }
        else 
        {
            cur_copy_size = (PAGE_SIZE - page_offset);
            to_addr = iv->iov_base;
            iv->iov_base += cur_copy_size;
            iv->iov_len  -= cur_copy_size;
            inc_index = 1;
        }
        from_kaddr = pvfs2_kmap(from->page_array[index]);
        ret = copy_to_user(to_addr, from_kaddr + page_offset, cur_copy_size);
        pvfs2_kunmap(from->page_array[index]);
#if 0
        pvfs2_print("pvfs2_bufmap_copy_to_user_iovec: copying to user %p from kernel %p %d bytes (from_kaddr:%p, page_offset:%d)\n",
                to_addr, from_kaddr + page_offset, cur_copy_size, from_kaddr, page_offset); 
#endif
        if (ret)
        {
            pvfs2_error("Failed to copy data to user space\n");
            kfree(copied_iovec);
            return -EFAULT;
        }

        amt_copied += cur_copy_size;
        if (inc_index) {
            page_offset = 0;
            index++;
        }
        else {
            page_offset += cur_copy_size;
        }
    }
    kfree(copied_iovec);
    return 0;
}

#ifdef HAVE_AIO_VFS_SUPPORT

/* pvfs_bufmap_copy_to_user_task()
 *
 * copies data out of a mapped buffer to a user space address
 * of a given task specified by the task structure argument (tsk)
 * This is used by the client-daemon for completing an aio
 * operation that was issued by an arbitrary user program.
 * Unfortunately, we cannot use a copy_to_user
 * in that case and need to map in the user pages before
 * attempting the copy!
 * returns number of bytes copied on success,
 * -errno on failure
 */
int pvfs_bufmap_copy_to_user_task(
        struct task_struct *tsk,
        void __user *to, 
        int buffer_index,
        int size)
{
    int ret = 0, amt_copied = 0, amt_remaining = 0;
    int cur_copy_size = 0, index = 0;
    void *from_kaddr = NULL;
    struct pvfs_bufmap_desc *from = &desc_array[buffer_index];

    struct mm_struct *mm = NULL;
    struct vm_area_struct *vma = NULL;
    struct page *page = NULL;
    unsigned long to_addr = (unsigned long) to;
    void *maddr = NULL;
    int to_offset = 0, from_offset = 0;
    int inc_index = 0;

    pvfs2_print("pvfs_bufmap_copy_to_user_task: "
            " PID: %d, to %p, from %p, index %d, "
            " size %d\n", tsk->pid, to, from, buffer_index, size);

    if (bufmap_init == 0)
    {
        pvfs2_error("pvfs2_bufmap_copy_to_user: not yet "
                    "initialized.\n");
        pvfs2_error("pvfs2: please confirm that pvfs2-client "
                "daemon is running.\n");
        return -EIO;
    }
    mm = get_task_mm(tsk);
    if (!mm) 
    {
        return -EIO;
    }
    /* 
     * Go through each of the page in the specified process
     * address space and copy from the mapped
     * buffer, and make sure to do this one page at a time!
     */
    down_read(&mm->mmap_sem);
    while(amt_copied < size)
    {
        int bytes = 0;

        ret = get_user_pages(tsk, mm, to_addr, 
                1,/* count */
                1,/* write */
                1,/* force */
                &page, &vma);
        if (ret <= 0)
            break;
        to_offset = to_addr & (PAGE_SIZE - 1);
        amt_remaining = (size - amt_copied);
        if ((PAGE_SIZE - to_offset) < (PAGE_SIZE - from_offset))
        {
            bytes = PAGE_SIZE - to_offset;
            inc_index = 0;
        }
        else if ((PAGE_SIZE - to_offset) == (PAGE_SIZE - from_offset))
        {
            bytes = (PAGE_SIZE - to_offset);
            inc_index = 1;
        }
        else 
        {
            bytes = (PAGE_SIZE - from_offset);
            inc_index = 1;
        }
        cur_copy_size =
            amt_remaining > bytes 
                  ? bytes : amt_remaining;
        maddr = pvfs2_kmap(page);
        from_kaddr = pvfs2_kmap(from->page_array[index]);
        copy_to_user_page(vma, page, to_addr,
             maddr + to_offset /* dst */, 
             from_kaddr + from_offset, /* src */
             cur_copy_size /* len */);
        set_page_dirty_lock(page);
        pvfs2_kunmap(from->page_array[index]);
        pvfs2_kunmap(page);
        page_cache_release(page);

        amt_copied += cur_copy_size;
        to_addr += cur_copy_size;
        if (inc_index)
        {
            from_offset = 0;
            index++;
        }
        else 
        {
            from_offset += cur_copy_size;
        }
    }
    up_read(&mm->mmap_sem);
    mmput(mm);
    return (amt_copied < size) ? -EFAULT: amt_copied;
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pint-dev-shared.h"

#define BUFMAP_PAGE_COUNT (PVFS2_BUFMAP_TOTAL_SIZE/PAGE_SIZE)
#define PAGES_PER_DESC (PVFS2_BUFMAP_DEFAULT_DESC_SIZE/PAGE_SIZE)

static int bufmap_init = 0;

static struct page** bufmap_page_array = NULL;

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
int pvfs_bufmap_initialize(struct PVFS_dev_map_desc* user_desc)
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
	pvfs2_error("pvfs2: error: memory alignment (front).\n");
        goto init_failure;
    }

    if (PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->size)) != 
	(unsigned long)(user_desc->ptr + user_desc->size))
    {
	pvfs2_error("pvfs2: error: memory alignment (back).\n");
        goto init_failure;
    }

    if (user_desc->size != PVFS2_BUFMAP_TOTAL_SIZE)
    {
	pvfs2_error("pvfs2: error: user provided an oddly "
                    "sized buffer...\n");
        goto init_failure;
    }

    if ((PVFS2_BUFMAP_DEFAULT_DESC_SIZE % PAGE_SIZE) != 0)
    {
	pvfs2_error("pvfs2: error: bufmap size not page size "
                    "divisable.\n");
        goto init_failure;
    }

    /* allocate storage to track our page mappings */
    bufmap_page_array = (struct page**)kmalloc(
        BUFMAP_PAGE_COUNT*sizeof(struct page*), PVFS2_BUFMAP_GFP_FLAGS);
    if (!bufmap_page_array)
    {
        ret = -ENOMEM;
        goto init_failure;
    }

    /* map the pages */
    down_read(&current->mm->mmap_sem);
    ret = get_user_pages(
	current,
	current->mm,
	(unsigned long)user_desc->ptr,
	BUFMAP_PAGE_COUNT,
	1,
	0,
	bufmap_page_array,
	NULL);
    up_read(&current->mm->mmap_sem);

    if (ret < 0)
    {
	kfree(bufmap_page_array);
        goto init_failure;
    }

    /* in theory we could run with what we got, but I will just treat
     * it as an error for simplicity's sake right now
     */
    if (ret != BUFMAP_PAGE_COUNT)
    {
	pvfs2_error("pvfs2: error: asked for %d pages, only got %d.\n",
                    (int)BUFMAP_PAGE_COUNT, ret);
	for(i = 0; i < ret; i++)
	{
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
      kaddr is needed.
    */
    for(i = 0; i < BUFMAP_PAGE_COUNT; i++)
    {
        flush_dcache_page(bufmap_page_array[i]);
    }

    /* build a list of available descriptors */
    offset = 0;
    for(i = 0; i < PVFS2_BUFMAP_DESC_COUNT; i++)
    {
	desc_array[i].page_array = &bufmap_page_array[offset];
	desc_array[i].array_count = PAGES_PER_DESC;
	desc_array[i].uaddr = user_desc->ptr +
	    (i*PAGES_PER_DESC*PAGE_SIZE);
	offset += PAGES_PER_DESC;
    }

    /* clear any previously used buffer indices */
    for(i = 0; i < PVFS2_BUFMAP_DESC_COUNT; i++)
    {
        buffer_index_array[i] = 0;
    }

    bufmap_init = 1;

    pvfs2_print("pvfs2_bufmap_initialize: exiting normally\n");
    return(0);

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

    for(i=0; i<BUFMAP_PAGE_COUNT; i++)
    {
	page_cache_release(bufmap_page_array[i]);
    }
    kfree(bufmap_page_array);

    bufmap_init = 0;

    pvfs2_print("pvfs2_bufmap_finalize: exiting normally\n");
    return;
}

/* pvfs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_get(int* buffer_index)
{
    int ret = -1, i = 0;
    DECLARE_WAITQUEUE(my_wait, current);

    add_wait_queue_exclusive(&bufmap_waitq, &my_wait);

    while(1)
    {
	set_current_state(TASK_INTERRUPTIBLE);

	/* check for available desc */
	spin_lock(&buffer_index_lock);
	for(i=0; i<PVFS2_BUFMAP_DESC_COUNT; i++)
	{
	    if(buffer_index_array[i] == 0)
	    {
		buffer_index_array[i] = 1;
		*buffer_index = i;
		ret = 0;
		break;
	    }
	}
	spin_unlock(&buffer_index_lock);

	/* if we acquired a buffer, then break out of while */
	if(ret == 0)
	{
	    break;
	}

	if(signal_pending(current))
	{
	    pvfs2_print("pvfs2: bufmap_get() interrupted.\n");
	    ret = -EINTR;
	    break;
	}
	schedule();
    }

    set_current_state(TASK_RUNNING);
    remove_wait_queue(&bufmap_waitq, &my_wait);

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
    /* put the desc back on the queue */
    spin_lock(&buffer_index_lock);
    buffer_index_array[buffer_index] = 0;
    spin_unlock(&buffer_index_lock);

    /* wake up anyone who may be sleeping on the queue */
    wake_up_interruptible(&bufmap_waitq);

    return;
}

/* pvfs_bufmap_copy_to_user()
 *
 * copies data out of a mapped buffer to a user space address
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user(void* to, int buffer_index, int size)
{
    int amt_copied = 0;
    int amt_this = 0;
    int index = 0;
    void* offset = to;
    void *from_kaddr = NULL;
    struct pvfs_bufmap_desc* from = &desc_array[buffer_index];
    
    if (bufmap_init == 0)
    {
        pvfs2_print("pvfs2_bufmap_copy_to_user: not yet "
                    "initialized; returning\n");
	return 1;
    }

    while(amt_copied < size)
    {
	if((size - amt_copied) > PAGE_SIZE)
	    amt_this = PAGE_SIZE;
	else
	    amt_this = size - amt_copied;

        from_kaddr = kmap(from->page_array[index]);
	copy_to_user(offset, from_kaddr, amt_this);
        kunmap(from->page_array[index]);

	offset += amt_this;
	amt_copied += amt_this;
	index++;
    }

    return(0);
}

int pvfs_bufmap_copy_to_kernel(void* to, int buffer_index, int size)
{
    int amt_copied = 0;
    int amt_this = 0;
    int index = 0;
    void* offset = to;
    void *from_kaddr = NULL;
    struct pvfs_bufmap_desc* from = &desc_array[buffer_index];

    if (bufmap_init == 0)
    {
        pvfs2_print("pvfs2_bufmap_copy_to_kernel: not yet "
                    "initialized; returning\n");
	return 1;
    }

    while(amt_copied < size)
    {
	if((size - amt_copied) > PAGE_SIZE)
	    amt_this = PAGE_SIZE;
	else
	    amt_this = size - amt_copied;

        from_kaddr = kmap(from->page_array[index]);
	memcpy(offset, from_kaddr, amt_this);
        kunmap(from->page_array[index]);

	offset += amt_this;
	amt_copied += amt_this;
	index++;
    }

    return(0);
}

/* pvfs_bufmap_copy_from_user()
 *
 * copies data from a user space address to a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_from_user(int buffer_index, void* from, int size)
{
    int amt_copied = 0;
    int amt_this = 0;
    int index = 0;
    void* offset = from;
    void *to_kaddr = NULL;
    struct pvfs_bufmap_desc* to = &desc_array[buffer_index];

    if (bufmap_init == 0)
    {
        pvfs2_print("pvfs2_bufmap_copy_from_user: not yet "
                    "initialized; returning\n");
	return 1;
    }

    while(amt_copied < size)
    {
	if((size - amt_copied) > PAGE_SIZE)
	    amt_this = PAGE_SIZE;
	else
	    amt_this = size - amt_copied;

	to_kaddr = kmap(to->page_array[index]);
	copy_from_user(to_kaddr, offset, amt_this);
        kunmap(to->page_array[index]);

	offset += amt_this;
	amt_copied += amt_this;
	index++;
    }

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

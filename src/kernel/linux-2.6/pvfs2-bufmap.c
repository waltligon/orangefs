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

static int bufmap_init = 0;
static struct page** bufmap_page_array = NULL;
static void** bufmap_kaddr_array = NULL;
static int bufmap_page_count = 0;

/* list of available descriptors, and lock to protect it */
static LIST_HEAD(desc_list);
static spinlock_t desc_list_lock = SPIN_LOCK_UNLOCKED;
static struct pvfs_bufmap_desc desc_array[PVFS2_BUFMAP_DESC_COUNT];

/* pvfs_bufmap_initialize()
 *
 * initializes the mapped buffer interface
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_initialize(struct PVFS_dev_map_desc* user_desc)
{
    int ret = -1;
    int i;
    int pages_per_desc = 0;
    int offset = 0;

    if(bufmap_init)
	return(-EALREADY);

    /* check the alignment and see if the caller was nice to us */
    if(PAGE_ALIGN((unsigned long)user_desc->ptr) != 
	(unsigned long)user_desc->ptr)
    {
	pvfs2_error("pvfs2: error: memory alignment (front).\n");
	return(-EINVAL);
    }
    if(PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->size)) != 
	(unsigned long)(user_desc->ptr + user_desc->size))
    {
	pvfs2_error("pvfs2: error: memory alignment (back).\n");
	return(-EINVAL);
    }

    bufmap_page_count = user_desc->size / PAGE_SIZE;
    pages_per_desc = bufmap_page_count / PVFS2_BUFMAP_DESC_COUNT;
    if(pages_per_desc < 1)
    {
	pvfs2_error("pvfs2: error: memory buffer too small.\n");
	return(-EINVAL);
    }

    /* allocate storage to track our page mappings */
    bufmap_page_array = 
	(struct page**)kmalloc(bufmap_page_count*sizeof(struct page*), 
	GFP_KERNEL);
    if(!bufmap_page_array)
    {
	return(-ENOMEM);
    }

    bufmap_kaddr_array =
	(void**)kmalloc(bufmap_page_count*sizeof(void*),
	GFP_KERNEL);
    if(!bufmap_kaddr_array)
    {
	kfree(bufmap_page_array);
	return(-ENOMEM);
    }

    /* map the pages */
    down_read(&current->mm->mmap_sem);

    ret = get_user_pages(
	current,
	current->mm,
	(unsigned long)user_desc->ptr,
	bufmap_page_count,
	1,
	0,
	bufmap_page_array,
	NULL);

    up_read(&current->mm->mmap_sem);

    if(ret < 0)
    {
	kfree(bufmap_page_array);
	kfree(bufmap_kaddr_array);
	return(ret);
    }

    /* in theory we could run with what we got, but I will just treat it
     * as an error for simplicity's sake right now
     */
    if(ret < bufmap_page_count)
    {
	pvfs2_error("pvfs2: error: asked for %d pages, only got %d.\n",
	    bufmap_page_count, ret);
	for(i=0; i<ret; i++)
	{
	    page_cache_release(bufmap_page_array[i]);
	}
	kfree(bufmap_page_array);
	kfree(bufmap_kaddr_array);
	return(-ENOMEM);
    }

    /* get kernel space pointers for each page */
    for(i=0; i<bufmap_page_count; i++)
    {
	bufmap_kaddr_array[i] = kmap(bufmap_page_array[i]);
    }

    /* build a list of available descriptors */
    offset = 0;
    for(i=0; i<PVFS2_BUFMAP_DESC_COUNT; i++)
    {
	desc_array[i].page_array = &bufmap_page_array[offset];
	desc_array[i].kaddr_array = &bufmap_kaddr_array[offset];
	desc_array[i].array_count = pages_per_desc;
	desc_array[i].uaddr = user_desc->ptr +
	    (i*pages_per_desc*PAGE_SIZE);
	list_add_tail(&(desc_array[i].list_entry), &desc_list);
	offset += pages_per_desc;
    }

    bufmap_init = 1;

    return(0);
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
    int i;

    if(!bufmap_init)
    {
	return;
    }

    for(i=0; i<bufmap_page_count; i++)
    {
	page_cache_release(bufmap_page_array[i]);
    }
    kfree(bufmap_page_array);
    kfree(bufmap_kaddr_array);

    bufmap_init = 0;

    return;
}

/* pvfs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_get(struct pvfs_bufmap_desc** desc)
{
    pvfs2_error("pvfs2: error: function not implemented.\n");
    return(-ENOSYS);
}

/* pvfs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void pvfs_bufmap_put(struct pvfs_bufmap_desc* desc)
{
    pvfs2_error("pvfs2: error: function not implemented.\n");
    return;
}

/* pvfs_bufmap_size_query()
 *
 * queries to determine the size of the memory represented by each 
 * mapped buffer description
 *
 * returns size on success, -errno on failure
 */
int pvfs_bufmap_size_query(void)
{
    if(!bufmap_init)
	return(-EINVAL);

    return((bufmap_page_count/PVFS2_BUFMAP_DESC_COUNT)*PAGE_SIZE);
}

/* pvfs_bufmap_copy_to_user()
 *
 * copies data out of a mapped buffer to a user space address
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user(void* to, struct pvfs_bufmap_desc* from,
    int size)
{
    pvfs2_error("pvfs2: error: function not implemented.\n");
    return(-ENOSYS);
}

/* pvfs2_bufmap_copy_from_user()
 *
 * copies data from a user space address to a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_from_user(struct pvfs_bufmap_desc* to, void* from,
    int size)
{
    pvfs2_error("pvfs2: error: function not implemented.\n");
    return(-ENOSYS);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

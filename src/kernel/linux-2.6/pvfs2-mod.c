/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/vermagic.h>
#include "pvfs2-kernel.h"

extern struct file_operations pvfs2_devreq_file_operations;

extern struct super_block *pvfs2_get_sb(
    struct file_system_type *fst,
    int flags,
    const char *devname,
    void *data);

extern void pvfs2_kill_sb(
    struct super_block *sb);

static int hash_func(
    void *key,
    int table_size)
{
    unsigned long tmp = 0;
    unsigned long *real_tag = (unsigned long *) key;
    tmp += (*(real_tag));
    tmp = tmp % table_size;
    return ((int) tmp);
}

static int hash_compare(
    void *key,
    struct qhash_head *link)
{
    pvfs2_kernel_op_t *op = NULL;
    unsigned long *real_tag = (unsigned long *) key;

    op = qhash_entry(link, pvfs2_kernel_op_t, list);

    /* use unlikely here since most hash compares will fail */
    if (unlikely(op->tag == *real_tag))
    {
	return (1);
    }
    return (0);
}


/*************************************
 * global variables declared here
 *************************************/

/* the assigned device major number */
static int pvfs2_dev_major = 0;

/* the pvfs2 memory caches (see pvfs2-cache.c) */
kmem_cache_t *op_cache = NULL;
kmem_cache_t *dev_req_cache = NULL;
kmem_cache_t *pvfs2_inode_cache = NULL;

/* the size of the hash tables for ops in progress */
static int hash_table_size = 509;
module_param(hash_table_size, int, 0);

/* hash table for storing operations waiting for matching downcall */
struct qhash_table *htable_ops_in_progress = NULL;

/* list for queueing upcall operations */
LIST_HEAD(pvfs2_request_list);

/* used to protect the above pvfs2_request_list */
spinlock_t pvfs2_request_list_lock = SPIN_LOCK_UNLOCKED;

struct file_system_type pvfs2_fs_type = {
    .name = "pvfs2",
    .get_sb = pvfs2_get_sb,
    .kill_sb = pvfs2_kill_sb,
    .owner = THIS_MODULE
};

static int __init pvfs2_init(
    void)
{
    pvfs2_print("pvfs2: pvfs2_init called\n");

    /* register pvfs2-req device  */
    pvfs2_dev_major = register_chrdev(0, PVFS2_REQDEVICE_NAME,
                                      &pvfs2_devreq_file_operations);
    if (pvfs2_dev_major < 0)
    {
	pvfs2_print("Failed to register /dev/%s (error %d)\n",
		    PVFS2_REQDEVICE_NAME, pvfs2_dev_major);
	return pvfs2_dev_major;
    }
    pvfs2_print("*** /dev/%s character device registered ***\n",
		PVFS2_REQDEVICE_NAME);
    pvfs2_print("'mknod /dev/%s c %d 0'.\n", PVFS2_REQDEVICE_NAME,
                pvfs2_dev_major);

    /* initialize global book keeping data structures */
    op_cache_initialize();
    dev_req_cache_initialize();
    pvfs2_inode_cache_initialize();

    htable_ops_in_progress =
	qhash_init(hash_compare, hash_func, hash_table_size);
    if (!htable_ops_in_progress)
    {
	panic("Failed to initialize op hashtable");
    }
    return register_filesystem(&pvfs2_fs_type);
}

static void __exit pvfs2_exit(
    void)
{
    int i;
    pvfs2_kernel_op_t *cur_op = NULL;
    struct qhash_head *hash_link = NULL;

    pvfs2_print("pvfs2: pvfs2_exit called\n");

    /* first unregister the pvfs2-req chrdev */
    if (unregister_chrdev(pvfs2_dev_major, PVFS2_REQDEVICE_NAME) < 0)
    {
	pvfs2_print("Failed to unregister pvfs2 device /dev/%s\n",
		    PVFS2_REQDEVICE_NAME);
    }
    pvfs2_print("Unregistered pvfs2 device /dev/%s\n", PVFS2_REQDEVICE_NAME);

    /* then unregister the filesystem */
    unregister_filesystem(&pvfs2_fs_type);

    /* uninitialize global book keeping data structures */

    /* clear out all pending upcall op requests */
    spin_lock(&pvfs2_request_list_lock);
    while (!list_empty(&pvfs2_request_list))
    {
	cur_op = list_entry(pvfs2_request_list.next, pvfs2_kernel_op_t, list);
	list_del(&cur_op->list);
	pvfs2_print("Freeing unhandled upcall request type %d\n",
		    cur_op->upcall.type);
	op_release(cur_op);
    }
    spin_unlock(&pvfs2_request_list_lock);

    /*
       this is an exhaustive and slow iterate through two hashtables
       of the same size.  since we're only doing this on unload only,
       there shouldn't be a significant performance penalty.
     */
    for (i = 0; i < htable_ops_in_progress->table_size; i++)
    {
	do
	{
	    hash_link = qhash_search_and_remove(htable_ops_in_progress, &(i));
	    if (hash_link)
	    {
		cur_op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
		op_release(cur_op);
	    }
	} while (hash_link);
    }
    qhash_finalize(htable_ops_in_progress);

    op_cache_finalize();
    dev_req_cache_finalize();
    pvfs2_inode_cache_finalize();
}

module_init(pvfs2_init);
module_exit(pvfs2_exit);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

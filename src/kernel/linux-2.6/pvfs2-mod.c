/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add proc file handler for pvfs2 client
 * parameters, Copyright © Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-proc.h"
#include "pvfs2-internal.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#define DEBUG_HELP_STRING_SIZE 4096


/* these functions are defined in pvfs2-utils.c */
uint64_t PVFS_proc_debug_eventlog_to_mask(const char *);
uint64_t PVFS_proc_kmod_eventlog_to_mask(const char *event_logging);
int PVFS_proc_kmod_mask_to_eventlog(uint64_t mask, char *debug_string);
int PVFS_proc_mask_to_eventlog(uint64_t mask, char *debug_string);

/* external references */
extern char kernel_debug_string[];

/* prototypes */
static int hash_func(const void *key, int table_size);
static int hash_compare(const void *key, struct qhash_head *link);

/*************************************
 * global variables declared here
 *************************************/

/* the size of the hash tables for ops in progress */
static int hash_table_size = 509;

/* the insmod command only understands "unsigned long" and NOT "unsigned long long" as
 * an input parameter.  So, to accomodate both 32- and 64- bit machines, we will read
 * the debug mask parameter as an unsigned long (4-bytes on a 32-bit machine and 8-bytes
 * on a 64-bit machine) and then cast the "unsigned long" to an "unsigned long long"
 * once we have the value in the kernel.  In this way, the gossip_debug_mask can remain
 * as a "uint64_t" and the kernel and client may continue to use the same gossip functions.
 * NOTE: the kernel debug mask currently does not have more than 32 valid keywords, so
 * only reading a 32-bit integer from the insmod command line is not a problem.  However,
 * the /proc/sys/pvfs2/kernel-debug functionality can accomodate up to 64 keywords, in 
 * the event that the kernel debug mask supports more than 32 keywords.
*/
uint32_t module_parm_debug_mask = 0;
uint64_t gossip_debug_mask = 0;
unsigned int kernel_mask_set_mod_init = false;
int op_timeout_secs = PVFS2_DEFAULT_OP_TIMEOUT_SECS;
int slot_timeout_secs = PVFS2_DEFAULT_SLOT_TIMEOUT_SECS;
uint32_t DEBUG_LINE = 50;
char debug_help_string[DEBUG_HELP_STRING_SIZE] = {0};


MODULE_LICENSE("GPL");
MODULE_AUTHOR("PVFS2 Development Team");
MODULE_DESCRIPTION("The Linux Kernel VFS interface to PVFS2");
MODULE_PARM_DESC(debug, "debugging level (see pvfs2-debug.h for values)");
MODULE_PARM_DESC(op_timeout_secs, "Operation timeout in seconds");
MODULE_PARM_DESC(slot_timeout_secs, "Slot timeout in seconds");
MODULE_PARM_DESC(hash_table_size, "size of hash table for operations in progress");

#ifdef PVFS2_LINUX_KERNEL_2_4
/*
  for 2.4.x nfs exporting, we need to add fsid=# to the /etc/exports
  file rather than using the FS_REQUIRES_DEV flag
*/
DECLARE_FSTYPE(pvfs2_fs_type, "pvfs2", pvfs2_get_sb, 0);

MODULE_PARM(hash_table_size, "i");
MODULE_PARM(module_parm_debug_mask, "i");
MODULE_PARM(op_timeout_secs, "i");
MODULE_PARM(slot_timeout_secs, "i");

#else /* !PVFS2_LINUX_KERNEL_2_4 */

struct file_system_type pvfs2_fs_type =
{
    .name = "pvfs2",
/* only define mount if the kernel no longer supports get_sb */
#ifdef HAVE_FSTYPE_MOUNT_ONLY
    .mount = pvfs2_mount,
#else
    .get_sb = pvfs2_get_sb,
#endif /* HAVE_FSTYPE_MOUNT_ONLY */
    .kill_sb = pvfs2_kill_sb,
    .owner = THIS_MODULE,
/*
  NOTE: the FS_REQUIRES_DEV flag is used to honor NFS exporting,
  though we're not really requiring a device.  pvfs2_get_sb is still
  get_sb_nodev and we're using kill_litter_super instead of
  kill_block_super.
*/
    .fs_flags = FS_REQUIRES_DEV,
};

module_param(hash_table_size, int, 0);
module_param(module_parm_debug_mask, uint, 0);
module_param(op_timeout_secs, int, 0);
module_param(slot_timeout_secs, int, 0);

#endif /* PVFS2_LINUX_KERNEL_2_4 */

/* synchronizes the request device file */
struct semaphore devreq_semaphore;

/*
  blocks non-priority requests from being queued for servicing.  this
  could be used for protecting the request list data structure, but
  for now it's only being used to stall the op addition to the request
  list
*/
struct semaphore request_semaphore;

/* hash table for storing operations waiting for matching downcall */
struct qhash_table *htable_ops_in_progress = NULL;

/* list for queueing upcall operations */
LIST_HEAD(pvfs2_request_list);

/* used to protect the above pvfs2_request_list */
#ifdef HAVE_SPIN_LOCK_UNLOCKED
spinlock_t pvfs2_request_list_lock = SPIN_LOCK_UNLOCKED;
#else 
DEFINE_SPINLOCK(pvfs2_request_list_lock);
#endif /* HAVE_SPIN_LOCK_UNLOCKED */

/* used for incoming request notification */
DECLARE_WAIT_QUEUE_HEAD(pvfs2_request_list_waitq);

static int __init pvfs2_init(void)
{
    int ret = -1;
    uint32_t index = 0;
    char client_title[] = "Client Debug Keywords:\n";
    char kernel_title[] = "Kernel Debug Keywords:\n";
    uint32_t i = 0;

    /* convert input debug mask to a 64-bit unsigned integer */
    gossip_debug_mask = (uint64_t)module_parm_debug_mask;

    /*set the kernel's gossip debug string; invalid mask values will be ignored.*/
    PVFS_proc_kmod_mask_to_eventlog(gossip_debug_mask,kernel_debug_string);

    /* remove any invalid values from the mask */
    gossip_debug_mask = PVFS_proc_kmod_eventlog_to_mask(kernel_debug_string);

    /* if the mask has a non-zero value, then indicate that the mask was set when the kernel module
     * was loaded.  The pvfs2 dev ioctl command will look at this boolean to determine if the kernel's
     * debug mask should be overwritten when the client-core is started.
    */
    if (gossip_debug_mask != 0)
    {
        kernel_mask_set_mod_init = true;
    }

    /*print information message to the system log*/
    printk(KERN_INFO "pvfs2: pvfs2_init called with debug mask: \"%s\" (0x%08llx)\n"
          ,kernel_debug_string,gossip_debug_mask);


    /* load debug_help_string...this string is used during the /proc/sys/pvfs2/debug-help operation */
    if (strlen(client_title) < DEBUG_LINE)
    {
       memcpy(&debug_help_string[index],client_title,sizeof(client_title));
       index += strlen(client_title);
    }
 
    for(i=0;i<num_keyword_mask_map;i++)
    {
       if ( (strlen(s_keyword_mask_map[i].keyword) + 2) < DEBUG_LINE)
       {
          debug_help_string[index] = '\t';
          index++;
          memcpy(&debug_help_string[index],s_keyword_mask_map[i].keyword
                ,strlen(s_keyword_mask_map[i].keyword));
          index += strlen(s_keyword_mask_map[i].keyword);
          debug_help_string[index] = '\n';
          index++;
       }
    }/*end for*/

    if ( (strlen(kernel_title) + 1) < DEBUG_LINE)
    {
       debug_help_string[index] = '\n';
       index++;

       memcpy(&debug_help_string[index],kernel_title,sizeof(kernel_title));
       index += strlen(kernel_title);
    }/*end if*/

    for(i=0;i<num_kmod_keyword_mask_map;i++)
    {
       if ( (strlen(s_kmod_keyword_mask_map[i].keyword) + 2) < DEBUG_LINE)
       {
          debug_help_string[index] = '\t';
          index++;
          memcpy(&debug_help_string[index],s_kmod_keyword_mask_map[i].keyword
                ,strlen(s_kmod_keyword_mask_map[i].keyword));
          index += strlen(s_kmod_keyword_mask_map[i].keyword);
          debug_help_string[index] = '\n';
          index++;
       }
    }/*end for*/

#ifdef HAVE_BDI_INIT
    ret = bdi_init(&pvfs2_backing_dev_info);
    if(ret)
        return(ret);
#endif

    if(op_timeout_secs < 0)
    {
        op_timeout_secs = 0;
    }

    if(slot_timeout_secs < 0)
    {
        slot_timeout_secs = 0;
    }

    /* initialize global book keeping data structures */
    if ((ret = op_cache_initialize()) < 0) {
        goto err;
    }
    if ((ret = dev_req_cache_initialize()) < 0) {
        goto cleanup_op;
    }
    if ((ret = pvfs2_inode_cache_initialize()) < 0) {
        goto cleanup_req;
    }
    if ((ret = kiocb_cache_initialize()) < 0) {
        goto cleanup_inode;
    }

    /* Initialize the pvfsdev subsystem. */
    if ((ret = pvfs2_dev_init()) < 0)
    {
        gossip_err("pvfs2: could not initialize device subsystem %d!\n",
                ret);
        goto cleanup_kiocb;
    }

    sema_init(&devreq_semaphore, 1);
    sema_init(&request_semaphore, 1);

    htable_ops_in_progress =
	qhash_init(hash_compare, hash_func, hash_table_size);
    if (!htable_ops_in_progress)
    {
	gossip_err("Failed to initialize op hashtable");
        ret = -ENOMEM;
        goto cleanup_device;
    }
    if ((ret = fsid_key_table_initialize()) < 0) 
    {
        goto cleanup_progress_table;
    }
    pvfs2_proc_initialize();
    ret = register_filesystem(&pvfs2_fs_type);

    if(ret == 0)
    {
        printk("pvfs2: module version %s loaded\n", PVFS2_VERSION);
        return 0;
    }
    pvfs2_proc_finalize();
    fsid_key_table_finalize();
cleanup_progress_table:
    qhash_finalize(htable_ops_in_progress);
cleanup_device:
    pvfs2_dev_cleanup();
cleanup_kiocb:
    kiocb_cache_finalize();
cleanup_inode:
    pvfs2_inode_cache_finalize();
cleanup_req:
    dev_req_cache_finalize();
cleanup_op:
    op_cache_finalize();
err:
#ifdef HAVE_BDI_INIT
    bdi_destroy(&pvfs2_backing_dev_info);
#endif
    return ret;
}

static void __exit pvfs2_exit(void)
{
    int i = 0;
    pvfs2_kernel_op_t *cur_op = NULL;
    struct qhash_head *hash_link = NULL;

    gossip_debug(GOSSIP_INIT_DEBUG, "pvfs2: pvfs2_exit called\n");

    unregister_filesystem(&pvfs2_fs_type);
    pvfs2_proc_finalize();
    fsid_key_table_finalize();
    pvfs2_dev_cleanup();
    /* clear out all pending upcall op requests */
    spin_lock(&pvfs2_request_list_lock);
    while (!list_empty(&pvfs2_request_list))
    {
	cur_op = list_entry(pvfs2_request_list.next,
                            pvfs2_kernel_op_t, list);
	list_del(&cur_op->list);
	gossip_debug(GOSSIP_INIT_DEBUG, "Freeing unhandled upcall request type %d\n",
		    cur_op->upcall.type);
	op_release(cur_op);
    }
    spin_unlock(&pvfs2_request_list_lock);

    for (i = 0; i < htable_ops_in_progress->table_size; i++)
    {
	do
	{
	    hash_link = qhash_search_and_remove_at_index(
                htable_ops_in_progress, i);
	    if (hash_link)
	    {
		cur_op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
                op_release(cur_op);
	    }
	} while (hash_link);
    }
    kiocb_cache_finalize();
    pvfs2_inode_cache_finalize();
    dev_req_cache_finalize();
    op_cache_finalize();

    qhash_finalize(htable_ops_in_progress);
    
#ifdef HAVE_BDI_INIT
    bdi_destroy(&pvfs2_backing_dev_info);
#endif

    printk("pvfs2: module version %s unloaded\n", PVFS2_VERSION);
}

/* simply return an index valid for the table_size */
static int hash_func(const void *key, int table_size)
{
    uint64_t ret;
    const uint64_t *tag = (const uint64_t *) key;

    ret = *tag % ((unsigned int) table_size);

    return (int) ret;
}

static int hash_compare(const void *key, struct qhash_head *link)
{
    const uint64_t *real_tag = (const uint64_t *)key;
    pvfs2_kernel_op_t *op = qhash_entry(
        link, pvfs2_kernel_op_t, list);

    return (op->tag == *real_tag);
}

/* What we do in this function is to walk the list of operations that are in progress
 * in the hash table and mark them as purged as well.
 */
void purge_inprogress_ops(void)
{
    int i;

    for (i = 0; i < hash_table_size; i++)
    {
        struct qhash_head *tmp_link = NULL, *scratch = NULL;
        qhash_for_each_safe(tmp_link, scratch, &(htable_ops_in_progress->array[i]))
        {
            pvfs2_kernel_op_t *op = qhash_entry(tmp_link, pvfs2_kernel_op_t, list);
            spin_lock(&op->lock);
            gossip_debug(GOSSIP_INIT_DEBUG, "pvfs2-client-core: purging in-progress op tag %llu %s\n",
                    llu(op->tag), get_opname_string(op));
            set_op_state_purged(op);
            spin_unlock(&op->lock);
            wake_up_interruptible(&op->waitq);
        }
    }
    return;
}

module_init(pvfs2_init);
module_exit(pvfs2_exit);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-bufmap.h"

/* this file implements the /dev/pvfs2-req device node */
extern kmem_cache_t *dev_req_cache;

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern struct qhash_table *htable_ops_in_progress;

static int open_access_count = 0;

/* a pointer to the task that opens the dev-req device file */
static struct task_struct *device_owner = NULL;

/* a function that forces termination of the device owner */
void kill_device_owner(void)
{
    if (device_owner)
    {
        pvfs2_print("**************************************\n");
        pvfs2_print("Killing pvfs2 daemon with pid %d\n",
                    device_owner->pid);
        pvfs2_print("**************************************\n");
        force_sig(SIGKILL, device_owner);
    }
    else
    {
        panic("Trying to kill pvfs2 daemon before pvfs2-req "
              "device was opened\n");
    }
}

static int pvfs2_devreq_open(
    struct inode *inode,
    struct file *file)
{
    int ret = -EACCES;

    spin_lock(&inode->i_lock);
    if (open_access_count == 0)
    {
	ret = generic_file_open(inode, file);
	if (ret == 0)
	{
	    open_access_count++;
            device_owner = current;
	}
	spin_unlock(&inode->i_lock);
    }
    else
    {
	spin_unlock(&inode->i_lock);
	pvfs2_error("*****************************************************\n");
	pvfs2_error("PVFS2 Device Error:  You cannot open the device file ");
	pvfs2_error("\n/dev/%s more than once.  Please make sure that\nthere "
		    "are no ", PVFS2_REQDEVICE_NAME);
	pvfs2_error("instances of a program using this device\ncurrently "
		    "running. (You must verify this!)\n");
	pvfs2_error("For example, you can use the lsof program as follows:\n");
	pvfs2_error("'lsof | grep %s' (run this as root)\n",
		    PVFS2_REQDEVICE_NAME);
	pvfs2_error("*****************************************************\n");
    }
    return ret;
}

static ssize_t pvfs2_devreq_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t * offset)
{
    int len = 0;
    pvfs2_kernel_op_t *cur_op = (pvfs2_kernel_op_t *) 0;
    static int32_t magic = PVFS2_DEVREQ_MAGIC;

    /* retrieve and remove next pending up-going op from list */
  check_if_req_pending:
    spin_lock(&pvfs2_request_list_lock);
    if (!list_empty(&pvfs2_request_list))
    {
	cur_op = list_entry(pvfs2_request_list.next, pvfs2_kernel_op_t, list);
	list_del(&cur_op->list);
    }
    spin_unlock(&pvfs2_request_list_lock);

    if (cur_op)
    {
        set_current_state(TASK_RUNNING);

	spin_lock(&cur_op->lock);

	/* FIXME: this is a sanity check and should be removed */
	if ((cur_op->op_state == PVFS2_VFS_STATE_INPROGR) ||
	    (cur_op->op_state == PVFS2_VFS_STATE_SERVICED))
	{
	    spin_unlock(&cur_op->lock);
	    panic("FIXME: Current op already queued...skipping\n");
	    return -1;
	}
	cur_op->op_state = PVFS2_VFS_STATE_INPROGR;

	/* atomically move the operation to the htable_ops_in_progress */
	qhash_add(htable_ops_in_progress,
		  (void *) &(cur_op->tag), &cur_op->list);

	spin_unlock(&cur_op->lock);

	len = MAX_DEV_REQ_UPSIZE;
	if ((size_t) len <= count)
	{
	    copy_to_user(buf, &magic, sizeof(int32_t));
	    copy_to_user(buf + sizeof(int32_t),
                         &cur_op->tag, sizeof(int64_t));
	    copy_to_user(buf + sizeof(int32_t) + sizeof(int64_t),
			 &cur_op->upcall, sizeof(pvfs2_upcall_t));
	}
	else
	{
	    pvfs2_error("Read buffer is too small to copy pvfs2 op\n");
	    len = -1;
	}
    }
    else if (!(file->f_flags & O_NONBLOCK))
    {
        set_current_state(TASK_INTERRUPTIBLE);
	/*
	   keep checking if a req is pending since
	   we're in a blocking mode
	 */
	schedule_timeout(MSECS_TO_JIFFIES(10));

	/* unless we got a signal */
	if (!signal_pending(current))
	{
	    goto check_if_req_pending;
	}
    }
    return len;
}

static ssize_t pvfs2_devreq_writev(
    struct file *file,
    const struct iovec *iov,
    unsigned long count,
    loff_t * offset)
{
    pvfs2_kernel_op_t *op = NULL;
    struct qhash_head *hash_link = NULL;
    void *buffer = NULL;
    void *ptr = NULL;
    unsigned long i = 0;
    static int max_downsize = MAX_DEV_REQ_DOWNSIZE;
    int num_remaining = max_downsize;
    int payload_size = 0;
    int32_t magic = 0;
    int64_t _tag = 0;		/* a hack for now (we only looks at 32 bit tags) */
    unsigned long tag = 0;

    buffer = kmem_cache_alloc(dev_req_cache, SLAB_KERNEL);
    if (!buffer)
    {
	return (-ENOMEM);
    }
    ptr = buffer;

    for (i = 0; i < count; i++)
    {
	if (iov[i].iov_len > num_remaining)
	{
	    pvfs2_error("writev error: Freeing buffer and returning\n");
	    kmem_cache_free(dev_req_cache, buffer);
	    return (-EMSGSIZE);
	}
	copy_from_user(ptr, iov[i].iov_base, iov[i].iov_len);
	num_remaining -= iov[i].iov_len;
	ptr += iov[i].iov_len;
	payload_size += iov[i].iov_len;
    }

    ptr = buffer;
    magic = *((int32_t *) ptr);
    ptr += sizeof(int32_t);

    _tag = *((int64_t *) ptr);
    ptr += sizeof(int64_t);
    tag = (unsigned long) _tag;	/* a hack for now */

    /* lookup (and remove) the op based on the tag */
    hash_link = qhash_search_and_remove(htable_ops_in_progress, &(tag));
    if (hash_link)
    {
	op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
	if (op)
	{
	    /* cut off magic and tag from payload size */
	    payload_size -= (sizeof(int32_t) + sizeof(int64_t));
	    if (payload_size <= sizeof(pvfs2_downcall_t))
	    {
		/* copy the passed in downcall into the op */
		memcpy(&op->downcall, ptr, sizeof(pvfs2_downcall_t));
	    }
	    else
	    {
		pvfs2_print("writev: Ignoring %d bytes\n", payload_size);
	    }

	    /* tell the vfs op waiting on a waitqueue that this op is done */
	    spin_lock(&op->lock);
	    op->op_state = PVFS2_VFS_STATE_SERVICED;
	    spin_unlock(&op->lock);

	    wake_up_interruptible(&op->waitq);
	}
    }
    else
    {
        /* ignore downcalls that we're not interested in */
	pvfs2_print("WARNING: No one's waiting for the tag %lu\n", tag);
    }

    kmem_cache_free(dev_req_cache, buffer);

    return count;
}

/*
  NOTE: gets called when the last reference to this device
  is dropped.  Using the open_access_count variable, we
  enforce a reference count on this file so that it can be
  opened by only one process at a time.
*/
static int pvfs2_devreq_release(
    struct inode *inode,
    struct file *file)
{
    spin_lock(&inode->i_lock);
    open_access_count--;

    pvfs_bufmap_finalize();

    device_owner = NULL;
    spin_unlock(&inode->i_lock);

    return 0;
}

static int pvfs2_devreq_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int command,
    unsigned long arg)
{
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    static int32_t max_up_size =
	sizeof(int32_t) + sizeof(int64_t) + sizeof(pvfs2_upcall_t);
    static int32_t max_down_size =
	sizeof(int32_t) + sizeof(int64_t) + sizeof(pvfs2_downcall_t);
    struct PVFS_dev_map_desc user_desc;

    switch (command)
    {
    case PVFS_DEV_GET_MAGIC:
	copy_to_user((void *) arg, &magic, sizeof(int32_t));
	return 0;
    case PVFS_DEV_GET_MAX_UPSIZE:
	copy_to_user((void *) arg, &max_up_size, sizeof(int32_t));
	return 0;
    case PVFS_DEV_GET_MAX_DOWNSIZE:
	copy_to_user((void *) arg, &max_down_size, sizeof(int32_t));
	return 0;
    case PVFS_DEV_MAP:
	copy_from_user(&user_desc, (void *) arg, sizeof(struct
	    PVFS_dev_map_desc));
	return(pvfs_bufmap_initialize(&user_desc));
    default:
	return -ENOSYS;
    }
    return -ENOSYS;
}

struct file_operations pvfs2_devreq_file_operations = {
    .read = pvfs2_devreq_read,
    .writev = pvfs2_devreq_writev,
    .open = pvfs2_devreq_open,
    .release = pvfs2_devreq_release,
    .ioctl = pvfs2_devreq_ioctl
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

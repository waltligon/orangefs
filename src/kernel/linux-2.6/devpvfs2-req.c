/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-bufmap.h"

/* this file implements the /dev/pvfs2-req device node */
extern kmem_cache_t *dev_req_cache;

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern struct qhash_table *htable_ops_in_progress;
extern struct file_system_type pvfs2_fs_type;
extern struct semaphore devreq_semaphore;
extern struct semaphore request_semaphore;

/* defined in super.c */
extern struct list_head pvfs2_superblocks;
extern spinlock_t pvfs2_superblocks_lock;

static int open_access_count = 0;

/* a pointer to the task that opens the dev-req device file */
static struct task_struct *device_owner = NULL;


/* a function that forces termination of the device owner */
void kill_device_owner(void)
{
    down(&devreq_semaphore);
    if (device_owner)
    {
        force_sig(SIGKILL, device_owner);
    }
    up(&devreq_semaphore);
}

static int pvfs2_devreq_open(
    struct inode *inode,
    struct file *file)
{
    int ret = -EACCES;

    pvfs2_print("pvfs2_devreq_open: trying to open\n");

    down(&devreq_semaphore);

    if (open_access_count == 0)
    {
        ret = generic_file_open(inode, file);
        if (ret == 0)
        {
#ifdef PVFS2_LINUX_KERNEL_2_4
            MOD_INC_USE_COUNT;
#else
            ret = (try_module_get(pvfs2_fs_type.owner) ? 0 : 1);
#endif
            open_access_count++;
            device_owner = current;
        }
    }
    else
    {
	pvfs2_error("*****************************************************\n");
	pvfs2_error("PVFS2 Device Error:  You cannot open the device file ");
	pvfs2_error("\n/dev/%s more than once.  Please make sure that\nthere "
		    "are no ", PVFS2_REQDEVICE_NAME);
	pvfs2_error("instances of a program using this device\ncurrently "
		    "running. (You must verify this!)\n");
	pvfs2_error("For example, you can use the lsof program as follows:\n");
	pvfs2_error("'lsof | grep %s' (run this as root)\n",
		    PVFS2_REQDEVICE_NAME);
        pvfs2_error("  open_access_count = %d\n", open_access_count);
	pvfs2_error("*****************************************************\n");
    }
    up(&devreq_semaphore);

    pvfs2_print("pvfs2_devreq_open: open complete (ret = %d)\n", ret);
    return ret;
}

static ssize_t pvfs2_devreq_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t * offset)
{
    int len = 0;
    pvfs2_kernel_op_t *cur_op = NULL;
    static int32_t magic = PVFS2_DEVREQ_MAGIC;

    if (!(file->f_flags & O_NONBLOCK))
    {
        /* block until we have a request */
        DECLARE_WAITQUEUE(wait_entry, current);

        add_wait_queue_exclusive(&pvfs2_request_list_waitq, &wait_entry);

        while(1)
        {
            set_current_state(TASK_INTERRUPTIBLE);

            spin_lock(&pvfs2_request_list_lock);
            if (!list_empty(&pvfs2_request_list))
            {
                cur_op = list_entry(
                    pvfs2_request_list.next, pvfs2_kernel_op_t, list);
                list_del(&cur_op->list);
                spin_unlock(&pvfs2_request_list_lock);
                break;
            }
            spin_unlock(&pvfs2_request_list_lock);

            if (!signal_pending(current))
            {
                schedule();
                continue;
            }

            pvfs2_print("*** device read interrupted by signal\n");
            break;
        }

        set_current_state(TASK_RUNNING);
        remove_wait_queue(&pvfs2_request_list_waitq, &wait_entry);
    }
    else
    {
        /* get next op (if any) from top of list */
        spin_lock(&pvfs2_request_list_lock);
        if (!list_empty(&pvfs2_request_list))
        {
            cur_op = list_entry(
                pvfs2_request_list.next, pvfs2_kernel_op_t, list);
            list_del(&cur_op->list);
        }
        spin_unlock(&pvfs2_request_list_lock);
    }

    if (cur_op)
    {
        spin_lock(&cur_op->lock);

        /* FIXME: this is a sanity check and should be removed */
        if ((cur_op->op_state == PVFS2_VFS_STATE_INPROGR) ||
            (cur_op->op_state == PVFS2_VFS_STATE_SERVICED))
        {
            pvfs2_error("WARNING: Current op already queued...skipping\n");
        }
        cur_op->op_state = PVFS2_VFS_STATE_INPROGR;

        /* atomically move the operation to the htable_ops_in_progress */
        qhash_add(htable_ops_in_progress,
                  (void *) &(cur_op->tag), &cur_op->list);

        spin_unlock(&cur_op->lock);

        len = MAX_ALIGNED_DEV_REQ_UPSIZE;
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
    static int max_downsize = MAX_ALIGNED_DEV_REQ_DOWNSIZE;
    int num_remaining = max_downsize;
    int payload_size = 0;
    int32_t magic = 0;
    /* FIXME: tags are a hack for now (we only looks at 32 bit tags) */
    int64_t _tag = 0;
    unsigned long tag = 0;

    buffer = kmem_cache_alloc(dev_req_cache, PVFS2_CACHE_ALLOC_FLAGS);
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

            /*
              if this operation is an I/O operation, we need to wait
              for all data to be copied before we can return to avoid
              buffer corruption and races that can pull the buffers
              out from under us.

              Essentially we're synchronizing with other parts of the
              vfs implicitly by not allowing the user space
              application reading/writing this device to return until
              the buffers are done being used.
            */
            if (op->upcall.type == PVFS2_VFS_OP_FILE_IO)
            {
                int timed_out = 0;
                DECLARE_WAITQUEUE(wait_entry, current);

                add_wait_queue(&op->io_completion_waitq, &wait_entry);
                wake_up_interruptible(&op->waitq);

                while(1)
                {
                    set_current_state(TASK_INTERRUPTIBLE);

                    spin_lock(&op->lock);
                    if (op->io_completed)
                    {
                        spin_unlock(&op->lock);
                        break;
                    }
                    spin_unlock(&op->lock);

                    if (!signal_pending(current))
                    {
                        int timeout = MSECS_TO_JIFFIES(
                            1000 * MAX_SERVICE_WAIT_IN_SECONDS);
                        if (!schedule_timeout(timeout))
                        {
                            pvfs2_print("*** I/O wait time is up\n");
                            timed_out = 1;
                            break;
                        }
                        continue;
                    }

                    pvfs2_print("*** signal on I/O wait -- aborting\n");
                    break;
                }

                set_current_state(TASK_RUNNING);
                remove_wait_queue(&op->io_completion_waitq, &wait_entry);

                /*
                  NOTE: for I/O operations we handle releasing the op
                  object except in the case of timeout.  the reason we
                  can't free the op in timeout cases is that the op
                  service logic in the vfs retries operations using
                  the same op ptr, thus it can't be freed.
                */
                if (!timed_out)
                {
                    op_release(op);
                }
            }
            else
            {
                /*
                  for every other operation (i.e. non-I/O), we need to
                  wake up the callers for downcall completion
                  notification
                */
                wake_up_interruptible(&op->waitq);
            }
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
  NOTE: gets called when the last reference to this device is dropped.
  Using the open_access_count variable, we enforce a reference count
  on this file so that it can be opened by only one process at a time.
  the devreq_semaphore is used to make sure all i/o has completed
  before we cann pvfs_bufmap_finalize, and similar such tricky
  situations
*/
static int pvfs2_devreq_release(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2_devreq_release: trying to finalize\n");

    down(&devreq_semaphore);
    pvfs_bufmap_finalize();

    open_access_count--;
    device_owner = NULL;

#ifdef PVFS2_LINUX_KERNEL_2_4
    MOD_DEC_USE_COUNT;
#else
    module_put(pvfs2_fs_type.owner);
#endif

    /*
      prune dcache here to get rid of entries that may no longer exist
      on device re-open
    */
    shrink_dcache_sb(inode->i_sb);

    up(&devreq_semaphore);

    pvfs2_print("pvfs2_devreq_release: finalize complete\n");
    return 0;
}

static int pvfs2_devreq_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int command,
    unsigned long arg)
{
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    static int32_t max_up_size = MAX_ALIGNED_DEV_REQ_UPSIZE;
    static int32_t max_down_size = MAX_ALIGNED_DEV_REQ_DOWNSIZE;
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
            copy_from_user(&user_desc, (void *) arg,
                           sizeof(struct PVFS_dev_map_desc));
            return pvfs_bufmap_initialize(&user_desc);
        case PVFS_DEV_REMOUNT_ALL:
        {
            int ret = 0;
            struct list_head *tmp = NULL;
            pvfs2_sb_info_t *pvfs2_sb = NULL;

            pvfs2_print("ioctl: pvfs_dev_remount_all called\n");

            /*
              remount all mounted pvfs2 volumes to regain the lost
              dynamic mount tables (if any) -- NOTE: this is done
              without keeping the superblock list locked due to the
              upcall/downcall waiting.  also, the request semaphore is
              used to ensure that no operations will be serviced until
              all of the remounts are serviced (to avoid ops between
              mounts to fail)
            */
            down_interruptible(&request_semaphore);
            pvfs2_print("pvfs2_devreq_ioctl: priority remount "
                        "in progress\n");
            list_for_each(tmp, &pvfs2_superblocks) {
                pvfs2_sb = list_entry(tmp, pvfs2_sb_info_t, list);
                if (pvfs2_sb && (pvfs2_sb->sb))
                {
                    pvfs2_print("Remounting SB %p\n", pvfs2_sb);

                    ret = pvfs2_remount(pvfs2_sb->sb, NULL,
                                        pvfs2_sb->data);
                    if (ret)
                    {
                        pvfs2_print("Failed to remount SB %p\n", pvfs2_sb);
                        break;
                    }
                }
            }
            pvfs2_print("pvfs2_devreq_ioctl: priority remount "
                        "complete\n");
            up(&request_semaphore);
            return ret;
        }
        break;
    default:
	return -ENOSYS;
    }
    return -ENOSYS;
}


struct file_operations pvfs2_devreq_file_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    owner:   THIS_MODULE,
    read : pvfs2_devreq_read,
    writev : pvfs2_devreq_writev,
    open : pvfs2_devreq_open,
    release : pvfs2_devreq_release,
    ioctl : pvfs2_devreq_ioctl
#else
    .read = pvfs2_devreq_read,
    .writev = pvfs2_devreq_writev,
    .open = pvfs2_devreq_open,
    .release = pvfs2_devreq_release,
    .ioctl = pvfs2_devreq_ioctl
#endif
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

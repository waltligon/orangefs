/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add protocol version to kernel
 * communication, Copyright © Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"

/* this file implements the /dev/pvfs2-req device node */

static int open_access_count = 0;

#define DUMP_DEVICE_ERROR()                                            \
gossip_err("*****************************************************\n");\
gossip_err("PVFS2 Device Error:  You cannot open the device file ");  \
gossip_err("\n/dev/%s more than once.  Please make sure that\nthere " \
            "are no ", PVFS2_REQDEVICE_NAME);                          \
gossip_err("instances of a program using this device\ncurrently "     \
            "running. (You must verify this!)\n");                     \
gossip_err("For example, you can use the lsof program as follows:\n");\
gossip_err("'lsof | grep %s' (run this as root)\n",                   \
            PVFS2_REQDEVICE_NAME);                                     \
gossip_err("  open_access_count = %d\n", open_access_count);          \
gossip_err("*****************************************************\n")

#ifdef HAVE_KERNEL_DEVICE_CLASSES
static struct class *pvfs2_dev_class;
#endif

static int pvfs2_devreq_open(
    struct inode *inode,
    struct file *file)
{
    int ret = -EINVAL;

    if (!(file->f_flags & O_NONBLOCK))
    {
        gossip_err("pvfs2: device cannot be opened in blocking mode\n");
        return ret;
    }
    ret = -EACCES;
    gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2-client-core: opening device\n");
    down(&devreq_semaphore);

    if (open_access_count == 0)
    {
        ret = generic_file_open(inode, file);
        if (ret == 0)
        {
#ifdef PVFS2_LINUX_KERNEL_2_4
/*             MOD_INC_USE_COUNT; */
#else
            ret = (try_module_get(pvfs2_fs_type.owner) ? 0 : 1);
#endif
            if (ret == 0)
            {
                open_access_count++;
            }
            else
            {
                gossip_err("PVFS2 Device Error: Cannot obtain reference "
                            "for device file\n");
            }
        }
    }
    else
    {
        DUMP_DEVICE_ERROR();
    }
    up(&devreq_semaphore);

    gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2-client-core: open device complete (ret = %d)\n", ret);
    return ret;
}

static ssize_t pvfs2_devreq_read(
    struct file *file,
    char __user *buf,
    size_t count,
    loff_t *offset)
{
    int ret = 0;
    ssize_t len = 0;
    pvfs2_kernel_op_t *cur_op = NULL;
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    int32_t proto_ver = PVFS_KERNEL_PROTO_VERSION;

    if (!(file->f_flags & O_NONBLOCK))
    {
        /* We do not support blocking reads/opens any more */
        gossip_err("pvfs2: blocking reads are not supported! (pvfs2-client-core bug)\n");
        return -EINVAL;
    }
    else
    {
        pvfs2_kernel_op_t *op = NULL;
        /* get next op (if any) from top of list */
        spin_lock(&pvfs2_request_list_lock);
        list_for_each_entry (op, &pvfs2_request_list, list)
        {
            PVFS_fs_id fsid = fsid_of_op(op);
            /* Check if this op's fsid is known and needs remounting */
            if (fsid != PVFS_FS_ID_NULL && fs_mount_pending(fsid) == 1)
            {
                gossip_debug(GOSSIP_DEV_DEBUG, "Skipping op tag %lld %s\n", lld(op->tag), get_opname_string(op));
                continue;
            }
            /* op does not belong to any particular fsid or already 
             * mounted.. let it through
             */
            else {
                cur_op = op;
                list_del(&cur_op->list);
                cur_op->op_linger_tmp--;
                /* if there is a trailer, re-add it to the request list */
                if (cur_op->op_linger == 2 && cur_op->op_linger_tmp == 1)
                {
                    if (cur_op->upcall.trailer_size <= 0 || cur_op->upcall.trailer_buf == NULL) 
                    {
                        gossip_err("BUG:trailer_size is %ld and trailer buf is %p\n",
                                (long) cur_op->upcall.trailer_size, cur_op->upcall.trailer_buf);
                    }
                    /* readd it to the head of the list */
                    list_add(&cur_op->list, &pvfs2_request_list);
                }
                break;
            }
        }
        spin_unlock(&pvfs2_request_list_lock);
    }

    if (cur_op)
    {
        spin_lock(&cur_op->lock);

        gossip_debug(GOSSIP_DEV_DEBUG, "client-core: reading op tag %lld %s\n", lld(cur_op->tag), get_opname_string(cur_op));
        if (op_state_in_progress(cur_op) || op_state_serviced(cur_op))
        {
            if (cur_op->op_linger == 1)
                gossip_err("WARNING: Current op already queued...skipping\n");
        }
        else if (cur_op->op_linger == 1 
                || (cur_op->op_linger == 2 && cur_op->op_linger_tmp == 0)) 
        {
            set_op_state_inprogress(cur_op);

            /* atomically move the operation to the htable_ops_in_progress */
            qhash_add(htable_ops_in_progress,
                      (void *) &(cur_op->tag), &cur_op->list);
        }

        spin_unlock(&cur_op->lock);

        /* 2 cases
         * a) OPs with no trailers
         * b) OPs with trailers, Stage 1
         * Either way push the upcall out
         */
        if (cur_op->op_linger == 1 
                || (cur_op->op_linger == 2 && cur_op->op_linger_tmp == 1))
        {
            len = MAX_ALIGNED_DEV_REQ_UPSIZE;
            if ((size_t) len <= count)
            {
                ret = copy_to_user(buf, &proto_ver, sizeof(int32_t));
                if(ret == 0)
                {
                    ret = copy_to_user(buf + sizeof(int32_t), &magic, sizeof(int32_t));
                    if (ret == 0)
                    {
                        ret = copy_to_user(buf + 2*sizeof(int32_t),
                                           &cur_op->tag, sizeof(uint64_t));
                        if (ret == 0)
                        {
                            ret = copy_to_user(
                                buf + 2*sizeof(int32_t) + sizeof(uint64_t),
                                &cur_op->upcall, sizeof(pvfs2_upcall_t));
                        }
                    }
                }

                if (ret)
                {
                    gossip_err("Failed to copy data to user space\n");
                    len = -EFAULT;
                }
            }
            else
            {
                gossip_err("Failed to copy data to user space\n");
                len = -EIO;
            }
        }
        /* Stage 2: Push the trailer out */
        else if (cur_op->op_linger == 2 && cur_op->op_linger_tmp == 0)
        {
            len = cur_op->upcall.trailer_size;
            if ((size_t) len <= count)
            {
                ret = copy_to_user(buf, cur_op->upcall.trailer_buf, len);
                if (ret)
                {
                    gossip_err("Failed to copy trailer to user space\n");
                    len = -EFAULT;
                }
            }
            else {
                gossip_err("Read buffer for trailer is too small (%ld as opposed to %ld)\n",
                        (long) count, (long) len);
                len = -EIO;
            }
        }
        else {
            gossip_err("cur_op: %p (op_linger %d), (op_linger_tmp %d),"
                    "erroneous request list?\n", cur_op, 
                    cur_op->op_linger, cur_op->op_linger_tmp);
            len = 0;
        }
    }
    else if (file->f_flags & O_NONBLOCK)

    {
        /*
          if in non-blocking mode, return EAGAIN since no requests are
          ready yet
        */
        len = -EAGAIN;
    }
    return len;
}

/* Common function for writev() and aio_write() callers into the device */
static ssize_t pvfs2_devreq_writev(
    struct file *file,
    const struct iovec *iov,
    unsigned long count,
    loff_t *offset)
{
    pvfs2_kernel_op_t *op = NULL;
    struct qhash_head *hash_link = NULL;
    void *buffer = NULL;
    void *ptr = NULL;
    unsigned long i = 0;
    static int max_downsize = MAX_ALIGNED_DEV_REQ_DOWNSIZE;
    int ret = 0, num_remaining = max_downsize;
    int notrailer_count = 4; /* number of elements in the iovec without the trailer */
    int payload_size = 0;
    int32_t magic = 0;
    int32_t proto_ver = 0;
    uint64_t tag = 0;
    ssize_t total_returned_size = 0;

    /* Either there is a trailer or there isn't */
    if (count != notrailer_count && count != (notrailer_count + 1))
    {
        gossip_err("Error: Number of iov vectors is (%ld) and notrailer count is %d\n",
                count, notrailer_count);
        return -EPROTO;
    }
    buffer = dev_req_alloc();
    if (!buffer)
    {
	return -ENOMEM;
    }
    ptr = buffer;

    for (i = 0; i < notrailer_count; i++)
    {
	if (iov[i].iov_len > num_remaining)
	{
	    gossip_err("writev error: Freeing buffer and returning\n");
	    dev_req_release(buffer);
	    return -EMSGSIZE;
	}
	ret = copy_from_user(ptr, iov[i].iov_base, iov[i].iov_len);
        if (ret)
        {
            gossip_err("Failed to copy data from user space\n");
	    dev_req_release(buffer);
            return -EIO;
        }
	num_remaining -= iov[i].iov_len;
	ptr += iov[i].iov_len;
	payload_size += iov[i].iov_len;
    }
    total_returned_size = payload_size;

    /* these elements are currently 8 byte aligned (8 bytes for (version +  
     * magic) 8 bytes for tag).  If you add another element, either make it 8
     * bytes big, or use get_unaligned when asigning  */
    ptr = buffer;
    proto_ver = *((int32_t *)ptr);
    ptr += sizeof(int32_t);

    magic = *((int32_t *)ptr);
    ptr += sizeof(int32_t);

    tag = *((uint64_t *)ptr);
    ptr += sizeof(uint64_t);

    if (magic != PVFS2_DEVREQ_MAGIC)
    {
        gossip_err("Error: Device magic number does not match.\n");
        dev_req_release(buffer);
        return -EPROTO;
    }
    if (proto_ver != PVFS_KERNEL_PROTO_VERSION)
    {
        gossip_err("Error: Device protocol version numbers do not match.\n");
        gossip_err("Please check that your pvfs2 module and pvfs2-client versions are consistent.\n");
        dev_req_release(buffer);
        return -EPROTO;
    }


    /* lookup (and remove) the op based on the tag */
    hash_link = qhash_search_and_remove(htable_ops_in_progress, &(tag));
    if (hash_link)
    {
	op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
	if (op)
	{
            /* Increase ref count! */
            get_op(op);
	    /* cut off magic and tag from payload size */
	    payload_size -= (2*sizeof(int32_t) + sizeof(uint64_t));
	    if (payload_size <= sizeof(pvfs2_downcall_t))
	    {
		/* copy the passed in downcall into the op */
		memcpy(&op->downcall, ptr, sizeof(pvfs2_downcall_t));
	    }
	    else
	    {
		gossip_debug(GOSSIP_DEV_DEBUG, "writev: Ignoring %d bytes\n", payload_size);
	    }

            /* Do not allocate needlessly if client-core forgets to reset trailer size on op errors */
            if (op->downcall.status == 0 && op->downcall.trailer_size > 0)
            {
                gossip_debug(GOSSIP_DEV_DEBUG, "writev: trailer size %ld\n", (unsigned long) op->downcall.trailer_size);
                if (count != (notrailer_count + 1))
                {
                    gossip_err("Error: trailer size (%ld) is non-zero, no trailer elements though? (%ld)\n",
                            (unsigned long) op->downcall.trailer_size, count);
                    dev_req_release(buffer);
                    put_op(op);
                    return -EPROTO;
                }
                if (iov[notrailer_count].iov_len > op->downcall.trailer_size)
                {
                    gossip_err("writev error: trailer size (%ld) != iov_len (%ld)\n",
                            (unsigned long) op->downcall.trailer_size, 
                            (unsigned long) iov[notrailer_count].iov_len);
                    dev_req_release(buffer);
                    put_op(op);
                    return -EMSGSIZE;
                }
                /* Allocate a buffer large enough to hold the trailer bytes */
                op->downcall.trailer_buf = (void *) vmalloc(op->downcall.trailer_size);
                if (op->downcall.trailer_buf != NULL) 
                {
                    gossip_debug(GOSSIP_DEV_DEBUG, "vmalloc: %p\n", op->downcall.trailer_buf);
                    ret = copy_from_user(op->downcall.trailer_buf, iov[notrailer_count].iov_base,
                            iov[notrailer_count].iov_len);
                    if (ret)
                    {
                        gossip_err("Failed to copy trailer data from user space\n");
                        dev_req_release(buffer);
                        gossip_debug(GOSSIP_DEV_DEBUG, "vfree: %p\n", op->downcall.trailer_buf);
                        vfree(op->downcall.trailer_buf);
                        op->downcall.trailer_buf = NULL;
                        put_op(op);
                        return -EIO;
                    }
                }
                else {
                    /* Change downcall status */
                    op->downcall.status = -ENOMEM;
                    gossip_err("writev: could not vmalloc for trailer!\n");
                }
            }

            /*
              if this operation is an I/O operation and if it was
              initiated on behalf of a *synchronous* VFS I/O operation,
              only then we need to wait
              for all data to be copied before we can return to avoid
              buffer corruption and races that can pull the buffers
              out from under us.

              Essentially we're synchronizing with other parts of the
              vfs implicitly by not allowing the user space
              application reading/writing this device to return until
              the buffers are done being used.
            */
            if ((op->upcall.type == PVFS2_VFS_OP_FILE_IO 
                    && op->upcall.req.io.async_vfs_io == PVFS_VFS_SYNC_IO) 
                    || op->upcall.type == PVFS2_VFS_OP_FILE_IOX)
            {
                int timed_out = 0;
                DECLARE_WAITQUEUE(wait_entry, current);
                
                /*
                 * tell the vfs op waiting on a waitqueue 
                 * that this op is done 
                 */
                spin_lock(&op->lock);
                set_op_state_serviced(op);
                spin_unlock(&op->lock);

                add_wait_queue_exclusive(
                    &op->io_completion_waitq, &wait_entry);
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
                            1000 * op_timeout_secs);
                        if (!schedule_timeout(timeout))
                        {
                            gossip_debug(GOSSIP_DEV_DEBUG, "*** I/O wait time is up\n");
                            timed_out = 1;
                            break;
                        }
                        continue;
                    }

                    gossip_debug(GOSSIP_DEV_DEBUG, "*** signal on I/O wait -- aborting\n");
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
#ifdef HAVE_AIO_VFS_SUPPORT
            else if (op->upcall.type == PVFS2_VFS_OP_FILE_IO
                    && op->upcall.req.io.async_vfs_io == PVFS_VFS_ASYNC_IO)
            {
                pvfs2_kiocb *x = (pvfs2_kiocb *) op->priv;
                if (x == NULL || x->iov == NULL 
                        || x->op != op 
                        || x->bytes_to_be_copied <= 0)
                {
                    if (x)
                    {
                        gossip_debug(GOSSIP_DEV_DEBUG, "WARNING: pvfs2_iocb from op"
                                "has invalid fields! %p, %p(%p), %d\n",
                                x->iov, x->op, op, (int) x->bytes_to_be_copied);
                    }
                    else
                    {
                        gossip_debug(GOSSIP_DEV_DEBUG, "WARNING: cannot retrieve the "
                                "pvfs2_iocb pointer from op!\n");
                    }
                    /* Most likely means that it was cancelled! */
                }
                else
                {
                    int bytes_copied;

                    if (op->downcall.status != 0)
                    {
                        ret = pvfs2_normalize_to_errno(
                                op->downcall.status);
                        bytes_copied = ret;
                    }
                    else {
                        bytes_copied = op->downcall.resp.io.amt_complete;
                    }
                    gossip_debug(GOSSIP_DEV_DEBUG, "[AIO] status of transfer: %d\n", bytes_copied);
                    if (x->rw == PVFS_IO_READ
                            && bytes_copied > 0)
                    {
                        /* try and copy it out to user-space */
                        bytes_copied = pvfs_bufmap_copy_to_user_task_iovec(
                                x->tsk,
                                x->iov, x->nr_segs,
                                x->buffer_index,
                                bytes_copied);
                    }
                    spin_lock(&op->lock);
                    /* we tell VFS that the op is now serviced! */
                    set_op_state_serviced(op);
                    gossip_debug(GOSSIP_DEV_DEBUG, "Setting state of %p to %d [SERVICED]\n",
                            op, op->op_state);
                    x->bytes_copied = bytes_copied;
                    /* call aio_complete to finish the operation to wake up regular aio waiters */
                    aio_complete(x->kiocb, x->bytes_copied, 0);
                    op->io_completed = 1;
                    /* also wake up any aio cancellers that may be waiting for us to finish the op */
                    wake_up_interruptible(&op->io_completion_waitq);
                    spin_unlock(&op->lock);
                }
                put_op(op);
            }
#endif
            else
            {
                
                /* tell the vfs op waiting on a waitqueue that this op is done */
                spin_lock(&op->lock);
                set_op_state_serviced(op);
                spin_unlock(&op->lock);
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
	gossip_debug(GOSSIP_DEV_DEBUG, "WARNING: No one's waiting for tag %llu\n", llu(tag));
    }
    dev_req_release(buffer);

    /* if we are called from aio context, just mark that the iocb is completed */
    return total_returned_size;
}

#ifdef HAVE_COMBINED_AIO_AND_VECTOR
/*
 * Kernels >= 2.6.19 have no writev, use this instead with SYNC_KEY.
 */
static ssize_t pvfs2_devreq_aio_write(struct kiocb *kiocb,
                                      const struct iovec *iov,
                                      unsigned long count, loff_t offset)
{
    return pvfs2_devreq_writev(kiocb->ki_filp, iov, count, &kiocb->ki_pos);
}
#endif

/* Returns whether any FS are still pending remounted */
static int mark_all_pending_mounts(void)
{
    int unmounted = 1;
    pvfs2_sb_info_t *pvfs2_sb = NULL;

    spin_lock(&pvfs2_superblocks_lock);
    list_for_each_entry (pvfs2_sb, &pvfs2_superblocks, list)
    {
        /* All of these file system require a remount */
        pvfs2_sb->mount_pending = 1;
        unmounted = 0;
    }
    spin_unlock(&pvfs2_superblocks_lock);
    return unmounted;
}

/* Determine if a given file system needs to be remounted or not 
 *  Returns -1 on error
 *           0 if already mounted
 *           1 if needs remount
 */
int fs_mount_pending(PVFS_fs_id fsid)
{
    int mount_pending = -1;
    pvfs2_sb_info_t *pvfs2_sb = NULL;

    spin_lock(&pvfs2_superblocks_lock);
    list_for_each_entry (pvfs2_sb, &pvfs2_superblocks, list)
    {
        if (pvfs2_sb->fs_id == fsid) 
        {
            mount_pending = pvfs2_sb->mount_pending;
            break;
        }
    }
    spin_unlock(&pvfs2_superblocks_lock);
    return mount_pending;
}

/*
  NOTE: gets called when the last reference to this device is dropped.
  Using the open_access_count variable, we enforce a reference count
  on this file so that it can be opened by only one process at a time.
  the devreq_semaphore is used to make sure all i/o has completed
  before we call pvfs_bufmap_finalize, and similar such tricky
  situations
*/
static int pvfs2_devreq_release(
    struct inode *inode,
    struct file *file)
{
    int unmounted = 0;

    gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2-client-core: exiting, closing device\n");

    down(&devreq_semaphore);
    pvfs_bufmap_finalize();

    open_access_count--;

#ifdef PVFS2_LINUX_KERNEL_2_4
/*     MOD_DEC_USE_COUNT; */
#else
    module_put(pvfs2_fs_type.owner);
#endif

    unmounted = mark_all_pending_mounts();
    gossip_debug(GOSSIP_DEV_DEBUG, "PVFS2 Device Close: Filesystem(s) %s\n",
                (unmounted ? "UNMOUNTED" : "MOUNTED"));
    /*
      prune dcache here to get rid of entries that may no longer exist
      on device re-open, assuming that the sb has been properly filled
      (may not have been if a mount wasn't attempted)
    */
    if (unmounted && inode && inode->i_sb)
    {
        shrink_dcache_sb(inode->i_sb);
    }

    up(&devreq_semaphore);

    /* Walk through the list of ops in the request list, mark them as purged and wake them up */
    purge_waiting_ops();
    /* Walk through the hash table of in progress operations; mark them as purged and wake them up */
    purge_inprogress_ops();
    gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2-client-core: device close complete\n");
    return 0;
}

int is_daemon_in_service(void)
{
    int in_service;

    /* 
     * What this function does is checks if client-core is alive based on the access
     * count we maintain on the device.
     */
    down(&devreq_semaphore);
    in_service = open_access_count == 1 ? 0 : -EIO;
    up(&devreq_semaphore);
    return in_service;
}

static inline long check_ioctl_command(unsigned int command)
{
    /* Check for valid ioctl codes */
    if (_IOC_TYPE(command) != PVFS_DEV_MAGIC) 
    {
        gossip_err("device ioctl magic numbers don't match! "
                "did you rebuild pvfs2-client-core/libpvfs2?"
                "[cmd %x, magic %x != %x]\n",
                command,
                _IOC_TYPE(command),
                PVFS_DEV_MAGIC);
        return -EINVAL;
    }
    /* and valid ioctl commands */
    if (_IOC_NR(command) >= PVFS_DEV_MAXNR || _IOC_NR(command) <= 0) 
    {
        gossip_err("Invalid ioctl command number [%d >= %d]\n",
                _IOC_NR(command), PVFS_DEV_MAXNR);
        return -ENOIOCTLCMD;
    }
    return 0;
}

static long dispatch_ioctl_command(unsigned int command, unsigned long arg)
{
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    static int32_t max_up_size = MAX_ALIGNED_DEV_REQ_UPSIZE;
    static int32_t max_down_size = MAX_ALIGNED_DEV_REQ_DOWNSIZE;
    struct PVFS_dev_map_desc user_desc;

    switch(command)
    {
        case PVFS_DEV_GET_MAGIC:
            return ((put_user(magic, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0);
        case PVFS_DEV_GET_MAX_UPSIZE:
            return ((put_user(max_up_size, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0);
        case PVFS_DEV_GET_MAX_DOWNSIZE:
            return ((put_user(max_down_size, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0);
        case PVFS_DEV_MAP:
        {
            int ret;
            ret = copy_from_user(
                &user_desc, (struct PVFS_dev_map_desc __user *)arg,
                sizeof(struct PVFS_dev_map_desc));
            return (ret ? -EIO : pvfs_bufmap_initialize(&user_desc));
        }
        case PVFS_DEV_REMOUNT_ALL:
        {
            int ret = 0;
            struct list_head *tmp = NULL;
            pvfs2_sb_info_t *pvfs2_sb = NULL;

            gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2_devreq_ioctl: got PVFS_DEV_REMOUNT_ALL\n");

            /*
              remount all mounted pvfs2 volumes to regain the lost
              dynamic mount tables (if any) -- NOTE: this is done
              without keeping the superblock list locked due to the
              upcall/downcall waiting.  also, the request semaphore is
              used to ensure that no operations will be serviced until
              all of the remounts are serviced (to avoid ops between
              mounts to fail)
            */
            ret = down_interruptible(&request_semaphore);
            if(ret < 0)
            {
                return(ret);
            }
            gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2_devreq_ioctl: priority remount "
                        "in progress\n");
            list_for_each(tmp, &pvfs2_superblocks) {
                pvfs2_sb = list_entry(tmp, pvfs2_sb_info_t, list);
                if (pvfs2_sb && (pvfs2_sb->sb))
                {
                    gossip_debug(GOSSIP_DEV_DEBUG, "Remounting SB %p\n", pvfs2_sb);

                    ret = pvfs2_remount(pvfs2_sb->sb, NULL,
                                        pvfs2_sb->data);
                    if (ret)
                    {
                        gossip_debug(GOSSIP_DEV_DEBUG, "Failed to remount SB %p\n", pvfs2_sb);
                        break;
                    }
                }
            }
            gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2_devreq_ioctl: priority remount "
                        "complete\n");
            up(&request_semaphore);
            return ret;
        }
        case PVFS_DEV_DEBUG:
            return (get_user(gossip_debug_mask, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0;
        break;
    default:
	return -ENOIOCTLCMD;
    }
    return -ENOIOCTLCMD;
}

static int pvfs2_devreq_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int command,
    unsigned long arg)
{
    long ret;

    /* Check for properly constructed commands */
    if ((ret = check_ioctl_command(command)) < 0)
    {
        return (int) ret;
    }
    return (int) dispatch_ioctl_command(command, arg);
}

#ifdef CONFIG_COMPAT

#if defined(HAVE_COMPAT_IOCTL_HANDLER) || defined(HAVE_REGISTER_IOCTL32_CONVERSION)

/*  Compat structure for the PVFS_DEV_MAP ioctl */
struct PVFS_dev_map_desc32 
{
    compat_uptr_t ptr;
    int32_t      total_size;
    int32_t      size;
    int32_t      count;
};

#ifndef PVFS2_LINUX_KERNEL_2_4
static unsigned long translate_dev_map26(
        unsigned long args, long *error)
{
    struct PVFS_dev_map_desc32  __user *p32 = (void __user *) args;
    /* Depending on the architecture, allocate some space on the user-call-stack based on our expected layout */
    struct PVFS_dev_map_desc    __user *p   = compat_alloc_user_space(sizeof(*p));
    u32    addr;

    *error = 0;
    /* get the ptr from the 32 bit user-space */
    if (get_user(addr, &p32->ptr))
        goto err;
    /* try to put that into a 64-bit layout */
    if (put_user(compat_ptr(addr), &p->ptr))
        goto err;
    /* copy the remaining fields */
    if (copy_in_user(&p->total_size, &p32->total_size, sizeof(int32_t)))
        goto err;
    if (copy_in_user(&p->size, &p32->size, sizeof(int32_t)))
        goto err;
    if (copy_in_user(&p->count, &p32->count, sizeof(int32_t)))
        goto err;
    return (unsigned long) p;
err:
    *error = -EFAULT;
    return 0;
}
#else
static unsigned long translate_dev_map24(
        unsigned long args, struct PVFS_dev_map_desc *p, long *error)
{
    struct PVFS_dev_map_desc32  __user *p32 = (void __user *) args;
    u32 addr, size, total_size, count;

    *error = 0;
    /* get the ptr from the 32 bit user-space */
    if (get_user(addr, &p32->ptr))
        goto err;
    p->ptr = compat_ptr(addr);
    /* copy the remaining fields */
    if (get_user(total_size, &p32->total_size))
        goto err;
    if (get_user(size, &p32->size))
        goto err;
    if (get_user(count, &p32->count))
        goto err;
    p->total_size = total_size;
    p->size = size;
    p->count = count;
    return 0;
err:
    *error = -EFAULT;
    return 0;
}
#endif

#endif

#ifdef HAVE_COMPAT_IOCTL_HANDLER
/*
 * 32 bit user-space apps' ioctl handlers when kernel modules
 * is compiled as a 64 bit one
 */
static long pvfs2_devreq_compat_ioctl(
        struct file *filp, unsigned int cmd, unsigned long args)
{
    long ret;
    unsigned long arg = args;

    /* Check for properly constructed commands */
    if ((ret = check_ioctl_command(cmd)) < 0)
    {
        return ret;
    }
    if (cmd == PVFS_DEV_MAP)
    {
        /* convert the arguments to what we expect internally in kernel space */
        arg = translate_dev_map26(args, &ret);
        if (ret < 0)
        {
            gossip_err("Could not translate dev map\n");
            return ret;
        }
    }
    /* no other ioctl requires translation */
    return dispatch_ioctl_command(cmd, arg);
}

#endif

#ifdef HAVE_REGISTER_IOCTL32_CONVERSION

#ifndef PVFS2_LINUX_KERNEL_2_4

static int pvfs2_translate_dev_map(
        unsigned int fd,
        unsigned int cmd,
        unsigned long arg,
        struct   file *file)
{
    long ret;
    unsigned long p;

    /* Copy it as the kernel module expects it */
    p = translate_dev_map26(arg, &ret);
    if (ret < 0)
    {
        gossip_err("Could not translate dev map structure\n");
        return ret;
    }
    /* p is still a user space address */
    return sys_ioctl(fd, cmd, p);
}

#else

static int pvfs2_translate_dev_map(
        unsigned int fd,
        unsigned int cmd,
        unsigned long arg,
        struct   file *file)
{
    long ret;
    struct PVFS_dev_map_desc p;

    translate_dev_map24(arg, &p, &ret);
    /* p is a kernel space address */
    return (ret ? -EIO : pvfs_bufmap_initialize(&p));
}

#endif

#ifdef PVFS2_LINUX_KERNEL_2_4
typedef int (*ioctl_fn)(unsigned int fd,
        unsigned int cmd, unsigned long arg, struct   file *file);

struct ioctl_trans {
    unsigned int cmd;
    ioctl_fn     handler;
};
#endif

static struct ioctl_trans pvfs2_ioctl32_trans[] = {
    {PVFS_DEV_GET_MAGIC,        NULL},
    {PVFS_DEV_GET_MAX_UPSIZE,   NULL},
    {PVFS_DEV_GET_MAX_DOWNSIZE, NULL},
    {PVFS_DEV_MAP,              pvfs2_translate_dev_map},
    {PVFS_DEV_REMOUNT_ALL,      NULL},
    {PVFS_DEV_DEBUG,            NULL},
    /* Please add stuff above this line and retain the entry below */
    {0, },
};

/* Must be called on module load */
static int pvfs2_ioctl32_init(void)
{
    int i, error;

    for (i = 0;  pvfs2_ioctl32_trans[i].cmd != 0; i++)
    {
        error = register_ioctl32_conversion(
                    pvfs2_ioctl32_trans[i].cmd, pvfs2_ioctl32_trans[i].handler);
        if (error) 
            goto fail;
        gossip_debug(GOSSIP_DEV_DEBUG, "Registered ioctl32 command %08x with handler %p\n",
                (unsigned int) pvfs2_ioctl32_trans[i].cmd, pvfs2_ioctl32_trans[i].handler);
    }
    return 0;
fail:
    while (--i)
        unregister_ioctl32_conversion(pvfs2_ioctl32_trans[i].cmd);
    return error;
}

/* Must be called on module unload */
static void pvfs2_ioctl32_cleanup(void)
{
    int i;
    for (i = 0;  pvfs2_ioctl32_trans[i].cmd != 0; i++)
    {
        gossip_debug(GOSSIP_DEV_DEBUG, "Deregistered ioctl32 command %08x\n",
               (unsigned int) pvfs2_ioctl32_trans[i].cmd);
        unregister_ioctl32_conversion(pvfs2_ioctl32_trans[i].cmd);
    }
}

#endif /* end HAVE_REGISTER_IOCTL32_CONVERSION */

#endif /* CONFIG_COMPAT */

#if (defined(CONFIG_COMPAT) && !defined(HAVE_REGISTER_IOCTL32_CONVERSION)) || !defined(CONFIG_COMPAT)
static int pvfs2_ioctl32_init(void)
{
    return 0;
}

static void pvfs2_ioctl32_cleanup(void)
{
    return;
}
#endif

/* the assigned character device major number */
static int pvfs2_dev_major = 0;
/* Initialize pvfs2 device specific state: 
 * Must be called at module load time only 
 */
int pvfs2_dev_init(void)
{
    int ret;

    /* register the ioctl32 sub-system */
    if ((ret = pvfs2_ioctl32_init()) < 0) {
        return ret;
    }
    /* register pvfs2-req device  */
    pvfs2_dev_major = register_chrdev(0, PVFS2_REQDEVICE_NAME,
                                      &pvfs2_devreq_file_operations);
    if (pvfs2_dev_major < 0)
    {
	gossip_debug(GOSSIP_INIT_DEBUG, "Failed to register /dev/%s (error %d)\n",
		    PVFS2_REQDEVICE_NAME, pvfs2_dev_major);
        pvfs2_ioctl32_cleanup();
        return pvfs2_dev_major;
    }
#ifdef HAVE_KERNEL_DEVICE_CLASSES
    pvfs2_dev_class = class_create(THIS_MODULE, "pvfs2");
    if (IS_ERR(pvfs2_dev_class)) {
        pvfs2_ioctl32_cleanup();
        unregister_chrdev(pvfs2_dev_major, PVFS2_REQDEVICE_NAME);
        ret = PTR_ERR(pvfs2_dev_class);
        return ret;
    }
    class_device_create(pvfs2_dev_class, NULL,
                        MKDEV(pvfs2_dev_major, 0), NULL,
                        PVFS2_REQDEVICE_NAME);
#endif

    gossip_debug(GOSSIP_INIT_DEBUG, "*** /dev/%s character device registered ***\n",
		PVFS2_REQDEVICE_NAME);
    gossip_debug(GOSSIP_INIT_DEBUG, "'mknod /dev/%s c %d 0'.\n", PVFS2_REQDEVICE_NAME,
                pvfs2_dev_major);
    return 0;
}

void pvfs2_dev_cleanup(void)
{
#ifdef HAVE_KERNEL_DEVICE_CLASSES
    class_device_destroy(pvfs2_dev_class, MKDEV(pvfs2_dev_major, 0));
    class_destroy(pvfs2_dev_class);
#endif
    unregister_chrdev(pvfs2_dev_major, PVFS2_REQDEVICE_NAME);
    gossip_debug(GOSSIP_INIT_DEBUG, "*** /dev/%s character device unregistered ***\n",
            PVFS2_REQDEVICE_NAME);
    /* unregister the ioctl32 sub-system */
    pvfs2_ioctl32_cleanup();
    return;
}

static unsigned int pvfs2_devreq_poll(
    struct file *file,
    struct poll_table_struct *poll_table)
{
    int poll_revent_mask = 0;

    if (open_access_count == 1)
    {
        poll_wait(file, &pvfs2_request_list_waitq, poll_table);

        spin_lock(&pvfs2_request_list_lock);
        if (!list_empty(&pvfs2_request_list))
        {
            poll_revent_mask |= POLL_IN;
        }
        spin_unlock(&pvfs2_request_list_lock);
    }
    return poll_revent_mask;
}

struct file_operations pvfs2_devreq_file_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    owner: THIS_MODULE,
    read : pvfs2_devreq_read,
    writev : pvfs2_devreq_writev,
    open : pvfs2_devreq_open,
    release : pvfs2_devreq_release,
    ioctl : pvfs2_devreq_ioctl,
    poll : pvfs2_devreq_poll
#else
    .read = pvfs2_devreq_read,
#ifdef HAVE_COMBINED_AIO_AND_VECTOR
    .aio_write = pvfs2_devreq_aio_write,
#else
    .writev = pvfs2_devreq_writev,
#endif
    .open = pvfs2_devreq_open,
    .release = pvfs2_devreq_release,
    .ioctl = pvfs2_devreq_ioctl,
#ifdef CONFIG_COMPAT
#ifdef HAVE_COMPAT_IOCTL_HANDLER
    .compat_ioctl = pvfs2_devreq_compat_ioctl,
#endif
#endif
    .poll = pvfs2_devreq_poll
#endif
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

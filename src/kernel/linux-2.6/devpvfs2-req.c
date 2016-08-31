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
#include "pint-dev.h"

/* these functions are defined in pvfs2-utils.c */
int PVFS_proc_kmod_mask_to_eventlog(uint64_t mask, char *debug_string);
int PVFS_proc_mask_to_eventlog(uint64_t mask, char *debug_string);

/*these variables are defined in pvfs2-proc.c*/
extern char kernel_debug_string[PVFS2_MAX_DEBUG_STRING_LEN];
extern char client_debug_string[PVFS2_MAX_DEBUG_STRING_LEN];

/*these variables are defined in pvfs2-mod.c*/
extern unsigned int kernel_mask_set_mod_init;

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
    uint64_t tag;
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    int32_t proto_ver = PVFS_KERNEL_PROTO_VERSION;
    PVFS_fs_id fs_id;
    pvfs2_kernel_op_t *op=NULL, *temp=NULL;

    if (!(file->f_flags & O_NONBLOCK))
    {
        /* We do not support blocking reads/opens any more */
        gossip_err("pvfs2: blocking reads are not supported! (pvfs2-client-core bug)\n");
        return -EINVAL;
    }
    else
    {
        /* get next op (if any) from top of list */
        spin_lock(&pvfs2_request_list_lock);
        list_for_each_entry_safe (op, temp, &pvfs2_request_list, list)
        {
            spin_lock(&op->lock);

            tag = 0;
            cur_op = NULL;

            fs_id = fsid_of_op(op);
            /* Check if this op's fsid is known and needs remounting */
            if (fs_id != PVFS_FS_ID_NULL && fs_mount_pending(fs_id) == 1)
            {
                spin_unlock(&op->lock);
                gossip_debug(GOSSIP_DEV_DEBUG, "Skipping op tag %llu %s\n", llu(op->tag), get_opname_string(op));
                continue;
            }
            /* op does not belong to any particular fsid or already 
             * mounted.. let it through
             */
            else {
                cur_op = op;
                if ( !op_state_waiting(op) )
                {
                   spin_unlock(&op->lock);
                   continue; /*check next op*/
                }
                tag = op->tag;
                list_del(&op->list);
                op->op_linger_tmp--;
                /* if there is a trailer, re-add it to the request list */
                if (op->op_linger == 2 && op->op_linger_tmp == 1)
                {
                    /* re-add it to the head of the list */
                    list_add(&op->list, &pvfs2_request_list);

                    if (op->upcall.trailer_size <= 0 || op->upcall.trailer_buf == NULL)
                    {
                       spin_unlock(&op->lock);
                       gossip_err("BUG:trailer_size is %ld and trailer buf is %p\n",
                               (long) cur_op->upcall.trailer_size, cur_op->upcall.trailer_buf);
                       break;
                    }
                }
                spin_unlock(&op->lock);
                break;
            }
        }
        spin_unlock(&pvfs2_request_list_lock);
    }

    if (cur_op)
    {
        gossip_debug(GOSSIP_DEV_DEBUG, "%s : client-core: reading op tag %llu %s\n"
                                     , __func__, llu(cur_op->tag), get_opname_string(cur_op));

        spin_lock(&cur_op->lock);

        if ( ! (op_state_waiting(cur_op) && cur_op->tag == tag) )
        {
           spin_unlock(&cur_op->lock);
           gossip_err("%s : WARNING : Op is not in WAITING state but in state(%d); tag (%llu) should be (%llu)\n"
                     ,__func__,cur_op->op_state,llu(cur_op->tag),llu(tag));
           len = -EAGAIN;
           goto exit_pvfs2_devreq_read;
        }


        if ( cur_op->op_linger == 1 ||
           ( cur_op->op_linger == 2 && cur_op->op_linger_tmp == 0 ) ) 
        {
            set_op_state_inprogress(cur_op);

            /* atomically move the operation to the htable_ops_in_progress */
            qhash_add(htable_ops_in_progress, (void *) &(cur_op->tag), &cur_op->list);
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

exit_pvfs2_devreq_read:

    return len;
}

#ifndef HAVE_IOV_ITER
/*
 * Old-fashioned (non iov_iter) function for writev() callers into the device.
 *
 * Userspace should have written:
 *  - __u32 version                      iov[0]
 *  - __u32 magic                        iov[1]
 *  - __u64 tag                          iov[2]
 *  - struct orangefs_downcall_s         iov[3]
 *  - trailer buffer (READDIR op only)   iov[4]
 */
static ssize_t pvfs2_devreq_aio_write(struct kiocb *kiocb,
                                      const struct iovec *iov,
                                      unsigned long count,
                                      loff_t offset)
{
    int i;
    int ret = 0;
    int total = 0;
    struct {
      __u32 version;
      __u32 magic;
      __u64 tag;
    } head  = { 0 ,0, 0 };
    int head_size = sizeof(head);
    int bad_iov_len = 0;
    int bad_iov_copy = 0;
    static char *head_parts_names[] = {
      "version",
      "magic",
      "tag"
    };
    int downcall_size = sizeof(pvfs2_downcall_t);
    int downcall_iovec = 3;
    int trailer_iovec = 4;

    struct qhash_head *hash_link = NULL;
    pvfs2_kernel_op_t *op = NULL;

    gossip_debug(GOSSIP_DEV_DEBUG, "%s: count:%lu:\n", __func__, count);

    /*
     * Grind through the iovec array, make sure it is at least
     * as big as it should be and fetch out version, magic and teg
     * on the way through...
     */
    for (i = 0; i < count; i++) {

      switch (i) {

      case 0:

        if (iov[i].iov_len != sizeof(head.version))
          bad_iov_len = 1;
	else
          bad_iov_copy = copy_from_user(&(head.version),
                                        (__u32 __user *)(iov[i].iov_base),
                                        iov[i].iov_len);
        break;

      case 1:

        if (iov[i].iov_len != sizeof(head.magic))
          bad_iov_len = 1;
	else
          bad_iov_copy = copy_from_user(&(head.magic),
                                        (__u32 __user *)(iov[i].iov_base),
                                        iov[i].iov_len);
        break;

      case 2:

        if (iov[i].iov_len != sizeof(head.tag))
          bad_iov_len = 1;
	else
          bad_iov_copy = copy_from_user(&(head.tag),
                                        (__u64 __user *)(iov[i].iov_base),
                                        iov[i].iov_len);
        break;

      case 3:

        if (iov[i].iov_len != downcall_size)
          bad_iov_len = 1;
        break;

      default:
        break;
      }

      if (bad_iov_len) {
        gossip_err("%s: bad iov_len for %s, bailing.\n",
                   __func__,
                   head_parts_names[i]);
        ret = -EFAULT;
        goto out;
      }

      if (bad_iov_copy) {
        gossip_err("%s: failed copy_from_user for %s, bailing.\n",
                   __func__,
                   head_parts_names[i]);
        ret = -EFAULT;
        goto out;
      }

      total = total + iov[i].iov_len;
    }

    ret = total;

    if (total < MAX_DEV_REQ_DOWNSIZE) {
      gossip_err("%s: total:%d: must be at least:%u:\n",
                 __func__,
                 total,
                 (unsigned int) MAX_DEV_REQ_DOWNSIZE);
      ret = -EFAULT;
      goto out;
    }

    if (head.magic != PVFS2_DEVREQ_MAGIC) {
      gossip_err("%s: bad magic.\n", __func__);
      ret = -EFAULT;
      goto out;
    }

    if (head.version != PVFS_KERNEL_PROTO_VERSION) {
      gossip_err("%s: kernel module and userspace versions do not match.\n",
                 __func__);
      ret = -EFAULT;
      goto out;
    }

    hash_link = qhash_search_and_remove(htable_ops_in_progress, &(head.tag));
    if (!hash_link) {
      gossip_err("WARNING: No one's waiting for tag %llu\n", llu(head.tag));
      goto out;
    }

    op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
    if (!op) {
      gossip_err("%s: got hash link, but no op.\n", __func__);
      ret = -EPROTO;
      goto out;
    }

    get_op(op); /* increase ref count. */

    if (copy_from_user(&op->downcall,
                      (pvfs2_downcall_t __user *)(iov[downcall_iovec].iov_base),
                      iov[downcall_iovec].iov_len)) {
      gossip_err("%s: failed to copy downcall.\n", __func__);
      put_op(op);
      ret = -EFAULT;
      goto out;
    }

    if (op->downcall.status)
      goto wakeup;

    /*
     * We've successfully peeled off the head and the downcall.
     * Something has gone awry if total doesn't equal the
     * sum of head_size, downcall_size and trailer_size.
     */
    if ((head_size + downcall_size + op->downcall.trailer_size) != total) {
      gossip_err("%s: funky write, head_size:%d: downcall_size:%d: "
                 "trailer_size:%lld: total size:%d:\n",
                 __func__,
                 head_size,
                 downcall_size,
                 op->downcall.trailer_size,
                 total);
      put_op(op);
      ret = -EFAULT;
      goto out;
    }

    /* Only READDIR operations should have trailers. */
    if ((op->downcall.type != PVFS2_VFS_OP_READDIR) &&
        (op->downcall.trailer_size != 0)) {
            gossip_err("%s: %x operation with trailer.",
                       __func__,
                       op->downcall.type);
            put_op(op);
            ret = -EFAULT;
            goto out;
    }

    /* READDIR operations should always have trailers. */
    if ((op->downcall.type == PVFS2_VFS_OP_READDIR) &&
        (op->downcall.trailer_size == 0)) {
      gossip_err("%s: %x operation with no trailer.",
                 __func__,
                 op->downcall.type);
      put_op(op);
      ret = -EFAULT;
      goto out;
    }

    if (op->downcall.type != PVFS2_VFS_OP_READDIR)
      goto wakeup;

    op->downcall.trailer_buf = vmalloc(op->downcall.trailer_size);
    if (op->downcall.trailer_buf == NULL) {
      gossip_err("%s: failed trailer vmalloc.\n", __func__);
      put_op(op);
      ret = -ENOMEM;
      goto out;
    }
    memset(op->downcall.trailer_buf, 0, op->downcall.trailer_size);

    if (copy_from_user(op->downcall.trailer_buf,
                       (char __user *)(iov[trailer_iovec].iov_base),
                       iov[trailer_iovec].iov_len)) {
      gossip_err("%s: failed to copy trailer.\n", __func__);
      vfree(op->downcall.trailer_buf);
      put_op(op);
      ret = -EFAULT;
      goto out;
    }

wakeup:

    /*
     * If this operation is an I/O operation we need to wait
     * for all data to be copied before we can return to avoid
     * buffer corruption and races that can pull the buffers
     * out from under us.
     *
     * Essentially we're synchronizing with other parts of the
     * vfs implicitly by not allowing the user space
     * application reading/writing this device to return until
     * the buffers are done being used.
     */
    if (op->downcall.type == PVFS2_VFS_OP_FILE_IO) {
      int timed_out = 0;
      DECLARE_WAITQUEUE(wait_entry, current);
    
      /*
       * tell the vfs op waiting on a waitqueue
       * that this op is done
       */
      spin_lock(&op->lock);
      set_op_state_serviced(op);
      spin_unlock(&op->lock);
    
      add_wait_queue_exclusive(&op->io_completion_waitq, &wait_entry);
      wake_up_interruptible(&op->waitq);
    
      while (1) {
        set_current_state(TASK_INTERRUPTIBLE);

        spin_lock(&op->lock);
        if (op->io_completed) {
          spin_unlock(&op->lock);
          break;
        }
        spin_unlock(&op->lock);
    
        if (!signal_pending(current)) {
          int timeout = MSECS_TO_JIFFIES(1000 * op_timeout_secs);
          if (!schedule_timeout(timeout)) {
            gossip_debug(GOSSIP_DEV_DEBUG, "%s: timed out.\n", __func__);
            timed_out = 1;
            break;
          }
          continue;
        }
 
        gossip_debug(GOSSIP_DEV_DEBUG,
                     "%s: signal on I/O wait, aborting\n",
                     __func__);
        break;
      }

      set_current_state(TASK_RUNNING);
      remove_wait_queue(&op->io_completion_waitq, &wait_entry);
    
      /* NOTE: for I/O operations we handle releasing the op
       * object except in the case of timeout.  the reason we
       * can't free the op in timeout cases is that the op
       * service logic in the vfs retries operations using
       * the same op ptr, thus it can't be freed.
       */
      if (!timed_out)
        op_release(op);
    } else {
      /*
       * tell the vfs op waiting on a waitqueue that
       * this op is done
       */
      spin_lock(&op->lock);
      set_op_state_serviced(op);
      spin_unlock(&op->lock);
      /*
       * for every other operation (i.e. non-I/O), we need to
       * wake up the callers for downcall completion
       * notification
       */
      wake_up_interruptible(&op->waitq);
    }

out:

    return ret;
}
#endif

#ifdef HAVE_IOV_ITER
/*
 * Function for writev() callers into the device.
 *
 * Userspace should have written:
 *  - __u32 version
 *  - __u32 magic
 *  - __u64 tag
 *  - struct orangefs_downcall_s
 *  - trailer buffer (in the case of READDIR operations)
 */
static ssize_t pvfs2_devreq_write_iter(struct kiocb *iocb,
                                      struct iov_iter *iter)
{
	ssize_t ret;
	pvfs2_kernel_op_t *op = NULL;
	struct {
		__u32 version;
		__u32 magic;
		__u64 tag;
	} head;
	int total = ret = iov_iter_count(iter);
	int n;
	int downcall_size = sizeof(pvfs2_downcall_t);
	int head_size = sizeof(head);
	struct qhash_head *hash_link = NULL;

	gossip_debug(GOSSIP_DEV_DEBUG, "%s: total:%d: ret:%zd:\n",
		     __func__,
		     total,
		     ret);

        if (total < MAX_DEV_REQ_DOWNSIZE) {
		gossip_err("%s: total:%d: must be at least:%lu:\n",
			   __func__,
			   total,
			   MAX_DEV_REQ_DOWNSIZE);
		ret = -EFAULT;
		goto out;
	}
     
	n = copy_from_iter(&head, head_size, iter);
	if (n < head_size) {
		gossip_err("%s: failed to copy head.\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	gossip_debug(GOSSIP_DEV_DEBUG,
		     "%s: userspace claims version:%d:\n",
		     __func__,
		     head.version);

	if (head.magic != PVFS2_DEVREQ_MAGIC) {
		gossip_err("Error: Device magic number does not match.\n");
		ret = -EPROTO;
		goto out;
	}

	hash_link =
		qhash_search_and_remove(htable_ops_in_progress, &(head.tag));
	if (!hash_link) {
		gossip_err("WARNING: No one's waiting for tag %llu\n",
			   llu(head.tag));
		goto out;
	}

	op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
	if (!op) {
		gossip_err("%s: got hash link, but no op.\n", __func__);
		ret = -EPROTO;
		goto out;
	}

	get_op(op); /* increase ref count. */

	n = copy_from_iter(&op->downcall, downcall_size, iter);
	if (n != downcall_size) {
		gossip_err("%s: failed to copy downcall.\n", __func__);
		put_op(op);
		ret = -EFAULT;
		goto out;
	}

	if (op->downcall.status)
		goto wakeup;

	/*
	 * We've successfully peeled off the head and the downcall. 
	 * Something has gone awry if total doesn't equal the
	 * sum of head_size, downcall_size and trailer_size.
	 */
	if ((head_size + downcall_size + op->downcall.trailer_size) != total) {
		gossip_err("%s: funky write, head_size:%d"
			   ": downcall_size:%d: trailer_size:%lld"
			   ": total size:%d:\n",
			   __func__,
			   head_size,
			   downcall_size,
			   op->downcall.trailer_size,
			   total);
		put_op(op);
		ret = -EFAULT;
		goto out;
	}

	/* Only READDIR operations should have trailers. */
	if ((op->downcall.type != PVFS2_VFS_OP_READDIR) &&
	    (op->downcall.trailer_size != 0)) {
		gossip_err("%s: %x operation with trailer.",
			   __func__,
			   op->downcall.type);
		put_op(op);
		ret = -EFAULT;
		goto out;
	}

	/* READDIR operations should always have trailers. */
	if ((op->downcall.type == PVFS2_VFS_OP_READDIR) &&
	    (op->downcall.trailer_size == 0)) {
		gossip_err("%s: %x operation with no trailer.",
			   __func__,
			   op->downcall.type);
		put_op(op);
		ret = -EFAULT;
		goto out;
	}

	if (op->downcall.type != PVFS2_VFS_OP_READDIR)
		goto wakeup;

	op->downcall.trailer_buf =
		vmalloc(op->downcall.trailer_size);
	if (op->downcall.trailer_buf == NULL) {
		gossip_err("%s: failed trailer vmalloc.\n",
			   __func__);
		put_op(op);
		ret = -ENOMEM;
		goto out;
	}
	memset(op->downcall.trailer_buf, 0, op->downcall.trailer_size);
	n = copy_from_iter(op->downcall.trailer_buf,
			   op->downcall.trailer_size,
			   iter);
	if (n != op->downcall.trailer_size) {
		gossip_err("%s: failed to copy trailer.\n", __func__);
		vfree(op->downcall.trailer_buf);
		put_op(op);
		ret = -EFAULT;
		goto out;
	}

wakeup:

	/*
	 * If this operation is an I/O operation we need to wait
	 * for all data to be copied before we can return to avoid
	 * buffer corruption and races that can pull the buffers
	 * out from under us.
	 *
	 * Essentially we're synchronizing with other parts of the
	 * vfs implicitly by not allowing the user space
	 * application reading/writing this device to return until
	 * the buffers are done being used.
	 */
	if (op->downcall.type == PVFS2_VFS_OP_FILE_IO) {
		int timed_out = 0;
		DEFINE_WAIT(wait_entry);

		/*
		 * tell the vfs op waiting on a waitqueue
		 * that this op is done
		 */
		spin_lock(&op->lock);
		set_op_state_serviced(op);
		spin_unlock(&op->lock);

		wake_up_interruptible(&op->waitq);

		while (1) {
			spin_lock(&op->lock);
			prepare_to_wait_exclusive(
				&op->io_completion_waitq,
				&wait_entry,
				TASK_INTERRUPTIBLE);
			if (op->io_completed) {
				spin_unlock(&op->lock);
				break;
			}
			spin_unlock(&op->lock);

			if (!signal_pending(current)) {
				int timeout =
				    MSECS_TO_JIFFIES(1000 *
						     op_timeout_secs);
				if (!schedule_timeout(timeout)) {
					gossip_debug(GOSSIP_DEV_DEBUG,
						"%s: timed out.\n",
						__func__);
					timed_out = 1;
					break;
				}
				continue;
			}

			gossip_debug(GOSSIP_DEV_DEBUG,
				"%s: signal on I/O wait, aborting\n",
				__func__);
			break;
		}

		spin_lock(&op->lock);
		finish_wait(&op->io_completion_waitq, &wait_entry);
		spin_unlock(&op->lock);

		/* NOTE: for I/O operations we handle releasing the op
		 * object except in the case of timeout.  the reason we
		 * can't free the op in timeout cases is that the op
		 * service logic in the vfs retries operations using
		 * the same op ptr, thus it can't be freed.
		 */
		if (!timed_out)
			op_release(op);
	} else {
		/*
		 * tell the vfs op waiting on a waitqueue that
		 * this op is done
		 */
		spin_lock(&op->lock);
		set_op_state_serviced(op);
		spin_unlock(&op->lock);
		/*
		 * for every other operation (i.e. non-I/O), we need to
		 * wake up the callers for downcall completion
		 * notification
		 */
		wake_up_interruptible(&op->waitq);
	}
out:
	return ret;
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

    gossip_debug(GOSSIP_DEV_DEBUG, "%s:pvfs2-client-core: exiting, closing device\n",__func__);

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
    int ret;
    dev_mask_info_t mask_info = {0};
    int upstream_kmod = 0;

    /* mtmoore: add locking here */

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
                    gossip_debug(GOSSIP_DEV_DEBUG,
                                 "Remounting SB %p\n", pvfs2_sb);

                    ret = pvfs2_remount(pvfs2_sb->sb, NULL,
                                        pvfs2_sb->data);
                    if (ret)
                    {
                        gossip_debug(GOSSIP_DEV_DEBUG,
                                     "Failed to remount SB %p\n", pvfs2_sb);
                        break;
                    }
                }
            }
            gossip_debug(GOSSIP_DEV_DEBUG, "pvfs2_devreq_ioctl: priority remount "
                        "complete\n");
            up(&request_semaphore);
            return ret;
        }

        case PVFS_DEV_UPSTREAM:
                ret = copy_to_user((void __user *)arg,
                                    &upstream_kmod,
                                    sizeof(upstream_kmod));

                if (ret != 0)
                        return -EIO;
                else
                        return ret;

        case PVFS_DEV_DEBUG:
            ret = copy_from_user(&mask_info, (void __user *)arg
                                ,sizeof(mask_info));
            if (ret != 0)
               return(-EIO);

            if (mask_info.mask_type == KERNEL_MASK)
            {
               if ( (mask_info.mask_value == 0) && (kernel_mask_set_mod_init) )
               {
                   /* the kernel debug mask was set when the kernel module was loaded;
                    * don't override it if the client-core was started without a value
                    * for PVFS2_KMODMASK.
                   */
                   return(0);
               }
               ret = PVFS_proc_kmod_mask_to_eventlog(mask_info.mask_value
                                                    ,kernel_debug_string);
               gossip_debug_mask = mask_info.mask_value;
               printk("PVFS: kernel debug mask has been modified to \"%s\" (0x%08llx)\n"
                     ,kernel_debug_string,llu(gossip_debug_mask));
            } 
            else if (mask_info.mask_type == CLIENT_MASK)
            {
               ret = PVFS_proc_mask_to_eventlog(mask_info.mask_value
                                               ,client_debug_string);
               printk("PVFS: client debug mask has been modified to \"%s\" (0x%08llx)\n"
                     ,client_debug_string,llu(mask_info.mask_value));
            } 
            else
            {
              gossip_lerr("Invalid mask type....\n");
              return(-EINVAL);
            }
            
            return(ret);
        break;
    default:
        return -ENOIOCTLCMD;
    }
    return -ENOIOCTLCMD;
}

#ifdef HAVE_UNLOCKED_IOCTL_HANDLER
static long pvfs2_devreq_ioctl(
#else
static int pvfs2_devreq_ioctl(
    struct inode *inode,
#endif /* HAVE_UNLOCKED_IOCTL_HANDLER */
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
    {PVFS_DEV_MAXNR,            NULL},
    {PVFS_DEV_DEBUG2,           NULL},
    {PVFS_DEV_UPSTREAM,         NULL},
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
#ifdef HAVE_IOV_ITER
    .write_iter = pvfs2_devreq_write_iter,
#else
    .aio_write = pvfs2_devreq_aio_write,
#endif
    .open = pvfs2_devreq_open,
    .release = pvfs2_devreq_release,
#ifdef HAVE_UNLOCKED_IOCTL_HANDLER
    .unlocked_ioctl = pvfs2_devreq_ioctl,
#else
    .ioctl = pvfs2_devreq_ioctl,
#endif /* HAVE_UNLOCKED_IOCTL_HANDLER */

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

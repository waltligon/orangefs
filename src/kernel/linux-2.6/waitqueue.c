/*
 * (C) 2001 Clemson University and The University of Chicago
 * 
 * Changes by Acxiom Corporation to implement generic service_operation()
 * function, Copyright © Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  In-kernel waitqueue operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-internal.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern struct qhash_table *htable_ops_in_progress;
extern int debug;
extern int op_timeout_secs;
extern wait_queue_head_t pvfs2_request_list_waitq;

/**
 * submits a PVFS2 operation and waits for it to complete
 *
 * \note op->downcall.status will contain the status of the operation (in
 * errno format), whether provided by pvfs2-client or a result of failure to
 * service the operation.  If the caller wishes to distinguish, then
 * op->state can be checked to see if it was serviced or not.
 *
 * \returns contents of op->downcall.status for convenience
 */
int service_operation(
    pvfs2_kernel_op_t* op,  /**< operation structure to process */
    const char* op_name,    /**< string name for operation */
    int num_retries,        /**< number of times to retry (may be zero) */
    int flags)               /**< flags to modify behavior */
{
    sigset_t orig_sigset; 
    int ret = 0;

    pvfs2_print("pvfs2: service_operation: %s\n", op_name);

    /* mask out signals if this operation is not to be interrupted */
    if(!(flags & PVFS2_OP_INTERRUPTIBLE)) 
    {
        mask_blocked_signals(&orig_sigset);
    }

    if(!(flags & PVFS2_OP_NO_SEMAPHORE))
    {
        ret = down_interruptible(&request_semaphore);
        /* check to see if we were interrupted while waiting for semaphore */
        if(ret < 0)
        {
            if(!(flags & PVFS2_OP_INTERRUPTIBLE)) 
            {
                unmask_blocked_signals(&orig_sigset);
            }
            op->downcall.status = ret;
            pvfs2_print("pvfs2: service_operation interrupted.\n");
            return(ret);
        }
    }
    
    /* queue up the operation */
    if(flags & PVFS2_OP_PRIORITY)
    {
        add_priority_op_to_request_list(op);
    }
    else
    {
        add_op_to_request_list(op);
    }

    if(!(flags & PVFS2_OP_NO_SEMAPHORE))
    {
        up(&request_semaphore);
    }
    
    /* loop up to num_retries if we hit a timeout */
    do
    {
        if(flags & PVFS2_OP_CANCELLATION)
        {
            ret = wait_for_cancellation_downcall(op);
        }
        else
        {
            ret = wait_for_matching_downcall(op);
        }
        num_retries--;
    }while(ret == -ETIMEDOUT && num_retries >= 0);

    if(ret < 0)
    {
        /* failed to get matching downcall */
        if(ret == -ETIMEDOUT)
        { 
            pvfs2_error("pvfs2: %s -- wait timed out and retries exhausted. "
                        "aborting attempt.\n", op_name);
        }
        op->downcall.status = ret;
    }
    else
    {
        /* got matching downcall; make sure status is in errno format */
        op->downcall.status = pvfs2_normalize_to_errno(op->downcall.status);
        ret = op->downcall.status;
    }

    if(!(flags & PVFS2_OP_INTERRUPTIBLE)) 
    {
        unmask_blocked_signals(&orig_sigset);
    }

    BUG_ON(ret != op->downcall.status);
    pvfs2_print("pvfs2: service_operation returning: %d.\n", ret);
    return(ret);
}

void clean_up_interrupted_operation(
    pvfs2_kernel_op_t * op)
{
    /*
      handle interrupted cases depending on what state we were in when
      the interruption is detected.  there is a coarse grained lock
      across the operation.

      NOTE: be sure not to reverse lock ordering by locking an op lock
      while holding the request_list lock.  Here, we first lock the op
      and then lock the appropriate list.
    */
    spin_lock(&op->lock);
    switch (op->op_state)
    {
	case PVFS2_VFS_STATE_WAITING:
	    /*
              upcall hasn't been read; remove op from upcall request
              list.
            */
	    remove_op_from_request_list(op);
	    pvfs2_print("Interrupted: Removed op from request_list\n");
	    break;
	case PVFS2_VFS_STATE_INPROGR:
	    /* op must be removed from the in progress htable */
	    remove_op_from_htable_ops_in_progress(op);
	    pvfs2_print("Interrupted: Removed op from "
			"htable_ops_in_progress\n");
	    break;
	case PVFS2_VFS_STATE_SERVICED:
	    /*
              can this happen? even if it does, I think we're ok with
              doing nothing since no cleanup is necessary
	     */
	    break;
    }
    spin_unlock(&op->lock);
}

/** sleeps on waitqueue waiting for matching downcall for some amount of
 *    time and then wakes up.
 *
 *  \post when this call returns to the caller, the specified op will no
 *        longer be on any list or htable.
 *
 *  \returns 0 on success and -errno on failure
 */
int wait_for_matching_downcall(pvfs2_kernel_op_t * op)
{
    int ret = -EINVAL;
    DECLARE_WAITQUEUE(wait_entry, current);

    spin_lock(&op->lock);
    add_wait_queue(&op->waitq, &wait_entry);
    spin_unlock(&op->lock);

    while (1)
    {
	set_current_state(TASK_INTERRUPTIBLE);

	spin_lock(&op->lock);
	if (op->op_state == PVFS2_VFS_STATE_SERVICED)
	{
	    spin_unlock(&op->lock);
	    ret = 0;
	    break;
	}
	spin_unlock(&op->lock);

	if (!signal_pending(current))
	{
	    if (!schedule_timeout
		(MSECS_TO_JIFFIES(1000 * op_timeout_secs)))
	    {
                pvfs2_print("*** operation timed out (tag %lld)\n",
                            lld(op->tag));
                clean_up_interrupted_operation(op);
		ret = -ETIMEDOUT;
		break;
	    }
	    continue;
	}

        pvfs2_print("*** operation interrupted by a signal (tag %lld)\n",
                    lld(op->tag));
        clean_up_interrupted_operation(op);
        ret = -EINTR;
        break;
    }

    set_current_state(TASK_RUNNING);

    spin_lock(&op->lock);
    remove_wait_queue(&op->waitq, &wait_entry);
    spin_unlock(&op->lock);

    return ret;
}

/** similar to wait_for_matching_downcall(), but used in the special case
 *  of I/O cancellations.
 *
 *  \note we need a special wait function because if this is called we already
 *        know that a signal is pending in current and need to service the
 *        cancellation upcall anyway.  the only way to exit this is to either
 *        timeout or have the cancellation be serviced properly.
*/
int wait_for_cancellation_downcall(pvfs2_kernel_op_t * op)
{
    int ret = -EINVAL;
    DECLARE_WAITQUEUE(wait_entry, current);

    spin_lock(&op->lock);
    add_wait_queue(&op->waitq, &wait_entry);
    spin_unlock(&op->lock);

    while (1)
    {
	set_current_state(TASK_INTERRUPTIBLE);

	spin_lock(&op->lock);
	if (op->op_state == PVFS2_VFS_STATE_SERVICED)
	{
	    spin_unlock(&op->lock);
	    ret = 0;
	    break;
	}
	spin_unlock(&op->lock);

        if (!schedule_timeout
            (MSECS_TO_JIFFIES(1000 * op_timeout_secs)))
        {
            pvfs2_print("*** operation timed out\n");
            clean_up_interrupted_operation(op);
            ret = -ETIMEDOUT;
            break;
        }
    }

    set_current_state(TASK_RUNNING);

    spin_lock(&op->lock);
    remove_wait_queue(&op->waitq, &wait_entry);
    spin_unlock(&op->lock);

    return ret;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

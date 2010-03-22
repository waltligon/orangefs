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


/* What we do in this function is to walk the list of operations that are present 
 * in the request queue and mark them as purged.
 * NOTE: This is called from the device close after client-core has guaranteed that no new
 * operations could appear on the list since the client-core is anyway going to exit.
 */
void purge_waiting_ops(void)
{
    pvfs2_kernel_op_t *op;
    spin_lock(&pvfs2_request_list_lock);
    list_for_each_entry(op, &pvfs2_request_list, list)
    {
        spin_lock(&op->lock);
        gossip_debug(GOSSIP_WAIT_DEBUG, "pvfs2-client-core: purging op tag %lld %s\n", lld(op->tag), get_opname_string(op));
        set_op_state_purged(op);
        spin_unlock(&op->lock);
        wake_up_interruptible(&op->waitq);
    }
    spin_unlock(&pvfs2_request_list_lock);
    return;
}

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
    int flags)               /**< flags to modify behavior */
{
    sigset_t orig_sigset; 
    int ret = 0;
    op->upcall.pid = current->pid;
#ifdef PVFS2_LINUX_KERNEL_2_4
    op->upcall.tgid = -1;
#else
    op->upcall.tgid = current->tgid;
#endif

retry_servicing:
    op->downcall.status = 0;
    gossip_debug(GOSSIP_WAIT_DEBUG, "pvfs2: service_operation: %s %p\n", op_name, op);
    gossip_debug(GOSSIP_WAIT_DEBUG, "pvfs2: operation posted by process: %s, pid: %i\n", current->comm, current->pid);

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
            gossip_debug(GOSSIP_WAIT_DEBUG, "pvfs2: service_operation interrupted.\n");
            return(ret);
        }
    }
    
    if (is_daemon_in_service() < 0)
    {
        /* By incrementing the per-operation attempt counter, we directly go into the timeout logic
         * while waiting for the matching downcall to be read
         */
        op->attempts++;
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

    /* If we are asked to service an asynchronous operation from VFS perspective, we are done */
    if (flags & PVFS2_OP_ASYNC)
    {
        return 0;
    }
    
    if(flags & PVFS2_OP_CANCELLATION)
    {
        ret = wait_for_cancellation_downcall(op);
    }
    else
    {
        ret = wait_for_matching_downcall(op);
    }

    if(ret < 0)
    {
        /* failed to get matching downcall */
        if(ret == -ETIMEDOUT)
        { 
            gossip_err("pvfs2: %s -- wait timed out; aborting attempt.\n",
                       op_name);
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
    /* retry if operation has not been serviced and if requested */
    if (!op_state_serviced(op) && op->downcall.status == -EAGAIN)
    {
        gossip_debug(GOSSIP_WAIT_DEBUG, "pvfs2: tag %lld (%s) -- operation to be retried (%d attempt)\n", 
                lld(op->tag), op_name, op->attempts + 1);
        goto retry_servicing;
    }
    gossip_debug(GOSSIP_WAIT_DEBUG, "pvfs2: service_operation %s returning: %d for %p.\n", op_name, ret, op);
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

    if (op_state_waiting(op))
    {
        /*
          upcall hasn't been read; remove op from upcall request
          list.
        */
        remove_op_from_request_list(op);
        gossip_debug(GOSSIP_WAIT_DEBUG, "Interrupted: Removed op %p from request_list\n", op);
    }
    else if (op_state_in_progress(op))
    {
        /* op must be removed from the in progress htable */
        remove_op_from_htable_ops_in_progress(op);
        gossip_debug(GOSSIP_WAIT_DEBUG, "Interrupted: Removed op %p from "
                    "htable_ops_in_progress\n", op);
    }
    else if (!op_state_serviced(op))
    {
            gossip_err("interrupted operation is in a weird state 0x%x\n",
                    op->op_state);
    }
    spin_unlock(&op->lock);
}

/** sleeps on waitqueue waiting for matching downcall.
 *  if client-core finishes servicing, then we are good to go.
 *  else if client-core exits, we get woken up here, and retry with a timeout
 *
 *  \post when this call returns to the caller, the specified op will no
 *        longer be on any list or htable.
 *
 *  \returns 0 on success and -errno on failure
 *  Errors are:
 *  EAGAIN in case we want the caller to requeue and try again..
 *  EINTR/EIO/ETIMEDOUT indicating we are done trying to service this 
 *                operation since client-core seems to be exiting too often
 *                or if we were interrupted.
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
        if (op_state_serviced(op))
	{
	    spin_unlock(&op->lock);
	    ret = 0;
	    break;
	}

	if (!signal_pending(current))
	{
            /* if this was our first attempt and client-core has not purged our operation,
             * we are happy to simply wait 
             */
            if (op->attempts == 0 && !op_state_purged(op))
            {
                spin_unlock(&op->lock);
                schedule();
            }
            else {
                spin_unlock(&op->lock);
                /* subsequent attempts, we retry exactly once with timeouts */
                if (!schedule_timeout(MSECS_TO_JIFFIES(1000 * op_timeout_secs)))
                {
                    gossip_debug(GOSSIP_WAIT_DEBUG, "*** operation timed out (tag %lld, %p, att %d)\n",
                                lld(op->tag), op, op->attempts);
                    ret = -ETIMEDOUT;
                    clean_up_interrupted_operation(op);
                    break;
                }
            }
            spin_lock(&op->lock);
            op->attempts++;
            /* if the operation was purged in the meantime, it is better to requeue it afresh 
             *  but ensure that we have not been purged repeatedly. This could happen if client-core
             *  crashes when an op is being serviced, so we requeue the op, client core crashes again
             *  so we requeue the op, client core starts, and so on...*/
            if (op_state_purged(op))
            {
                ret = (op->attempts < PVFS2_PURGE_RETRY_COUNT) ? -EAGAIN : -EIO;
                spin_unlock(&op->lock);
                clean_up_interrupted_operation(op);
                break;
            }
            spin_unlock(&op->lock);
            continue;
	}
        else {
            spin_unlock(&op->lock);
        }

        gossip_debug(GOSSIP_WAIT_DEBUG, "*** operation interrupted by a signal (tag %lld, op %p)\n",
                    lld(op->tag), op);
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
        if (op_state_serviced(op))
	{
	    spin_unlock(&op->lock);
	    ret = 0;
	    break;
	}
	spin_unlock(&op->lock);

        if (!schedule_timeout
            (MSECS_TO_JIFFIES(1000 * op_timeout_secs)))
        {
            gossip_debug(GOSSIP_WAIT_DEBUG, "*** operation timed out: %p\n", op);
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

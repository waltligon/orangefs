/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include "pvfs2-kernel.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern struct qhash_table *htable_ops_in_progress;

#ifdef PVFS2_KERNEL_DEBUG
#define MAX_SERVICE_WAIT_IN_SECONDS       10
#else
#define MAX_SERVICE_WAIT_IN_SECONDS       30
#endif

static inline void clean_up_interrupted_operation(
    pvfs2_kernel_op_t * op)
{
    /*
      handle interrupted cases depending on what state
      we were in when the interruption is detected.
      there is a coarse grained lock across the operation.

      NOTE: be sure not to reverse lock ordering by locking
      an op lock while holding the request_list lock.

      (Here, we first lock the op and then lock
      the appropriate list).
    */
    spin_lock(&op->lock);
    switch (op->op_state)
    {
	case PVFS2_VFS_STATE_WAITING:
	    /*
              upcall hasn't been read; remove
              op from upcall request list
            */
	    remove_op_from_request_list(op);
	    pvfs2_print("Interrupted: Removed op from request_list\n");
	    break;
	case PVFS2_VFS_STATE_INPROGR:
	    /*
              op must be removed from the in progress htable
            */
	    remove_op_from_htable_ops_in_progress(op);
	    pvfs2_print("Interrupted: Removed op from "
			"htable_ops_in_progress\n");
	    break;
	case PVFS2_VFS_STATE_SERVICED:
	    /*
              can this happen? even if it does, I think we're
              ok with doing nothing since no cleanup is necessary
	     */
	    break;
    }
    spin_unlock(&op->lock);
}

/*
  sleeps on waitqueue waiting for matching downcall
  for some amount of time and then wakes up.

  NOTE: when this call returns to the caller, the specified
  op will no longer be on any list or htable.

  return values and op status changes:

  -1 - an error occurred; op status unknown
   0 - success; everything ok.
       the op state will be marked as serviced
   1 - timeout reached (before downcall recv'd)
       the caller has the choice of either requeueing the op
       or failing the operation when this occurs.
       the op observes no state change.
   2 - sleep interrupted (signal recv'd)
       the op observes no state change.
*/
int wait_for_matching_downcall(
    pvfs2_kernel_op_t * op)
{
    int ret = -1;
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
		(MSECS_TO_JIFFIES(1000 * MAX_SERVICE_WAIT_IN_SECONDS)))
	    {
                pvfs2_print("*** operation timed out\n");
                clean_up_interrupted_operation(op);
		ret = 1;
		break;
	    }
	    continue;
	}

        pvfs2_print("*** operation interrupted by signal\n");
        clean_up_interrupted_operation(op);
	ret = 2;
	break;
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

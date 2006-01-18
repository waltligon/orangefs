/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add proc file handler for pvfs2 client
 * parameters, Copyright © Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"

#include <linux/sysctl.h>
#include <linux/proc_fs.h>

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

extern int debug;
extern int op_timeout_secs;
extern spinlock_t pvfs2_request_list_lock;
extern struct list_head pvfs2_request_list;
extern wait_queue_head_t pvfs2_request_list_waitq;

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
/* extra parameters provided to pvfs2 param proc handlers */
struct pvfs2_param_extra
{
    int op;  /* parameter type */
    int min; /* minimum value */
    int max; /* maximum value */
};

/* pvfs2_param_proc_handler()
 *
 * generic proc file handler for getting and setting various tunable
 * pvfs2-client parameters
 */
#ifdef HAVE_PROC_HANDLER_SIX_ARG
static int pvfs2_param_proc_handler(
    ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#else
static int pvfs2_param_proc_handler(
    ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp)
#endif
{       
    pvfs2_kernel_op_t *new_op = NULL;
    struct pvfs2_param_extra* extra = ctl->extra1;
    int val = 0;
    int ret = 0;
    ctl_table tmp_ctl = *ctl;

    /* override fields in control structure for call to generic proc handler */
    tmp_ctl.data = &val;
    tmp_ctl.extra1 = &extra->min;
    tmp_ctl.extra2 = &extra->max;

    /* build an op structure to send request to pvfs2-client */
    new_op = op_alloc();
    if (!new_op)
    {
        return -ENOMEM;
    }

    if(write)
    {
        /* use generic proc handling function to retrive value to set */
#ifdef HAVE_PROC_HANDLER_SIX_ARG
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp, ppos);
#else
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp);
#endif
        if(ret != 0)
        {
            op_release(new_op);
            return(ret);
        }
        pvfs2_print("pvfs2: proc write %d\n", val);
        new_op->upcall.req.param.value = val;
        new_op->upcall.req.param.type = PVFS2_PARAM_REQUEST_SET;
    }
    else
    {
        /* get parameter from client, we will output afterwards */
        new_op->upcall.req.param.type = PVFS2_PARAM_REQUEST_GET;
    }

    new_op->upcall.type = PVFS2_VFS_OP_PARAM;
    new_op->upcall.req.param.op = extra->op;

    /* perform operation (get or set) */
    ret = service_operation(new_op, "pvfs2_param", PVFS2_OP_RETRY_COUNT, 
        PVFS2_OP_INTERRUPTIBLE);
    
    if(ret == 0 && !write)
    {
        /* use generic proc handling function to output value */
        val = (int)new_op->downcall.resp.param.value;
        pvfs2_print("pvfs2: proc read %d\n", val);
#ifdef HAVE_PROC_HANDLER_SIX_ARG
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp, ppos);
#else
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp);
#endif
    }

    op_release(new_op);
    return(ret);
}

#ifdef HAVE_PROC_HANDLER_SIX_ARG
static int pvfs2_acache_pc_proc_handler(
    ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#else
static int pvfs2_acache_pc_proc_handler(
    ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp)
#endif
{
    pvfs2_kernel_op_t *new_op = NULL;
    int ret;
    int pos = 0;
    int to_copy = 0;
    int* pc_type = ctl->extra1;
#ifdef HAVE_PROC_HANDLER_SIX_ARG
    loff_t *offset = ppos;
#else
    loff_t *offset = &filp->f_pos;
#endif

    if(write)
    {
        /* don't allow writes to this file */
        *lenp = 0;
        return(-EPERM);
    }

    /* build an op structure to send request to pvfs2-client */
    new_op = op_alloc();
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.perf_count.type = *pc_type;
    new_op->upcall.type = PVFS2_VFS_OP_PERF_COUNT;

    /* retrieve performance counters */
    ret = service_operation(new_op, "pvfs2_perf_count",
        PVFS2_OP_RETRY_COUNT, PVFS2_OP_INTERRUPTIBLE);

    if(ret == 0)
    {
        /* figure out how many bytes we will copy out */
        pos = strlen(new_op->downcall.resp.perf_count.buffer);
        to_copy = pos - *offset;
        if(to_copy < 0)
        {
            to_copy = 0;
        }
        if(to_copy > *lenp)
        {
            to_copy = *lenp;
        }

        if(to_copy)
        {
            /* copy correct portion of the string buffer */
            if(copy_to_user(buffer, 
                (new_op->downcall.resp.perf_count.buffer+(*offset)), to_copy))
            {
                ret = -EFAULT;
            }
            else
            {
                /* update offsets etc. if successful */
                *lenp = to_copy;
                *offset += to_copy;
                ret = to_copy;
            }
        }
        else
        {
            *lenp = 0;
            ret = 0;
        }
    }

    op_release(new_op);

    return(ret);
}

static struct ctl_table_header *fs_table_header = NULL;

static struct pvfs2_param_extra acache_timeout_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra acache_hard_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra acache_soft_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra acache_rec_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE,
    .min = 0,
    .max = 100,
};
static struct pvfs2_param_extra perf_time_interval_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra perf_history_size_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_PERF_HISTORY_SIZE,
    .min = 1,
    .max = INT_MAX,
};
static struct pvfs2_param_extra perf_reset_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_PERF_RESET,
    .min = 0,
    .max = 1,
};
static int min_debug[] = {0}, max_debug[] = {1};
static int min_op_timeout_secs[] = {0}, max_op_timeout_secs[] = {INT_MAX};
static ctl_table pvfs2_acache_table[] = {
    /* controls acache timeout */
    {1, "timeout-msecs", NULL, sizeof(int), 0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &acache_timeout_extra, NULL},
    /* controls acache hard limit */
    {2, "hard-limit", NULL, sizeof(int), 0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &acache_hard_extra, NULL},
    /* controls acache soft limit */
    {3, "soft-limit", NULL, sizeof(int), 0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &acache_soft_extra, NULL},
    /* controls acache reclaim percentage */
    {4, "reclaim-percentage", NULL, sizeof(int), 
        0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &acache_rec_extra, NULL},
    {0}
};
static int acache_perf_count = PVFS2_PERF_COUNT_REQUEST_ACACHE;
static ctl_table pvfs2_pc_table[] = {
    {1, "acache", NULL, 4096, 0444, NULL,
        pvfs2_acache_pc_proc_handler, NULL, NULL, &acache_perf_count, NULL},
    {0}
};
static ctl_table pvfs2_table[] = {
    /* controls debugging level */
    {1, "debug", &debug, sizeof(int), 0644, NULL,
        &proc_dointvec_minmax, &sysctl_intvec,
        NULL, &min_debug, &max_debug},
    /* operation timeout */
    {2, "op-timeout-secs", &op_timeout_secs, sizeof(int), 0644, NULL,
        &proc_dointvec_minmax, &sysctl_intvec,
        NULL, &min_op_timeout_secs, &max_op_timeout_secs},
    /* time interval for client side performance counters */
    {3, "perf-time-interval-secs", NULL, sizeof(int), 0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &perf_time_interval_extra, NULL},
    /* time interval for client side performance counters */
    {4, "perf-history-size", NULL, sizeof(int), 0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &perf_history_size_extra, NULL},
    /* reset performance counters */
    {5, "perf-counter-reset", NULL, sizeof(int), 0644, NULL,
        &pvfs2_param_proc_handler, NULL, NULL, &perf_reset_extra, NULL},
    /* subdir for acache control */
    {6, "acache", NULL, 0, 0555, pvfs2_acache_table},
    {7, "perf-counters", NULL, 0, 0555, pvfs2_pc_table},
    {0}
};
static ctl_table fs_table[] = {
    {1, "pvfs2", NULL, 0, 0555, pvfs2_table},
    {0}
};
#endif

int pvfs2_proc_initialize(void)
{
    int ret = 0;

#ifdef CONFIG_SYSCTL
    if (!fs_table_header)
    {
        fs_table_header = register_sysctl_table(fs_table, 0);
    }
#endif

    return(ret);
}

int pvfs2_proc_finalize(void)
{
    int ret = 0;

#ifdef CONFIG_SYSCTL
    if(fs_table_header) 
    {
        unregister_sysctl_table(fs_table_header);
        fs_table_header = NULL;
    }
#endif

    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

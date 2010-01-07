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
#include "pvfs2-proc.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

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
#if defined(HAVE_PROC_HANDLER_FILE_ARG)
static int pvfs2_param_proc_handler(
    ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
static int pvfs2_param_proc_handler(
    ctl_table       *ctl,
    int             write,
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
    new_op = op_alloc(PVFS2_VFS_OP_PARAM);
    if (!new_op)
    {
        return -ENOMEM;
    }

    if(write)
    {
        /* use generic proc handling function to retrive value to set */
#if defined(HAVE_PROC_HANDLER_FILE_ARG)
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp, ppos);
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
        ret = proc_dointvec_minmax(&tmp_ctl, write, buffer, lenp, ppos);
#else
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp);
#endif
        if(ret != 0)
        {
            op_release(new_op);
            return(ret);
        }
        gossip_debug(GOSSIP_PROC_DEBUG, "pvfs2: proc write %d\n", val);
        new_op->upcall.req.param.value = val;
        new_op->upcall.req.param.type = PVFS2_PARAM_REQUEST_SET;
    }
    else
    {
        /* get parameter from client, we will output afterwards */
        new_op->upcall.req.param.type = PVFS2_PARAM_REQUEST_GET;
    }

    new_op->upcall.req.param.op = extra->op;

    /* perform operation (get or set) */
    ret = service_operation(new_op, "pvfs2_param",  
        PVFS2_OP_INTERRUPTIBLE);
    
    if(ret == 0 && !write)
    {
        /* use generic proc handling function to output value */
        val = (int)new_op->downcall.resp.param.value;
        gossip_debug(GOSSIP_PROC_DEBUG, "pvfs2: proc read %d\n", val);
#if defined(HAVE_PROC_HANDLER_FILE_ARG)
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp, ppos);
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
        ret = proc_dointvec_minmax(&tmp_ctl, write, buffer, lenp, ppos);
#else
        ret = proc_dointvec_minmax(&tmp_ctl, write, filp, buffer, lenp);
#endif
    }

    op_release(new_op);
    return(ret);
}

#if defined(HAVE_PROC_HANDLER_FILE_ARG)
static int pvfs2_pc_proc_handler(
    ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
static int pvfs2_pc_proc_handler(
    ctl_table       *ctl,
    int             write,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#else
static int pvfs2_pc_proc_handler(
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
#if defined(HAVE_PROC_HANDLER_PPOS_ARG) || defined(HAVE_PROC_HANDLER_FILE_ARG)
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
    new_op = op_alloc(PVFS2_VFS_OP_PERF_COUNT);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.perf_count.type = *pc_type;

    /* retrieve performance counters */
    ret = service_operation(new_op, "pvfs2_perf_count",
         PVFS2_OP_INTERRUPTIBLE);

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
static struct pvfs2_param_extra static_acache_timeout_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_TIMEOUT_MSECS,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra static_acache_hard_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_HARD_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra static_acache_soft_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_SOFT_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra static_acache_rec_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_RECLAIM_PERCENTAGE,
    .min = 0,
    .max = 100,
};
static struct pvfs2_param_extra ncache_timeout_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra ncache_hard_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra ncache_soft_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra ncache_rec_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE,
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
static int min_debug[] = {0}, max_debug[] = {GOSSIP_MAX_DEBUG};
static int min_op_timeout_secs[] = {0}, max_op_timeout_secs[] = {INT_MAX};
static int min_slot_timeout_secs[] = {0}, max_slot_timeout_secs[] = {INT_MAX};

/*
 * Modern kernels prefer to number the controls themselves.
 */
#ifdef CTL_UNNUMBERED
#define UNNUMBERED_OR_VAL(x) CTL_UNNUMBERED
#else
#define UNNUMBERED_OR_VAL(x) x
#endif

static ctl_table pvfs2_acache_table[] = {
    /* controls acache timeout */
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "timeout-msecs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_timeout_extra
    },
    /* controls acache hard limit */
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_hard_extra
    },
    /* controls acache soft limit */
    {
        .ctl_name = UNNUMBERED_OR_VAL(3),
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_soft_extra
    },
    /* controls acache reclaim percentage */
    {
        .ctl_name = UNNUMBERED_OR_VAL(4),
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_rec_extra,
    },
    {.ctl_name = 0}
};
static ctl_table pvfs2_static_acache_table[] = {
    /* controls static acache timeout */
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "timeout-msecs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &static_acache_timeout_extra
    },
    /* controls static acache hard limit */
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &static_acache_hard_extra
    },
    /* controls static acache soft limit */
    {
        .ctl_name = UNNUMBERED_OR_VAL(3),
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &static_acache_soft_extra
    },
    /* controls static acache reclaim percentage */
    {
        .ctl_name = UNNUMBERED_OR_VAL(4),
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &static_acache_rec_extra,
    },
    {.ctl_name = 0}
};

static ctl_table pvfs2_ncache_table[] = {
    /* controls ncache timeout */
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "timeout-msecs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_timeout_extra
    },
    /* controls ncache hard limit */
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_hard_extra
    },
    /* controls ncache soft limit */
    {
        .ctl_name = UNNUMBERED_OR_VAL(3),
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_soft_extra
    },
    /* controls ncache reclaim percentage */
    {
        .ctl_name = UNNUMBERED_OR_VAL(4),
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_rec_extra
    },
    {.ctl_name = 0}
};
static int acache_perf_count = PVFS2_PERF_COUNT_REQUEST_ACACHE;
static int static_acache_perf_count = PVFS2_PERF_COUNT_REQUEST_STATIC_ACACHE;
static int ncache_perf_count = PVFS2_PERF_COUNT_REQUEST_NCACHE;
static ctl_table pvfs2_pc_table[] = {
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "acache",
        .maxlen = 4096,
        .mode = 0444,
        .proc_handler = pvfs2_pc_proc_handler,
        .extra1 = &acache_perf_count,
    },
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "static-acache",
        .maxlen = 4096,
        .mode = 0444,
        .proc_handler = pvfs2_pc_proc_handler,
        .extra1 = &static_acache_perf_count,
    },
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "ncache",
        .maxlen = 4096,
        .mode = 0444,
        .proc_handler = pvfs2_pc_proc_handler,
        .extra1 = &ncache_perf_count
    },
    {.ctl_name = 0}
};

pvfs2_stats g_pvfs2_stats;

static ctl_table pvfs2_stats_table[] = {
    /* shows number of hits in cache */
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "hits",
        .data     = &g_pvfs2_stats.cache_hits,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "misses",
        .data     = &g_pvfs2_stats.cache_misses,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {
        .ctl_name = UNNUMBERED_OR_VAL(3),
        .procname = "reads",
        .data     = &g_pvfs2_stats.reads,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {
        .ctl_name = UNNUMBERED_OR_VAL(4),
        .procname = "writes",
        .data     = &g_pvfs2_stats.writes,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {.ctl_name = 0}
};

static ctl_table pvfs2_table[] = {
    /* controls debugging level */
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "debug",
        .data = &gossip_debug_mask,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &proc_dointvec_minmax, 
        .strategy = &sysctl_intvec,
        .extra1 = &min_debug,
        .extra2 = &max_debug
    },
    /* operation timeout */
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "op-timeout-secs",
        .data = &op_timeout_secs,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &proc_dointvec_minmax,
        .strategy = &sysctl_intvec,
        .extra1 = &min_op_timeout_secs,
        .extra2 = &max_op_timeout_secs
    },
    /* slot timeout */
    {
        .ctl_name = UNNUMBERED_OR_VAL(2),
        .procname = "slot-timeout-secs",
        .data = &slot_timeout_secs,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &proc_dointvec_minmax,
        .strategy = &sysctl_intvec,
        .extra1 = &min_slot_timeout_secs,
        .extra2 = &max_slot_timeout_secs
    },
    /* time interval for client side performance counters */
    {
        .ctl_name = UNNUMBERED_OR_VAL(3),
        .procname = "perf-time-interval-secs",
        .maxlen = sizeof(int), 
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &perf_time_interval_extra
    },
    /* time interval for client side performance counters */
    {
        .ctl_name = UNNUMBERED_OR_VAL(4),
        .procname = "perf-history-size",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &perf_history_size_extra
    },
    /* reset performance counters */
    {
        .ctl_name = UNNUMBERED_OR_VAL(5),
        .procname = "perf-counter-reset",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &perf_reset_extra,
    },
    /* subdir for acache control */
    {
        .ctl_name = UNNUMBERED_OR_VAL(6),
        .procname = "acache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_acache_table
    },
    /* subdir for static acache control */
    {
        .ctl_name = UNNUMBERED_OR_VAL(6),
        .procname = "static-acache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_static_acache_table
    },
    {
        .ctl_name = UNNUMBERED_OR_VAL(7),
        .procname = "perf-counters",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_pc_table
    },
    /* subdir for ncache control */
    {
        .ctl_name = UNNUMBERED_OR_VAL(8),
        .procname = "ncache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_ncache_table
    },
    /* statistics maintained by the kernel module (output only below this) */
    {
        .ctl_name = UNNUMBERED_OR_VAL(9),
        .procname = "stats",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_stats_table
    },
    {.ctl_name = 0}
};
static ctl_table fs_table[] = {
    {
        .ctl_name = UNNUMBERED_OR_VAL(1),
        .procname = "pvfs2",
        .mode = 0555,
        .child = pvfs2_table
    },
    {.ctl_name = 0}
};
#endif

void pvfs2_proc_initialize(void)
{
#ifdef CONFIG_SYSCTL
    if (!fs_table_header)
    {
#ifdef HAVE_TWO_ARG_REGISTER_SYSCTL_TABLE
        fs_table_header = register_sysctl_table(fs_table, 0);
#else
        fs_table_header = register_sysctl_table(fs_table);
#endif
    }
#endif

    return;
}

void pvfs2_proc_finalize(void)
{
#ifdef CONFIG_SYSCTL
    if(fs_table_header) 
    {
        unregister_sysctl_table(fs_table_header);
        fs_table_header = NULL;
    }
#endif
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

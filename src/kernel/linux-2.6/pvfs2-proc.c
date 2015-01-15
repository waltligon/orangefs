/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add proc file handler for pvfs2 client
 * parameters, Copyright © Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-internal.h"

#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include "pvfs2-proc.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>

#define KERNEL_DEBUG "kernel-debug"
#define CLIENT_DEBUG "client-debug"
#define DEBUG_HELP "debug-help"

/* these functions are defined in pvfs2-utils.c */
uint64_t PVFS_proc_debug_eventlog_to_mask(const char *);
uint64_t PVFS_proc_kmod_eventlog_to_mask(const char *event_logging);
int PVFS_proc_kmod_mask_to_eventlog(uint64_t mask, char *debug_string);
int PVFS_proc_mask_to_eventlog(uint64_t mask, char *debug_string);

/* these strings will be initialized by invoking the PVFS_DEV_DEBUG ioctl
 * command when the client-core is started.  otherwise, these variables are
 * only set via the proc sys calls.
*/
char client_debug_string[PVFS2_MAX_DEBUG_STRING_LEN] = "none";
char kernel_debug_string[PVFS2_MAX_DEBUG_STRING_LEN] = "none";
extern char debug_help_string[];


/* extra parameters provided to pvfs2 param proc handlers */
struct pvfs2_param_extra
{
    int op;  /* parameter type */
    int min; /* minimum value */
    int max; /* maximum value */
};

/* pvfs2_proc_debug_mask_handler()
 * proc file handler that will take a debug string and convert it
 * into the proper debug value and then send a request to update the
 * debug mask if client or update the local debug mask if kernel.
*/
#if defined(HAVE_PROC_HANDLER_FILE_ARG)
static int pvfs2_proc_debug_mask_handler(
    struct ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
static int pvfs2_proc_debug_mask_handler(
    struct ctl_table       *ctl,
    int             write,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#else
static int pvfs2_proc_debug_mask_handler(
    struct ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp)
#endif
{
  int ret=0;
  pvfs2_kernel_op_t *new_op = NULL;

  gossip_debug(GOSSIP_PROC_DEBUG,"Executing pvfs2_proc_debug_mask_handler...\n");

  /* use generic proc string handling function to retrieve/set string. */
#if defined(HAVE_PROC_HANDLER_FILE_ARG)
        ret = proc_dostring(ctl, write, filp, buffer, lenp, ppos);
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
        ret = proc_dostring(ctl, write, buffer, lenp, ppos);
#else
        ret = proc_dostring(ctl, write, filp, buffer, lenp);
#endif

  if (ret != 0)
  {
     return(ret);
  }

  gossip_debug(GOSSIP_PROC_DEBUG,"%s: debug string: %s\n"
                                ,"pvfs2_proc_debug_mask_handler"
                                ,(char *)ctl->data);

  /*For a user write, ctl->data will now contain the new debug string as given
   *by the user.  For a user read, the user's "buffer" will now contain the string 
   *stored in ctl->data.
  */

  /*For a write, we must convert the debug string into the proper debug mask. 
   *The conversion will ignore any invalid keywords sent in by the user, so we
   *re-convert the debug mask back into the correct debug string.
  */
  if (write && !strcmp(ctl->procname,KERNEL_DEBUG))
  {
     gossip_debug_mask=PVFS_proc_kmod_eventlog_to_mask((const char *)ctl->data);
     ret=PVFS_proc_kmod_mask_to_eventlog(gossip_debug_mask,(char *)ctl->data);

     gossip_debug(GOSSIP_PROC_DEBUG,"%s: kernel debug mask: %lu\n"
                                   ,"pvfs2_proc_debug_mask_handler"
                                   ,(unsigned long)gossip_debug_mask);
     gossip_debug(GOSSIP_PROC_DEBUG,"New kernel debug string is %s.\n"
                                   ,kernel_debug_string);
     printk("PVFS: kernel debug mask has been modified to \"%s\" (0x%08llx).\n"
           ,kernel_debug_string, llu(gossip_debug_mask));
  } 
  else if (write && !strcmp(ctl->procname,CLIENT_DEBUG)) 
  {
     new_op = op_alloc(PVFS2_VFS_OP_PARAM);
     if (!new_op)
        return (-ENOMEM);
     strcpy(new_op->upcall.req.param.s_value,ctl->data);
     new_op->upcall.req.param.type = PVFS2_PARAM_REQUEST_SET;
     new_op->upcall.req.param.op = PVFS2_PARAM_REQUEST_OP_CLIENT_DEBUG;

     ret=service_operation(new_op,"pvfs2_param",PVFS2_OP_INTERRUPTIBLE);
     
     if (ret==0)
     {
        gossip_debug(GOSSIP_PROC_DEBUG,"Downcall:\treturn status:%d\treturn "
                                       "value:%x\n"
                                      ,(int)new_op->downcall.status
                                      ,(int)new_op->downcall.resp.param.value);

        ret=PVFS_proc_mask_to_eventlog(new_op->downcall.resp.param.value
                                      ,client_debug_string);
        gossip_debug(GOSSIP_PROC_DEBUG,"New client debug string is %s\n"
                                      ,client_debug_string);
     }
     op_release(new_op);
     printk("PVFS: client debug mask has been modified to \"%s\" (0x%08llx).\n"
           ,client_debug_string, llu(new_op->downcall.resp.param.value));
  }
  else if (write && !strcmp(ctl->procname,DEBUG_HELP))
  {
    /*do nothing...the user can only READ the debug help*/
    return (0);
  }

  return (0);
}/*end pvfs2_proc_debug_mask_handler*/

/* pvfs2_param_proc_handler()
 *
 * generic proc file handler for getting and setting various tunable
 * pvfs2-client parameters
 */
#if defined(HAVE_PROC_HANDLER_FILE_ARG)
static int pvfs2_param_proc_handler(
    struct ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
static int pvfs2_param_proc_handler(
    struct ctl_table       *ctl,
    int             write,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#else
static int pvfs2_param_proc_handler(
    struct ctl_table       *ctl,
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
    struct ctl_table tmp_ctl = *ctl;

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
    struct ctl_table       *ctl,
    int             write,
    struct file     *filp,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#elif defined(HAVE_PROC_HANDLER_PPOS_ARG)
static int pvfs2_pc_proc_handler(
    struct ctl_table       *ctl,
    int             write,
    void            *buffer,
    size_t          *lenp,
    loff_t          *ppos)
#else
static int pvfs2_pc_proc_handler(
    struct ctl_table       *ctl,
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
static struct pvfs2_param_extra ccache_timeout_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CCACHE_TIMEOUT_SECS,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra ccache_hard_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CCACHE_HARD_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra ccache_soft_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CCACHE_SOFT_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra ccache_rec_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CCACHE_RECLAIM_PERCENTAGE,
    .min = 0,
    .max = 100,
};
static struct pvfs2_param_extra capcache_timeout_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CAPCACHE_TIMEOUT_SECS,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra capcache_hard_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CAPCACHE_HARD_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra capcache_soft_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CAPCACHE_SOFT_LIMIT,
    .min = 0,
    .max = INT_MAX,
};
static struct pvfs2_param_extra capcache_rec_extra = {
    .op = PVFS2_PARAM_REQUEST_OP_CAPCACHE_RECLAIM_PERCENTAGE,
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

static int min_op_timeout_secs[] = {0}, max_op_timeout_secs[] = {INT_MAX};
static int min_slot_timeout_secs[] = {0}, max_slot_timeout_secs[] = {INT_MAX};

/*
 * Modern kernels (up to 2.6.33) prefer to number the controls themselves.
 */
#ifdef CTL_UNNUMBERED
#define UNNUMBERED_OR_VAL(x) ((x==CTL_NONE) ? CTL_NONE : CTL_UNNUMBERED)
#else
#define UNNUMBERED_OR_VAL(x) x
#endif

/*
 * API change in 2.6.33 removes .ctl_name and .strategy from ctl_table
 */
#ifdef HAVE_CTL_NAME
#define CTL_NAME(c_name)        .ctl_name = UNNUMBERED_OR_VAL(c_name),
#else
#define CTL_NAME(c_name)
#endif /* HAVE_CTL_NAME */

#ifdef HAVE_CTL_STRATEGY
#define CTL_STATEGY(c_strategy) .strategy = (c_strategy),
#else
#define CTL_STRATEGY(strat)     
#endif /* HAVE_CTL_STRATEGY */

static struct ctl_table pvfs2_acache_table[] = {
    /* controls acache timeout */
    {
        CTL_NAME(1)
        .procname = "timeout-msecs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_timeout_extra
    },
    /* controls acache hard limit */
    {
        CTL_NAME(2)
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_hard_extra
    },
    /* controls acache soft limit */
    {
        CTL_NAME(3)
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_soft_extra
    },
    /* controls acache reclaim percentage */
    {
        CTL_NAME(4)
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &acache_rec_extra,
    },
    { CTL_NAME(CTL_NONE) }
};

static struct ctl_table pvfs2_ncache_table[] = {
    /* controls ncache timeout */
    {
        CTL_NAME(1)
        .procname = "timeout-msecs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_timeout_extra
    },
    /* controls ncache hard limit */
    {
        CTL_NAME(2)
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_hard_extra
    },
    /* controls ncache soft limit */
    {
        CTL_NAME(3)
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_soft_extra
    },
    /* controls ncache reclaim percentage */
    {
        CTL_NAME(4)
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ncache_rec_extra
    },
    { CTL_NAME(CTL_NONE) }
};

static struct ctl_table pvfs2_ccache_table[] = {
    /* controls credential cache timeout */
    {
        CTL_NAME(1)
        .procname = "timeout-secs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ccache_timeout_extra
    },
    /* controls credential cache hard limit */
    {
        CTL_NAME(2)
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ccache_hard_extra
    },
    /* controls credential cache soft limit */
    {
        CTL_NAME(3)
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ccache_soft_extra
    },
    /* controls credential cache reclaim percentage */
    {
        CTL_NAME(4)
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &ccache_rec_extra
    },
    { CTL_NAME(CTL_NONE) }
};

static struct ctl_table pvfs2_capcache_table[] = {
    /* controls capcache timeout */
    {
        CTL_NAME(1)
        .procname = "timeout-secs",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &capcache_timeout_extra
    },
    /* controls capability cache hard limit */
    {
        CTL_NAME(2)
        .procname = "hard-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &capcache_hard_extra
    },
    /* controls capability cache soft limit */
    {
        CTL_NAME(3)
        .procname = "soft-limit",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &capcache_soft_extra
    },
    /* controls capability cache reclaim percentage */
    {
        CTL_NAME(4)
        .procname = "reclaim-percentage",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &capcache_rec_extra
    },
    { CTL_NAME(CTL_NONE) }
};

static int acache_perf_count = PVFS2_PERF_COUNT_REQUEST_ACACHE;
static int ncache_perf_count = PVFS2_PERF_COUNT_REQUEST_NCACHE;
static int capcache_perf_count = PVFS2_PERF_COUNT_REQUEST_CAPCACHE;
static struct ctl_table pvfs2_pc_table[] = {
    {
        CTL_NAME(1)
        .procname = "acache",
        .maxlen = 4096,
        .mode = 0444,
        .proc_handler = pvfs2_pc_proc_handler,
        .extra1 = &acache_perf_count,
    },
    {
        CTL_NAME(2)
        .procname = "ncache",
        .maxlen = 4096,
        .mode = 0444,
        .proc_handler = pvfs2_pc_proc_handler,
        .extra1 = &ncache_perf_count
    },
    {
        CTL_NAME(3)
        .procname = "capcache",
        .maxlen = 4096,
        .mode = 0444,
        .proc_handler = pvfs2_pc_proc_handler,
        .extra1 = &capcache_perf_count
    },
    { CTL_NAME(CTL_NONE) }
};

pvfs2_stats g_pvfs2_stats;

static struct ctl_table pvfs2_stats_table[] = {
    /* shows number of hits in cache */
    {
        CTL_NAME(1)
        .procname = "hits",
        .data     = &g_pvfs2_stats.cache_hits,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {
        CTL_NAME(2)
        .procname = "misses",
        .data     = &g_pvfs2_stats.cache_misses,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {
        .procname = "reads",
        .data     = &g_pvfs2_stats.reads,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    {
        CTL_NAME(4)
        .procname = "writes",
        .data     = &g_pvfs2_stats.writes,
        .maxlen   = sizeof(unsigned long),
        .mode     = 0444,
        .proc_handler = &proc_dointvec,
    },
    { CTL_NAME(CTL_NONE) }
};



static struct ctl_table pvfs2_table[] = {
    /* outputs the available debugging keywords */
    {
        CTL_NAME(14)
        .procname = DEBUG_HELP,
        .data = &debug_help_string,
        .maxlen = PVFS2_MAX_DEBUG_STRING_LEN,
        .mode = 0444,
        .proc_handler = &pvfs2_proc_debug_mask_handler
    },
    /* controls client-core debugging level */
    {
        CTL_NAME(1)
        .procname = CLIENT_DEBUG,
        .data = &client_debug_string,
        .maxlen = PVFS2_MAX_DEBUG_STRING_LEN,  
        .mode = 0644,
        .proc_handler = &pvfs2_proc_debug_mask_handler
    },
    /* controls kernel debugging level using string input */
    {
        CTL_NAME(2)
       .procname = KERNEL_DEBUG,
       .data = &kernel_debug_string,
       .maxlen = PVFS2_MAX_DEBUG_STRING_LEN,
       .mode = 0644,
       .proc_handler = &pvfs2_proc_debug_mask_handler
    },
    /* operation timeout */
    {
        CTL_NAME(3)
        .procname = "op-timeout-secs",
        .data = &op_timeout_secs,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &proc_dointvec_minmax,
        CTL_STRATEGY(&sysctl_intvec)
        .extra1 = &min_op_timeout_secs,
        .extra2 = &max_op_timeout_secs
    },
    /* slot timeout */
    {
        CTL_NAME(4)
        .procname = "slot-timeout-secs",
        .data = &slot_timeout_secs,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &proc_dointvec_minmax,
        CTL_STRATEGY(&sysctl_intvec)
        .extra1 = &min_slot_timeout_secs,
        .extra2 = &max_slot_timeout_secs
    },
    /* time interval for client side performance counters */
    {
        CTL_NAME(5)
        .procname = "perf-time-interval-secs",
        .maxlen = sizeof(int), 
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &perf_time_interval_extra
    },
    /* time interval for client side performance counters */
    {
        CTL_NAME(6)
        .procname = "perf-history-size",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &perf_history_size_extra
    },
    /* reset performance counters */
    {
        CTL_NAME(7)
        .procname = "perf-counter-reset",
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = &pvfs2_param_proc_handler,
        .extra1 = &perf_reset_extra,
    },
    /* subdir for acache control */
    {
        CTL_NAME(8)
        .procname = "acache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_acache_table
    },
    {
        CTL_NAME(10)
        .procname = "perf-counters",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_pc_table
    },
    /* subdir for ncache control */
    {
        CTL_NAME(11)
        .procname = "ncache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_ncache_table
    },
    /* subdir for credential cache control */
    {   
        CTL_NAME(12)
        .procname = "ccache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_ccache_table
    },
    /* statistics maintained by the kernel module (output only below this) */
    {
        CTL_NAME(13)
        .procname = "stats",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_stats_table
    },
    /* subdir for capability cache control */
    {
        CTL_NAME(14)
        .procname = "capcache",
        .maxlen = 0,
        .mode = 0555,
        .child = pvfs2_capcache_table
    },
    { CTL_NAME(CTL_NONE) }
};
static struct ctl_table fs_table[] = {
    {
        CTL_NAME(13)
        .procname = "pvfs2",
        .mode = 0555,
        .child = pvfs2_table
    },
    { CTL_NAME(CTL_NONE) }
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

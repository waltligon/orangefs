/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

/**
 * a perf counter (pc) has a linked list of samples (pc->sample) that
 * in turn has a start time, and interval, and a pointer to an array of 
 * counters.
 */

#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <sys/time.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-util.h"
#include "pint-perf-counter.h"
#include "pint-util.h"
#include "gossip.h"

#define PINT_PERF_REALLOC_ARRAY(__pc, __tmp_ptr, __src_ptr, __new_history, __type) \
{                                                                      \
    __tmp_ptr = (__type *)malloc(__new_history * sizeof(__type));      \
    if(!__tmp_ptr)                                                     \
        return(-PVFS_ENOMEM);                                          \
    memset(__tmp_ptr, 0, (__new_history * sizeof(__type)));            \
    memcpy(__tmp_ptr, __src_ptr,                                       \
        (__pc->history_size * sizeof(__type)));                        \
    free(__src_ptr);                                                   \
    __src_ptr = __tmp_ptr;                                             \
}

/**
 * track performance counters for the server 
 * keys must be defined here in order based on the 
 * enumeration in include/pvfs2-mgmt.h
 */
struct PINT_perf_key server_keys[] =
{
    {"bytes read", PINT_PERF_READ, PINT_PERF_PRESERVE},
    {"bytes written", PINT_PERF_WRITE, PINT_PERF_PRESERVE},
    {"metadata reads", PINT_PERF_METADATA_READ, PINT_PERF_PRESERVE},
    {"metadata writes", PINT_PERF_METADATA_WRITE, PINT_PERF_PRESERVE},
    {"metadata dspace ops", PINT_PERF_METADATA_DSPACE_OPS, PINT_PERF_PRESERVE},
    {"metadata keyval ops", PINT_PERF_METADATA_KEYVAL_OPS, PINT_PERF_PRESERVE},
    {"request scheduler", PINT_PERF_REQSCHED, PINT_PERF_PRESERVE},
    {"requests received ", PINT_PERF_REQUESTS, PINT_PERF_PRESERVE},
    {"bytes read by small_io", PINT_PERF_SMALL_READ, PINT_PERF_PRESERVE},
    {"bytes written by small_io", PINT_PERF_SMALL_WRITE, PINT_PERF_PRESERVE},
    {"bytes read by flow", PINT_PERF_FLOW_READ, PINT_PERF_PRESERVE},
    {"bytes written by flow", PINT_PERF_FLOW_WRITE, PINT_PERF_PRESERVE},
    {"create requests called", PINT_PERF_CREATE, PINT_PERF_PRESERVE},
    {"remove requests called", PINT_PERF_REMOVE, PINT_PERF_PRESERVE},
    {"mkdir requests called", PINT_PERF_MKDIR, PINT_PERF_PRESERVE},
    {"rmdir requests called", PINT_PERF_RMDIR, PINT_PERF_PRESERVE},
    {"getattr requests called", PINT_PERF_GETATTR, PINT_PERF_PRESERVE},
    {"setattr requests called", PINT_PERF_SETATTR, PINT_PERF_PRESERVE},
    {NULL, 0, 0},
};

/**
 * this utility removes all of the samples from a perf counter
 * this is mostly for cleanup in case of a memory alloc error
 */
void PINT_free_pc (struct PINT_perf_counter *pc)
{
    struct PINT_perf_sample *tmp, *tmp2;
    if (!pc)
        return;
    tmp = pc->sample;
    while(tmp)
    {
        if (tmp->value)
            free (tmp->value);
        tmp2 = tmp;
        tmp = tmp->next;
        free (tmp2);
    }
    free(pc);
}

/** 
 * creates a new perf counter instance
 * \note key_array must not be freed by caller until after
 * PINT_perf_finalize()
 * \returns pointer to perf counter on success, NULL on failure
 */
struct PINT_perf_counter *PINT_perf_initialize(struct PINT_perf_key *key_array)
{
    struct PINT_perf_counter *pc = NULL;
    struct PINT_perf_key *key = NULL;
    int i;
    struct PINT_perf_sample *tmp;

    pc = (struct PINT_perf_counter *)malloc(sizeof(struct PINT_perf_counter));
    if(!pc)
    {
        return(NULL);
    }
    memset(pc, 0, sizeof(struct PINT_perf_counter));
    gen_mutex_init(&pc->mutex);
    pc->key_array = key_array;

    key = &key_array[pc->key_count]; /* key count is zero */
    while(key->key_name)
    {
        /* keys must be in order from zero */
        if(key->key != pc->key_count)
        {
            gossip_err("Error: PINT_perf_initialize(): key out of order.\n");
            gen_mutex_destroy(&pc->mutex);
            free(pc);
            return(NULL);
        }
        
        pc->key_count++;
        key = &key_array[pc->key_count];
    }
    if(pc->key_count < 1)
    {
        gossip_err("Error: PINT_perf_initialize(): no keys specified.\n");
        gen_mutex_destroy(&pc->mutex);
        free(pc);
        return(NULL);
    }

    /* running will be used to decide if we should start an update process */
    pc->history_size = PERF_DEFAULT_HISTORY_SIZE;
    pc->running = (pc->history_size > 1);
    pc->interval = PERF_DEFAULT_UPDATE_INTERVAL;

    /* create a simple linked list of samples, each with a value array */
    tmp = (struct PINT_perf_sample *)malloc(sizeof (struct PINT_perf_sample));
    if(!tmp)
    {
        gen_mutex_destroy(&pc->mutex);
        free(pc);
        return(NULL);
    }
    memset(tmp, 0, sizeof(struct PINT_perf_sample));
    tmp->next = NULL;
    tmp->value = (int64_t *)malloc(pc->key_count * sizeof(int64_t));
    if(!tmp->value)
    {
        gen_mutex_destroy(&pc->mutex);
        PINT_free_pc(pc);
        return(NULL);
    }
    memset(tmp->value, 0, pc->key_count * sizeof(int64_t));
    pc->sample = tmp;
    for (i = pc->history_size - 1; i > 0 && tmp; i--)
    {
        tmp->next = (struct PINT_perf_sample *)
                        malloc(sizeof (struct PINT_perf_sample));
        if(!tmp->next)
        {
            gen_mutex_destroy(&pc->mutex);
            PINT_free_pc(pc);
            return(NULL);
        }
        memset(tmp->next, 0, sizeof(struct PINT_perf_sample));
        tmp->next->next = NULL;
        tmp->next->value = (int64_t *)malloc(pc->key_count * sizeof(int64_t));
        if(!tmp->value)
        {
            gen_mutex_destroy(&pc->mutex);
            PINT_free_pc(pc);
            return(NULL);
        }
        memset(tmp->next->value, 0, pc->key_count * sizeof(int64_t));
        tmp = tmp->next;
    }

    /* set initial timestamp */
    pc->sample->start_time_ms = PINT_util_get_time_ms();

    return(pc);
}

/**
 * resets all counters within a perf counter instance, except for those that
 * have the PRESERVE bit set
 */
void PINT_perf_reset(struct PINT_perf_counter* pc)
{
    int i;
    struct PINT_perf_sample *s;

    gen_mutex_lock(&pc->mutex);

    if (!pc || !pc->sample || !pc->sample->value)
        return;
    for(s = pc->sample; s; s = s->next)
    {
        /* zero out all fields */
        memset(&s->start_time_ms, 0, sizeof(uint64_t));
        memset(&s->interval_ms, 0, sizeof(uint64_t));
        for(i = 0; i < pc->key_count; i++)
        {
            if(!(pc->key_array[i].flag & PINT_PERF_PRESERVE))
            {
                memset(&s->value[i], 0, sizeof(int64_t));
            }
        }
    }

    /* set initial timestamp */
    pc->sample->start_time_ms = PINT_util_get_time_ms();

    gen_mutex_unlock(&pc->mutex);

    return;
}

/** 
 * destroys a perf counter instance
 */
void PINT_perf_finalize(
        struct PINT_perf_counter *pc)    /**< pointer to counter instance */
{
    PINT_free_pc(pc);
    return;
}

/**
 * performs an operation on the given key within a performance counter
 * \see PINT_perf_count macro
 */
void __PINT_perf_count( struct PINT_perf_counter* pc,
                        int key, 
                        int64_t value,
                        enum PINT_perf_ops op)
{
#if 0
    int64_t tmp; /* this is for debugging purposes */
#endif

    if(!pc || !pc->sample || !pc->sample->value)
    {
        /* do nothing if perf counter is not initialized */
        return;
    }

    gen_mutex_lock(&pc->mutex);

#if 0
    tmp = pc->sample->value[key];
#endif

    if(key >= pc->key_count)
    {
        gossip_err("Error: PINT_perf_count(): invalid key.\n");
        goto errorout;
    }

    switch(op)
    {
        case PINT_PERF_ADD:
            pc->sample->value[key] += value;
            break;
        case PINT_PERF_SUB:
            pc->sample->value[key] -= value;
            break;
        case PINT_PERF_SET:
            pc->sample->value[key] = value;
            break;
    }

#if 0
/* debug code shows counters being manipulated */
gossip_err("COUNT %d %lld was %lld is now %lld\n",
key,
(unsigned long long)value,
(unsigned long long)tmp,
(unsigned long long)pc->sample->value[key]);
#endif

errorout:
    gen_mutex_unlock(&pc->mutex);
    return;
}

/** 
 * rolls over the current history window
 */
void PINT_perf_rollover( struct PINT_perf_counter* pc)
{
    int i;
    uint64_t int_time;
    struct PINT_perf_sample *head, *tail;

    if(!pc || !pc->sample || !pc->sample->value)
    {
        /* do nothing if perf counter is not initialized */
        return;
    }

    int_time = PINT_util_get_time_ms();

    gen_mutex_lock(&pc->mutex);

    /*
     * rotate newest sample to the back
     *
     * sample1 -> sample2 -> sample3 -> NULL
     *
     * head = sample1
     *
     * tail = sample3
     *
     * pc->sample = sample2
     *
     * sample3 -> sample1
     * 
     * sample1 -> NULL
     *
     * sample2 -> sample3 -> sample1 -> NULL
     *
     * associate the "current" values with the "current" sample.
     */
    head = pc->sample;
    for(tail = head; tail && tail->next; tail = tail->next);
    if(head != tail)
    {
        /* move head to the tail */
        pc->sample = head->next;
        tail->next = head;
        head->next = NULL;
        memcpy(pc->sample->value,
               head->value,
               pc->key_count * sizeof *head->value);
    }

    /* reset times for next interval */
    pc->sample->start_time_ms = int_time;
    pc->sample->interval_ms = 0;

    for(i = 0; i < pc->key_count; i++)
    {
        if(!(pc->key_array[i].flag & PINT_PERF_PRESERVE))
        {
            memset(&pc->sample->value[i], 0, sizeof(int64_t));
        }
    }

    gen_mutex_unlock(&pc->mutex);

    return;
}

/**
 * sets runtime tunable performance counter options 
 * \returns 0 on success, -PVFS_error on failure
 */
int PINT_perf_set_info(  struct PINT_perf_counter* pc,
                         enum PINT_perf_option option,
                         unsigned int arg)
{
    if(!pc || !pc->sample || !pc->sample->value)
    {
        /* do nothing if perf counter is not initialized */
        return 0;
    }

    if (arg < 1)
    {
        /* bad argument */
        return(-PVFS_EINVAL);
    }

    gen_mutex_lock(&pc->mutex);
    switch(option)
    {
    case PINT_PERF_HISTORY_SIZE:
        if(arg <= pc->history_size)
        {
            while(arg < pc->history_size)
            {
                struct PINT_perf_sample *s;
                /* remove one sample from list */
                s = pc->sample->next;
                if (s)
                {
                    /* removing just behind first sample */
                    pc->sample->next = s->next;
                    s->next = NULL;
                    free(s->value);
                    free(s);
                    pc->history_size--;
                }
                else
                {
                    /* something is wrong */
                    gen_mutex_unlock(&pc->mutex);
                    return(-PVFS_EINVAL);
                }
            }
            /* if history_size is now 1 stop the rollover SM */
            pc->running = (pc->history_size > 1);
        }
        else
        {
            while(arg > pc->history_size)
            {
                struct PINT_perf_sample *s;
                /* add one sample to list */
                s = (struct PINT_perf_sample *)
                        malloc(sizeof(struct PINT_perf_sample));
                if(!s)
                {
                    gen_mutex_unlock(&pc->mutex);
                    return(-PVFS_ENOMEM);
                }
                memset(s, 0, sizeof(struct PINT_perf_sample));
                s->value = calloc(pc->key_count, sizeof *(s->value));
                if(!s->value)
                {
                    free(s);
                    gen_mutex_unlock(&pc->mutex);
                    return(-PVFS_ENOMEM);
                }
                /* adding just after first sample */
                s->next = pc->sample->next;
                pc->sample->next = s;
                pc->history_size++;
            }
            /* if not running start rollover SM */
            pc->running = (pc->history_size > 1);
        }
        break;
    case PINT_PERF_UPDATE_INTERVAL:
        if (arg > 0)
            pc->interval = arg;
        break;
    default:
        gen_mutex_unlock(&pc->mutex);
        return(-PVFS_EINVAL);
    }
    
    gen_mutex_unlock(&pc->mutex);
    return(0);
}

/**
 * retrieves runtime tunable performance counter options 
 * \returns 0 on success, -PVFS_error on failure
 */
int PINT_perf_get_info( struct PINT_perf_counter* pc,
                        enum PINT_perf_option option,
                        unsigned int* arg)
{
    if(!pc)
    {
        /* do nothing if perf counter is not initialized */
        return (0);
    }

    gen_mutex_lock(&pc->mutex);
    switch(option)
    {
    case PINT_PERF_HISTORY_SIZE:
        *arg = pc->history_size;
        break;
    case PINT_PERF_KEY_COUNT:
        *arg = pc->key_count;
        break;
    case PINT_PERF_UPDATE_INTERVAL:
        *arg = pc->interval;
        break;
    default:
        gen_mutex_unlock(&pc->mutex);
        return(-PVFS_EINVAL);
    }
    
    gen_mutex_unlock(&pc->mutex);
    return(0);
}

/**
 * retrieves measurement history
 *
 * This copies the data from the samples (stored in a linked list) into
 * a temporary array where they can be inspected without worry of update
 * this array will store up to max_key counters PLUS two time values, the
 * start time and intervale, both as ms counts.  The samples might have
 * more or less keys in them, and the system might have more or less
 * samples than space in the array.
 *
 * the array is really a 2D Matrix (keys+times vs history) but is treated
 * as a 1D array because the sizes aren't well known until runtime, and
 * even then can change.  This results in some 2D indexing (i*max_key+2) 
 * in the loop where
 * data is copied from the samples to the value_array.  Also the location
 * of the time stampls is generally index max_key, and max_key+1
 */
void PINT_perf_retrieve(
        struct PINT_perf_counter* pc,    /* performance counter */
        int64_t *value_array,            /* array of output measurements */
        int max_key,                     /* max key value (1st dimension) */
        int max_history)                 /* max history (2nd dimension) */
{
    int i;
    int tmp_max_key;
    int tmp_max_history;
    uint64_t int_time;
    struct PINT_perf_sample *s;

    if(!pc || !pc->sample || !pc->sample->value)
    {
        /* do nothing if perf counter is not initialized */
        return;
    }

    gen_mutex_lock(&pc->mutex);

    /* it isn't very safe to allow the caller to ask for more keys than are
     * available, because they will probably overrun key array bounds when
     * interpretting results
     */
    assert(max_key <= pc->key_count);
    
    tmp_max_key = PVFS_util_min(max_key, pc->key_count);
    tmp_max_history = PVFS_util_min(max_history, pc->history_size);

    if(max_key > pc->key_count || max_history > pc->history_size)
    {
        memset(value_array, 0,
                (max_history * (max_key + 2) * sizeof(int64_t)));
    }

    /* copy data out */
    /* running sample list, and counting at the same time */
    /* i is the sample index, they keys of each sample stay together */
    /* there are max_key+2 spaces in the destination for each sample */
    /* but we will only copy tmp_max_key and the time stamps - which */
    /* should be less than or equal to the space available */
    /* normally, max_key == tmp_max_key */
    for(i = 0, s = pc->sample; i < tmp_max_history && s; i++, s = s->next)
    {
        /* copy counters */
        memcpy(&value_array[i * (max_key + 2)], s->value,
                            (tmp_max_key * sizeof(int64_t)));
        /* copy time codes */
        value_array[(i * (max_key + 2)) + max_key] = s->start_time_ms;
        value_array[(i * (max_key + 2)) + max_key + 1] = s->interval_ms;
    }

#if 0
/* debug code prints first sample to log */
{int k; for(k=0;k<max_key;k++)
gossip_err("sample value[%d] = %lld\n",k,pc->sample->value[k]);}
#endif
    
    gen_mutex_unlock(&pc->mutex);

    /* fill in interval length for newest interval */
    int_time = PINT_util_get_time_ms();
    if(int_time > value_array[max_key])
    {
        value_array[max_key + 1] = int_time - value_array[max_key];
    }

    /* auto-rollover when data is retrieved */
    /* this may obviate last step above */
    PINT_perf_rollover(pc);
    
    return;
}

char *PINT_perf_generate_text( struct PINT_perf_counter* pc,
                               int max_size)
{
    int total_size = 0;
    int line_size = 0;
    int actual_size = 0;
    char *tmp_str;
    char *position;
    int i, j;
    uint64_t int_time;
    time_t tmp_time_t;
    struct tm tmp_tm;
#ifdef WIN32
    struct tm *ptmp_tm;
#endif
    int ret;
    struct PINT_perf_sample *s = NULL;

    if (!pc || !pc->sample || !pc->sample->value)
    {
        return NULL;
    }

    gen_mutex_lock(&pc->mutex);
    
    line_size = 26 + (24 * pc->history_size); 
    total_size = (pc->key_count + 2) * line_size + 1;
    
    actual_size = PVFS_util_min(total_size, max_size);

    if((actual_size / line_size) < 3)
    {
        /* don't bother trying to display anything, can't fit any results in
         * that size
         */
        return(NULL);
    }

    tmp_str = (char *)malloc(actual_size * sizeof(char));
    if(!tmp_str)
    {
        gen_mutex_unlock(&pc->mutex);
        return(NULL);
    }
    position = tmp_str;

    /* start times */
    sprintf(position, "%-24.24s: ", "Start times (hr:min:sec)");
    position += 25;
    for(i = 0, s = pc->sample; i < pc->history_size && s; i++, s = s->next)
    {
        PVFS_time start_i = (PVFS_time)s->start_time_ms;
        if(start_i)
        {
            tmp_time_t = start_i / 1000;
#ifdef WIN32
            ptmp_tm = localtime(&tmp_time_t);
            tmp_tm = *ptmp_tm;
#else
            localtime_r(&tmp_time_t, &tmp_tm);
#endif
            strftime(position, 11, "  %H:%M:%S", &tmp_tm);
            position += 10;
            sprintf(position, ".%03u", (unsigned)(start_i % 1000));
            position += 4;
        }
        else
        {
            sprintf(position, "%14d", 0);
            position += 14;
        }
    }
    sprintf(position, "\n");
    position++;

    /* fill in interval length for newest interval */
    int_time = PINT_util_get_time_ms();
    if(int_time > pc->sample->interval_ms)
    {
        pc->sample->interval_ms = int_time - pc->sample->start_time_ms;
    }

    /* intervals */
    sprintf(position, "%-24.24s:", "Intervals (hr:min:sec)");
    position += 25;
    for(i = 0, s = pc->sample; i < pc->history_size && s; i++, s = s->next)
    {
        PVFS_time interval_i = s->interval_ms;
        if(interval_i)
        {
            tmp_time_t = interval_i / 1000;
#ifdef WIN32
            gmtime_s(&tmp_tm, &tmp_time_t);
#else
            gmtime_r(&tmp_time_t, &tmp_tm);
#endif
            strftime(position, 11, "  %H:%M:%S", &tmp_tm);
            position += 10;
            sprintf(position, ".%03u", (unsigned)(interval_i % 1000));
            position += 4;
        }
        else
        {
            sprintf(position, "%14d", 0);
            position += 14;
        }

    }
    sprintf(position, "\n");
    position++;

    sprintf(position, "-------------------------");
    position += 25;
    for(i = 0; i < pc->history_size; i++)
    {
        sprintf(position, "--------------");
        position += 14;
    }
    sprintf(position, "\n");
    position++;

    /* values */
    for(i = 0; i < pc->key_count; i++)
    {
        sprintf(position, "%-24.24s:", pc->key_array[i].key_name);
        position += 25;
        for(j = 0, s = pc->sample; j < pc->history_size && s; j++, s = s->next)
        {
#ifdef WIN32
            ret = _snprintf(position, 15, " %13Ld", lld(s->value[i]));
#else
            ret = snprintf(position, 15, " %13Ld", lld(s->value[i]));
#endif
            if(ret >= 15)
            {
                sprintf(position, "%14.14s", "Overflow!");
            }
            position += 14;
        }
        sprintf(position, "\n");
        position++;
    }

    gen_mutex_unlock(&pc->mutex);

    return(tmp_str);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

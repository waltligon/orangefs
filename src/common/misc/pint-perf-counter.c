/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "pvfs2-types.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"
#include "pint-perf-counter.h"
#include "gossip.h"

#define PINT_PERF_REALLOC_ARRAY(__pc, __tmp_ptr, __src_ptr, __new_history, __type) \
{                                                                      \
    __tmp_ptr = (__type*)malloc(__new_history*sizeof(__type));         \
    if(!__tmp_ptr)                                                     \
        return(-PVFS_ENOMEM);                                          \
    memset(__tmp_ptr, 0, (__new_history*sizeof(__type)));              \
    memcpy(__tmp_ptr, __src_ptr,                                       \
        (__pc->history_size*sizeof(__type)));                          \
    free(__src_ptr);                                                   \
    __src_ptr = __tmp_ptr;                                             \
}

/** 
 * creates a new perf counter instance
 * \note key_array must not be freed by caller until after
 * PINT_perf_finalize()
 * \returns pointer to perf counter on success, NULL on failure
 */
struct PINT_perf_counter* PINT_perf_initialize(
    struct PINT_perf_key* key_array) /**< NULL terminated array of keys */
{
    struct PINT_perf_counter* pc = NULL;
    struct PINT_perf_key* key = NULL;
    int i;
    struct timeval tv;

    pc = (struct PINT_perf_counter*)malloc(sizeof(struct PINT_perf_counter));
    if(!pc)
    {
        return(NULL);
    }
    memset(pc, 0, sizeof(struct PINT_perf_counter));
    gen_mutex_init(&pc->mutex);
    pc->key_array = key_array;

    key = &key_array[pc->key_count];
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

    pc->history_size = PERF_DEFAULT_HISTORY_SIZE;

    /* allocate time arrays */
    pc->start_time_array_ms =
        (uint64_t*)malloc(PERF_DEFAULT_HISTORY_SIZE*sizeof(uint64_t));
    if(!pc->start_time_array_ms)
    {
        gen_mutex_destroy(&pc->mutex);
        free(pc);
        return(NULL);
    }
    pc->interval_array_ms = 
        (uint64_t*)malloc(PERF_DEFAULT_HISTORY_SIZE*sizeof(uint64_t));
    if(!pc->interval_array_ms)
    {
        free(pc->start_time_array_ms);
        gen_mutex_destroy(&pc->mutex);
        free(pc);
        return(NULL);
    }
    memset(pc->start_time_array_ms, 0,
        PERF_DEFAULT_HISTORY_SIZE*sizeof(uint64_t));
    memset(pc->interval_array_ms, 0,
        PERF_DEFAULT_HISTORY_SIZE*sizeof(uint64_t));
        
    /* allocate value matrix */
    pc->value_matrix = (int64_t**)malloc(pc->key_count*sizeof(int64_t*));
    if(!pc->value_matrix)
    {
        free(pc->start_time_array_ms);
        free(pc->interval_array_ms);
        gen_mutex_destroy(&pc->mutex);
        free(pc);
        return(NULL);
    }

    for(i=0; i<pc->key_count; i++)
    {
        pc->value_matrix[i] =
            (int64_t*)malloc(pc->history_size*sizeof(int64_t));
        if(!pc->value_matrix[i])
        {
            for(i=i-1; i>= 0; i--)
            {
                free(pc->value_matrix[i]);
            }
            free(pc->value_matrix);
            free(pc->start_time_array_ms);
            free(pc->interval_array_ms);
            gen_mutex_destroy(&pc->mutex);
            free(pc);
            return(NULL);
        }
        memset(pc->value_matrix[i], 0, pc->history_size*sizeof(int64_t));
    }

    /* set initial timestamp */
    gettimeofday(&tv, NULL);
    pc->start_time_array_ms[0] = ((uint64_t)tv.tv_sec)*1000 +
	tv.tv_usec/1000;

    return(pc);
}

/**
 * resets all counters within a perf counter instance, except for those that
 * have the PRESERVE bit set
 */
void PINT_perf_reset(
    struct PINT_perf_counter* pc)
{
    int i;
    struct timeval tv;

    gen_mutex_lock(&pc->mutex);

    /* zero out all fields */
    memset(pc->start_time_array_ms, 0,
        PERF_DEFAULT_HISTORY_SIZE*sizeof(uint64_t));
    memset(pc->interval_array_ms, 0,
        PERF_DEFAULT_HISTORY_SIZE*sizeof(uint64_t));
    for(i=0; i<pc->key_count; i++)
    {
        if(!(pc->key_array[i].flag & PINT_PERF_PRESERVE))
        {
            memset(pc->value_matrix[i], 0, pc->history_size*sizeof(int64_t));
        }
    }

    /* set initial timestamp */
    gettimeofday(&tv, NULL);
    pc->start_time_array_ms[0] = ((uint64_t)tv.tv_sec)*1000 +
	tv.tv_usec/1000;

    gen_mutex_unlock(&pc->mutex);

    return;
}

/** 
 * destroys a perf counter instance
 */
void PINT_perf_finalize(
    struct PINT_perf_counter* pc)    /**< pointer to counter instance */
{
    int i;

    for(i=0; i<pc->key_count; i++)
    {
        free(pc->value_matrix[i]);
    }
    free(pc->value_matrix);
    free(pc->start_time_array_ms);
    free(pc->interval_array_ms);
    gen_mutex_destroy(&pc->mutex);
    free(pc);
    pc = NULL;
    
    return;
}

/**
 * performs an operation on the given key within a performance counter
 * \see PINT_perf_count macro
 */
void __PINT_perf_count(
    struct PINT_perf_counter* pc,
    int key, 
    int64_t value,
    enum PINT_perf_ops op)
{
    if(!pc)
    {
        /* do nothing if perf counter is not initialized */
        return;
    }

    gen_mutex_lock(&pc->mutex);

    if(key >= pc->key_count)
    {
        gossip_err("Error: PINT_perf_count(): invalid key.\n");
        return;
    }

    switch(op)
    {
        case PINT_PERF_ADD:
            pc->value_matrix[key][0] = pc->value_matrix[key][0] + value;
            break;
        case PINT_PERF_SUB:
            pc->value_matrix[key][0] = pc->value_matrix[key][0] - value;
            break;
        case PINT_PERF_SET:
            pc->value_matrix[key][0] = value;
            break;
    }

    gen_mutex_unlock(&pc->mutex);
    return;
}

#ifdef __PVFS2_DISABLE_PERF_COUNTERS__
    #define PINT_perf_count(w,x,y,z) do{}while(0)
#else
    #define PINT_perf_count __PINT_perf_count
#endif

/** 
 * rolls over the current history window
 */
void PINT_perf_rollover(
    struct PINT_perf_counter* pc)
{
    int i;
    struct timeval tv;
    uint64_t int_time;

    if(!pc)
    {
        /* do nothing if perf counter is not initialized */
        return;
    }

    gettimeofday(&tv, NULL);
    int_time = ((uint64_t)tv.tv_sec)*1000 + tv.tv_usec/1000;

    gen_mutex_lock(&pc->mutex);

    /* rotate all values back one */
    if(pc->history_size > 1)
    {
        for(i=0; i<pc->key_count; i++)
        {
            memmove(&pc->value_matrix[i][1], &pc->value_matrix[i][0],
                ((pc->history_size-1)*sizeof(int64_t)));
        }
        memmove(&pc->interval_array_ms[1], &pc->interval_array_ms[0],
            ((pc->history_size-1)*sizeof(uint64_t)));
        memmove(&pc->start_time_array_ms[1], &pc->start_time_array_ms[0],
            ((pc->history_size-1)*sizeof(uint64_t)));
        if(int_time > pc->start_time_array_ms[1])
        {
            pc->interval_array_ms[1] = int_time - pc->start_time_array_ms[1];
        }
    }

    /* reset times for next interval */
    pc->start_time_array_ms[0] = int_time;
    pc->interval_array_ms[0] = 0;

    for(i=0; i<pc->key_count; i++)
    {
        /* reset next interval's value, unless preserve flag set */
        if(!(pc->key_array[i].flag & PINT_PERF_PRESERVE))
        {
            pc->value_matrix[i][0] = 0;
        }
    }

    gen_mutex_unlock(&pc->mutex);

    return;
}

/**
 * sets runtime tunable performance counter options 
 * \returns 0 on success, -PVFS_error on failure
 */
int PINT_perf_set_info(
    struct PINT_perf_counter* pc,
    enum PINT_perf_option option,
    unsigned int arg)
{
    uint64_t* tmp_unsigned;
    int64_t* tmp_signed;
    int i;

    if(!pc)
    {
        /* do nothing if perf counter is not initialized */
        return 0;
    }

    gen_mutex_lock(&pc->mutex);
    switch(option)
    {
        case PINT_PERF_HISTORY_SIZE:
            if(arg <= pc->history_size)
            {
                pc->history_size = arg;
            }
            else
            {
                /* we have to reallocate everything */
                /* NOTE: these macros will return error if needed, and
                 * counter instance will still be operational
                 */
                PINT_PERF_REALLOC_ARRAY(pc,
                    tmp_unsigned,
                    pc->start_time_array_ms,
                    arg,
                    uint64_t);
                PINT_PERF_REALLOC_ARRAY(pc,
                    tmp_unsigned,
                    pc->interval_array_ms,
                    arg,
                    uint64_t);
                for(i=0; i<pc->key_count; i++)
                {
                    PINT_PERF_REALLOC_ARRAY(pc,
                        tmp_signed,
                        pc->value_matrix[i],
                        arg,
                        int64_t);
                }
                pc->history_size = arg;
            }
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
int PINT_perf_get_info(
    struct PINT_perf_counter* pc,
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
        default:
            gen_mutex_unlock(&pc->mutex);
            return(-PVFS_EINVAL);
    }
    
    gen_mutex_unlock(&pc->mutex);
    return(0);
}

/**
 * retrieves measurement history
 */
void PINT_perf_retrieve(
    struct PINT_perf_counter* pc,        /**< performance counter */
    int64_t** value_matrix, /**< 2d matrix to fill in with measurements */
    uint64_t* start_time_array_ms,       /**< array of start times */
    uint64_t* interval_array_ms,         /**< array of interval lengths */
    int max_key,                         /**< max key value (1st dimension) */
    int max_history)                     /**< max history (2nd dimension) */
{
    int i;
    int tmp_max_key;
    int tmp_max_history;
    struct timeval tv;
    uint64_t int_time;

    if(!pc)
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
        /* zero out value matrix, we won't use all fields */
        for(i=0; i<max_key; i++)
        {
            memset(value_matrix[i], 0, (max_history*sizeof(int64_t)));
        }
    }

    if(max_history > pc->history_size)
    {
        /* zero out time arrays, we won't use all fields */
        memset(start_time_array_ms, 0, (max_history*sizeof(uint64_t)));
        memset(interval_array_ms, 0, (max_history*sizeof(uint64_t)));
    }

    /* copy data out */
    for(i=0; i<tmp_max_key; i++)
    {
        memcpy(value_matrix[i], pc->value_matrix[i],
            (tmp_max_history*sizeof(int64_t)));
    }
    memcpy(start_time_array_ms, pc->start_time_array_ms,
        (tmp_max_history*sizeof(uint64_t)));
    memcpy(interval_array_ms, pc->interval_array_ms,
        (tmp_max_history*sizeof(uint64_t)));
    
    gen_mutex_unlock(&pc->mutex);

    /* fill in interval length for newest interval */
    gettimeofday(&tv, NULL);
    int_time = ((uint64_t)tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    if(int_time > start_time_array_ms[0])
    {
        interval_array_ms[0] = int_time - start_time_array_ms[0];
    }
    
    return;
}

char* PINT_perf_generate_text(
    struct PINT_perf_counter* pc,
    int max_size)
{
    int total_size = 0;
    int line_size = 0;
    int actual_size = 0;
    char* tmp_str;
    char* position;
    int i, j;
    uint64_t int_time;
    struct timeval tv;
    time_t tmp_time_t;
    struct tm tmp_tm;
    int ret;

    gen_mutex_lock(&pc->mutex);
    
    line_size = 26 + (24*pc->history_size); 
    total_size = (pc->key_count+2)*line_size + 1;
    
    actual_size = PVFS_util_min(total_size, max_size);

    if((actual_size/line_size) < 3)
    {
        /* don't bother trying to display anything, can't fit any results in
         * that size
         */
        return(NULL);
    }

    tmp_str = (char*)malloc(actual_size*sizeof(char));
    if(!tmp_str)
    {
        gen_mutex_unlock(&pc->mutex);
        return(NULL);
    }
    position = tmp_str;

    /* start times */
    sprintf(position, "%-24.24s: ", "Start times (hr:min:sec)");
    position += 25;
    for(i=0; i<pc->history_size; i++)
    {
        if(pc->start_time_array_ms[i])
        {
            tmp_time_t = pc->start_time_array_ms[i]/1000;
            localtime_r(&tmp_time_t, &tmp_tm);
            strftime(position, 11, "  %H:%M:%S", &tmp_tm);
            position += 10;
            sprintf(position, ".%03u", 
                (unsigned)(pc->start_time_array_ms[i]%1000));
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
    gettimeofday(&tv, NULL);
    int_time = ((uint64_t)tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    if(int_time > pc->start_time_array_ms[0])
    {
        pc->interval_array_ms[0] = int_time - pc->start_time_array_ms[0];
    }

    /* intervals */
    sprintf(position, "%-24.24s:", "Intervals (hr:min:sec)");
    position += 25;
    for(i=0; i<pc->history_size; i++)
    {
        if(pc->interval_array_ms[i])
        {
            tmp_time_t = pc->interval_array_ms[i]/1000;
            gmtime_r(&tmp_time_t, &tmp_tm);
            strftime(position, 11, "  %H:%M:%S", &tmp_tm);
            position += 10;
            sprintf(position, ".%03u", 
                (unsigned)(pc->interval_array_ms[i]%1000));
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
    for(i=0; i<pc->history_size; i++)
    {
        sprintf(position, "--------------");
        position += 14;
    }
    sprintf(position, "\n");
    position++;

    /* values */
    for(i=0; i<pc->key_count; i++)
    {
        sprintf(position, "%-24.24s:", pc->key_array[i].key_name);
        position += 25;
        for(j=0; j<pc->history_size; j++)
        {
            ret = snprintf(position, 15, " %13Ld", lld(pc->value_matrix[i][j]));
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

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_PERF_COUNTER_H
#define __PINT_PERF_COUNTER_H

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"

#define PINT_PERF_HISTORY_SIZE 8

enum PINT_perf_count_keys
{
    PINT_PERF_WRITE = 0,
    PINT_PERF_READ = 1
};
#define PINT_PERF_COUNT_KEY_MAX 1

enum PINT_perf_ops
{
    PINT_PERF_ADD = 0,
    PINT_PERF_SUB = 1
};

int PINT_perf_initialize(void);

void PINT_perf_finalize(void);

void __PINT_perf_count(enum PINT_perf_count_keys key, 
    int64_t value,
    enum PINT_perf_ops op);

#ifdef __PVFS2_DISABLE_PERF_COUNTERS__
    #define PINT_perf_count(x,y,z) do{}while(0)
#else
    #define PINT_perf_count __PINT_perf_count
#endif

void PINT_perf_rollover(void);

void PINT_perf_retrieve(
    uint32_t* next_id,
    struct PVFS_mgmt_perf_stat* perf_array,
    int count,
    uint64_t* end_time_ms);

#endif /* __PINT_PERF_COUNTER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

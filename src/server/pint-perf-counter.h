/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_PERF_COUNTER_H
#define __PINT_PERF_COUNTER_H

#include "pvfs2-types.h"

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

void PINT_perf_count(enum PINT_perf_count_keys key, 
    int64_t value,
    enum PINT_perf_ops op);

int PINT_perf_rollover(void);

#endif /* __PINT_PERF_COUNTER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_THREAD_H__
#define __DBPF_THREAD_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "trove.h"
#include "dbpf.h"

#define DBPF_OPS_PER_WORK_CYCLE 5

int dbpf_thread_intialize(void);

int dbpf_thread_finalize(void);

void *dbpf_thread_function(void *ptr);

int dbpf_do_one_work_cycle(int *out_count);

#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif

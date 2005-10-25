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

int dbpf_thread_initialize(void);

int dbpf_thread_finalize(void);

void *dbpf_thread_function(void *ptr);

int dbpf_do_one_work_cycle(int *out_count);

#define move_op_to_completion_queue(cur_op, ret_state, end_state)  \
do { TROVE_context_id cid = cur_op->op.context_id;                 \
cur_op->state = ret_state;                                         \
context_mutex = dbpf_completion_queue_array_mutex[cid];            \
assert(context_mutex);                                             \
/*                                                                 \
  it's important to atomically place the op in the completion      \
  queue and change the op state to 'end_state' so that dspace_test \
  and dspace_testcontext play nicely together                      \
*/                                                                 \
gen_mutex_lock(context_mutex);                                     \
dbpf_op_queue_add(dbpf_completion_queue_array[cid],cur_op);        \
gen_mutex_lock(&cur_op->mutex);                                    \
cur_op->op.state = end_state;                                      \
gen_mutex_unlock(&cur_op->mutex);                                  \
/* wake up one waiting thread, if any */                           \
pthread_cond_signal(&dbpf_op_completed_cond);                      \
gen_mutex_unlock(context_mutex);                                   \
} while(0)


#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif

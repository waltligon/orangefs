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

#define DBPF_COMPLETION_START(cur_op, end_state)                   \
do {                                                               \
    TROVE_context_id cid = cur_op->op.context_id;                  \
    gen_mutex_lock(&dbpf_completion_queue_array_mutex[cid]);       \
    dbpf_op_queue_add(dbpf_completion_queue_array[cid],cur_op);    \
    gen_mutex_lock(&cur_op->mutex);                                \
    cur_op->op.state = end_state;                                  \
    gen_mutex_unlock(&cur_op->mutex);                              \
} while(0)

#define DBPF_COMPLETION_ADD(__add_op, __endstate)                       \
do {                                                                    \
    gen_mutex_lock(&__add_op->mutex);                                   \
    __add_op->op.state = __endstate;                                    \
    gen_mutex_unlock(&__add_op->mutex);                                 \
    dbpf_op_queue_add(                                                  \
        dbpf_completion_queue_array[__add_op->op.context_id],__add_op); \
} while(0)

#ifdef __PVFS2_TROVE_THREADED__
#define DBPF_COMPLETION_SIGNAL()                                   \
do {                                                               \
    pthread_cond_signal(&dbpf_op_completed_cond);                  \
} while(0)
#else
#define DBPF_COMPLETION_SIGNAL()  do { } while (0)
#endif
    
#define DBPF_COMPLETION_FINISH(__context_id)                           \
do {                                                                   \
    gen_mutex_unlock(&dbpf_completion_queue_array_mutex[__context_id]);\
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

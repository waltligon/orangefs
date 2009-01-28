/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_WORKER_THREADED_QUEUES_H
#define PINT_WORKER_THREADED_QUEUES_H

#include "gen-locks.h"
#include "quicklist.h"
#include "pint-op.h"

typedef struct
{
    /* The number of threads to create for this worker */
    int thread_count;

    /* The number of operations that should be serviced before moving on
     * to the next queue
     */
    int ops_per_queue;

    /* time to wait (in microsecs) for an operation to be added to the queue.
     * 0 means no timeout.
     */
    int timeout;

} PINT_worker_threaded_queues_attr_t;

struct PINT_worker_thread_entry
{
    gen_thread_t thread_id;
    struct PINT_worker_threaded_queues_s *worker;
    gen_mutex_t mutex;
    int running;
    int error;
};

struct PINT_manager_s;

struct PINT_worker_threaded_queues_s
{
    PINT_worker_threaded_queues_attr_t attr;
    struct PINT_worker_thread_entry *threads;
    struct qlist_head queues;
    struct qlist_head inuse_queues;
    gen_mutex_t mutex;
    gen_cond_t cond;
    struct PINT_manager_s *manager;
    int remove_requested;
};

struct PINT_worker_impl PINT_worker_threaded_queues_impl;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include "gossip.h"
#include "trove.h"
#include "trove-internal.h"
#include "trove-ledger.h"
#include "trove-handle-mgmt.h"
#include "dbpf.h"
#include "dbpf-thread.h"
#include "dbpf-bstream.h"
#include "dbpf-op-queue.h"
#include "dbpf-sync.h"
#include "pint-context.h"
#include "pint-mgmt.h"

extern struct qlist_head dbpf_op_queue;
extern gen_mutex_t dbpf_op_queue_mutex;
extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];

#ifdef __PVFS2_TROVE_THREADED__
static pthread_t dbpf_thread;
static int dbpf_thread_running = 0;
pthread_cond_t dbpf_op_incoming_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t dbpf_op_completed_cond = PTHREAD_COND_INITIALIZER;
#endif

extern int TROVE_max_concurrent_io;

int dbpf_thread_initialize(void)
{
    int ret = 0;
#ifdef __PVFS2_TROVE_THREADED__
    ret = -1;

    pthread_cond_init(&dbpf_op_incoming_cond, NULL);
    pthread_cond_init(&dbpf_op_completed_cond, NULL);

    dbpf_thread_running = 1;
    ret = pthread_create(&dbpf_thread, NULL,
                         dbpf_thread_function, NULL);
    if (ret == 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "dbpf_thread_initialize: initialized\n");
    }
    else
    {
        dbpf_thread_running = 0;
        gossip_debug(
            GOSSIP_TROVE_DEBUG, "dbpf_thread_initialize: failed (1)\n");
    }

#endif
    return ret;
}

int dbpf_thread_finalize(void)
{
    int ret = 0;
#ifdef __PVFS2_TROVE_THREADED__
    dbpf_thread_running = 0;
    ret = pthread_join(dbpf_thread, NULL);

    pthread_cond_destroy(&dbpf_op_completed_cond);
    pthread_cond_destroy(&dbpf_op_incoming_cond);
#endif
    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_thread_finalize: finalized\n");
    return ret;
}

int synccount = 0;

void *dbpf_thread_function(void *ptr)
{
#ifdef __PVFS2_TROVE_THREADED__
    int out_count = 0, op_queued_empty = 0, ret = 0;
    struct timeval base;
    struct timespec wait_time;

    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_thread_function started\n");

    PINT_event_thread_start("TROVE-DBPF");
    while(dbpf_thread_running)
    {
        /* check if we any have ops to service in our work queue */
        gen_mutex_lock(&dbpf_op_queue_mutex);
        op_queued_empty = qlist_empty(&dbpf_op_queue);

        if (!op_queued_empty)
        {
            gen_mutex_unlock(&dbpf_op_queue_mutex);
            dbpf_do_one_work_cycle(&out_count);
#ifndef __PVFS2_TROVE_AIO_THREADED__
            if(out_count == 0)
            {
                /* if we aren't using aio callbacks, and the outcount is
                 * zero, then that means that the only ops in the queue are
                 * I/O operations that we can do nothing with except call
                 * aio_error() repeatedly.  
                 */
                /* There is no convenient way to handle this.  Just sleep
                 * breifly to prevent busy spin (which means we may sleep
                 * through a metadata op being posted).  If someone cares about
                 * optimizing this case then the solution is to explicitly
                 * track if any of the operations in the queue are metadata
                 * operations or not so that we know up front to do a
                 * cond_timedwait() here.
                 */
                wait_time.tv_sec = 0;
                wait_time.tv_nsec = 1000;
                nanosleep(&wait_time, NULL);
            }
#endif
        }
        else
        {
            /* compute how long to wait */
            gettimeofday(&base, NULL);
            wait_time.tv_sec = base.tv_sec +
                (TROVE_DEFAULT_TEST_TIMEOUT / 1000);
            wait_time.tv_nsec = base.tv_usec * 1000 + 
                ((TROVE_DEFAULT_TEST_TIMEOUT % 1000) * 1000000);
            if (wait_time.tv_nsec > 1000000000)
            {
                wait_time.tv_nsec = wait_time.tv_nsec - 1000000000;
                wait_time.tv_sec++;
            }

            ret = pthread_cond_timedwait(&dbpf_op_incoming_cond,
                                         &dbpf_op_queue_mutex,
                                         &wait_time);
            gen_mutex_unlock(&dbpf_op_queue_mutex);
        }
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_thread_function ending\n");
    PINT_event_thread_stop();
#endif
    return ptr;
}

int dbpf_do_one_work_cycle(int *out_count)
{
#ifdef __PVFS2_TROVE_THREADED__
    int ret = 1;
    int max_num_ops_to_service = DBPF_OPS_PER_WORK_CYCLE;
    dbpf_queued_op_t *cur_op = NULL;
#endif

    assert(out_count);
    *out_count = 0;

#ifdef __PVFS2_TROVE_THREADED__
    do
    {
        /* grab next op from queue and mark it as in service */
        gen_mutex_lock(&dbpf_op_queue_mutex);
        cur_op = dbpf_op_queue_shownext(&dbpf_op_queue);
        if (cur_op)
        {
            gen_mutex_lock(&cur_op->mutex);
            if (cur_op->op.state != OP_QUEUED)
            {
                gossip_err("INVALID OP STATE FOUND %d (op is %p)\n",
                           cur_op->op.state, cur_op);
                assert(cur_op->op.state == OP_QUEUED);
            }

            dbpf_queued_op_dequeue_nolock(cur_op);
            if(DBPF_OP_IS_KEYVAL(cur_op->op.type)) 
            {
                --synccount;
                gossip_debug(GOSSIP_TROVE_DEBUG, "[DBPF THREAD]: [KEYVAL -1]: %d\n", synccount);
            }

            cur_op->op.state = OP_IN_SERVICE;
            gen_mutex_unlock(&cur_op->mutex);
        }
        gen_mutex_unlock(&dbpf_op_queue_mutex);

        /* if there's no work to be done, return immediately */
        if (cur_op == NULL)
        {
            return ret;
        }

        /* otherwise, service the current operation now */
        gossip_debug(GOSSIP_TROVE_OP_DEBUG,"[DBPF THREAD]: STARTING TROVE "
                     "SERVICE ROUTINE (%s)\n",
                     dbpf_op_type_to_str(cur_op->op.type));

        ret = cur_op->op.svc_fn(&(cur_op->op));

        gossip_debug(GOSSIP_TROVE_OP_DEBUG,"[DBPF THREAD]: FINISHED TROVE "
                     "SERVICE ROUTINE (%s) (ret: %d)\n",
                     dbpf_op_type_to_str(cur_op->op.type),
                     ret);
        if (ret == DBPF_OP_COMPLETE || ret < 0)
        {
            /* Some dbpf calls may return non-fatal errors
             * (for example, a crdirent for an entry that already exists).
             * If the dbpf call returned an error, we need to call
             * sync_coalesce, which will sync the database, and return
             * and move _all_ the ready-to-be-synced operations to the
             * completion queue.
             */
            ret = dbpf_sync_coalesce(cur_op, (ret == 1 ? 0 : ret), out_count);
            if(ret < 0)
            {
                return ret; /* not sure how to recover from failure here */
            }
        }
        else if(ret == -DBPF_ERROR_UNKNOWN || ret == DBPF_ERROR_UNKNOWN)
        {
            /* If we get an error back from Berkeley DB, we assume fatal
             * and just return.  Make sure the return code is negative
             * here though.
             */
            return (ret < 0) ? ret : -ret;
        }
        else
        {
#ifndef __PVFS2_TROVE_AIO_THREADED__
            /*
              check if trove is telling us to NOT mark this as
              completed, and also to NOT re-add it to the service
              queue.  this can happen if trove is throttling I/O
              internally and will handle re-starting the operation
              without our help.
            */
            if (cur_op->op.state == OP_INTERNALLY_DELAYED)
            {
                continue;
            }
#endif
            assert(cur_op->op.state != OP_COMPLETED);
            dbpf_queued_op_queue(cur_op);
        }

    } while(--max_num_ops_to_service);
#endif

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

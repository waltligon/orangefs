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
#include "dbpf-dspace.h"
#include "dbpf-bstream.h"
#include "dbpf-keyval.h"
#include "dbpf-op-queue.h"

extern struct qlist_head dbpf_op_queue;
extern gen_mutex_t dbpf_op_queue_mutex;
extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];

#ifdef __PVFS2_TROVE_THREADED__
static pthread_t dbpf_thread;
static int dbpf_thread_running = 0;
#endif

int dbpf_thread_initialize(void)
{
    int ret = 0;
#ifdef __PVFS2_TROVE_THREADED__
    ret = pthread_create(&dbpf_thread, NULL, dbpf_thread_function, NULL);
    dbpf_thread_running = ((ret == 0) ? 1 : 0);
#endif
    return ret;
}

int dbpf_thread_finalize(void)
{
    int ret = 0;
#ifdef __PVFS2_TROVE_THREADED__
    dbpf_thread_running = 0;
    usleep(500);
    ret = pthread_cancel(dbpf_thread);
#endif
    return ret;
}

void *dbpf_thread_function(void *ptr)
{
    gossip_debug(TROVE_DEBUG, "thread function started\n");

#ifdef __PVFS2_TROVE_THREADED__
    do
    {
        usleep(100); /* FOR NOW; will be removed */
    } while(dbpf_thread_running);
#endif

    gossip_debug(TROVE_DEBUG, "thread function ending\n");
    return ptr;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

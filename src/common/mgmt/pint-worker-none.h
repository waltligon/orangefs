
#include "pint-mgmt.h"

typedef struct
{
    /* The number of operations that should be serviced before moving on
     * to the next queue
     */
    int ops_per_queue;

    /* time to wait (in microsecs) for an operation to be added to the queue.
     * 0 means no timeout.
     */
    int timeout;

} PINT_worker_queues_attr_t;

struct PINT_worker_queues_s
{
    PINT_worker_queues_attr_t attr;
    PINT_op_id *ids;
    PINT_service_callout *callouts;
    void **service_ptrs;
    PVFS_hint **hints;
    struct qlist_head queues;
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


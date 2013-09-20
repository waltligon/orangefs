/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_WORKER_H
#define PINT_WORKER_H

#include "pint-context.h"
#include "pint-worker-queues.h"
#include "pint-worker-threaded-queues.h"
#include "pint-worker-per-op.h"
#include "pint-worker-pool.h"
#include "pint-worker-blocking.h"
#include "pint-worker-external.h"

typedef PVFS_id_gen_t PINT_worker_id;

typedef enum
{
    PINT_WORKER_TYPE_QUEUES,
    PINT_WORKER_TYPE_THREADED_QUEUES,
    PINT_WORKER_TYPE_PER_OP,
    PINT_WORKER_TYPE_POOL,
    PINT_WORKER_TYPE_BLOCKING,
    PINT_WORKER_TYPE_EXTERNAL
} PINT_worker_type_t;

extern PINT_worker_id PINT_worker_implicit_id;
extern PINT_worker_id PINT_worker_blocking_id;

union PINT_worker_attr_u
{
    PINT_worker_queues_attr_t queues;
    PINT_worker_threaded_queues_attr_t threaded;
    PINT_worker_per_op_attr_t per_op;
    PINT_worker_pool_attr_t pool;
    PINT_worker_external_attr_t external;
};

typedef struct
{
    PINT_worker_type_t type;
    union PINT_worker_attr_u u;
} PINT_worker_attr_t;

typedef union
{
    struct PINT_worker_queues_s queues;
    struct PINT_worker_threaded_queues_s threaded;
    struct PINT_worker_per_op_s per_op;
    struct PINT_worker_pool_s pool;
    struct PINT_worker_external_s external;
} PINT_worker_inst;

struct PINT_manager_s;

struct PINT_worker_impl
{
    /* The name of the worker impl. */
    const char *name;

    /**
     * Initialize this worker impl.
     *
     * @param manager the manager to which this worker belongs.
     * @param inst the union of worker internal instances.  The
     *        specific worker's fields should be set on this instance.
     * @param attr the attributes of the worker impl.  These should
     *        be copied to the worker instance.
     *
     * @return should return 0 on success or -PVFS_error on error
     */
    int (*init) (struct PINT_manager_s *manager,
                 PINT_worker_inst *inst,
                 PINT_worker_attr_t *attr);
    
    /**
     * Destroy the worker impl.
     *
     * @param manager the manager this worker is on
     * @param the instance of the worker impl.
     *
     * @return 0 on success, -PVFS_error on error
     */
    int (*destroy) (struct PINT_manager_s *manager, PINT_worker_inst *inst);

    /**
     * Add a queue to the worker.  Some workers don't accept queues
     * (such as the thread-per-op or thread-pool workers), and should
     * not implement this callback.  Other workers that manage queues
     * should add this queue to their list of queues.  The queue
     * should not be free within the worker, Its lifetime should
     * be managed separately.
     */
    int (*queue_add) (struct PINT_manager_s *manager,
                      PINT_worker_inst *inst, 
                      PINT_queue_id queue_id);

    /**
     * Remove a queue from the worker.
     */
    int (*queue_remove) (struct PINT_manager_s *manager,
                         PINT_worker_inst *inst, 
                         PINT_queue_id queue_id);

    /**
     * Post an operation to this worker.  This callback is required for
     * all worker impls to implement.
     *
     * @param manager the manager this worker is on
     * @param inst the worker instance
     * @param queue_id the queue in this worker to post the operation to.  For
     *        workers that don't manage queues, the queue_id should be zero.
     *
     * @param operation the operation to post to the worker.  This object
     *        is managed outside the worker instance, so the worker does not
     *        need to copy or free it.  It can be added to a queue directly.
     * 
     * @return PINT_MGMT_OP_POSTED if the operation was posted successfully.
     *         PINT_MGMT_OP_COMPLETE if the worker is a blocking worker and
     *         the operation was completed successfully.
     *         -PVFS_error on error
     */
    int (*post) (struct PINT_manager_s *manager,
                 PINT_worker_inst *inst,
                 PINT_queue_id queue_id,
                 PINT_operation_t *operation);

    /**
     * Do work for the operations in the worker.  Some workers don't service
     * operations in separate threads, and so servicing must progress by
     * calling this function.  Other workers that service operations separately
     * shouldn't implement this callback.
     *
     * @param manager the manager this worker is in
     * @param inst the worker instance
     * @param op_id do work on a particular operation.  If the value is 0,
     *        do work on all operations until the timeout is reached.
     * @param microsecs The timeout for doing work.  This is a hint to
     *        return from the do_work callback after the timeout has passed,
     *        but there are not strict guarantees that do_work will return
     *        before it is reached.
     */
    int (*do_work) (struct PINT_manager_s *manager,
                    PINT_worker_inst *inst,
                    PINT_context_id context_id,
                    PINT_operation_t *op,
                    int microsecs);

    int (*cancel) (struct PINT_manager_s *manager,
                   PINT_worker_inst *inst,
                   PINT_queue_id queue_id,
                   PINT_operation_t *op);
};

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

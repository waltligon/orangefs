/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_MGMT_H
#define PINT_MGMT_H

#include "id-generator.h"
#include "pint-context.h"
#include "pint-worker.h"
#include "pint-queue.h"

/**
 * @defgroup pint-mgmt
 * 
 * The PVFS management interfaces provide abstraction for posting and testing
 * operations, and allowing them to be serviced via different methods,
 * both blocking and asynchronous (using different models such as 
 * thread pools or queues).  An 'operation' is a unit of work defined by
 * the caller via a callback function.  The management interfaces allow
 * callers to hand off these units of work, ignore the details of how
 * and when the operation is serviced, and test for completion later.
 *
 * Use of the API is done by creating a 'manager',
 * which provides a common reference for all the operations that should 
 * be grouped together somehow.  To a manager are added 'workers', which
 * specify how an operation is serviced.  Some examples of worker types are
 * 'per-op', which creates a thread for that operation and services it,
 * 'blocking', which blocks on the post call, services the operation and
 * returns, or 'threaded-queues', which create a number of threads and
 * pull operations off queues in-turn to service them.  Finally, once a 
 * manager has been setup with the appropriate workers, operations can 
 * be posted to the manager.  The worker that services the operation
 * is chosen either explicitly in the post call, or dynamically using
 * the type of the operation as the key.  Notification
 * of completed operations is made through the completion context,
 * which is specified for a particular manager, or passed in with the
 * post call.
 *
 */

enum
{
    /**
     * The post calls will return POSTED if the operation was posted
     * but not complete.
     */
    PINT_MGMT_OP_POSTED = 0,

    /**
     * The service functions will return COMPLETE if the operation was completed
     * (either by blocking or because it was done speculatively).
     */
    PINT_MGMT_OP_COMPLETED = 1,

    /**
     * The service functions will return CONTINUE if the operation wasn't completed
     * but added back to the queue, and will be completed later.
     */
    PINT_MGMT_OP_CONTINUE = 2
};

typedef struct PINT_manager_s *PINT_manager_t;

int PINT_manager_init(PINT_manager_t *new_manager,
                      PINT_context_id ctx);

int PINT_manager_destroy(PINT_manager_t manager);

int PINT_manager_worker_add(PINT_manager_t manager,
                            PINT_worker_attr_t *attr,
                            PINT_worker_id *worker_id);

int PINT_manager_worker_remove(PINT_manager_t manager, PINT_worker_id id);

int PINT_manager_queue_add(PINT_manager_t manager,
                           PINT_worker_id worker_id,
                           PINT_queue_id queue_id);

int PINT_manager_queue_remove(PINT_manager_t manager,
                              PINT_queue_id queue_id);

/* post an operation without specifying a worker explicitly.  The management
 * code tries to figure out the proper queue/worker to use for
 * this operation based on the op-to-worker mappings
 */
#define PINT_manager_post(mgr, ptr, mid, callout, opptr, hint) \
    PINT_manager_id_post( \
        mgr, ptr, mid, callout, opptr, hint, PINT_worker_implicit_id)

int PINT_manager_id_post(PINT_manager_t manager,
                         void *user_ptr,
                         PINT_op_id *id,
                         PINT_service_callout callout,
                         void *op_ptr,
                         PVFS_hint hint,
                         PVFS_id_gen_t queue_worker_id);

int PINT_manager_ctx_post(PINT_manager_t manager,
                          PINT_context_id context_id,
                          void *user_ptr,
                          PINT_op_id *id,
                          PINT_service_callout callout,
                          void *op_ptr,
                          PVFS_hint hint,
                          PVFS_id_gen_t queue_worker_id);

int PINT_manager_cancel(PINT_manager_t manager,
                        PINT_op_id op_id);


typedef int (*PINT_worker_mapping_callout) (PINT_manager_t manager,
                                            PINT_service_callout callout,
                                            void *op_ptr,
                                            PVFS_hint hint,
                                            PVFS_id_gen_t *id);

int PINT_manager_add_map(PINT_manager_t manager,
                         PINT_worker_mapping_callout map);

#define PINT_MGMT_TIMEOUT_NONE 0xFFFFFFFF

int PINT_manager_test_context(PINT_manager_t manager,
                              PINT_context_id context_id,
                              int * opcount,
                              PINT_op_id *ids,
                              void **user_ptrs,
                              PVFS_error *errors,
                              int microsecs);

int PINT_manager_test(PINT_manager_t manager,
                      int *opcount,
                      PINT_op_id *ids,
                      void **user_ptrs,
                      PVFS_error *errors,
                      int microsecs);

int PINT_manager_test_op(PINT_manager_t manager,
                         PINT_op_id op_id,
                         void **user_ptr,
                         PVFS_error *error,
                         int microsecs);

int PINT_manager_wait_context(PINT_manager_t manager,
                              PINT_context_id context_id,
                              int microsecs);

int PINT_manager_wait(PINT_manager_t manager,
                      int microsecs);

int PINT_manager_wait_op(PINT_manager_t manager,
                         PINT_op_id op_id,
                         int microsecs);

int PINT_manager_service_op(PINT_manager_t manager,
                            PINT_operation_t *op,
                            int *service_time,
                            int *error);

int PINT_manager_complete_op(PINT_manager_t manager,
                             PINT_operation_t *op,
                             int error);

/* Event handling. */

enum PINT_event_type
{
    PINT_OP_EVENT_START = 1,
    PINT_OP_EVENT_END = 2
};

typedef void (*PINT_event_callback) (
    enum PINT_event_type type, void *event_ptr, PINT_op_id id, PVFS_hint hint);

void PINT_manager_event_handler_add(PINT_manager_t manager, 
                                    PINT_event_callback callback,
                                    void *event_ptr);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

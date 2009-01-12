/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include "pvfs2-types.h"
#include "pvfs2-internal.h"
#include "pint-mgmt.h"
#include "pint-util.h"
#include "quickhash.h"
#include "pvfs2-debug.h"
#include "gossip.h"

#define DEFAULT_TIMEOUT_MICROSECS 1000

#define PINT_QUEUE_TO_WORKER_TABLESIZE  1024
#define PINT_OP_ENTRY_TABLESIZE         32768

/* Used to specify that the management code should figure out which
 * worker to use based on the op to worker mappings
 */
PINT_worker_id PINT_worker_implicit_id = 0;

/* Every manager gets a blocking worker type.  This global id allows
 * us to reference the blocking worker without knowing the specific
 * id for that manager. 
 */
PINT_worker_id PINT_worker_blocking_id = 0xFFFFFFFFFFFFFFFFULL;

struct PINT_worker_s
{
    PINT_worker_type_t type;
    struct PINT_worker_impl *impl;

    PINT_worker_inst inst;

    PINT_worker_id id;
    struct qlist_head link;
};

struct PINT_op_entry
{
    void *user_ptr;
    PINT_operation_t op;
    PVFS_id_gen_t wq_id;
    PINT_context_id ctx_id;
    PVFS_error error;

    struct qhash_head link;
};

struct PINT_manager_s
{
    PINT_context_id context;

    gen_mutex_t mutex;

    /* mapping functions for mapping an operation to the
     * queue or worker it should be added to.
     */
    struct qlist_head op_maps;

    /* list of workers */
    struct qlist_head workers;

    /* hash of which queues are owned by which workers */
    struct qhash_table *queue_to_worker;

    /* list of operations that are being serviced.  A post
     * call can post a sequence of operations to be serviced,
     * each of which may be handled by a different worker.
     * Since C doesn't have continuations, we create a linked
     * list on a post of the operations, and then add the first
     * operation to the appropriate queue/worker, and the rest
     * (the linked list) to this list.  The manager keeps popping
     * operations off the list and servicing them with the appropriate
     * worker.
     */
    struct qhash_table *ops;

    int op_count;

    PINT_worker_id blocking_id;

    struct qlist_head event_handlers;
};

struct PINT_worker_map_entry_s
{
    struct PINT_worker_s *worker;
    PINT_queue_id queue_id;
    struct qhash_head link;
};

struct PINT_worker_id_mapper_entry_s
{
    PINT_worker_mapping_callout fn;
    struct qlist_head link;
};

static void PINT_worker_destroy(PINT_manager_t manager, 
                                struct PINT_worker_s *worker);

static int PINT_manager_find_worker(PINT_manager_t manager,
                                    PINT_service_callout callout,
                                    void *op_ptr,
                                    PVFS_hint hint,
                                    PVFS_id_gen_t input_work_id,
                                    struct PINT_worker_s **result_worker,
                                    PINT_queue_id *queue_id);

static int PINT_queue_to_worker_compare(void *key, struct qhash_head *link);
static int PINT_queue_to_worker_hash(void *key, int tablesize);
static int PINT_op_entry_compare(void *key, struct qhash_head *link);
static int PINT_op_entry_hash(void *key, int tablesize);

struct PINT_manager_event_handler_entry_s
{
    PINT_event_callback callback;
    void *event_ptr;

    struct qlist_head link;
};

static void PINT_manager_op_start(PINT_manager_t manager, PINT_operation_t *op);
static void PINT_manager_op_end(PINT_manager_t manager, PINT_operation_t *op);

inline static int PINT_op_entry_create(struct PINT_op_entry **op,
                                       void *user_ptr,
                                       PINT_service_callout callout,
                                       void *op_ptr,
                                       PVFS_hint hint,
                                       PVFS_id_gen_t wq_id,
                                       PINT_context_id context_id);

/**
 * Create a manager that can be referenced and used for posting
 * operations.  Be default a new manager has a blocking worker
 * (the post call blocks while servicing the operation) but nothing
 * else.  Other workers must be added to this manager using
 * the PINT_manager_worker_add call.
 *
 * @param new_manager The new manager reference
 * @param ctx The context id that all operations posted to this
 * manager will by default get completed on.  Operations can
 * override this context by posting using the PINT_manager_ctx_post call.
 *
 * @return 0 on success
 *         -PVFS_ENOMEM if out of memory
 */
int PINT_manager_init(
    PINT_manager_t *new_manager,
    PINT_context_id ctx)
{
    int ret;
    struct PINT_manager_s *manager;
    PINT_worker_attr_t blocking_attr;

    manager = (struct PINT_manager_s *)malloc(sizeof(struct PINT_manager_s));
    if(!manager)
    {
        return -PVFS_ENOMEM;
    }

    gen_mutex_init(&manager->mutex);
    if(ctx)
    {
        PINT_context_reference(ctx);
        manager->context = ctx;
    }

    manager->queue_to_worker = qhash_init(PINT_queue_to_worker_compare,
                                          PINT_queue_to_worker_hash,
                                          PINT_QUEUE_TO_WORKER_TABLESIZE);
    if(!manager->queue_to_worker)
    {
        free(manager);
        return -PVFS_ENOMEM;
    }

    INIT_QLIST_HEAD(&manager->op_maps);
    INIT_QLIST_HEAD(&manager->workers);
    INIT_QLIST_HEAD(&manager->event_handlers);

    gen_mutex_lock(&manager->mutex);
    manager->ops = qhash_init(PINT_op_entry_compare,
                              PINT_op_entry_hash,
                              PINT_OP_ENTRY_TABLESIZE);
    if(!manager->ops)
    {
        gen_mutex_unlock(&manager->mutex);
        free(manager);
        return -PVFS_ENOMEM;
    }

    manager->op_count = 0;
    gen_mutex_unlock(&manager->mutex);

    /* add the blocking worker to the manager.  Every manager
     * gets one of these.  The blocking_id is held internally, and the global
     * PINT_worker_blocking_id is used to reference the blocking worker.
     */

    blocking_attr.type = PINT_WORKER_TYPE_BLOCKING;
    ret = PINT_manager_worker_add(
        manager, &blocking_attr, &manager->blocking_id);
    if(ret < 0)
    {
        qhash_finalize(manager->ops);
        free(manager);
        return ret;
    }

    *new_manager = manager;
    return 0;
}

/**
 * Destroy a manager.
 *
 * @param manager  The manager to destroy.
 *
 * @return 0 on succes.
 *         -PVFS_EINVAL if operations are still being worked on.
 */
int PINT_manager_destroy(PINT_manager_t manager)
{
    struct PINT_worker_s *worker, *tmp;

    gen_mutex_lock(&manager->mutex);

    if(manager->op_count > 0)
    {
        gen_mutex_unlock(&manager->mutex);
        return -PVFS_EINVAL;
    }

    qhash_finalize(manager->ops);

    qlist_for_each_entry_safe(worker, tmp, &manager->workers, link)
    {
        qlist_del(&worker->link);
        PINT_worker_destroy(manager, worker);
    }

    qhash_finalize(manager->queue_to_worker);

    if(manager->context)
    {
        PINT_context_dereference(manager->context);
    }

    gen_mutex_unlock(&manager->mutex);
    return 0;
}

/**
 * Add a worker to a manager.
 *
 * @param manager The manager to add the worker to
 * @param attr The worker attrs.  This specifies both the type of
 * worker, as well as the attributes defining how that worker should
 * be initialized.
 * @param worker_id The id of the worker created.  This id can
 * be used to specify the worker on post calls, or via mapping from
 * the operation type to this worker.
 *
 * @return 0 on success
 *         -PVFS_ENOMEM if out of memory
 */
int PINT_manager_worker_add(PINT_manager_t manager,
                            PINT_worker_attr_t *attr,
                            PINT_worker_id *worker_id)
{
    struct PINT_worker_s *worker = NULL;
    struct PINT_worker_s *tmp_worker = NULL;
    int ret = 0;

    assert(manager);
    assert(attr);

    do
    {
        worker = malloc(sizeof(struct PINT_worker_s));
        if(!worker)
        {
            return -PVFS_ENOMEM;
        }
        memset(worker, 0, sizeof(struct PINT_worker_s));
        id_gen_fast_register(&worker->id, &worker->impl);
        if(worker->id == PINT_worker_blocking_id)
        {
            /* happened to choose the id we statically defined
             * for the blocking worker, so we try another */
            tmp_worker = worker;
            worker = NULL;
        }
    } while(worker == NULL);

    if(tmp_worker)
    {
        free(tmp_worker);
    }

    /* match the worker type to a worker implementation */
    worker->type = attr->type;
    switch(worker->type)
    {
        case PINT_WORKER_TYPE_QUEUES:
            worker->impl = &PINT_worker_queues_impl;
            break;
        case PINT_WORKER_TYPE_THREADED_QUEUES:
            worker->impl = &PINT_worker_threaded_queues_impl;
            break;
        case PINT_WORKER_TYPE_PER_OP:
            worker->impl = &PINT_worker_per_op_impl;
            break;
        case PINT_WORKER_TYPE_BLOCKING:
            worker->impl = &PINT_worker_blocking_impl;
            break;
        case PINT_WORKER_TYPE_EXTERNAL:
            worker->impl = &PINT_worker_external_impl;
            break;
        case PINT_WORKER_TYPE_POOL:
            ret = -PVFS_ENOSYS;
            goto free_worker;
            break;
        default:
            assert(0);
    }

    /* found a valid worker.  initialize it! */
    if(worker->impl->init)
    {
        ret = worker->impl->init(manager, &worker->inst, attr);
        if(ret < 0)
        {
            return ret;
        }
    }

    /* add the worker to the list of workers */
    gen_mutex_lock(&manager->mutex);
    qlist_add_tail(&worker->link, &manager->workers);
    gen_mutex_unlock(&manager->mutex);

    /* use the worker->impl as the id so that worker impls can
     * access the id as well
     */
    *worker_id = worker->id;

    return 0;

free_worker:

    free(worker);
    return ret;
}

/**
 * Remove a worker from a manager.
 *
 * @param manager The manager
 * @param id The worker id to remove from the manager
 *
 * @return 0 on success
 */
int PINT_manager_worker_remove(PINT_manager_t manager, PINT_worker_id wid)
{
    struct PINT_worker_s *worker;

    worker = id_gen_fast_lookup(wid);
    assert(worker);

    gen_mutex_lock(&manager->mutex);
    qlist_del(&worker->link);
    gen_mutex_unlock(&manager->mutex);

    PINT_worker_destroy(manager, worker);
    return 0;
}

static void PINT_worker_destroy(PINT_manager_t manager, 
                                struct PINT_worker_s *worker)
{
    int ret;
    if(worker->impl->destroy)
    {
        ret = worker->impl->destroy(manager, &worker->inst);
        if(ret < 0)
        {
            gossip_err("%s: worker_impl->destroy (type: %s) "
                       "failed with error: %d\n",
                       __func__,
                       worker->impl->name,
                       ret);
            return;
        }
    }

    free(worker);
    return;
}

/**
 * Add a queue to a worker.  There are worker types that manage/service
 * operations by first queuing them, and then servicing them one-by-one.
 * These worker types require queues to be added to them.  This function
 * provides the interface into the individual worker type that adds
 * the queue.
 *
 * @param manager the manager holding the worker
 * @param worker_id the worker to add the queue to
 * @param queue_id the queue to add to the worker
 *
 * @return 0 on success
 *         -PVFS_ENOMEM if out of memory
 *         -PVFS_EINVAL if the worker_id doesn't match any workers held
 *                      by the manager
 */
int PINT_manager_queue_add(PINT_manager_t manager,
                           PINT_worker_id worker_id,
                           PINT_queue_id qid)
{
    int ret = 0;
    struct PINT_worker_s *worker;
    struct PINT_worker_map_entry_s *entry;

    gen_mutex_lock(&manager->mutex);

    /* verify that worker has been added to manager */
    qlist_for_each_entry(worker, &manager->workers, link)
    {
        if(worker->id == worker_id)
        {
            break;
        }
    }

    if(worker->id != worker_id)
    {
        /* couldn't find a valid worker in the list that
         * matches passed in id.
         */
        gen_mutex_unlock(&manager->mutex);
        return -PVFS_EINVAL;
    }

    if(!worker->impl->queue_add)
    {
        gossip_err("%s: can't add queue to worker type %s\n",
                   __func__,
                   worker->impl->name);
        return -PVFS_EINVAL;
    }

    ret = worker->impl->queue_add(manager, &worker->inst, qid);
    if(ret < 0)
    {
        goto done;
    }

    /* map queue to worker in hashtable */
    entry = malloc(sizeof(struct PINT_worker_map_entry_s));
    if(!entry)
    {
        ret = -PVFS_ENOMEM;
        goto done;
    }

    entry->worker = worker;
    entry->queue_id = qid;
    qhash_add(manager->queue_to_worker, &qid, &entry->link);

done:
    gen_mutex_unlock(&manager->mutex);
    return ret;
}

/**
 * Remove a queue from a worker.  Internally, the manager keeps track of
 * which queues were added to which workers, so only the queue id is needed.
 *
 * @param manager The manager holding the worker holding the queue
 * @param queue_id The queue_id to remove
 *
 * @param 0 on success
 * @param -PVFS_EINVAL if the queue isn't held by any workers held 
 *                     by this manager
 */
int PINT_manager_queue_remove(PINT_manager_t manager, PINT_queue_id id)
{
    int ret;
    struct PINT_worker_map_entry_s *entry;
    struct qlist_head *link;

    gen_mutex_lock(&manager->mutex);

    /* find the worker where the queue lives */
    link = qhash_search_and_remove(manager->queue_to_worker, &id);
    if(!link)
    {
        gen_mutex_unlock(&manager->mutex);
        return -PVFS_EINVAL;
    }

    entry = qhash_entry(link, struct PINT_worker_map_entry_s, link);

    ret = entry->worker->impl->queue_remove(manager, &entry->worker->inst, id);

    /* free the entry that was in the queue_to_worker hashtable, since
     * we've just removed queue.
     */
    free(entry);

    gen_mutex_unlock(&manager->mutex);
    return ret;
}

/**
 * Post an operation to the manager.  The operation is completed to the
 * context specified when the worker was created.
 *
 * @param manager the manager to post to
 * @param user_ptr the caller's object to pass to the completion context
 * @param mid the op id returned to the caller to keep track of this
 *            chained operation.
 * @param callout This is the actual service function for
 *      the operation.
 * @param op_ptr The operation pointer to pass to the service function.
 * @param hint A set of hints that can specify how the operation is
 *      to be serviced.
 * @param queue_worker_id The worker id or queue id specifying that the 
 *      operation should be added to the worker with this id, or (if no 
 *      such worker exists) to the queue with this id.  
 *      If the specific worker id PINT_worker_implicit is used, the
 *      worker or queue will be chosen dynamically using the mapping functions
 *      specified by calls to PINT_manager_add_map.
 *
 * @return PINT_MGMT_OP_POSTED if the op was posted successfully
 *         PINT_MGMT_OP_COMPLETED if the op completed immediately, either
 *         because the blocking worker was used, or because the op was
 *         completed speculatively.
 */
int PINT_manager_id_post(PINT_manager_t manager,
                         void *user_ptr,
                         PINT_op_id *id,
                         PINT_service_callout callout,
                         void *op_ptr,
                         PVFS_hint hint,
                         PVFS_id_gen_t queue_worker_id)
{
    return PINT_manager_ctx_post(manager, manager->context, user_ptr,
                                 id, callout, op_ptr, hint, queue_worker_id);
}

/**
 * Post an operation to the manager with an explicit context. 
 *
 * @param manager the manager to post to
 * @param user_ptr the caller's object to pass to the completion context
 * @param mid the op id returned to the caller to keep track of this
 *            chained operation.
 * @param ctx_id the context to complete to.
 * @param callout This is the actual service function for
 *      the operation.
 * @param op_ptr The operation pointer to pass to the service function.
 * @param hint A set of hints that can specify how the operation is
 *      to be serviced.
 * @param queue_worker_id The worker id or queue id specifying that the 
 *      operation should be added to the worker with this id, or (if no 
 *      such worker exists) to the queue with this id.  
 *      If the specific worker id PINT_worker_implicit is used, the
 *      worker or queue will be chosen dynamically using the mapping functions
 *      specified by calls to PINT_manager_add_map.
 *
 * @return PINT_MGMT_OP_POSTED if the op was posted successfully
 *         PINT_MGMT_OP_COMPLETED if the op completed immediately, either
 *         because the blocking worker was used, or because the op was
 *         completed speculatively.
 */
int PINT_manager_ctx_post(PINT_manager_t manager,
                          PINT_context_id context_id,
                          void *user_ptr,
                          PINT_op_id *id,
                          PINT_service_callout callout,
                          void *op_ptr,
                          PVFS_hint hint,
                          PVFS_id_gen_t worker_id)
{
    struct PINT_op_entry *op_entry;
    struct PINT_worker_s *worker;
    PINT_queue_id queue_id;
    int ret = 0;

    ret = PINT_manager_find_worker(
        manager, callout, op_ptr, hint, worker_id, &worker, &queue_id);
    if(ret < 0)
    {
        return ret;
    }

    /* special case for blocking worker - don't allocate op entry.  We
     * need to know in advance that the type of worker isn't going to queue
     * (or reference) the operation pointer passed into the post call 
     */
    if(PINT_WORKER_TYPE_BLOCKING == worker->type)
    {
        PINT_operation_t op;

        op.operation = callout;
        op.operation_ptr = op_ptr;
        PVFS_hint_copy(hint, &op.hint);

        ret = worker->impl->post(manager, &worker->inst, 0, &op);
        assert(ret != PINT_MGMT_OP_POSTED);

        if(id)
        {
            *id = -1;
        }

        return ret;
    }

    ret = PINT_op_entry_create(
        &op_entry, user_ptr, callout, op_ptr, hint, worker_id, context_id);
    if(ret < 0)
    {
        return ret;
    }

    id_gen_fast_register(&op_entry->op.id, op_entry);

    gossip_debug(GOSSIP_MGMT_DEBUG,
                 "[MGMT]: manager ops: adding op id: %llu\n",
                 llu(op_entry->op.id));
    gen_mutex_lock(&manager->mutex);
    qhash_add(manager->ops, &op_entry->op.id, &op_entry->link);
    manager->op_count++;
    gen_mutex_unlock(&manager->mutex);
    if(id)
    {
        *id = op_entry->op.id;
    }

    ret = worker->impl->post(
        manager, &worker->inst, queue_id, &op_entry->op);
    if(ret < 0 || PINT_MGMT_OP_COMPLETED == ret)
    {
        /* if there was an error (either from a blocking operation serviced or
         * from just posting the operation), we stop all servicing for this
         * operation and return the error
         */
        gen_mutex_lock(&manager->mutex);
        qhash_search_and_remove(manager->ops, &op_entry->op.id);
        gen_mutex_unlock(&manager->mutex);

        free(op_entry);
        return ret;
    }

    return ret;
}

inline static int PINT_op_entry_create(struct PINT_op_entry **op,
                                       void *user_ptr,
                                       PINT_service_callout callout,
                                       void *op_ptr,
                                       PVFS_hint hint,
                                       PVFS_id_gen_t wq_id,
                                       PINT_context_id context_id)
{
    struct PINT_op_entry *opentry;

    opentry = malloc(sizeof(*opentry));
    if(!opentry)
    {
        return -PVFS_ENOMEM;
    }
    memset(opentry, 0, sizeof(*opentry));

    opentry->user_ptr = user_ptr;

    opentry->op.operation = callout;
    opentry->op.operation_ptr = op_ptr;
    if(hint)
    {
        PVFS_hint_copy(hint, &opentry->op.hint);
    }

    opentry->wq_id = wq_id;
    opentry->ctx_id = context_id;

    *op = opentry;
    return 0;
}

static int PINT_manager_find_worker(PINT_manager_t manager,
                                    PINT_service_callout callout,
                                    void *op_ptr,
                                    PVFS_hint hint,
                                    PVFS_id_gen_t input_worker_id,
                                    struct PINT_worker_s **result_worker,
                                    PINT_queue_id *queue_id)
{
    struct PINT_worker_map_entry_s *worker_entry;
    struct PINT_worker_s *w;
    int ret = 0;
    struct qhash_head *listlink;
    PVFS_id_gen_t result_worker_id;

    gen_mutex_lock(&manager->mutex);

    result_worker_id = input_worker_id;

    /* if the queue/worker id refers to the global blocking worker id, we
     * set it to the correct id maintained by the manager
     */
    if(input_worker_id == PINT_worker_blocking_id)
    {
        result_worker_id = manager->blocking_id;
    }

    /* if the queue/worker id is 0, assume the queue/worker id is supposed
     * to be fetched from the id mapping functions. (or if there's only one
     * worker that isn't a queue type or only has one queue)
     */
    if(input_worker_id == PINT_worker_implicit_id && callout != NULL)
    {
        struct PINT_worker_id_mapper_entry_s *map;

        /* get queue/worker from mapper functions */
        qlist_for_each_entry(map, &manager->op_maps, link)
        {
            /* try each mapping function to see if it returns
             * a queue or worker id that this operation should be added to
             */
            ret = map->fn(manager,
                          callout,
                          op_ptr,
                          hint,
                          &result_worker_id);
            if(0 == ret && result_worker_id != PINT_worker_implicit_id)
            {
                /* found one! */
                break;
            }

            if(ret < 0)
            {
                goto exit;
            }
        }

        if(0 == ret && PINT_worker_implicit_id == result_worker_id)
        {
            /* didn't find any worker/queue ids in any of the mapping
             * functions, so use the blocking worker
             */
            result_worker_id = manager->blocking_id;
        }
    }

    /* so now we should have a valid worker/queue id */

    /* check that the queue/worker id is a worker id that the manager
     * manages.  Otherwise assume its a queue id and look for
     * the associated worker in the queue-to-worker map.
     */
    qlist_for_each_entry(w, &manager->workers, link)
    {
        if(w->id == result_worker_id)
        {
            /* Its a worker id.  This should only be specified for
             * worker types that don't manage queues
             */
            *result_worker = w;
            *queue_id = 0;
            goto exit;
        }
    }

    /* if its not a recognized worker id, assume its a queue id
     * and verify that its a queue maintained by one of the workers
     */
    listlink = qhash_search(manager->queue_to_worker, &result_worker_id);
    if(!listlink)
    {
        /* not a worker id/queue id we recognize. */
        gossip_err("%s: operation posted with a queue id (%llu) that isn't "
                   "held by this manager\n",
                   __func__, llu(result_worker_id));
        ret = -PVFS_EINVAL;
        goto exit;
    }

    worker_entry = qhash_entry(
        listlink, struct PINT_worker_map_entry_s, link);
    *result_worker = worker_entry->worker;
    *queue_id = result_worker_id;

exit:

    gen_mutex_unlock(&manager->mutex);
    return ret;
}

/**
 * Add an id to queue/worker callback.  Operations can be posted without
 * specifying explicitly which worker or queue to post the operation to.
 * This allows changing the behavior of how certain operations are serviced
 * (which worker handles them, etc.) dynamically via mapping functions that
 * map the operation (based on its type or the hints specified) to the worker
 * or queue that should eventually service it.  The mapping callout functions
 * are kept in a list by the manager.  Once an 'implicit' operation is
 * posted, the mapping functions are called in the order they were added
 * to the manager until a valid worker or queue id is returned.
 *
 * @param manager the manager to add the mapping callout to
 * @param callout the callout function to add
 *
 * @return 0 on success
 *         -PVFS_error on erro
 */
int PINT_manager_add_map(PINT_manager_t manager,
                         PINT_worker_mapping_callout map)
{
    struct PINT_worker_id_mapper_entry_s *entry;

    entry = malloc(sizeof(struct PINT_worker_id_mapper_entry_s));
    if(!entry)
    {
        return -PVFS_ENOMEM;
    }

    entry->fn = map;

    qlist_add_tail(&entry->link, &manager->op_maps);

    return 0;
}

/**
 * Test for completion on a particular context.  This is a wrapper
 * function for the PINT_context_testall function, because the queue
 * worker does not service operations in a separate thread.  Instead it
 * does all work in the actual test call.  This call essentiall tries
 * to return completed operations, or do work on queue-worker operations
 * and returned those that completed.
 *
 * @param manager the manager
 * @param context_id the context to test on
 * @param opcount As an input parameter, this holds the sizes of the
 *        arrays for the following parameters (max number of ops that
 *        can be returned).  As an output parameter, this holds the
 *        actual number of completed ops returned.
 * @param mids the completed operation ids
 * @param user_ptrs the completed operation user pointers
 * @param errors the errors returned from each serviced operation
 * @param microsecs A hint as to how long to wait for some completed
 *        operations.  There's no guarantee that this function will
 *        return before the the timeout, but it should be around there.
 *
 * @return 0 on success
 *         -PVFS_ETIMEOUT if the timeout was reached and no ops were completed
 *         -PVFS_error on error
 */
int PINT_manager_test_context(PINT_manager_t manager,
                              PINT_context_id context,
                              int *opcount,
                              PINT_op_id *ids,
                              void **user_ptrs,
                              PVFS_error *errors,
                              int microsecs)
{
    int ret;
    int count;
    struct PINT_worker_s *worker;
    struct timeval start, now;
    int timeleft = microsecs;

    gettimeofday(&start, NULL);

    count = *opcount;

    /* we don't want to wait for operations to complete here in case
     * operations get serviced later in this function, so we just check
     * that there aren't a bunch of operations that already completed
     * that would fill up the output arrays of completed operations.
     */
    ret = PINT_context_test_all(
        context, opcount, ids, user_ptrs, errors, 0);
    if(ret < 0 && ret != -PVFS_ETIMEDOUT)
    {
        gossip_debug(GOSSIP_MGMT_DEBUG, "%s: context_test_all failed: %d\n",
                     __func__, ret);
        return ret;
    }

    /* if the test_all succeeds but returns zero in opcount or times-out
     * we want to try to do work on non-threaded workers and test again
     */
    /* if no operations have completed, then we try to make progress
     * on workers that don't do work themselves.
     */
    if(0 == *opcount)
    {
        gen_mutex_lock(&manager->mutex);
        /* try to do work if the op is in that type of worker */
        qlist_for_each_entry(worker, &manager->workers, link)
        {
            gen_mutex_unlock(&manager->mutex);
            if(worker->impl->do_work)
            {
                ret = worker->impl->do_work(manager, &worker->inst,
                                            context, 0, timeleft);
                if(ret < 0)
                {
                    return ret;
                }
                break;
            }
            gen_mutex_lock(&manager->mutex);
        }
        gen_mutex_unlock(&manager->mutex);

        /* test again. */
        *opcount = count;

        /*  If the timeout is forever, keep testing until
         * we've filled in the output arrays or an error occurs 
         */
        if(PINT_MGMT_TIMEOUT_NONE == microsecs)
        {
            microsecs = DEFAULT_TIMEOUT_MICROSECS;
            do
            {
                gossip_debug(
                    GOSSIP_MGMT_DEBUG,
                    "%s: calling context_test_all again: opcount: %d\n",
                    __func__, *opcount);
                ret = PINT_context_test_all(
                    manager->context, opcount, ids,
                    user_ptrs, errors, microsecs);
                gossip_debug(GOSSIP_MGMT_DEBUG,
                             "%s: context_test_all: res: %d, "
                             "opcount: %d, "
                             "microsecs: %d\n",
                             __func__, ret, *opcount, microsecs);

            } while(0 == ret && 0 == *opcount);
        }
        else
        {
            /* see how much time is left.  If there's no timeleft (negative)
             * then we zero and get any ops that completed during the callout
             * to do_work
             */
            gettimeofday(&now, NULL);
            timeleft = microsecs -
                (((now.tv_sec * 1e6) + now.tv_usec) -
                 ((start.tv_sec * 1e6) + start.tv_usec));
            if(timeleft < 0)
            {
                timeleft = 0;
            }

            gossip_debug(
                GOSSIP_MGMT_DEBUG,
                "%s: calling context_test_all again: "
                "opcount: %d, timeleft: %d\n",
                __func__, *opcount, timeleft);
            ret = PINT_context_test_all(
                manager->context, opcount, ids, user_ptrs, errors, timeleft);
        }
    }

    return 0;
}

/**
 * Test for completion on the manager's context, and returns an array
 * of completed operations on that context.  If the manager manages workers
 * that implement the do_work callback (i.e. they don't service operations
 * in a separate thread), this function will also drive work for those workers.
 * This function should only be called if the context associated with this manager
 * is of the queueing type.  If the context is a callback type, PINT_manager_wait
 * should be used.
 *
 * @param manager the manager
 * @param opcount As an input parameter, this holds the sizes of the
 *        arrays for the following parameters (max number of ops that
 *        can be returned).  As an output parameter, this holds the
 *        actual number of completed ops returned.
 * @param mids the completed operation ids
 * @param user_ptrs the completed operation user pointers
 * @param errors the errors returned from each serviced operation
 * @param microsecs A hint as to how long to wait for some completed
 *        operations.  There's no guarantee that this function will
 *        return before the the timeout, but it should be around there.
 *
 * @return 0 on success,  PINT_CONTEXT_TYPE_CALLBACK if the context tested
 *         on is a callback context.
 *         -PVFS_ETIMEOUT if the timeout was reached and no ops were completed
 *         -PVFS_error on error
 */
int PINT_manager_test(PINT_manager_t manager,
                      int *count,
                      PINT_op_id *op_ids,
                      void **user_ptrs,
                      PVFS_error *errors,
                      int timeout_ms)
{
    return PINT_manager_test_context(
        manager, manager->context, count,
        op_ids, user_ptrs, errors, timeout_ms);
}

/* Test for completion on an individual operation.  This function tests for
 * completion of the operation specified, or tries to do work if its not
 * completed and the operation is in a worker that doesn't service operations
 * in a separate thread.
 *
 * @param manager the manager the operation was posted to
 * @param op_id the operation id to test on
 * @param user_ptr the user pointer for the completed operation
 * @param error the error value returned by the service callback
 * @param microsecs timeout to wait for completion of the operation
 *
 * @return 0 on success, -PVFS_EBUSY if the operation hasn't completed, or
 * PINT_CONTEXT_TYPE_CALLBACK if the context type was a callback type.
 *  Otherwise, return -PVFS_error on error
 */
int PINT_manager_test_op(PINT_manager_t manager,
                         PINT_op_id op_id,
                         void **user_ptr,
                         PVFS_error *error,
                         int microsecs)
{
    int ret;
    struct PINT_worker_s *worker;
    struct PINT_op_entry *entry;
    PINT_context_id context;

    entry = id_gen_fast_lookup(op_id);
    if(!entry)
    {
        return -PVFS_EINVAL;
    }
    context = entry->ctx_id;

    /* don't use the entry for anything else since it might get freed
     * by the worker calling complete_op
     */
    entry = NULL;

    /* test for completion -- set the timeout to
     * return immediately if not complete */
    ret = PINT_context_test(context, op_id, user_ptr, error, 0);
    if(ret == 0 || ret != -PVFS_ENOENT)
    {
        /* if the op was completed and the user_ptr and error filled in,
         * then we can return.  Or if there was a fatal error we return.
         */
        return ret;
    }

    gen_mutex_lock(&manager->mutex);

    /* must be ENOENT or a callback context,
     * so we try to service if its in a worker that
     * doesn't service operations separately
     */
    qlist_for_each_entry(worker, &manager->workers, link)
    {
        gen_mutex_unlock(&manager->mutex);
        if(worker->impl->do_work)
        {
            ret = worker->impl->do_work(
                manager, &worker->inst, context, &entry->op, microsecs);
            if(ret < 0)
            {
                return ret;
            }
        }
        gen_mutex_lock(&manager->mutex);
    }
    gen_mutex_unlock(&manager->mutex);

    assert(!PINT_context_is_callback(context));

    /* now test again with the timeout.  If the timeout is zero,
     * wait indefinitely */
    if(PINT_MGMT_TIMEOUT_NONE == microsecs)
    {
        ret = -PVFS_ENOENT;
        microsecs = 1000;
        while(ret == -PVFS_ENOENT)
        {
            ret = PINT_context_test(
                context, op_id, user_ptr, error, microsecs);
        }
    }
    else
    {
        ret = PINT_context_test(
            context, op_id, user_ptr, error, microsecs);
    }

    return ret;
}

int PINT_manager_wait(PINT_manager_t manager,
                      int microsecs)
{
    return PINT_manager_wait_context(manager, manager->context, microsecs);
}

/* Cancel an individual operation.  This function attempts to cancel
 * a posted operation.
 *
 * @param manager the manager the operation was posted to
 * @param op_id the operation id to test on
 *
 * @return 0 on successful cancellation, otherwise, return -PVFS_error on error
 */
int PINT_manager_cancel(PINT_manager_t manager,
                        PINT_op_id op_id)
{
    int ret;
    struct PINT_worker_s *worker;
    PINT_queue_id queue_id;
    struct PINT_op_entry *entry;
    PINT_operation_t *op;
    PINT_context_id context;
    int (*cancel_impl)(struct PINT_manager_s *,
                       PINT_worker_inst *,
                       PINT_queue_id,
                       PINT_operation_t *);

    entry = id_gen_fast_lookup(op_id);
    if(!entry)
    {
        return -PVFS_EINVAL;
    }
    ret = PINT_manager_find_worker(
        manager, NULL, NULL, NULL, entry->wq_id, &worker, &queue_id);
    context = entry->ctx_id;
    op = &entry->op;

    /* don't use the entry for anything else since it might get freed
     * by the worker calling complete_op
     */
    entry = NULL;

    gen_mutex_lock(&manager->mutex);
    cancel_impl = worker->impl->cancel;
    gen_mutex_unlock(&manager->mutex);

    if(cancel_impl)
    {
        ret = cancel_impl(
                manager, &worker->inst, context, op);
        if(ret < 0)
        {
            return ret;
        }
    }
    return 0;
}

int PINT_manager_wait_context(PINT_manager_t manager,
                              PINT_context_id context,
                              int microsecs)
{
    int ret = 0;
    struct PINT_worker_s *worker;

    /* can only wait on callback contexts. queue contexts require
     * test calls */
    assert(PINT_context_is_callback(context));

    gen_mutex_lock(&manager->mutex);
    qlist_for_each_entry(worker, &manager->workers, link)
    {
        gen_mutex_unlock(&manager->mutex);
        if(worker->impl->do_work)
        {
            ret = worker->impl->do_work(
                manager, &worker->inst, context, 0, microsecs);
            if(ret < 0)
            {
                return ret;
            }
        }
        gen_mutex_lock(&manager->mutex);
    }

    gen_mutex_unlock(&manager->mutex);
    return ret;
}

int PINT_manager_wait_op(PINT_manager_t manager,
                         PINT_op_id op_id,
                         int microsecs)
{
    int ret = 0;
    struct PINT_worker_s *worker;
    struct PINT_op_entry *entry;
    int msecs_remaining;
    struct timeval last, now;

    entry = id_gen_fast_lookup(op_id);
    if(!entry)
    {
        return -PVFS_EINVAL;
    }

    /* can only wait on callback contexts. queue contexts require
     * test calls */
    assert(PINT_context_is_callback(entry->ctx_id));

    msecs_remaining = microsecs;
    gettimeofday(&last, NULL);

    gen_mutex_lock(&manager->mutex);
    qlist_for_each_entry(worker, &manager->workers, link)
    {
        gen_mutex_unlock(&manager->mutex);
        if(worker->impl->do_work)
        {
            ret = worker->impl->do_work(
                manager, &worker->inst, entry->ctx_id,
                &entry->op, msecs_remaining);
            if(ret < 0)
            {
                return ret;
            }
            gettimeofday(&now, NULL);
            msecs_remaining -=
                ((now.tv_sec * 1e6) + now.tv_usec) -
                ((last.tv_sec * 1e6) + last.tv_usec);
            last = now;
            if(msecs_remaining < 0)
            {
                break;
            }
        }
        gen_mutex_lock(&manager->mutex);
    }

    gen_mutex_unlock(&manager->mutex);
    return ret;
}

static int PINT_queue_to_worker_compare(void *key, struct qhash_head *link)
{
    PINT_queue_id *queue_id;
    struct PINT_worker_map_entry_s *entry;

    queue_id = (PINT_queue_id *) key;
    entry = qhash_entry(link, struct PINT_worker_map_entry_s, link);

    if(*queue_id == entry->queue_id)
    {
        return 1;
    }
    return 0;
}

static int PINT_queue_to_worker_hash(void *key, int tablesize)
{
    unsigned long ret = 0;
    PINT_queue_id *queue_id;

    queue_id = (PINT_queue_id *)key;

    ret += *queue_id;
    ret = ret & (tablesize - 1);

    return (int) ret;
}

static int PINT_op_entry_compare(void *key, struct qhash_head *link)
{
    PINT_op_id *id;
    struct PINT_op_entry *entry;

    id = (PINT_op_id *)key;
    entry = qhash_entry(link, struct PINT_op_entry, link);

    if(entry->op.id == *id)
    {
        return 1;
    }
    return 0;
}

static int PINT_op_entry_hash(void *key, int tablesize)
{
    unsigned long ret = 0;
    PINT_op_id * id = (PINT_op_id *)key;

    ret += *id;
    ret = ret & (tablesize - 1);

    return (int) ret;
}

static void PINT_manager_op_start(PINT_manager_t manager, PINT_operation_t *op)
{
    struct PINT_manager_event_handler_entry_s *handler;

    /* invoke the event callbacks for this manager */
    qlist_for_each_entry(handler, &manager->event_handlers, link)
    {
        handler->callback(
            PINT_OP_EVENT_START, handler->event_ptr, op->id, op->hint);
    }
}

static void PINT_manager_op_end(PINT_manager_t manager, PINT_operation_t *op)
{
    struct PINT_manager_event_handler_entry_s *handler;

    qlist_for_each_entry(handler, &manager->event_handlers, link)
    {
        handler->callback(
            PINT_OP_EVENT_END, handler->event_ptr, op->id, op->hint);
    }
}

/**
 * Add an event handler to get called on operation start/stop events.
 *
 * @param manager the manager to add the handler to
 * @param callback the event handler function
 * @param event_ptr a user pointer to be passed to each event call
 *
 * @return 0 on success, -PVFS_ENOMEM if out of memory
 */
void PINT_manager_event_handler_add(PINT_manager_t manager,
                                    PINT_event_callback callback,
                                    void *event_ptr)
{
    struct PINT_manager_event_handler_entry_s *entry;

    entry = malloc(sizeof(struct PINT_manager_event_handler_entry_s));
    assert(entry);

    entry->callback = callback;
    entry->event_ptr = event_ptr;

    gen_mutex_lock(&manager->mutex);
    qlist_add(&entry->link, &manager->event_handlers);
    gen_mutex_unlock(&manager->mutex);
}

/**
 * Service an operation.  This function is a wrapper for calling the operation
 * callback.  It should only be called by workers.  Besides servicing the operation,
 * it triggers events and keeps track of service time.
 *
 * @param manager the manager used to service the operation
 * @param op the operation to service
 * @param service_time the time elapsed in microsecs to service the operation
 * @param error the error value returned from the operation callback
 *
 * @return 0 on successful servicing.  Right now this function always succeeds.
 */
int PINT_manager_service_op(PINT_manager_t manager,
                            PINT_operation_t *op,
                            int *service_time,
                            int *error)
{
    struct timeval after;
    /* get the timestamp for when the operation began servicing */
    PINT_util_get_current_timeval(&op->timestamp);

    PINT_manager_op_start(manager, op);

    /* service */
    *error = op->operation(op->operation_ptr, op->hint);

    PINT_manager_op_end(manager, op);

    PINT_util_get_current_timeval(&after); 
    *service_time = PINT_util_get_timeval_diff(&op->timestamp, &after);

    return 0;
}

/**
 * Complete an operation.  This function tells the manager to complete
 * the operation (add the op to the completion queue), and post the next
 * operation if this operation is part of a chained list of operations
 * managed by this manager.  This function should only be called by the
 * worker implementations.
 *
 * @param manager the manager for the operation
 * @param op the operation to complete
 * @param error the error returned by the operation callback
 *
 * @return 0 on success, -PVFS_error on error.  If the operation
 *         isn't found in the list of operations managed by this
 *         manager, -PVFS_EINVAL is returned.  As such, errors returned
 *         by this function are likely programmer errors, not system
 *         errors.
 */
int PINT_manager_complete_op(PINT_manager_t manager,
                             PINT_operation_t *op,
                             int error)
{
    struct qhash_head *hash_entry;
    struct PINT_op_entry *entry;
    int ret;
    struct qhash_head *link;

    gen_mutex_lock(&manager->mutex);
    hash_entry = qhash_search(manager->ops, &op->id);
    if(!hash_entry)
    {
        /* failed to get the managed op out of the manager operations queue */
        gossip_err("%s: failed to get the managed op %llu out of the "
                   "manager operations queue\n",
                   __func__, llu(op->id));
        gen_mutex_unlock(&manager->mutex);
        return -PVFS_EINVAL;
    }
    manager->op_count--;
    gen_mutex_unlock(&manager->mutex);

    entry = qhash_entry(hash_entry, struct PINT_op_entry, link);
    entry->error = error;

    ret = PINT_context_complete(manager->context,
                                entry->op.id,
                                entry->user_ptr,
                                entry->error);
    gen_mutex_lock(&manager->mutex);
    link = qhash_search_and_remove(
        manager->ops, &entry->op.id);
    gen_mutex_unlock(&manager->mutex);

    /* for now we ignore whether the op was in the table
     * since blocking calls never add the op
     * to the table
     */

    /* free the op entry */
    free(entry);
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

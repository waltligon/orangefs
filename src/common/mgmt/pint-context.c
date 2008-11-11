
#include <assert.h>
#include <string.h>
#include "pint-context.h"
#include "gen-locks.h"
#include "quickhash.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "pvfs2-debug.h"
#include "gossip.h"

#define PINT_CONTEXT_TABLE_SIZE 512

/* static hash of all contexts in this system.  This allows
 * callers to open contexts and keep track of them with context ids.
 */
static struct qhash_table *pint_contexts = NULL;
static int pint_context_count = 0;
static gen_mutex_t pint_context_mutex = GEN_MUTEX_INITIALIZER;

struct PINT_context_queue_entry
{
    PINT_op_id op_id;
    void *user_ptr;
    PVFS_error result;

    PINT_queue_entry_t qentry;
};

struct PINT_context
{
    PINT_context_id id;
    enum PINT_context_type type;
    union
    {
        PINT_queue_id queue;
        PINT_completion_callback callback;
    } u;
    int refcount;
    struct qlist_head link;
};

static struct PINT_context * PINT_context_lookup(PINT_context_id context_id);

static int PINT_context_compare(void *key, struct qhash_head *link);
static int PINT_context_hash(void *key, int tablesize);

/**
 * PINT_context_init
 *
 * Initialize the context layer.  This function should be called
 * before any contexts are created.
 */
static int PINT_context_init(void)
{
    if(pint_contexts)
    {
        return 0;
    }

    pint_contexts = qhash_init(PINT_context_compare, PINT_context_hash,
                               PINT_CONTEXT_TABLE_SIZE);
    if(!pint_contexts)
    {
        return -PVFS_ENOMEM;
    }

    return 0;
}

/**
 * PINT_context_finalize
 *
 * Finalize the context layer.  This function should be called
 * after all contexts have been finalized.
 */
static int PINT_context_finalize(void)
{
    struct PINT_context *ctx, *tmp;
    assert(pint_contexts);

    if(pint_contexts)
    {
        int i = 0;
        for(; i < pint_contexts->table_size; ++i)
        {
            qlist_for_each_entry_safe(ctx, tmp, &(pint_contexts->array[i]), link)
            {
                qhash_del(&ctx->link);
                PINT_close_context(ctx->id);

            }
        }
    }
    qhash_finalize(pint_contexts);

    return 0;
}

/**
 * PINT_open_context
 *
 * Open a new context.  The resulting context id can be used to add
 * completed operations to the completion context, or test for completed
 * operations on the completion context.
 *
 * @param context_id The context id created
 * @param callback  There are two types of contexts.  By default, if
 * this parameter is NULL, a queue context is created, which will queue
 * completed operations until requested for via the test functions.  If
 * the callback parameter is not NULL, the context becomes a callback
 * context, and the parameter specifies a callback function that should be
 * called once an operation is completed.
 *
 * @return 0 on success, -PVFS_ENOMEM if out of memory.
 */
int PINT_open_context(
    PINT_context_id *context_id,
    PINT_completion_callback callback)
{
    struct PINT_context *ctx;
    int ret;

    assert(context_id);

    gen_mutex_lock(&pint_context_mutex);
    if(pint_context_count == 0)
    {
        PINT_context_init();
    }
    gen_mutex_unlock(&pint_context_mutex);

    ctx = malloc(sizeof(struct PINT_context));
    if(!ctx)
    {
        gen_mutex_unlock(&pint_context_mutex);
        return -PVFS_ENOMEM;
    }
    memset(ctx, 0, sizeof(struct PINT_context));

    if(callback)
    {
        /* this is a callback context */
        ctx->type = PINT_CONTEXT_TYPE_CALLBACK;
        ctx->u.callback = callback;
    }
    else
    {
        ctx->type = PINT_CONTEXT_TYPE_QUEUE;
        ret = PINT_queue_create(&ctx->u.queue, NULL);
        if(ret < 0)
        {
            free(ctx);
            return ret;
        }
        PINT_queue_add_consumer(ctx->u.queue, ctx);
        PINT_queue_add_producer(ctx->u.queue, ctx);
    }

    id_gen_fast_register(&ctx->id, ctx);
    *context_id = ctx->id;

    gen_mutex_lock(&pint_context_mutex);
    qhash_add(pint_contexts, &ctx->id, &ctx->link);
    gen_mutex_unlock(&pint_context_mutex);
    return 0;
}

/**
 * PINT_close_context
 *
 * Close the context.
 */
int PINT_close_context(PINT_context_id context_id)
{
    struct qhash_head *entry;
    struct PINT_context *ctx;

    gen_mutex_lock(&pint_context_mutex);

    entry = qhash_search_and_remove(pint_contexts, &context_id);
    if(!entry)
    {
        gen_mutex_unlock(&pint_context_mutex);
        return -PVFS_EINVAL;
    }
    gen_mutex_unlock(&pint_context_mutex);

    ctx = qhash_entry(entry, struct PINT_context, link);
    assert(ctx->refcount == 0);

    if(ctx->type == PINT_CONTEXT_TYPE_QUEUE)
    {
        if(PINT_queue_count(ctx->u.queue) != 0)
        {
            /* the completion queue isn't empty!  Can't
             * close the context just yet.
             */
            gen_mutex_unlock(&pint_context_mutex);
            return -PVFS_EINVAL;
        }
        PINT_queue_remove_producer(ctx->u.queue, ctx);
        PINT_queue_remove_consumer(ctx->u.queue, ctx);
        PINT_queue_destroy(ctx->u.queue);
    }
    free(ctx);

    pint_context_count--;
    if(pint_context_count > 0)
    {
        PINT_context_finalize();
    }

    return 0;
}

int PINT_context_reference(PINT_context_id context_id)
{
    struct PINT_context *ctx;

    ctx = PINT_context_lookup(context_id);
    assert(ctx);

    ctx->refcount++;
    return 0;
}

int PINT_context_dereference(PINT_context_id context_id)
{
    struct PINT_context *ctx;

    ctx = PINT_context_lookup(context_id);
    assert(ctx);

    ctx->refcount--;
    return 0;
}

static struct PINT_context * PINT_context_lookup(PINT_context_id context_id)
{
    struct qhash_head *entry;

    entry = qhash_search(pint_contexts, &context_id);
    if(!entry)
    {
        return NULL;
    }
    return qhash_entry(entry, struct PINT_context, link);
}

/**
 * PINT_context_complete
 *
 * Give the completion context a completed operation to either queue
 * for later testing, or pass on to the callback for the completion context.
 *
 * @param context_id the context to give the completed operation to
 * @param op_id the operation id of the completed operation
 * @param user_ptr the user pointer given as part of the operation
 * @param result the result of the operation
 *
 * @return 0 on success
 */
int PINT_context_complete(PINT_context_id context_id,
                          PINT_op_id op_id,
                          void *user_ptr,
                          PVFS_error result)
{
    int ret;
    struct PINT_context *ctx;

    gossip_debug(GOSSIP_MGMT_DEBUG, 
                 "%s: completing op: %llu, user_ptr: %p, result: %d\n",
                 __func__, llu(op_id), user_ptr, result);

    ctx = PINT_context_lookup(context_id);
    if(!ctx)
    {
        return -PVFS_EINVAL;
    }

    if(ctx->type == PINT_CONTEXT_TYPE_CALLBACK)
    {
        ctx->u.callback(context_id, 1, &op_id, &user_ptr, &result);
    }
    else
    {
        struct PINT_context_queue_entry *ctx_entry;

        /* If the malloc here is too expensive for every completed
         * operation, we should add some logic that populates new
         * entries in an unused list and pulls them off of that when
         * operations are completed.
         */
        ctx_entry = malloc(sizeof(struct PINT_context_queue_entry));
        if(!ctx_entry)
        {
            return -PVFS_ENOMEM;
        }
        memset(ctx_entry, 0, sizeof(struct PINT_context_queue_entry));

        ctx_entry->op_id = op_id;
        ctx_entry->user_ptr = user_ptr;
        ctx_entry->result = result;
        ret = PINT_queue_push(ctx->u.queue, &ctx_entry->qentry);
        if(ret < 0)
        {
            free(ctx_entry);
            return ret;
        }
    }

    return 0;
}

/**
 * PINT_context_complete_list
 *
 * Complete a list of operations
 */
int PINT_context_complete_list(PINT_context_id context_id,
                               int count,
                               PINT_op_id *op_ids,
                               void **user_ptrs,
                               PVFS_error *errors)
{
    int ret;
    struct PINT_context *ctx;
    int i;

    assert(count > 0);

    /* get first op */
    ctx = PINT_context_lookup(context_id);
    if(!ctx)
    {
        return -PVFS_EINVAL;
    }

    /* callers can only test on queue contexts */
    assert(ctx->type != PINT_CONTEXT_TYPE_CALLBACK);

    for(i = 0; i < count; ++i)
    {
        struct PINT_context_queue_entry *ctx_entry;

        ctx_entry = malloc(sizeof(struct PINT_context_queue_entry));
        if(!ctx_entry)
        {
            return -PVFS_ENOMEM;
        }
        ctx_entry->op_id = op_ids[i];
        ctx_entry->user_ptr = user_ptrs[i];
        ctx_entry->result = errors[i];

        ret = PINT_queue_push(ctx->u.queue, &ctx_entry->qentry);
        if(ret < 0)
        {
            free(ctx_entry);
            return ret;
        }
    }

    return 0;
}

/**
 * PINT_context_test_all
 *
 * Test all the operations in the completion context, returning
 * as many as possible in the return parameters.
 *
 * @param context_id the context to test on
 * @param count As an input parameter, specifies the capacity of the
 * return parameters (the number of completed operations that can be returned).
 * As an output parameter, specifies the number of operations returned as
 * completed.
 * @param op_ids a return parameter - the array of completed operation ids
 * @param user_ptrs a return parameter - the array of user pointers associated 
 * with the completed operations 
 * @param errors a return paramter - the array of errors associated 
 * with the completed operations
 * @param timeout_ms The timeout in microseconds to wait for the completed
 * operations.
 *
 * @return 0 on success.  If the context type is a callback context, then
 * this function will return PINT_CONTEXT_TYPE_CALLBACK, 
 * which can be treated as non-fatal.  
 * Otherwise, the errors returned will be -PVFS_* and should be considered 
 * fatal.
 */
int PINT_context_test_all(PINT_context_id context_id,
                          int *count,
                          PINT_op_id *op_ids,
                          void **user_ptrs,
                          PVFS_error * errors,
                          int timeout_ms)
{
    struct PINT_context_queue_entry *ctx_entry;
    PINT_queue_entry_t *qentry;
    struct PINT_context *ctx;
    int ret;
    int i = 0;
    void *uptr;

    ctx = PINT_context_lookup(context_id);
    if(!ctx)
    {
        return -PVFS_EINVAL;
    }

    /* callers can only test on queue contexts */
    assert(ctx->type != PINT_CONTEXT_TYPE_CALLBACK);

    ret = PINT_queue_timedwait(ctx->u.queue, count,
                               (PINT_queue_entry_t **)user_ptrs, timeout_ms);
    if(ret < 0)
    {
        *count = 0;
        return ret;
    }

    for(; i < *count; ++i)
    {
        qentry = user_ptrs[i];
        ctx_entry = PINT_queue_entry_object(
            qentry, struct PINT_context_queue_entry, qentry);
        op_ids[i] = ctx_entry->op_id;
        uptr = ctx_entry->user_ptr;
        errors[i] = ctx_entry->result;
        user_ptrs[i] = uptr;
    }

    return 0;
}

static int PINT_context_find_id_callback(
    PINT_queue_entry_t *entry, void *user_ptr)
{
    struct PINT_context_queue_entry *ctx_entry;

    ctx_entry = PINT_queue_entry_object(entry,
                                        struct PINT_context_queue_entry, qentry);
    if(*((PINT_op_id *)user_ptr) == ctx_entry->op_id)
    {
        return 1;
    }

    return 0;
}

#if 0
static int PINT_context_find_id(
    struct PINT_context *ctx,
    PINT_op_id op_id,
    struct PINT_context_queue_entry **entry)
{
    PINT_queue_entry_t *qentry;
    int ret;

    /* if there's stuff in the queue we fill the output parameters and
     * return immediately */
    ret = PINT_queue_search_and_remove(
        ctx->u.queue, PINT_context_find_id_callback,
        &op_id, &qentry);
    if(ret < 0)
    {
        return ret;
    }

    *entry = PINT_queue_entry_object(qentry, 
                                     struct PINT_context_queue_entry, qentry);
    return 0;
}
#endif

/**
 * PINT_context_test
 *
 * Test the specified operation in the completion context, returning
 * the parameters of the operation in the output parameters.
 *
 * @param context_id the context to test on
 * @param op_id which completed operation to look for
 * @param user_ptr a return parameter - the user pointer associated
 * with the completed operation
 * @param error a return paramter - the error associated
 * with the completed operation
 * @param timeout_ms The timeout in microseconds to wait for the completed
 * operation.
 *
 * @return 0 on success.  If the context type is a callback context, then
 * this function will return -PVFS_ENOMSG.
 * Otherwise, the errors returned will be -PVFS_* and should be considered 
 * fatal.
 */
int PINT_context_test(PINT_context_id context_id,
                      PINT_op_id op_id,
                      void **user_ptr,
                      PVFS_error *error,
                      int microsecs)
{
    struct PINT_context *ctx;
    struct PINT_context_queue_entry *entry;
    PINT_queue_entry_t *qentry;
    int ret;

    ctx = PINT_context_lookup(context_id);
    if(!ctx)
    {
        gen_mutex_unlock(&pint_context_mutex);
        return -PVFS_EINVAL;
    }

    /* callers can only test on queue contexts */
    assert(ctx->type != PINT_CONTEXT_TYPE_CALLBACK);

    ret = PINT_queue_wait_for_entry(ctx->u.queue, PINT_context_find_id_callback,
                                    &op_id, &qentry, microsecs);
    if(ret == 0)
    {
        entry = PINT_queue_entry_object(
            qentry,
            struct PINT_context_queue_entry, qentry);
        *user_ptr = entry->user_ptr;
        *error = entry->result;
        free(entry);
    }

    /* otherwise nothing to do so just return. */
    return ret;
}

static int PINT_context_hash(void *key, int tablesize)
{
    unsigned long ret = 0;
    PINT_context_id *id;

    id = (PINT_context_id *)key;

    ret += *id;
    ret = ret & (tablesize - 1);

    return (int) ret;
}

static int PINT_context_compare(void *key, struct qhash_head *link)
{
    PINT_context_id *id = (PINT_context_id *)key;
    struct PINT_context *context;

    context = qhash_entry(link, struct PINT_context, link);

    if(*id == context->id)
    {
        return 1;
    }
    return 0;
}

int PINT_context_is_callback(PINT_context_id context_id)
{
    struct PINT_context *ctx;

    ctx = PINT_context_lookup(context_id);
    assert(ctx);
    if(ctx->type == PINT_CONTEXT_TYPE_CALLBACK)
    {
        return 1;
    }
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

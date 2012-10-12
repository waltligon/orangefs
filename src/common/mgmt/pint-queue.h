/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_QUEUE_H
#define PINT_QUEUE_H

#include "id-generator.h"
#include "quicklist.h"
#include "gen-locks.h"
#include "pvfs2-types.h"

typedef PVFS_id_gen_t PINT_queue_id;

typedef struct
{
    PVFS_id_gen_t id;
    struct timeval timestamp;
    struct qlist_head link;
} PINT_queue_entry_t;

typedef int (* PINT_queue_entry_compare_callback)(
    PINT_queue_entry_t *a, PINT_queue_entry_t *b);

struct PINT_queue_stats
{
    int avg_queued_time;
    int var_queued_time;
    int total_queued;
};

struct PINT_queue_s
{
    /* The generated id for this queue */
    PINT_queue_id id;

    PINT_queue_entry_compare_callback compare;

    /* Locks the queue during multithreaded access */
    gen_mutex_t mutex;

    gen_cond_t cond;

    /* The entries list */
    struct qlist_head entries;

    /* Count of entries in the queue */
    int count;

    /* Count of maximum number of entries to accept in the queue
     * before returning EAGAIN
     */
    int max;

    /* Triggers to be invoked on some action */
    struct qlist_head triggers;

    /* Queue stats.  Don't access this struct directly, that's
     * what the get_stats function is for 
     * */
    struct PINT_queue_stats stats;

    int producer_refcount;
    int consumer_refcount;

    /* link for adding this queue to lists of queues (used by worker) */
    struct qlist_head link;
};

#define PINT_queue_entry_object(_entryp, _type, _member) \
    ((_type *)((char *)(_entryp) - (unsigned long)&((_type *)0)->_member))

enum PINT_queue_action
{
    /* The EMPTIED action is triggered when the queue reaches empty.
     * Any triggers registered for the EMPTY action on this queue will
     * be called once it becomes empty.
     * The value argument passed to the trigger callback is NULL for this
     * action and should be ignored. */
    PINT_QUEUE_ACTION_EMPTIED,

    /* The POSTED action is triggered once a new operation is posted
     * to the queue.  Any triggers registered for the POSTED action
     * on this queue will be called once a new operation has been posted.
     * The value argument passed to the trigger callback is the count of
     * operations in the queue after the post.
     */
    PINT_QUEUE_ACTION_POSTED,

    /* The REMOVED action is triggered once operations are pulled from
     * the queue.  Any triggers registered for the REMOVED action
     * on this queue will be called once operations have been removed.
     * The value argument passed to the trigger callback is the count
     * of operations in the queue after the removals.
     */
    PINT_QUEUE_ACTION_REMOVED
};

typedef int (*PINT_queue_trigger_callback) (void *user_ptr, void *action_value);
typedef int (*PINT_queue_trigger_destroy) (void *user_ptr);

int PINT_queue_create(PINT_queue_id *queue,
                      PINT_queue_entry_compare_callback compare);

int PINT_queue_destroy(PINT_queue_id queue);

int PINT_queue_add_producer(PINT_queue_id queue, void *producer);

int PINT_queue_add_consumer(PINT_queue_id queue, void *consumer);

int PINT_queue_remove_producer(PINT_queue_id queue, void *producer);

int PINT_queue_remove_consumer(PINT_queue_id queue, void *consumer);

int PINT_queue_add_trigger(PINT_queue_id queue,
                           enum PINT_queue_action action,
                           PINT_queue_trigger_callback trigger,
                           PINT_queue_trigger_destroy destroy,
                           void *user_ptr);

int PINT_queue_count(PINT_queue_id queue_id);

int PINT_queue_push(PINT_queue_id queue_id, PINT_queue_entry_t *entry);

int PINT_queue_push_front(PINT_queue_id qid, PINT_queue_entry_t *entry);

int PINT_queue_pull(PINT_queue_id queue_id,
                    int *count,
                    PINT_queue_entry_t **entries);

int PINT_queue_remove(PINT_queue_id queue_id,
                      PINT_queue_entry_t *entry);

typedef int (*PINT_queue_entry_find_callback)(
    PINT_queue_entry_t *entry, void *user_ptr);

int PINT_queue_search_and_remove(PINT_queue_id queue_id,
                                 PINT_queue_entry_find_callback find,
                                 void *user_ptr,
                                 PINT_queue_entry_t **entry);

int PINT_queue_wait_for_entry(PINT_queue_id queue_id,
                              PINT_queue_entry_find_callback find,
                              void *user_ptr,
                              PINT_queue_entry_t **entry,
                              int microsecs);

int PINT_queue_timedwait(PINT_queue_id queue_id,
                         int *count,
                         PINT_queue_entry_t **entries,
                         int microsecs);

int PINT_queue_wait(PINT_queue_id queue_id,
                    int *count,
                    PINT_queue_entry_t **entries);

int PINT_queue_get_stats(PINT_queue_id queue,
                         struct PINT_queue_stats *stats);

int PINT_queue_reset_stats(PINT_queue_id queue);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

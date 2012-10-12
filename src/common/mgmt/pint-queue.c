/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pint-queue.h"
#include "gossip.h"
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "pint-util.h"
#include "pvfs2-debug.h"

struct PINT_queue_trigger
{
    enum PINT_queue_action action;
    PINT_queue_trigger_callback trigger;
    PINT_queue_trigger_destroy destroy;
    void *user_ptr;
    struct qlist_head link;
};

static int PINT_queue_update_stats(struct PINT_queue_s *queue, int microsecs);

inline static int PINT_default_compare(
    PINT_queue_entry_t *a, PINT_queue_entry_t *b)
{
    return 0;
}

int PINT_queue_create(
    PINT_queue_id *qid, PINT_queue_entry_compare_callback compare)
{
    struct PINT_queue_s *queue;
    int ret = 0;

    queue = malloc(sizeof(struct PINT_queue_s));
    if(!queue)
    {
        return -PVFS_ENOMEM;
    }
    memset(queue, 0, sizeof(struct PINT_queue_s));

    if(compare)
    {
        queue->compare = compare;
    }
    gen_mutex_init(&queue->mutex);
    gen_cond_init(&queue->cond);

    INIT_QLIST_HEAD(&queue->triggers);
    INIT_QLIST_HEAD(&queue->entries);
    id_gen_fast_register(&queue->id, queue);
    *qid = queue->id;
    return ret;
}

int PINT_queue_destroy(PINT_queue_id qid)
{
    struct PINT_queue_s *queue;
    struct PINT_queue_trigger *trigger;
    struct PINT_queue_trigger *tmp;

    queue = id_gen_fast_lookup(qid);
    assert(queue);
    gen_mutex_lock(&queue->mutex);

    /* the producers and consumers should have unregistered */
    assert(queue->producer_refcount == 0 &&
           queue->consumer_refcount == 0);

    if(!qlist_empty(&queue->entries))
    {
        gossip_err("%s: can't destroy non-empty queue\n", __func__);
        return -PVFS_EINVAL;
    }

    qlist_for_each_entry_safe(trigger, tmp, &queue->triggers, link)
    {
        trigger->destroy(trigger->user_ptr);
        qlist_del(&trigger->link);
        free(trigger);
    }

    gen_cond_destroy(&queue->cond);
    gen_mutex_unlock(&queue->mutex);

    free(queue);
    return 0;
}

int PINT_queue_add_producer(PINT_queue_id queue_id,
                            void *producer)
{
    /* right now we just increment the ref counter.  Could keep
     * track of producers at some point */

    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&queue->mutex);
    queue->producer_refcount++;
    gen_mutex_unlock(&queue->mutex);
    return 0;
}

int PINT_queue_add_consumer(PINT_queue_id queue_id,
                            void *consumer)
{
    /* right now we just increment the ref counter.  Could keep
     * track of consumers at some point */

    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&queue->mutex);
    queue->consumer_refcount++;
    gen_mutex_unlock(&queue->mutex);
    return 0;
}

int PINT_queue_remove_producer(PINT_queue_id queue_id,
                               void *producer)
{
    /* right now we just decrement the ref counter.  Could keep
     * track of producers at some point */

    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&queue->mutex);
    queue->producer_refcount--;
    gen_mutex_unlock(&queue->mutex);
    return 0;
}

int PINT_queue_remove_consumer(PINT_queue_id queue_id,
                               void *consumer)
{
    /* right now we just decrement the ref counter.  Could keep
     * track of consumers at some point */

    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&queue->mutex);
    queue->consumer_refcount--;
    gen_mutex_unlock(&queue->mutex);
    return 0;
}

int PINT_queue_add_trigger(PINT_queue_id queue_id,
                           enum PINT_queue_action action,
                           PINT_queue_trigger_callback trigger,
                           PINT_queue_trigger_destroy destroy,
                           void *user_ptr)
{
    struct PINT_queue_s *queue;
    struct PINT_queue_trigger *trigger_entry;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    trigger_entry = malloc(sizeof(struct PINT_queue_trigger));
    if(!trigger_entry)
    {
        return -PVFS_ENOMEM;
    }

    trigger_entry->trigger = trigger;
    trigger_entry->destroy = destroy;
    trigger_entry->action = action;
    trigger_entry->user_ptr = user_ptr;

    gen_mutex_lock(&queue->mutex);
    qlist_add(&queue->triggers, &trigger_entry->link);
    gen_mutex_unlock(&queue->mutex);
    return 0;
}

inline int PINT_queue_count(PINT_queue_id qid)
{
    struct PINT_queue_s *queue;
    int count;
    queue = id_gen_fast_lookup(qid);
    if(!queue)
    {
        return -PVFS_EINVAL;
    }

    gen_mutex_lock(&queue->mutex);
    count = queue->count;
    gen_mutex_unlock(&queue->mutex);
    return count;
}

static int PINT_queue_insert(PINT_queue_id qid,
                             PINT_queue_entry_t *entry,
                             int front)
{
    struct PINT_queue_s *queue;
    struct PINT_queue_trigger *trigger;

    assert(entry->link.next == NULL && entry->link.prev == NULL);

    queue = id_gen_fast_lookup(qid);
    if(!queue)
    {
        return -PVFS_EINVAL;
    }

    gen_mutex_lock(&queue->mutex);
    if(front)
    {
        /* push it onto the front */
        gossip_debug(GOSSIP_MGMT_DEBUG,
                     "%s: pushing entry: %p to front of queue: %p\n",
                     __func__, &entry->link, queue);
        qlist_add(&entry->link, &queue->entries);
    }
    else
    {
        gossip_debug(GOSSIP_MGMT_DEBUG,
                     "%s: pushing entry: %p to back of queue: %p\n",
                     __func__, &entry->link, queue);
        qlist_add_tail(&entry->link, &queue->entries);
    }

    /* set the timestamp for when the entries enters the queue */
    PINT_util_get_current_timeval(&entry->timestamp);

    queue->count++;

    /* for now, we signal on all any addition to the queue
     * (not just from an empty queue).  This allows us to test
     * on a particular entry
     */
    gen_cond_signal(&queue->cond);

    qlist_for_each_entry(trigger, &queue->triggers, link)
    {
        if(trigger->action == PINT_QUEUE_ACTION_POSTED)
        {
            trigger->trigger(trigger->user_ptr, &queue->count);
        }
    }

    gen_mutex_unlock(&queue->mutex);
    return 0;
}

int PINT_queue_push(PINT_queue_id qid, PINT_queue_entry_t *entry)
{
    return PINT_queue_insert(qid, entry, 0);
}

int PINT_queue_push_front(PINT_queue_id qid, PINT_queue_entry_t *entry)
{
    return PINT_queue_insert(qid, entry, 1);
}

int PINT_queue_pull(PINT_queue_id qid,
                    int *count,
                    PINT_queue_entry_t **entries)
{
    int retcount = 0;
    PINT_queue_entry_t *entry, *tmp;
    struct PINT_queue_trigger *trigger;
    struct PINT_queue_s *queue;
    struct timeval now;

    queue = id_gen_fast_lookup(qid);
    assert(queue);
    assert(*count > 0);

    gen_mutex_lock(&queue->mutex);
    qlist_for_each_entry_safe(entry, tmp, &queue->entries, link)
    {
        entries[retcount] = entry;

        gossip_debug(GOSSIP_MGMT_DEBUG,
                     "%s: removing entry: %p from queue: %p\n",
                     __func__, &entry->link, queue);
        qlist_del(&entry->link);
        memset(&entry->link, 0, sizeof(entry->link));
        queue->count--;

        /* update average queued time for this queue */
        PINT_util_get_current_timeval(&now);
        PINT_queue_update_stats(
            queue, PINT_util_get_timeval_diff(&entry->timestamp, &now));

        ++retcount;
        if(retcount == *count)
        {
            break;
        }
    }

    gossip_debug(GOSSIP_MGMT_DEBUG, "%s: returning %d entries\n",
                 __func__, *count);
    *count = retcount;

    if(retcount > 0)
    {

        qlist_for_each_entry(trigger, &queue->triggers, link)
        {
            if(trigger->action == PINT_QUEUE_ACTION_REMOVED)
            {
                trigger->trigger(trigger->user_ptr, &queue->count);
            }

            if(queue->count == 0 && 
               trigger->action == PINT_QUEUE_ACTION_EMPTIED)
            {
                trigger->trigger(trigger->user_ptr, NULL);
            }
        }
    }

    gen_mutex_unlock(&queue->mutex);

    return 0;
}

int PINT_queue_remove(PINT_queue_id queue_id,
                      PINT_queue_entry_t *entry)
{
    struct PINT_queue_s *queue;
    struct PINT_queue_trigger *trigger;
    PINT_queue_entry_t *e, *tmp;
    struct timeval now;
    int ret = -PVFS_ENOENT;

    if(entry->link.prev == NULL)
    {
        return -PVFS_ENOENT;
    }

    queue = id_gen_fast_lookup(queue_id);

    gen_mutex_lock(&queue->mutex);
    if(queue->count > 0)
    {
        /* make sure its actually in the queue somewhere */
        qlist_for_each_entry_safe(e, tmp, &queue->entries, link)
        {
            if(e == entry)
            {
                gossip_debug(GOSSIP_MGMT_DEBUG,
                             "%s: removing entry: %p from queue: %p\n",
                             __func__, &e->link, queue);
                qlist_del(&e->link);
                memset(&e->link, 0, sizeof(e->link));
                queue->count--;

                PINT_util_get_current_timeval(&now);

                PINT_queue_update_stats(
                    queue,
                    PINT_util_get_timeval_diff(&e->timestamp, &now));
                ret = 0;
                break;
            }
        }
    }

    qlist_for_each_entry(trigger, &queue->triggers, link)
    {
        if(trigger->action == PINT_QUEUE_ACTION_REMOVED)
        {
            trigger->trigger(trigger->user_ptr, &queue->count);
        }

        if(queue->count == 0 && trigger->action == PINT_QUEUE_ACTION_EMPTIED)
        {
            trigger->trigger(trigger->user_ptr, NULL);
        }
    }

    gen_mutex_unlock(&queue->mutex);

    return ret;
}

int PINT_queue_search_and_remove(PINT_queue_id queue_id,
                                 PINT_queue_entry_find_callback compare,
                                 void *user_ptr,
                                 PINT_queue_entry_t **entry)
{
    struct PINT_queue_s *queue;
    PINT_queue_entry_t *e, *tmp;

    queue = id_gen_fast_lookup(queue_id);

    gen_mutex_lock(&queue->mutex);
    qlist_for_each_entry_safe(e, tmp, &queue->entries, link)
    {
        if(compare(e, user_ptr))
        {
            *entry = e;
            qlist_del(&e->link);
            gen_mutex_unlock(&queue->mutex);
            return 0;
        }
    }
    gen_mutex_unlock(&queue->mutex);
    return -PVFS_ENOENT;
}

int PINT_queue_wait_for_entry(PINT_queue_id queue_id,
                              PINT_queue_entry_find_callback find,
                              void *user_ptr,
                              PINT_queue_entry_t **entry,
                              int microsecs)
{
    struct PINT_queue_s *queue;
    int ret = 0, cond_ret = 0;
    struct timespec timeout;

    ret = PINT_queue_search_and_remove(queue_id, find, user_ptr, entry);
    if(0 == ret)
    {
        return ret;
    }

    if(ret != -PVFS_ENOENT)
    {
        return ret;
    }

    queue = id_gen_fast_lookup(queue_id);

    /* queue does not have entry, wait for signal of addition */
    timeout = PINT_util_get_abs_timespec(microsecs);

    do
    {
        gen_mutex_lock(&queue->mutex);
        cond_ret = gen_cond_timedwait(&queue->cond, &queue->mutex, &timeout);
        gen_mutex_unlock(&queue->mutex);

        /* either the condition variable gets signalled, we have
         * a spurious wakeup, or the timeout was reached.  
         * If gen_cond_timedwait didn't return ETIMEDOUT,
         * then we assume something was added to the queue.  If the queue
         * is still empty, the return from timedwait must have been spurious
         * and we should try again with an updated value for microsecs.
         */
        ret = PINT_queue_search_and_remove(
            queue_id, find, user_ptr, entry);
        if(ret == 0 || ret != -PVFS_ENOENT)
        {
            return ret;
        }

    } while(cond_ret == 0);

    gen_mutex_unlock(&queue->mutex);
    if(cond_ret != 0)
    {
        if(cond_ret == ETIMEDOUT)
        {
            cond_ret = -PVFS_ETIMEDOUT;
        }
        if(cond_ret == EINVAL)
        {
            cond_ret = -PVFS_EINVAL;
        }
        return cond_ret;
    }

    return PINT_queue_search_and_remove(queue_id, find, user_ptr, entry);
}

int PINT_queue_timedwait(PINT_queue_id queue_id,
                         int *count,
                         PINT_queue_entry_t **entries,
                         int microsecs)
{
    struct PINT_queue_s *queue;
    int ret = 0;
    struct timespec timeout;

    queue = id_gen_fast_lookup(queue_id);

    gen_mutex_lock(&queue->mutex);

    if(queue->count > 0)
    {
        gen_mutex_unlock(&queue->mutex);
        return PINT_queue_pull(queue_id, count, entries);
    }

    /* queue is empty, wait for signal of addition */

    timeout = PINT_util_get_abs_timespec(microsecs);

    do
    {
        ret = gen_cond_timedwait(&queue->cond, &queue->mutex, &timeout);
        if(ret == EINVAL)
        {
            gossip_lerr("gen_cond_timedwait returned EINVAL!\n");
        }

        /* either the condition variable gets signalled, we have
         * a spurious wakeup, or the timeout was reached.  
         * If gen_cond_timedwait didn't return ETIMEDOUT,
         * then we assume something was added to the queue.  If the queue
         * is still empty, the return from timedwait must have been spurious
         * and we should try again with an updated value for microsecs.
         */
    } while(queue->count == 0 && ret == 0);

    gen_mutex_unlock(&queue->mutex);
    if(ret != 0)
    {
        *count = 0;
        if(ret == EINVAL)
        {
            ret = -PVFS_EINVAL;
        }
        else if(ret == ETIMEDOUT)
        {
            ret = -PVFS_ETIMEDOUT;
        }
        return ret;
    }

    return PINT_queue_pull(queue_id, count, entries);
}

int PINT_queue_wait(PINT_queue_id queue_id,
                    int *count,
                    PINT_queue_entry_t **entries)
{
    struct PINT_queue_s *queue;
    int ret = 0;

    queue = id_gen_fast_lookup(queue_id);
    assert(queue);

    gen_mutex_lock(&queue->mutex);
    if(queue->count > 0)
    {
        gen_mutex_unlock(&queue->mutex);
        return PINT_queue_pull(queue_id, count, entries);
    }
    /* queue is empty, wait for signal of addition */

    do
    {
        ret = gen_cond_wait(&queue->cond, &queue->mutex);

        /* either the condition variable gets signalled, or we have
         * a spurious wakeup.
         * If gen_cond_wait didn't return an error,
         * then we assume something was added to the queue.  If the queue
         * is still empty, the return from cond_wait must have been spurious
         * and we should try again.
         */
    } while(queue->count == 0 && ret == 0);

    gen_mutex_unlock(&queue->mutex);
    if(ret != 0)
    {
        return ret;
    }

    return PINT_queue_pull(queue_id, count, entries);
}

int PINT_queue_get_stats(PINT_queue_id queue_id, struct PINT_queue_stats *stats)
{
    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    stats->total_queued = queue->stats.total_queued;
    stats->avg_queued_time = queue->stats.avg_queued_time;
    stats->var_queued_time = queue->stats.var_queued_time / 
        (queue->stats.total_queued - 1);
    return 0;
}

int PINT_queue_reset_stats(PINT_queue_id queue_id)
{
    struct PINT_queue_s *queue;

    queue = id_gen_fast_lookup(queue_id);
    memset(&queue->stats, 0, sizeof(struct PINT_queue_stats));
    return 0;
}

static int PINT_queue_update_stats(struct PINT_queue_s *queue, int microsecs)
{
    int diff;

    /* note that the average and variance are estimates based on an
     * algorithm due to Knuth
     */

    queue->stats.total_queued++;

    diff = microsecs - queue->stats.avg_queued_time;
    queue->stats.avg_queued_time = (diff / queue->stats.total_queued);

    queue->stats.var_queued_time += 
        diff * (microsecs - queue->stats.avg_queued_time);

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


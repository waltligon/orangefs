/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* used for storing for storing Trove operation id's  
 *
 */

#include <stdlib.h>
#include <errno.h>

#include "gossip.h"
#include "trove-id-queue.h"

/* maximum number of trove collection ids we are willing to track */
#define TROVE_ID_QUEUE_MAX 16

/* trove_id_queue_new()
 *
 * create a new queue of trove ids
 *
 * returns pointer to queue on success, NULL on failure
 */
trove_id_queue_p trove_id_queue_new(void)
{
    trove_id_queue_p tmp_queue = NULL;
    int i;

    tmp_queue = (trove_id_queue_p)malloc(
	TROVE_ID_QUEUE_MAX* sizeof(struct coll_index));
    if(tmp_queue)
    {
	for(i=0; i<TROVE_ID_QUEUE_MAX; i++)
	{
	    tmp_queue[i].coll_id = -1;
	    INIT_QLIST_HEAD(&(tmp_queue[i].trove_id_queue));
	}
    }

    return(tmp_queue);
}

/* trove_id_queue_add()
 *
 * adds a trove op id to the queue
 *
 * returns 0 on success, -errno on failure
 */
int trove_id_queue_add(trove_id_queue_p queue,
		    PVFS_ds_id op_id,
		    PVFS_coll_id coll_id)
{
    int index = -1;
    int i;
    struct trove_id_entry* tmp_entry;

    /* first thing to do is to find an index into the array that matches
     * this collection id
     */

    /* look for match */
    for(i=0; i<TROVE_ID_QUEUE_MAX; i++)
    {
	if(queue[i].coll_id == coll_id)
	{
	    index = i;
	    break;
	}
    }

    /* if that fails, look for -1 entry */
    if(index == -1)
    {
	for(i=0; i<TROVE_ID_QUEUE_MAX; i++)
	{
	    if(queue[i].coll_id == -1)
	    {
		index = i;
		queue[i].coll_id = coll_id;
		break;
	    }
	}
    }

    /* if that fails, look for non -1 entry with empty queue to take over */
    if(index == -1)
    {
	for(i=0; i<TROVE_ID_QUEUE_MAX; i++)
	{
	    if(qlist_empty(&(queue[i].trove_id_queue)))
	    {
		index = i;
		queue[i].coll_id = coll_id;
		break;
	    }
	}
    }

    if(index == -1)
    {
	gossip_lerr("Error: out of space to store trove collection ids.\n");
	return(-ENOMEM);
    }

    /* now insert new op id */
    tmp_entry = (struct trove_id_entry*)malloc(sizeof(struct
	trove_id_entry));
    if(!tmp_entry)
	return(-ENOMEM);

    tmp_entry->trove_id = op_id;
    qlist_add_tail(&(tmp_entry->queue_link),
	&(queue[index].trove_id_queue));

    return(0);
}

/* trove_id_queue_del()
 *
 * deletes an entry from the queue
 *
 * no return value
 */
void trove_id_queue_del(trove_id_queue_p queue,
		     PVFS_ds_id op_id,
		     PVFS_coll_id coll_id)
{
    int i;
    int index = -1;
    struct qlist_head* iterator;
    struct qlist_head* scratch;
    struct trove_id_entry* tmp_entry = NULL;

    /* first, find the index for this collection id */
    for(i=0; i<TROVE_ID_QUEUE_MAX; i++)
    {
	if(queue[i].coll_id == coll_id)
	{
	    index = i;
	    break;
	}
    }

    if(index == -1)
	return;

    /* now find the specific id entry and remove it */
    qlist_for_each_safe(iterator, scratch, &(queue[i].trove_id_queue))
    {
	tmp_entry = qlist_entry(iterator, struct trove_id_entry,
	    queue_link);
	if(tmp_entry->trove_id == op_id)
	{
	    qlist_del(iterator);
	    free(tmp_entry);
	    return;
	}
    }

    return;
}


/* trove_id_queue_cleanup()
 *
 * destroys an existing trove id queue
 *
 * no return value
 */
void trove_id_queue_cleanup(trove_id_queue_p queue)
{
    int i;
    struct qlist_head* iterator;
    struct qlist_head* scratch;
    struct trove_id_entry* tmp_entry = NULL;


    /* run down the array of queues */
    for(i=0; i<TROVE_ID_QUEUE_MAX; i++)
    {
	/* run down each queue */
	qlist_for_each_safe(iterator, scratch, &(queue[i].trove_id_queue))
	{
	    tmp_entry = qlist_entry(iterator, struct trove_id_entry,
		queue_link);
	    /* remove and destroy the entry */
	    qlist_del(iterator);
	    free(tmp_entry);
	}
    }

    /* free the big array */
    free(queue);
    queue = NULL;
   
    return;
}


/* trove_id_queue_query()
 *
 * copies the first count ids in the queue into a contiguous array and
 * sets count to the actual number of ids.  It operates on the first
 * collection id that it can find.  If called successively with each
 * resulting query_offset it will continue to look at different
 * collection ids until it runs out and returns -EAGAIN.  Set
 * query_offset to 0 before making first call.
 *
 * returns 0 on success, -EAGAIN when collections exhausted, -errno on
 * failure
 */
int trove_id_queue_query(trove_id_queue_p queue,
		      PVFS_ds_id *array,
		      int *count,
		      int *query_offset,
		      PVFS_coll_id* coll_id)
{
    int i;
    struct qlist_head* iterator;
    int current_index = 0;
    struct trove_id_entry* tmp_entry;

    if(*query_offset < 0)
	return(-EINVAL);
    
    if(*query_offset >= TROVE_ID_QUEUE_MAX)
	return(-EAGAIN);

    for(i=*query_offset; i<TROVE_ID_QUEUE_MAX; i++)
    {
	if(!qlist_empty(&(queue[i].trove_id_queue)))
	{
	    qlist_for_each(iterator, &(queue[i].trove_id_queue))
	    {
		tmp_entry = qlist_entry(iterator, struct trove_id_entry,
		    queue_link);
		array[current_index] = tmp_entry->trove_id;
		current_index++;
		if(current_index == *count)
		    break;
	    }
	    *query_offset = i+1;
	    *count = current_index;
	    *coll_id = queue[i].coll_id;
	    return(0);
	}
    }

    return(-EAGAIN);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

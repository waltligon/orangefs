/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


#include <queue-list.h>
#include <gossip.h>

struct queue_list_entry
{
	op_id_queue_p queue;
	PVFS_fs_id fsid;
	queue_list_p list_head;

	struct qlist_head list_link;
};

/* queue_list_new()
 *
 * creates a new queue list
 *
 * returns pointer to list on success, NULL on failure
 */
queue_list_p queue_list_new(void)
{

	queue_list_p tmp_list = NULL;

	tmp_list = (queue_list_p)malloc(sizeof(struct qlist_head));
	if(tmp_list)
	{
		INIT_QLIST_HEAD(tmp_list);
	}

	return(tmp_list);
}


/* queue_list_cleanup()
 *
 * destroys an existing queue list
 * NOTE: it doesn't touch the queues that are stored in the list
 * at all; it is the caller's responsibility to get rid of those
 *
 * no return value 
 */
void queue_list_cleanup(queue_list_p list)
{
	queue_list_p tmp_link = NULL;
	struct queue_list_entry* tmp_entry = NULL;
	struct queue_list_entry* prev_entry = NULL;

	qlist_for_each(tmp_link, list)
	{
		/* TODO: pick up here; still thinking about what is safe to
		 * free...
		 */
	}

	gossip_lerr("UNIMPLEMENTED.\n");
	/* TODO: finish */

	return;
}

void queue_list_add(queue_list_p list, op_id_queue_p queue,
	PVFS_fs_id fsid)
{

	gossip_lerr("UNIMPLEMENTED.\n");
	/* TODO: finish */

	return;
}

op_id_queue_p queue_list_query(queue_list_p list, PVFS_fs_id fsid)
{

	gossip_lerr("UNIMPLEMENTED.\n");
	/* TODO: finish */

	return(NULL);
}

PVFS_fs_id queue_list_iterate(queue_list_p* current)
{

	gossip_lerr("UNIMPLEMENTED.\n");
	/* TODO: finish */

	return(-1);
}


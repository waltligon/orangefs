/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


#include <queue-list.h>
#include <gossip.h>

queue_list_p queue_list_new(void)
{

	gossip_lerr("UNIMPLEMENTED.\n");
	/* TODO: finish */

	return(NULL);
}

void queue_list_cleanup(void)
{

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


/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __QUEUE_LIST_H
#define __QUEUE_LIST_H

#include <pvfs2-types.h>
#include <quicklist.h>
#include <op-id-queue.h>

typedef struct qlist_head* queue_list_p;

queue_list_p queue_list_new(void);
void queue_list_cleanup(queue_list_p list);
void queue_list_add(queue_list_p list, op_id_queue_p queue,
	PVFS_fs_id fsid);
op_id_queue_p queue_list_query(queue_list_p list, PVFS_fs_id fsid);
PVFS_fs_id queue_list_iterate(queue_list_p* current);

#endif /* __QUEUE_LIST_H */

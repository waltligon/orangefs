/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* used for storing for storing Trove operation id's  
 *
 */

#ifndef __TROVE_ID_QUEUE_H
#define __TROVE_ID_QUEUE_H

#include "quicklist.h"
#include "trove-types.h"

struct trove_id_entry
{
    TROVE_op_id trove_id;
    struct qlist_head queue_link;
};

struct coll_index
{
    PVFS_fs_id coll_id;
    struct qlist_head trove_id_queue; 
};


typedef struct coll_index* trove_id_queue_p;

trove_id_queue_p trove_id_queue_new(void);
int trove_id_queue_add(trove_id_queue_p queue,
		    TROVE_op_id op_id,
		    PVFS_fs_id coll_id);
void trove_id_queue_del(trove_id_queue_p queue,
		     TROVE_op_id op_id,
		     PVFS_fs_id coll_id);
void trove_id_queue_cleanup(trove_id_queue_p queue);
int trove_id_queue_query(trove_id_queue_p queue,
		      TROVE_op_id *array,
		      int *count,
		      int *query_offset,
		      PVFS_fs_id* coll_id);

#endif /* __TROVE_ID_QUEUE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

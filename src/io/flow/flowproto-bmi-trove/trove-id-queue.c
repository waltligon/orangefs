/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* used for storing for storing Trove operation id's  
 *
 */

#include <errno.h>

#include "trove-id-queue.h"

trove_id_queue_p trove_id_queue_new(void)
{
    return(NULL);
}

int trove_id_queue_add(trove_id_queue_p queue,
		    PVFS_ds_id op_id,
		    PVFS_coll_id coll_id)
{
    return(-ENOSYS);
}

void trove_id_queue_del(trove_id_queue_p queue,
		     PVFS_ds_id op_id,
		     PVFS_coll_id coll_id)
{
    return;
}

void trove_id_queue_cleanup(trove_id_queue_p queue)
{
    return;
}

int trove_id_queue_query(trove_id_queue_p queue,
		      void *array,
		      int *count,
		      PVFS_coll_id* coll_id,
		      int index)
{
    return(-ENOSYS);
}

int trove_id_queue_index_count(trove_id_queue_p queue)
{
    return(-ENOSYS);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* used for storing for storing Trove operation id's  
 *
 */

#ifndef __OP_ID_QUEUE_H
#define __OP_ID_QUEUE_H

#include "quicklist.h"
#include "trove-types.h"

enum
{
    TROVE_OP_ID = 1
};

typedef struct qlist_head *op_id_queue_p;

op_id_queue_p op_id_queue_new(void);
int op_id_queue_add(op_id_queue_p queue,
		    void *id_pointer,
		    int type);
void op_id_queue_del(op_id_queue_p queue,
		     void *id_pointer,
		     int type);
void op_id_queue_cleanup(op_id_queue_p queue);
int op_id_queue_query(op_id_queue_p queue,
		      void *array,
		      int *count,
		      int type);

#endif /* __OP_ID_QUEUE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

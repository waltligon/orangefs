/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <dbpf-op-queue.h>
#include <malloc.h>

struct dbpf_queued_op *dbpf_op_queue_head = NULL;

/* Note: the majority of the operations on the queue are static inlines
 * and are defined in dbpf-op-queue.h for speed.
 */

struct dbpf_queued_op *dbpf_queued_op_alloc(void)
{
    struct dbpf_queued_op *q_op_p;

    q_op_p = (struct dbpf_queued_op *) malloc(sizeof(struct dbpf_queued_op));
    return q_op_p;
}

void dbpf_queued_op_free(struct dbpf_queued_op *q_op_p)
{
    free(q_op_p);
}

void dbpf_queue_list(void)
{
    struct dbpf_queued_op *q_op, *start_op;

    q_op = dbpf_op_queue_head;

    if (q_op == NULL) {
	printf("<queue empty>\n");
	return;
    }
    
    start_op = q_op;
    printf("op: id=%Lx, type=%d, state=%d, handle=%Lx\n",
	   q_op->op.id,
	   q_op->op.type,
	   q_op->op.state,
	   q_op->op.handle);
 
    q_op = q_op->next_p;
    while (q_op != start_op) {
	printf("op: id=%Lx, type=%d, state=%d, handle=%Lx\n",
	       q_op->op.id,
	       q_op->op.type,
	       q_op->op.state,
	       q_op->op.handle);
	q_op = q_op->next_p;
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 */

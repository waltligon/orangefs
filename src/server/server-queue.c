/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <signal.h>

#include <bmi.h>
#include <gossip.h>
#include <job.h>
#include <pvfs-debug.h>
#include <pvfs-storage.h>

#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <server-queue.h>


static PINT_queue_item *q_head;

static int q_size;

/* Functions */

/* Function: server_queue_halt(void)
	Params: None
	Returns: True
	Synopsis: This will dealloc all structures in the queue and return
 */

int PINT_server_queue_halt(void) 
{
	int i=0;
	PINT_queue_item *q;
	while(i++<=q_size) 
	{
		q = q_head->next_east_west;
		free(q_head);
		q_head = q;
	}
	return(0);
}

/* Function: server_queue_init(void)
	Params: None
	Returns: True
	Synopsis: This function is used to initialize queues for operation.
				 No Memory Allocation is necessary at this stage... The 
				 head of the queue will always be null;
 */

int PINT_server_queue_init(void)
{

	q_head = (PINT_queue_item *) malloc(sizeof(PINT_queue_item));
	q_head->next_east_west = 0;
	q_size = 0;

	return(0);
	
}

int PINT_server_enqueue(struct PVFS_server_op *op)
{
	PINT_queue_item *q = q_head->next_east_west;
        /* if (*q != 0) */
	if(0)
	{
		/* Look for op with same handle */
		/* If found, return negative */
		/* This tells user need to post to wait q */
	}
	q->next_east_west = (PINT_queue_item *) malloc(sizeof(PINT_queue_item));
	q->next_east_west->op = op;
	q->next_east_west->next_east_west = 0;
	q->next_east_west->previous_east_west = q;
	q_size++;
	return(0);

}

void PINT_server_check_dependancies(PINT_server_op *op, job_status_s *r)
{

	op->req  = (struct PVFS_server_req_s *) op->unexp_bmi_buff->buffer;
	op->addr = op->unexp_bmi_buff->addr;
	op->tag  = op->unexp_bmi_buff->tag;
	op->op = op->req->op;

	//op->location.index = PINT_state_array[op->op] + 1;

	//(PINT_state_array[op->op]->handler)(op,r);
		
}

int PINT_server_dequeue(struct PVFS_server_op *op)
{
	PINT_queue_item *q = q_head->next_east_west;

	if (q_size == 0) return(-1); /* This is bad */

	while(q->next_east_west != 0)
	{
	/* Search for item.  Return 0 if found, -1 if not */
		q = q->next_east_west;
	}
	q_size--;
	return(-1);
}


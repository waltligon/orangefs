/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this file contains an implementation of the server side request
 * scheduler.  
 */

/* NOTE: this is a prototype.  It simply hashes on the handle
 * value in the request and builds a linked list for each handle.
 * Only the request at the head of each list is allowed to
 * proceed.
 */

/* LONG TERM
 * TODO: come up with a more sophisticated scheduling and
 *  consistency model
 * TODO: implement without using so many mallocs
 * TODO: we need to audit the request protocol and find other
 *  inconsistencies that need to be avoided
 *
 * SHORT TERM
 * TODO: document that this isn't thread safe
 */

#include <errno.h>
#include <stdlib.h>

#include <request-scheduler.h>
#include <quickhash.h>
#include <pvfs2-types.h>
#include <pvfs2-req-proto.h>
#include <pvfs2-debug.h>
#include <gossip.h>

/* element states */
enum req_sched_states
{
	/* request is queued up, cannot be processed yet */
	REQ_QUEUED,           
	/* request is being processed */
	REQ_SCHEDULED,
	/* request could be processed, but caller has not asked for it
	 * yet 
	 */
	REQ_READY_TO_SCHEDULE
};

/* linked lists to be stored at each hash table element */
struct req_sched_list
{
	struct qlist_head hash_link;
	struct qlist_head req_list;
	PVFS_handle handle;
};

/* linked list elements; one for each request in the scheduler */
struct req_sched_element
{
	struct qlist_head list_link;       /* ties it to a queue */
	struct PVFS_server_req_s* req_ptr; /* ties it to a request */
	struct qlist_head ready_link;      /* ties to ready queue */
	void* user_ptr;                    /* user pointer */
	req_sched_id id;                   /* unique identifier */
	struct req_sched_list* list_head;  /* points to head of queue */
	enum req_sched_states state;       /* state of this element */
	PVFS_handle handle;
};

/* hash table */
static struct qhash_table* req_sched_table;

/* queue of requests that are ready for service (in case
 * test_world is called 
 */
static QLIST_HEAD(ready_queue);

static int hash_handle(void* handle, int table_size);
static int hash_handle_compare(void* key, struct qlist_head*
	link);
static int handle_from_request(struct PVFS_server_req_s*
	req, PVFS_handle* handle);

/* setup and teardown */

/* PINT_req_sched_initialize()
 *
 * intitializes the request scheduler
 *
 * returns 0 on success, -errno on failure
 */
int PINT_req_sched_initialize(
	void)
{
	/* build hash table */
	req_sched_table = qhash_init(hash_handle_compare, hash_handle, 
		1021);
	if(!req_sched_table)
	{
		return(-ENOMEM);
	}

	return(0);
}

/* PINT_req_sched_finalize()
 *
 * tears down the request scheduler and its data structures 
 *
 * returns 0 on success, -errno on failure
 */
int PINT_req_sched_finalize(
	void)
{
	int i;
	struct req_sched_list* tmp_list;
	struct req_sched_element* tmp_element;

	/* iterate through the hash table */
	for(i=0; i<req_sched_table->table_size; i++)
	{
		/* remove any queues from the table */
		while(!qlist_empty(&(req_sched_table->array[i])))
		{
			tmp_list = qlist_entry((req_sched_table->array[i].next),
				struct req_sched_list, hash_link);
			qlist_del(&(tmp_list->hash_link));
			/* remove any elements from each queue */
			while(!qlist_empty(&(tmp_list->req_list)))
			{
				tmp_element = qlist_entry((tmp_list->req_list.next),
					struct req_sched_element, list_link);
				qlist_del(&(tmp_element->list_link));
				free(tmp_element);
			}
			free(tmp_list);
		}
	}

	/* tear down hash table */
	qhash_finalize(req_sched_table);
	return(0);
}

/* scheduler submission */

/* PINT_req_sched_post()
 *
 * posts an incoming request to the scheduler
 *
 * returns 1 if request should proceed immediately, 0 if the
 * caller should check back later, and -errno on failure
 */
int PINT_req_sched_post(
	struct PVFS_server_req_s* in_request, 
	void* in_user_ptr, 
	req_sched_id* out_id)
{
	struct qlist_head* hash_link;
	PVFS_handle handle;
	int ret = -1;
	struct req_sched_element* tmp_element;
	struct req_sched_list* tmp_list;
	struct req_sched_element* next_element;

	/* find the handle */
	ret = handle_from_request(in_request, &handle);
	if(ret < 0)
	{
		return(ret);
	}

	/* NOTE: handle == 0 is a special case, the request isn't
	 * operating on a particular handle, but we will queue anyway
	 * on handle == 0 for the moment...
	 */
	
	/* create a structure to store in the request queues */
	tmp_element = (struct req_sched_element*)malloc(sizeof(struct
		req_sched_element));
	if(!tmp_element)
	{
		return(-errno);
	}

	tmp_element->req_ptr = in_request;
	tmp_element->user_ptr = in_user_ptr;
	id_gen_fast_register(out_id, tmp_element);
	tmp_element->id = *out_id;
	tmp_element->state = REQ_QUEUED;
	tmp_element->handle = handle;

	/* see if we have any requests queue up for this handle */
	hash_link = qhash_search(req_sched_table, &(handle));
	if(hash_link)
	{
		/* we already have a queue for this handle */
		tmp_list = qlist_entry(hash_link, struct req_sched_list,
			hash_link);
	}
	else
	{
		/* no queue yet for this handle */
		/* create one and add it in */
		tmp_list = (struct req_sched_list*)malloc(sizeof(struct
			req_sched_list));
		if(!tmp_list)
		{
			free(tmp_element);
			return(-ENOMEM);
		}
		
		tmp_list->handle = handle;
		INIT_QLIST_HEAD(&(tmp_list->req_list));

		qhash_add(req_sched_table, &(handle),
			&(tmp_list->hash_link));

	}

	/* at either rate, we now have a pointer to the list head */

	/* return 1 if the list is empty before we add this entry */
	ret = qlist_empty(&(tmp_list->req_list));
	if(ret == 1)
		tmp_element->state = REQ_SCHEDULED;
	else
	{
		/* if this is an I/O operation, AND the head of the list of
		 * pending operations for this handle is also an I/O
		 * operation, then we can go ahead and schedule (we allow
		 * concurrent I/O for a handle)
		 */
		next_element = qlist_entry((tmp_list->req_list.next),
			struct req_sched_element, list_link);
		if(in_request->op == PVFS_SERV_IO &&
			next_element->req_ptr->op == PVFS_SERV_IO)
		{
			tmp_element->state = REQ_SCHEDULED;
			ret = 1;
			gossip_debug(REQ_SCHED_DEBUG, 
				"REQ SCHED allowing concurrent I/O, handle: %ld\n", 
				(long)handle);
		}
		else
		{
			tmp_element->state = REQ_QUEUED;
			ret = 0;
		}
	}

	/* add this element to the list */
	tmp_element->list_head = tmp_list;
	qlist_add_tail(&(tmp_element->list_link), &(tmp_list->req_list));

	gossip_debug(REQ_SCHED_DEBUG, 
		"REQ SCHED POSTING, handle: %ld, queue_element: %p\n", 
		(long)handle, tmp_element);

	if(ret == 1)
	{
		gossip_debug(REQ_SCHED_DEBUG, 
			"REQ SCHED SCHEDULING, handle: %ld, queue_element: %p\n", 
			(long)handle, tmp_element);
	}
	return(ret);
}

/* PINT_req_sched_unpost()
 *
 * removes a request from the scheduler before it has even been
 * scheduled
 *
 * returns 0 on success, -errno on failure 
 */
int PINT_req_sched_unpost(
	req_sched_id in_id,
	void** returned_user_ptr)
{
	struct req_sched_element* tmp_element = NULL;
	struct req_sched_element* next_element = NULL;
	int next_ready_flag = 0;

	/* NOTE: we set the next_ready_flag to 1 if the next element in
	 * the queue should be put in the ready list 
	 */

	/* retrieve the element directly from the id */
	tmp_element = id_gen_fast_lookup(in_id);

	/* make sure it isn't already scheduled */
	if(tmp_element->state == REQ_SCHEDULED)
	{
		return(-EALREADY);
	}

	if(tmp_element->state == REQ_READY_TO_SCHEDULE)
	{
		qlist_del(&(tmp_element->ready_link));
		next_ready_flag = 1;
		/* fall through on purpose */
	}

	if(returned_user_ptr)
	{
		returned_user_ptr[0] = tmp_element->user_ptr;
	}

	qlist_del(&(tmp_element->list_link));

	/* see if there is another request queued behind this one */
	if(qlist_empty(&(tmp_element->list_head->req_list)))
	{
		/* queue now empty, remove from hash table and destroy */
		qlist_del(&(tmp_element->list_head->hash_link));
		free(tmp_element->list_head);
	}
	else
	{
		/* queue not empty, prepare next request in line for
		 * processing if necessary
		 */
		if(next_ready_flag)
		{
			next_element =
				qlist_entry((tmp_element->list_head->req_list.next),
				struct req_sched_element, list_link);
			/* skip looking at the next request if it is already
			 * ready to go 
			 */
			if(next_element->state != REQ_READY_TO_SCHEDULE)
			{
				next_element->state = REQ_READY_TO_SCHEDULE;
				qlist_add_tail(&(next_element->ready_link), &ready_queue);
				/* keep going as long as the operations are I/O requests;
				 * we let these all go concurrently
				 */
				while(next_element && next_element->req_ptr->op == PVFS_SERV_IO
					&& next_element->list_link.next != &(tmp_element->list_head->req_list))
				{
					next_element =
						qlist_entry(next_element->list_link.next,
						struct req_sched_element, list_link);
					if(next_element && next_element->req_ptr->op == PVFS_SERV_IO)
					{
						gossip_debug(REQ_SCHED_DEBUG, 
							"REQ SCHED allowing concurrent I/O, handle: %ld\n", 
							(long)next_element->handle);
						next_element->state = REQ_READY_TO_SCHEDULE;
						qlist_add_tail(&(next_element->ready_link), &ready_queue);
					}
				}
			}
		}
	}

	/* destroy the unposted element */
	free(tmp_element);

	return(0);
}

/* PINT_req_sched_release()
 *
 * releases a completed request from the scheduler, potentially
 * allowing other requests to proceed 
 *
 * returns 1 on immediate successful completion, 0 to test later,
 * -errno on failure
 */
int PINT_req_sched_release(
	req_sched_id in_completed_id, 
	void* in_user_ptr, 
	req_sched_id* out_id)
{
	struct req_sched_element* tmp_element = NULL;
	struct req_sched_list* tmp_list = NULL;
	struct req_sched_element* next_element = NULL;

	/* NOTE: for now, this function always returns immediately- no
	 * need to fill in the out_id
	 */
	*out_id = 0;

	/* retrieve the element directly from the id */
	tmp_element = id_gen_fast_lookup(in_completed_id);

	/* remove it from its handle queue */
	qlist_del(&(tmp_element->list_link));

	/* find the top of the queue */
	tmp_list = tmp_element->list_head;

	/* find out if there is another operation queued behind it or
	 * not 
	 */
	if(qlist_empty(&(tmp_list->req_list)))
	{
		/* nothing else in this queue, remove it from the hash table
		 * and deallocate 
		 */
		qlist_del(&(tmp_list->hash_link));
		free(tmp_list);
	}
	else
	{
		/* something is queued behind this request */
		/* find the next request, change its state, and add it to
		 * the queue of requests that are ready to be scheduled
		 */
		next_element = qlist_entry((tmp_list->req_list.next),
			struct req_sched_element, list_link);
		/* skip it if the top queue item is already ready for
		 * scheduling 
		 */
		if(next_element->state != REQ_READY_TO_SCHEDULE)
		{
			next_element->state = REQ_READY_TO_SCHEDULE;
			qlist_add_tail(&(next_element->ready_link), &ready_queue);
			/* keep going as long as the operations are I/O requests;
			 * we let these all go concurrently
			 */
			while(next_element && next_element->req_ptr->op == PVFS_SERV_IO
				&& next_element->list_link.next != &(tmp_list->req_list))
			{
				next_element =
					qlist_entry(next_element->list_link.next,
					struct req_sched_element, list_link);
				if(next_element && next_element->req_ptr->op == PVFS_SERV_IO)
				{
					gossip_debug(REQ_SCHED_DEBUG, 
						"REQ SCHED allowing concurrent I/O, handle: %ld\n", 
						(long)next_element->handle);
					next_element->state = REQ_READY_TO_SCHEDULE;
					qlist_add_tail(&(next_element->ready_link), &ready_queue);
				}
			}
		}
	}

	gossip_debug(REQ_SCHED_DEBUG, 
		"REQ SCHED RELEASING, handle: %ld, queue_element: %p\n", 
		(long)tmp_element->handle, tmp_element);

	/* destroy the released request element */
	free(tmp_element);

	return(1);
}

/* testing for completion */

/* PINT_req_sched_test()
 *
 * tests for completion of a single scheduler operation
 *
 * returns 0 on success, -errno on failure
 */
int PINT_req_sched_test(
	req_sched_id in_id,
	int* out_count_p,
	void** returned_user_ptr_p,
	req_sched_error_code* out_status)
{
	struct req_sched_element* tmp_element = NULL;

	*out_count_p = 0;
	
	/* retrieve the element directly from the id */
	tmp_element = id_gen_fast_lookup(in_id);

	/* sanity check the state */
	if(tmp_element->state == REQ_SCHEDULED)
	{
		/* it's already scheduled! */
		return(-EINVAL);
	}
	else if(tmp_element->state == REQ_QUEUED)
	{
		/* it still isn't ready to schedule */
		return(0);
	}
	else if(tmp_element->state == REQ_READY_TO_SCHEDULE)
	{
		/* let it roll */
		tmp_element->state = REQ_SCHEDULED;
		/* remove from ready queue */
		qlist_del(&(tmp_element->ready_link));
		if(returned_user_ptr_p)
		{
			returned_user_ptr_p[0] = tmp_element->user_ptr;
		}
		*out_count_p = 1;
		*out_status = 0;
		gossip_debug(REQ_SCHED_DEBUG, 
			"REQ SCHED SCHEDULING, handle: %ld, queue_element: %p\n", 
			(long)tmp_element->handle, tmp_element);
		return(0);
	}
	else
	{
		return(-EINVAL);
	}

	/* should not hit this point */
	return(-ENOSYS);
}

int PINT_req_sched_testsome(
	req_sched_id* in_id_array,
	int* inout_count_p,
	int* out_index_array,
	void** returned_user_ptr_array,
	req_sched_error_code* out_status_array)
{
	struct req_sched_element* tmp_element = NULL;
	int i;
	int incount = *inout_count_p;

	*inout_count_p = 0;
	
	for(i=0; i<incount; i++)
	{
		/* retrieve the element directly from the id */
		tmp_element = id_gen_fast_lookup(in_id_array[i]);

		/* sanity check the state */
		if(tmp_element->state == REQ_SCHEDULED)
		{
			/* it's already scheduled! */
			return(-EINVAL);
		}
		else if(tmp_element->state == REQ_QUEUED)
		{
			/* it still isn't ready to schedule */
			/* do nothing */
		}
		else if(tmp_element->state == REQ_READY_TO_SCHEDULE)
		{
			/* let it roll */
			tmp_element->state = REQ_SCHEDULED;
			/* remove from ready queue, leave in hash table queue */
			qlist_del(&(tmp_element->ready_link));
			if(returned_user_ptr_array)
			{
				returned_user_ptr_array[*inout_count_p] = 
					tmp_element->user_ptr;
			}
			out_index_array[*inout_count_p] = i;
			out_status_array[*inout_count_p] = 0;
			(*inout_count_p)++;
			gossip_debug(REQ_SCHED_DEBUG, 
				"REQ SCHED SCHEDULING, handle: %ld, queue_element: %p\n", 
				(long)tmp_element->handle, tmp_element);
		}
		else
		{
			return(-EINVAL);
		}
	}

	return(0);
}

int PINT_req_sched_testworld(
	int* inout_count_p,
	req_sched_id* out_id_array,
	void** returned_user_ptr_array,
	req_sched_error_code* out_status_array)
{
	int incount = *inout_count_p;
	struct req_sched_element* tmp_element; 

	*inout_count_p = 0;

	while(!qlist_empty(&ready_queue) && (*inout_count_p < incount))
	{
		tmp_element = qlist_entry((ready_queue.next), struct
			req_sched_element, ready_link);
		/* remove from ready queue */
		qlist_del(&(tmp_element->ready_link));
		out_id_array[*inout_count_p] = tmp_element->id;
		if(returned_user_ptr_array)
		{
			returned_user_ptr_array[*inout_count_p] =
				tmp_element->user_ptr;
		}
		out_status_array[*inout_count_p] = 0;
		tmp_element->state = REQ_SCHEDULED;
		(*inout_count_p)++;
		gossip_debug(REQ_SCHED_DEBUG, 
			"REQ SCHED SCHEDULING, handle: %ld, queue_element: %p\n", 
			(long)tmp_element->handle, tmp_element);
	}
	return(0);
}

/* hash_handle()
 *
 * hash function for handles added to table
 *
 * returns integer offset into table
 */
static int hash_handle(void* handle, int table_size)
{
	/* TODO: update this later with a better hash function,
	 * depending on what handles look like 
	 */
	int tmp = 0;
	PVFS_handle* real_handle = handle;
	
	tmp += (*(real_handle));

	return (tmp%table_size);
}

/* hash_handle_compare()
 *
 * performs a comparison of a hash table entro to a given key
 * (used for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_handle_compare(void* key, struct qlist_head* link)
{
	struct req_sched_list* my_list;
	PVFS_handle* real_handle = key;

	my_list = qlist_entry(link, struct req_sched_list, hash_link);
	if(my_list->handle == *real_handle)
	{
		return(1);
	}

	return(0);
}

/* handle_from_request()
 *
 * pulls the handle out of a request structure and fills in
 * argument
 *
 * returns 0 on success, -errno on failure
 * NOTE: a handle value of 0 and a return value of 0 indicates
 * that the request does not operate on any particular handle
 */
static int handle_from_request(struct PVFS_server_req_s*
	req, PVFS_handle* handle)
{
	*handle = 0;

	switch(req->op)
	{
		case PVFS_SERV_INVALID: 
			return(-EINVAL);
			break;
		case PVFS_SERV_NOOP: 
			return(0);
			break;
		case PVFS_SERV_CREATE:
			return(0);
			break;
		case PVFS_SERV_REMOVE:
			*handle = req->u.remove.handle;
			return(0);
			break;
		case PVFS_SERV_IO:
			*handle = req->u.io.handle;
			return(0);
			break;
		case PVFS_SERV_BATCH:
			return(-EINVAL);
			break;
		case PVFS_SERV_GETATTR:
			*handle = req->u.getattr.handle;
			return(0);
			break;
		case PVFS_SERV_SETATTR:
			*handle = req->u.setattr.handle;
			return(0);
			break;
		case PVFS_SERV_GETEATTR:
			*handle = req->u.geteattr.handle;
			return(0);
			break;
		case PVFS_SERV_SETEATTR:
			return(-EINVAL);
			break;
		case PVFS_SERV_LOOKUP_PATH:
			*handle = req->u.lookup_path.starting_handle;
			return(0);
			break;
		case PVFS_SERV_GETDIST:
			return(-EINVAL);
			break;
		case PVFS_SERV_CREATEDIRENT:
			*handle = req->u.crdirent.parent_handle;
			return(0);
			break;
		case PVFS_SERV_RMDIRENT:
			*handle = req->u.rmdirent.parent_handle;
			return(0);
			break;
		case PVFS_SERV_REVLOOKUP:
			return(-EINVAL);
			break;
		case PVFS_SERV_ALLOCATE:
			return(-EINVAL);
			break;
		case PVFS_SERV_TRUNCATE:
			*handle = req->u.truncate.handle;
			return(0);
			break;
		case PVFS_SERV_MKDIR:
			return(0);
			break;
		case PVFS_SERV_RMDIR:
			*handle = req->u.rmdir.handle;
			return(0);
			break;
		case PVFS_SERV_READDIR:
			*handle = req->u.readdir.handle;
			return(0);
			break;
		case PVFS_SERV_STATFS:
			return(0);
			break;
		case PVFS_SERV_IOSTATFS:
			return(-EINVAL);
			break;
		case PVFS_SERV_GETCONFIG:
			return(0);
			break;
		case PVFS_SERV_EXTENSION:
			return(-EINVAL);
			break;
		default:
			return(-EINVAL);
			break;
	}

	return(0);
}


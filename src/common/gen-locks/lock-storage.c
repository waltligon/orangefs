/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Written by Avery Ching - Northwestern University
 * See COPYING in top-level directory.
 * 
 */

#include "lock-storage.h"
#include "../server/pvfs2-server.h"
#include "../io/job/job-desc-queue.h"

/* Processing scratch space */
#define MAX_OL_PAIRS 64

PINT_Request_result *PINT_Request_result_init(void)
{
    PINT_Request_result * tmp_result_p;

    tmp_result_p = malloc(sizeof(PINT_Request_result));
    if (!tmp_result_p)
    {
	gossip_err("malloc PINT_Request_result failed\n");
	return NULL;
    }
    
    tmp_result_p->offset_array = malloc(MAX_OL_PAIRS*sizeof(PVFS_offset));
    if (!tmp_result_p->offset_array)
    {
	free(tmp_result_p);
	gossip_err("malloc offset_array failed\n");
	return NULL;
    }

    tmp_result_p->size_array = malloc(MAX_OL_PAIRS*sizeof(PVFS_size));
    if (!tmp_result_p->size_array)
    {
	free(tmp_result_p);
	free(tmp_result_p->offset_array);
	gossip_err("malloc size_array failed\n");
	return NULL;
    }
    
    tmp_result_p->segmax = MAX_OL_PAIRS;
    tmp_result_p->segs = 0;
    tmp_result_p->bytemax = __LONG_LONG_MAX__;
    tmp_result_p->bytes = 0;

    return tmp_result_p;
}

/* qlist_swap - Swap out one's nodes pointers for another's.  There
 * are 3 distinct cases. 1) node dest is before node src. 2) node dest
 * is after node src. 3) node src and node dest are not neighbors. 4)
 * also they are neighbors and there is one node, or no other nodes.
 * This solution is magic =).  */
static void qlist_swap(struct qlist_head *a_p,
		       struct qlist_head *b_p)
{
    struct qlist_head *a_prev_p, *a_next_p, *b_prev_p, *b_next_p;

    assert(a_p != b_p);

    a_prev_p = a_p->prev;
    a_next_p = a_p->next;
    b_prev_p = b_p->prev;
    b_next_p = b_p->next;

    a_prev_p->next = b_p;
    a_next_p->prev = b_p;
    b_prev_p->next = a_p;
    b_next_p->prev = a_p;

    a_prev_p = a_p->prev;
    a_next_p = a_p->next;
    b_prev_p = b_p->prev;
    b_next_p = b_p->next;

    a_p->next = b_next_p;
    a_p->prev = b_prev_p;
    b_p->next = a_next_p;
    b_p->prev = a_prev_p;
}

/* Sentinel creation for both itree.h and rbtree.h */

ITREE_NIL(ITREE_NIL);
RBTREE_NIL(RBTREE_NIL);

int linked_itree_cpy_fn(itree_t *dest_p, itree_t *src_p)
{
    linked_itree_t *tmp_dest_p =
	itree_entry(dest_p, linked_itree_t, itree_link);
    linked_itree_t *tmp_src_p =
        itree_entry(src_p, linked_itree_t, itree_link);
    int tmp_lock = tmp_dest_p->lock_id;

    tmp_dest_p->lock_id = tmp_src_p->lock_id;
    tmp_src_p->lock_id = tmp_lock;
    qlist_swap(&(tmp_dest_p->list_link), &(tmp_src_p->list_link));
    return 0;
}

void linked_itree_print_fn(itree_t *head_p)
{
    linked_itree_t *linked_itree_p =
	itree_entry(head_p, linked_itree_t, itree_link);
    
    fprintf(stdout, "{%Ld,%Ld,%Ld,%s,%d,",
	    head_p->start, head_p->end, head_p->max,
	    (head_p->color == ITREE_RED ? "r": "b"),
	    linked_itree_p->lock_id);

    if (head_p->parent == &ITREE_NIL)
	fprintf(stdout, "p=NIL,");
    else
    {
	linked_itree_p =
	    itree_entry(head_p->parent, linked_itree_t, itree_link);
	fprintf(stdout, "p=%d,", linked_itree_p->lock_id);
    }
    if (head_p->left == &ITREE_NIL)
	fprintf(stdout, "l=NIL,");
    else
    {
	linked_itree_p =
	    itree_entry(head_p->left, linked_itree_t, itree_link);
	fprintf(stdout, "l=%d,", linked_itree_p->lock_id);
    }
    if (head_p->right == &ITREE_NIL)
	fprintf(stdout, "r=NIL} ");
    else
    {
	linked_itree_p =
	    itree_entry(head_p->right, linked_itree_t, itree_link);
	fprintf(stdout, "r=%d} ", linked_itree_p->lock_id);
    }
}

int rbtree_lock_req_cpy_fn(rbtree_t *dest_p, rbtree_t *src_p)
{
    PINT_Request_state *tmp_file_req_state_p = NULL;
    PINT_Request *tmp_file_req_p = NULL;
    PINT_Request_result *tmp_result_p = NULL;

    lock_req_t *tmp_dest_p =
	rbtree_entry(dest_p, lock_req_t, granted_req_link);
    lock_req_t *tmp_src_p =
	rbtree_entry(src_p, lock_req_t, granted_req_link);

    assert(dest_p != &RBTREE_NIL);
    assert(src_p != &RBTREE_NIL);

    tmp_file_req_state_p = tmp_dest_p->file_req_state;    
    tmp_file_req_p = tmp_dest_p->file_req;
    tmp_result_p = tmp_dest_p->lock_result_p;

    qlist_swap(&(tmp_dest_p->lock_head), &(tmp_src_p->lock_head));
    tmp_dest_p->lock_req_status = tmp_src_p->lock_req_status;
    qlist_swap(&(tmp_dest_p->queued_req_link), 
	       &(tmp_src_p->queued_req_link));
    qlist_swap(&(tmp_dest_p->all_req_link), 
	       &(tmp_src_p->all_req_link));
    tmp_dest_p->req_id = tmp_src_p->req_id;
    tmp_dest_p->io_type = tmp_src_p->io_type;
    tmp_dest_p->lock_result_p = tmp_src_p->lock_result_p;
    tmp_dest_p->actual_locked_bytes = tmp_src_p->actual_locked_bytes;
    tmp_dest_p->file_req_state = tmp_src_p->file_req_state;
    tmp_dest_p->seg_num = tmp_src_p->seg_num;
    tmp_dest_p->seg_bytes_used = tmp_src_p->seg_bytes_used;
    qlist_swap(&(tmp_dest_p->removed_link),
	       &(tmp_src_p->removed_link));
    tmp_dest_p->wait_abs_offset = tmp_src_p->wait_abs_offset;
    tmp_dest_p->aggregate_size = tmp_src_p->aggregate_size;
    tmp_dest_p->file_req = tmp_src_p->file_req;
    tmp_dest_p->file_req_offset = tmp_src_p->file_req_offset;
    /* lock_callback is not necessary to swap since anything in the
     * granted list shouldn't have any callback information set up
     * with it! */

    /* Need to swap the file_req_state and file_req so that it can be
     * freed. */
    tmp_src_p->file_req_state = tmp_file_req_state_p;
    tmp_src_p->file_req = tmp_file_req_p;
    tmp_src_p->lock_result_p = tmp_result_p;

    return 0;
}

void free_lock_req(lock_req_t *lock_req_p)
{
    free(lock_req_p->lock_result_p->offset_array);
    free(lock_req_p->lock_result_p->size_array);
    free(lock_req_p->lock_result_p);
    PINT_free_request_state(lock_req_p->file_req_state);
    PVFS_Request_free(&lock_req_p->file_req);
    free(lock_req_p);
}

/* Basic functions and initialization for complex lock struct use */

#define LOCK_FILE_TABLE_SIZE 3

static PVFS_id_gen_t lock_req_id = 0;

static gen_mutex_t *lock_file_table_mutex = NULL;

static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);

static struct qhash_table *lock_file_table;

#define LOCK_FILE_TABLE_INITIALIZED() \
(lock_file_table_mutex && lock_file_table)

static int hash_key(void *key, int table_size)
{
    PVFS_object_ref *ref_p = (PVFS_object_ref *) key;
    
    return ((ref_p->handle + ref_p->fs_id) % table_size);
}

static int hash_key_compare(void *key, struct qlist_head *link)
{
    lock_node_t *lock_node_p = NULL;
    PVFS_object_ref *ref_p = (PVFS_object_ref *) key;
    
    lock_node_p = qlist_entry(link, lock_node_t, hash_link);
    assert(lock_node_p);

    return (((lock_node_p->refn.fs_id == ref_p->fs_id) &&
	     (lock_node_p->refn.handle == ref_p->handle)) ? 1 : 0);
}

int init_lock_file_table(void)
{
    if (!LOCK_FILE_TABLE_INITIALIZED())
    {
	lock_file_table = qhash_init(hash_key_compare, hash_key,
				     LOCK_FILE_TABLE_SIZE);
	if (!lock_file_table)
	    return -PVFS_ENOMEM;

	lock_file_table_mutex = gen_mutex_build();
	if (!lock_file_table_mutex)
	{
	    qhash_finalize(lock_file_table);
	    lock_file_table = NULL;
	    return -PVFS_ENOMEM;
	}
    }
    else
	gossip_err("init_lock_file_table: already exists!\n");

    return (lock_file_table ? 0 : -PVFS_ENOMEM);
}

void free_lock_file_table(void)
{
    if (LOCK_FILE_TABLE_INITIALIZED())
    {	    
	gen_mutex_lock(lock_file_table_mutex);
	qhash_finalize(lock_file_table);
	lock_file_table = NULL;
	gen_mutex_unlock(lock_file_table_mutex);
	gen_mutex_destroy(lock_file_table_mutex);
	lock_file_table_mutex = NULL;
    }
    else
	gossip_err("free_lock_file_table_all: Already NULL!\n");
}

void free_lock_file_table_all(void)
{
    int i = 0;
    struct qlist_head *tmp_link_p = NULL;
    lock_node_t *tmp_lock_node_p = NULL;
    
    if (LOCK_FILE_TABLE_INITIALIZED())
    {	    
	gen_mutex_lock(lock_file_table_mutex);
	for (i = 0; i < lock_file_table->table_size; i++)
	{
	    do
	    {
		tmp_link_p = qhash_search_and_remove_at_index(
		    lock_file_table, i);
		if (tmp_link_p)
		{
		    tmp_lock_node_p = qlist_entry(
			tmp_link_p, lock_node_t, hash_link);
		    free(tmp_lock_node_p);
		}
	    } while (tmp_link_p);
	}
	qhash_finalize(lock_file_table);
	lock_file_table = NULL;
	gen_mutex_unlock(lock_file_table_mutex);
	gen_mutex_destroy(lock_file_table_mutex);
	lock_file_table_mutex = NULL;
    }
    else
	fprintf(stdout, "free_lock_file_table_all: Already NULL!\n");
}

void print_lock_file_table_all(void)
{
    int i = 0;
    struct qlist_head *tmp_hash_head_p = NULL;
    lock_node_t *lock_node_p = NULL;

    if (LOCK_FILE_TABLE_INITIALIZED())
    {
	gen_mutex_lock(lock_file_table_mutex);
	for (i = 0; i < lock_file_table->table_size; i++)
	{
	    fprintf(stdout, "index %d: ", i);
	    qhash_for_each(tmp_hash_head_p, &(lock_file_table->array[i]))
	    {
		lock_node_p = qhash_entry(
		    tmp_hash_head_p, lock_node_t, hash_link);
		fprintf(stdout, "{fs=%d,id=%Ld} ", lock_node_p->refn.fs_id,
			lock_node_p->refn.handle);
	    }
	    
	    fprintf(stdout, "\n");
	}
	gen_mutex_unlock(lock_file_table_mutex);
    }
    else
	fprintf(stdout, "print_lock_file_table_all: NULL!\n");
}

void print_lock_file_table_all_info(void)
{
    int i = 0;
    struct qlist_head *tmp_hash_head_p = NULL;
    lock_node_t *lock_node_p = NULL;

    if (LOCK_FILE_TABLE_INITIALIZED())
    {
#if 0
	gen_mutex_lock(lock_file_table_mutex);
#endif
	fprintf(stdout, "---------------------------------------------\n");
	fprintf(stdout, "lock legend {start,end,max,color(red or black)}\n\n");
	for (i = 0; i < lock_file_table->table_size; i++)
	{
	    linked_itree_t *tmp_linked_itree_p = NULL;
	    struct qlist_head *tmp_req_p = NULL, *tmp_req_lock_p = NULL;
	    lock_req_t *tmp_lock_req_p = NULL;
	    fprintf(stdout, "index %d:\n", i);
	    qhash_for_each(tmp_hash_head_p, &(lock_file_table->array[i]))
	    {
		lock_node_p = qhash_entry(
		    tmp_hash_head_p, lock_node_t, hash_link);
		fprintf(stdout, "\n {fs=%d,id=%Ld}\n", 
			lock_node_p->refn.fs_id, lock_node_p->refn.handle);
		fprintf(stdout, "  all_req: ");
		qlist_for_each(tmp_req_p, &(lock_node_p->all_req))
		{
		    tmp_lock_req_p = qlist_entry(
			tmp_req_p, lock_req_t, all_req_link);
		    fprintf(stdout, "{req_id=%Ld ", tmp_lock_req_p->req_id);
		    qlist_for_each(tmp_req_lock_p, 
				   &(tmp_lock_req_p->lock_head))
		    {
			tmp_linked_itree_p = qlist_entry(
			    tmp_req_lock_p, linked_itree_t, list_link);

			fprintf(stdout, "(%Ld,%Ld)",
				tmp_linked_itree_p->itree_link.start,
				tmp_linked_itree_p->itree_link.end);
		    }
		    fprintf(stdout, "}");
		}	    
		fprintf(stdout, "\n  granted_req: ");
		rbtree_inorder_tree_print(lock_node_p->granted_req,
					  &RBTREE_NIL);
		fprintf(stdout, "\n  queued_req: ");
		qlist_for_each(tmp_req_p, &(lock_node_p->queued_req))
		{
		    tmp_lock_req_p = qlist_entry(
			tmp_req_p, lock_req_t, queued_req_link);
		    fprintf(stdout, "{req_id=%Ld} ", tmp_lock_req_p->req_id);
		}	    
		fprintf(stdout, "\n  write_itree: ");
		itree_inorder_tree_print_fn(lock_node_p->write_itree, 
					    &ITREE_NIL,
					    linked_itree_print_fn);
		fprintf(stdout, "\n  read_itree: ");
		itree_inorder_tree_print_fn(lock_node_p->read_itree,
					    &ITREE_NIL,
					    linked_itree_print_fn);
		/* for now */
#if 0
		fprintf(stdout, "\n  read_itree: ");
		itree_breadth_print_fn(lock_node_p->read_itree,
				       &ITREE_NIL, 
				       linked_itree_print_fn);
		itree_inorder_tree_check(lock_node_p->read_itree,
					 &ITREE_NIL);
		itree_nil_check(&ITREE_NIL);
#endif
		fprintf(stdout, "\n");
	    }
	    fprintf(stdout, "\n");
	}
#if 0
	gen_mutex_unlock(lock_file_table_mutex);
#endif
    }
    else
	gossip_err("print_lock_file_table_all: NULL!\n");
}

/* add_locks - Add as many of the locks that are requested until the
 * final_abs_offset is reached.  i.e.  if final_abs_offset is 5, then
 * lock bytes 0, 1, 2, 3, and 4, but NOT 5.  If final_abs_offset is
 * __LONG_LONG_MAX__, then try to do the full request.  It is assumed
 * that the request already has the file_req_state all setup
 * (i.e. PINT_REQUEST_STATE_SET_TARGET and
 * PINT_REQUEST_STATE_SET_FINAL) and that the file_req_state will be
 * reset later on. Returns 0 if all locks for the request were
 * added. Returns 1 if none or some locks were acquired.  Returns -1
 * for an error. */
static inline int add_locks(lock_req_t *lock_req_p,
			    lock_node_t *lock_node_p,
			    PVFS_offset final_abs_offset,
			    PVFS_offset *last_abs_offset_locked_p,
			    PVFS_offset *next_abs_offset_p,
			    PVFS_size *bytes_locked_p)
{
    enum PVFS_lock_origin lock_origin;
    int ret = 0;
    PVFS_offset tmp_start_offset = - 1,
	tmp_end_offset = -1, tmp_abs_offset = -1, *offset_arr;
    PVFS_size *size_arr;
    PINT_request_file_data *fdata_p = &lock_node_p->fdata;
    itree_t *itree_p = &ITREE_NIL;
    linked_itree_t *linked_itree_p = NULL;
    lock_t *tmp_removed_lock_p;

    /* Initialize return values */
    *last_abs_offset_locked_p = -1;
    *next_abs_offset_p = -1;
    *bytes_locked_p = 0;
    
    lock_req_p->wait_abs_offset = final_abs_offset;
    offset_arr = lock_req_p->lock_result_p->offset_array;
    size_arr   = lock_req_p->lock_result_p->size_array;

    gossip_debug(GOSSIP_LOCK_DEBUG, "add_locks: actual_locked_bytes = %Ld\n",
		 lock_req_p->actual_locked_bytes);

    while (ret == 0)
    {
	/* Check for whether any locks need to be added from the
	 * removed list */
	if (!qlist_empty(&(lock_req_p->removed_link)))
	{
	    tmp_removed_lock_p = 
		qlist_entry(lock_req_p->removed_link.prev, lock_t, 
			    lock_link);
	    tmp_start_offset = tmp_removed_lock_p->start;
	    tmp_end_offset   = tmp_removed_lock_p->end;
	    lock_origin = REMOVED_LIST;

	    gossip_debug(GOSSIP_LOCK_DEBUG,
			 "add_locks: Getting (start offset=%Ld,end_offset=%Ld)"
			 " from removed list\n", tmp_start_offset, 
			 tmp_end_offset);
	}
	else /* Process request if necessary then get the next piece */
	{
	    assert(lock_req_p->seg_num <= lock_req_p->lock_result_p->segs - 1);
	    assert(lock_req_p->seg_bytes_used <= 
		   size_arr[lock_req_p->seg_num]);
	    if ((lock_req_p->seg_num == 
		 (lock_req_p->lock_result_p->segs - 1)) &&
		(lock_req_p->seg_bytes_used == size_arr[lock_req_p->seg_num]))
	    {
		/* We're all done! */
		if (PINT_REQUEST_DONE(lock_req_p->file_req_state))
		    break;
		lock_req_p->lock_result_p->segs = 0;
		lock_req_p->lock_result_p->bytes = 0;

		ret = PINT_process_request(lock_req_p->file_req_state,
					   NULL,
					   &lock_node_p->fdata,
					   lock_req_p->lock_result_p,
					   PINT_SERVER);
		if (ret < 0)
		{
		    gossip_err("add_locks: Failed to process file request\n");
		    ret = -PVFS_EINVAL;
		    break;
		}
		
		/* Might finish here for some reason */
		if ((PINT_REQUEST_DONE(lock_req_p->file_req_state)) &&
		    (lock_req_p->lock_result_p->segs == 0))
		    break;

		lock_req_p->seg_num = 0;
		lock_req_p->seg_bytes_used = 0;
	    }

	    tmp_start_offset = 
		offset_arr[lock_req_p->seg_num] + lock_req_p->seg_bytes_used;
	    tmp_end_offset = tmp_start_offset + size_arr[lock_req_p->seg_num] -
		lock_req_p->seg_bytes_used - 1;
	    lock_origin = LOCK_REQUEST_SEG;
	}

	gossip_debug(GOSSIP_LOCK_DEBUG,
		     "add_locks: Lock (local off=%Ld,end=%Ld) "
		     "max_abs_off=%Ld...\n",
		     tmp_start_offset,
		     tmp_end_offset,
		     final_abs_offset);

	/* Ensure that the lock requests are valid */
#if 1
	if ((tmp_start_offset < 0) || (tmp_end_offset < 0))
	{
	    gossip_err("add_locks: Lock offset=%Ld with end offset=%Ld "
		       "invalid\n", tmp_start_offset, tmp_end_offset);
	    ret = -PVFS_EINVAL;
	    break;
	}
#endif
	/* Convert the PINT_process_request output to the absolute
	 * logical offset */
	tmp_abs_offset = 
	    (*fdata_p->dist->methods->physical_to_logical_offset)
	    (fdata_p->dist->params,
	     fdata_p,
	     tmp_start_offset);

	/* Don't go past the final_abs_offset */
	if (tmp_abs_offset >= final_abs_offset)
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG,
			 "add_locks: Lock not added since "
			 "(start=%Ld,end=%Ld) >= final_abs_offset=%Ld\n",
			 tmp_abs_offset,
			 tmp_end_offset - tmp_start_offset + 1,
			 final_abs_offset);
	    
	    *next_abs_offset_p = tmp_abs_offset;
	    ret = 1;
	    break;
	}
	    	    
	/* Check whether this lock should be trimmed */
	if (final_abs_offset < 
	    (tmp_abs_offset + tmp_end_offset - tmp_start_offset + 1))
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG,
			 "add_locks: Trimming lock from (start=%Ld,end=%Ld) to"
			 " (start=%Ld,end=%Ld)\n",
			 tmp_abs_offset,
			 tmp_end_offset,
			 tmp_abs_offset,
			 tmp_start_offset + (final_abs_offset - 
					     tmp_abs_offset - 1));
	    tmp_end_offset = 
		tmp_start_offset + (final_abs_offset - tmp_abs_offset - 1);
	}

	itree_p = itree_interval_search(
	    lock_node_p->write_itree, &ITREE_NIL, 
	    tmp_start_offset, 
	    tmp_end_offset);
	if (itree_p != &ITREE_NIL)
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "itree_interval_search WRITE (int=%Ld,%Ld,rw=%s)"
			 " failed\n", tmp_start_offset, tmp_end_offset,
			 ((lock_req_p->io_type == 
			   PVFS_IO_READ) ? "r" : "w"));
	    /* Still may be able to add part of this lock depending on
	     * the overlap */
	    if (itree_p->start > tmp_start_offset)
	    {
		if ((itree_p->left == &ITREE_NIL) ||
		    (itree_p->left->max < tmp_start_offset))
		{
		    tmp_end_offset = itree_p->start - 1;
		    gossip_debug(GOSSIP_LOCK_DEBUG,
				 "add_locks: WRITE reset interval to "
				 "(%Ld,%Ld)\n", tmp_start_offset, 
				 tmp_end_offset);
		}
		else
		{
		    *next_abs_offset_p = tmp_abs_offset;
		    ret = 1;
		    break;

		}
	    }
	    else
	    {
		*next_abs_offset_p = tmp_abs_offset;
		ret = 1;
		break;
	    }
	}
	    
	/* Writes have to check both the write and read interval
	 * trees.  Reads have to check both if a write in the * queue
	 * was already found.  Reads need to see whether any * writes
	 * are ahead of them before just going ahead (if * you want
	 * some kind of guarantee to remove * starvation). */
	if (lock_req_p->io_type == PVFS_IO_WRITE)
	{
	    itree_p = itree_interval_search(
		lock_node_p->read_itree, &ITREE_NIL,
		tmp_start_offset,
		tmp_end_offset);
	    if (itree_p != &ITREE_NIL)
	    {
		gossip_debug(GOSSIP_LOCK_DEBUG, 
			     "itree_interval_search READ (int=%Ld,%Ld,rw=%s)"
			     " failed\n", tmp_start_offset, tmp_end_offset,
			     ((lock_req_p->io_type == 
			       PVFS_IO_READ) ? "r" : "w"));
		/* Still may be able to add part of this lock depending on
		 * the overlap */
		if (itree_p->start > tmp_start_offset)
		{
		    if ((itree_p->left == &ITREE_NIL) ||
			(itree_p->left->max < tmp_start_offset))
		    {
			tmp_end_offset = itree_p->start - 1;
			gossip_debug(GOSSIP_LOCK_DEBUG,
				     "add_locks: READ reset interval to "
				     "(%Ld,%Ld)\n", tmp_start_offset, 
				     tmp_end_offset);
		    }
		    else
		    {
			*next_abs_offset_p = tmp_abs_offset;
			ret = 1;
			break;
			
		    }
		}
		else
		{
		    *next_abs_offset_p = tmp_abs_offset;
		    ret = 1;
		    break;
		}
	    }
	}
	
	/* Add the lock to the write/read interval tree */
	if ((linked_itree_p = (linked_itree_t *) 
	     calloc(1, sizeof(linked_itree_t))) == NULL)
	{
	    gossip_err("add_locks: calloc linked_itree_p failed\n");
	    ret = -PVFS_ENOMEM;
	    break;
	}
	
	linked_itree_p->lock_id = lock_req_p->req_id;
	linked_itree_p->itree_link.start = tmp_start_offset;
	linked_itree_p->itree_link.end =  tmp_end_offset;
	if (linked_itree_p->itree_link.start > 
	    linked_itree_p->itree_link.end)
	{
	    gossip_err("Invalid lock (start=%Ld,end=%Ld)\n",
		       linked_itree_p->itree_link.start,
		       linked_itree_p->itree_link.end);
	    break;
	}

	gossip_debug(GOSSIP_LOCK_DEBUG, 
		     "Inserting (int=%Ld,%Ld,rw=%s)\n",
		     linked_itree_p->itree_link.start,
		     linked_itree_p->itree_link.end,
		     ((lock_req_p->io_type == 
		       PVFS_IO_READ) ? "r" : "w"));
	    
	if (lock_req_p->io_type == PVFS_IO_WRITE)
	    ret = itree_insert(&(lock_node_p->write_itree), &ITREE_NIL, 
			       &(linked_itree_p->itree_link));
	else
	    ret = itree_insert(&(lock_node_p->read_itree), &ITREE_NIL, 
			       &(linked_itree_p->itree_link));
	if (ret != 0)
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "itree_insert of lock (int=%Ld,%Ld,rw=%s) "
			 "failed\n", linked_itree_p->itree_link.start,
			 linked_itree_p->itree_link.end,
			 ((lock_req_p->io_type == 
			   PVFS_IO_READ) ? "r" : "w"));
	    *next_abs_offset_p = linked_itree_p->itree_link.start;
	    break;
	}

	/* Remove the lock from the lock origin */
	if (lock_origin == REMOVED_LIST)
	{
	    if (tmp_removed_lock_p->end == tmp_end_offset)
	    {
		qlist_del(&(tmp_removed_lock_p->lock_link));
		free(tmp_removed_lock_p);
	    }
	    else
	    {
		tmp_removed_lock_p->start = tmp_end_offset + 1;
	    }
	}
	else if (lock_origin == LOCK_REQUEST_SEG)
	{
	    lock_req_p->seg_bytes_used += 
		tmp_end_offset - tmp_start_offset + 1;
	    assert(lock_req_p->seg_bytes_used <=
		   size_arr[lock_req_p->seg_num]);
	    if ((lock_req_p->seg_bytes_used == 
		 size_arr[lock_req_p->seg_num]) &&
		(lock_req_p->seg_num !=
                 (lock_req_p->lock_result_p->segs - 1)))
	    {
		lock_req_p->seg_num++;
		lock_req_p->seg_bytes_used = 0;
	    }
	}
	else
	{
	    gossip_err("add_locks: Invalid lock_origin\n");
	    ret = -PVFS_EINVAL;
	}

	/* Add the lock to the lock_req */
	qlist_add_tail(&(linked_itree_p->list_link), 
		       &(lock_req_p->lock_head));
	
	*bytes_locked_p += linked_itree_p->itree_link.end -
	    linked_itree_p->itree_link.start + 1;
    }

    lock_req_p->actual_locked_bytes += *bytes_locked_p;
    
    /* Check that locks were added */
    if (lock_req_p->lock_head.prev != &lock_req_p->lock_head)
    {
	linked_itree_t *tmp_linked_itree_p = 
	    qlist_entry(lock_req_p->lock_head.prev, linked_itree_t, 
			list_link);
	*last_abs_offset_locked_p = 
	    (*fdata_p->dist->methods->physical_to_logical_offset)
	    (fdata_p->dist->params,
	     fdata_p,
	     tmp_linked_itree_p->itree_link.end);
    }

    gossip_debug(GOSSIP_LOCK_DEBUG, "add_locks: granted %Ld actual bytes "
                 "(total %Ld) of %Ld requested bytes and reached "
		 "logical offset %Ld, last_abs_offset %Ld, "
		 "next_abs_offset %Ld ret=%d\n",
		 *bytes_locked_p, 
		 lock_req_p->actual_locked_bytes, lock_req_p->aggregate_size,
                 (*fdata_p->dist->methods->physical_to_logical_offset)
                 (fdata_p->dist->params,
                  fdata_p,
                  tmp_end_offset),
		 *last_abs_offset_locked_p,
		 *next_abs_offset_p, ret);

    return ret;
}

/* check_lock_reqs - Basically goes through the queued_reqs and tries
 * to add the rest of the locks that they are waiting for.  It
 * processes the reqs in the order in which they were received. */
int check_lock_reqs(lock_node_t *lock_node_p)
{
    int ret = -1, chk_rwlock = 0;
    struct qlist_head *pos = NULL, *scratch;
    lock_req_t *tmp_lock_req_p = NULL;
    PVFS_size tmp_bytes_locked = 0;
    struct PVFS_servresp_lock *resp_lock_p;

    qlist_for_each_safe(pos, scratch, &lock_node_p->queued_req)
    {
	tmp_lock_req_p = qlist_entry(pos, lock_req_t, queued_req_link);

	assert(tmp_lock_req_p->wait_abs_offset >= -1);

	/* Add as many blocking locks as we can, then use callback
	 * function if its done */
	if (tmp_lock_req_p->wait_abs_offset >= 0)
	{
	    assert(((struct job_desc *) 
		    tmp_lock_req_p->lock_callback.data)->job_user_ptr != NULL);
	    resp_lock_p = 
		&((PINT_server_op *) 
		  ((struct job_desc *) tmp_lock_req_p->lock_callback.data)
		  ->job_user_ptr)->resp.u.lock;
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "check_lock_reqs: Trying lock id=%Ld...\n", 
			 tmp_lock_req_p->req_id);

	    ret = add_locks(tmp_lock_req_p, lock_node_p, 
			    tmp_lock_req_p->wait_abs_offset,
			    &resp_lock_p->last_abs_offset_locked,
			    &resp_lock_p->next_abs_offset,
			    &tmp_bytes_locked);

	    /* Keep track of how many bytes have been accessed
	     * in possibly multiple add_lock calls */
	    resp_lock_p->bytes_accessed += tmp_bytes_locked;

	    /* There are two cases to return: (1) The next lock is
	     * beyond the wait_abs_offset. Return incomplete. (2) All
	     * locks are granted.  Return complete. */

	    /* At this point, the request must be INCOMPLETE */
	    assert(tmp_lock_req_p->lock_req_status == INCOMPLETE);
	    if (ret == 0) 
	    {
		resp_lock_p->request_finished = 1;

		/* If this request is now done, send it to the granted list. */
		if (resp_lock_p->next_abs_offset == -1)
		{
		    gossip_debug(GOSSIP_LOCK_DEBUG, 
				 "check_lock_req: Deleting req_id="
				 "%Ld from the queued list\n", 
				 tmp_lock_req_p->req_id);
		    qlist_del_init(&tmp_lock_req_p->queued_req_link);
		    tmp_lock_req_p->lock_req_status = ALL_LOCKS_GRANTED;
		    tmp_lock_req_p->granted_req_link.key = 
			tmp_lock_req_p->req_id;
		    rbtree_insert(&(lock_node_p->granted_req), &RBTREE_NIL,
				  &(tmp_lock_req_p->granted_req_link));

		    gossip_debug(GOSSIP_LOCK_DEBUG, 
				 "check_lock_req: all %Ld aggregate "
				 "bytes granted to req_id=%Ld "
				 "(req_id=%Ld added to granted list)\n",
				 tmp_lock_req_p->aggregate_size,
				 tmp_lock_req_p->req_id, 
				 tmp_lock_req_p->req_id);
		}
		tmp_lock_req_p->lock_callback.fn(
		    tmp_lock_req_p->lock_callback.data);
	    }
	    else if (ret == 1)  
	    {
		resp_lock_p->request_finished = 0;
		/* This request has gone as far as it can, while not
		 * being completed. Otherwise, it will wait in the
		 * queue. */
		if (resp_lock_p->next_abs_offset > 
		    tmp_lock_req_p->wait_abs_offset)
		{
		    gossip_debug(GOSSIP_LOCK_DEBUG,
                                 "check_lock_req: Returning req %Ld since "
				 "next_abs_offset %Ld is beyond the "
				 "wait_abs_offset %Ld (resetting "
				 "wait_abs_offset)\n",
                                 tmp_lock_req_p->req_id,
				 resp_lock_p->next_abs_offset,
				 tmp_lock_req_p->wait_abs_offset);

		    tmp_lock_req_p->wait_abs_offset = -1;
		    tmp_lock_req_p->lock_callback.fn(
			tmp_lock_req_p->lock_callback.data);
		}
	    }
	   
	}

	/* Todo: Implement some way to handle reads and writes so that
	 * writes don't get starved. */
	if (tmp_lock_req_p->io_type == PVFS_IO_WRITE)
	    chk_rwlock = 1;
    }

    return 0;
}

/* add_lock_req - Adds a number of locks to the object_ref_p.
 * Assumptions made are that all lock_reqs have been granted as many
 * locks as they can hold at this point.  Therefore, writes can
 * proceed as far as possible, but reads can proceed as far as
 * possible only if a write is not in front of it.  Otherwise it must
 * undergo the same checks as the writes in the queue. Returns -1 for
 * error, 0 for pending, and 1 for all locks acquired */
int add_lock_req(PVFS_object_ref *object_ref_p, 
		 enum PVFS_io_type io_type, 
		 enum PVFS_server_lock_type lock_type,
		 PVFS_id_gen_t client_lock_req_id,
		 PINT_request_file_data *fdata_p,
		 PINT_Request *file_req,
		 PVFS_offset file_req_offset,
		 PVFS_offset final_offset,
		 PVFS_size aggregate_size,
		 struct PVFS_servresp_lock *resp_lock_p,
		 lock_req_t **lock_req_p_p)
{
    int ret = 0;
    struct qhash_head *hash_link_p = NULL;
    lock_node_t *lock_node_p = NULL;
    lock_req_t *lock_req_p = NULL;
    PINT_Request_state *file_req_state;

    assert((lock_type != PVFS_SERVER_RELEASE_SOME) &&
	   (lock_type != PVFS_SERVER_RELEASE_ALL));

    QLIST_HEAD(tmp_lock_head);
    
    if (!LOCK_FILE_TABLE_INITIALIZED())
    {
	gossip_err("add_lock_req: Impossible lock_file_table not "
		   "initialized\n");
	return -1;
    }

    gen_mutex_lock(lock_file_table_mutex);
    
    /* First, search for existing lock_node.  If it's not there,
     * calloc it and add it to the hash table. */
    hash_link_p = qhash_search(lock_file_table, object_ref_p);
    if (!hash_link_p)
    {
	if ((lock_node_p = (lock_node_t *) 
	     calloc(1, sizeof(lock_node_t))) == NULL)
	{
	    gossip_err("add_lock_req: calloc lock_node_p failed\n");
	    return -PVFS_ENOMEM;
	}
	ITREE_HEAD_PTR_INIT(lock_node_p->write_itree, ITREE_NIL);
	ITREE_HEAD_PTR_INIT(lock_node_p->read_itree, ITREE_NIL);
	RBTREE_HEAD_PTR_INIT(lock_node_p->granted_req, RBTREE_NIL);
	INIT_QLIST_HEAD(&(lock_node_p->queued_req));
	INIT_QLIST_HEAD(&(lock_node_p->all_req));

	lock_node_p->refn.fs_id = object_ref_p->fs_id;
	lock_node_p->refn.handle = object_ref_p->handle;
	/* Copy fdata over */
	memcpy(&lock_node_p->fdata, fdata_p, sizeof(PINT_request_file_data));
	lock_node_p->fdata.dist = PINT_dist_copy(fdata_p->dist);
	lock_node_p->fdata.fsize = __LONG_LONG_MAX__;
	lock_node_p->fdata.extend_flag = 1;

	qhash_add(lock_file_table, &(lock_node_p->refn), 
		  &(lock_node_p->hash_link));
    }
    else
	lock_node_p = qlist_entry(hash_link_p, lock_node_t, hash_link);
    
    /* If this is a new request, it should be set up properly */
    if ((lock_type == PVFS_SERVER_ACQUIRE_NEW_NONBLOCK) ||
	(lock_type == PVFS_SERVER_ACQUIRE_NEW_BLOCK))
    {
	file_req_state = PINT_new_request_state(file_req);
	if (!file_req_state)
	{
	    ret = -PVFS_EINVAL;
	    gossip_err("PINT_new_request_state of file_req_state failed\n");
	    goto add_unlock_exit;
	}
	PINT_REQUEST_STATE_SET_TARGET(file_req_state,
				      file_req_offset);
	PINT_REQUEST_STATE_SET_FINAL(file_req_state, 
				     file_req_offset + aggregate_size);
	
	gossip_debug(GOSSIP_LOCK_DEBUG, 
		     "file_req_offset = %Ld aggregate_size = %Ld\n", 
		     file_req_offset, aggregate_size);

	/* Set up new lock request */
	lock_req_p = (lock_req_t *) calloc(1, sizeof(lock_req_t));
	INIT_QLIST_HEAD(&lock_req_p->lock_head);
	lock_req_p->lock_req_status = NEW;
	INIT_QLIST_HEAD(&lock_req_p->queued_req_link);
	INIT_QLIST_HEAD(&lock_req_p->all_req_link);
	lock_req_p->req_id = lock_req_id++;
	lock_req_p->io_type = io_type;
	lock_req_p->actual_locked_bytes = 0;
	lock_req_p->lock_result_p = PINT_Request_result_init();
	lock_req_p->file_req_state = file_req_state;
	lock_req_p->seg_num = 0;
	lock_req_p->seg_bytes_used = 0;
	INIT_QLIST_HEAD(&lock_req_p->removed_link);
	lock_req_p->aggregate_size = aggregate_size;
	lock_req_p->file_req = file_req;
	lock_req_p->file_req_offset = file_req_offset;

	/* Do some initial request processing */
	ret = PINT_process_request(lock_req_p->file_req_state,
				   NULL,
				   &lock_node_p->fdata,
				   lock_req_p->lock_result_p,
				   PINT_SERVER);

	qlist_add_tail(&(lock_req_p->all_req_link), &(lock_node_p->all_req));
    }
    else /* Find the lock req - look in the queued list (this could be
	  * optimized by placing it in a hash table */
    {
	struct qlist_head *tmp_qlist_head = NULL;
	qlist_for_each(tmp_qlist_head, &lock_node_p->queued_req)
	{
	    lock_req_p = qlist_entry(tmp_qlist_head, 
				     lock_req_t, queued_req_link);
	    if (lock_req_p->req_id == client_lock_req_id)
		break;
	}
	
	if (lock_req_p == NULL ||
	    lock_req_p->req_id != client_lock_req_id)
	{
	    gossip_err("add_lock_req: Request with id=%Ld should have been "
		       "found in the queued list!\n",
		       client_lock_req_id);
	    goto add_unlock_exit;
	}
    }

    /* Add the locks */
    ret = add_locks(lock_req_p,
		    lock_node_p,
		    final_offset,
		    &resp_lock_p->last_abs_offset_locked,
		    &resp_lock_p->next_abs_offset,
		    &resp_lock_p->bytes_accessed);

    /* Set up the rest of the response for the client.  Not necessary
     * in check_lock_req since it's set here. */
    resp_lock_p->lock_id = lock_req_p->req_id;

    /* Add the lock_req to either the granted tree or the queued
     * linked list */
    if (ret == 0)
    {
	if (lock_req_p->lock_req_status == INCOMPLETE)
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, "add_lock_req: Deleting req_id="
			 "%Ld from the queued list\n", lock_req_p->req_id);
	    qlist_del_init(&lock_req_p->queued_req_link);
	}

	lock_req_p->lock_req_status = ALL_LOCKS_GRANTED;
	lock_req_p->granted_req_link.key = lock_req_p->req_id;
	rbtree_insert(&(lock_node_p->granted_req), &RBTREE_NIL,
		      &(lock_req_p->granted_req_link));

	gossip_debug(GOSSIP_LOCK_DEBUG, "add_lock_req: all %Ld aggregate "
		     "bytes granted to req_id=%Ld "
		     "(req_id=%Ld added to granted list)\n",  
		     lock_req_p->aggregate_size,
		     lock_req_p->req_id, lock_req_p->req_id);
	*lock_req_p_p = NULL;
	ret = 1;
	resp_lock_p->request_finished = 1;
    }
    else if (ret == 1)
    {
	if (lock_req_p->lock_req_status == NEW) 
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, "add_lock_req: Adding req_id="
			 "%Ld to the queued list\n", lock_req_p->req_id);
	    
	    qlist_add_tail(&(lock_req_p->queued_req_link), 
			   &(lock_node_p->queued_req));
	}

	lock_req_p->lock_req_status = INCOMPLETE;

	gossip_debug(GOSSIP_LOCK_DEBUG, "add_lock_req: some %Ld of %Ld "
		     "aggregate bytes granted to req_id=%Ld (waiting "
		     "til %Ld)\n",  
		     lock_req_p->actual_locked_bytes, 
		     lock_req_p->aggregate_size,
		     lock_req_p->req_id, lock_req_p->wait_abs_offset);
	*lock_req_p_p = lock_req_p;

	if ((lock_type == PVFS_SERVER_ACQUIRE_NEW_BLOCK) ||
	    (lock_type == PVFS_SERVER_ACQUIRE_NEW_NONBLOCK) ||
	    (lock_type == PVFS_SERVER_ACQUIRE_NONBLOCK) ||
	    (resp_lock_p->next_abs_offset >= final_offset))
	{
	    lock_req_p->wait_abs_offset = -1;
	    ret = 1;
	    gossip_debug(GOSSIP_LOCK_DEBUG, "Returning immediately (wait=%Ld)"
			 " since next offset=%Ld is beyond final_offset=%Ld "
			 "or request is nonblocking\n",
			 lock_req_p->wait_abs_offset,
			 resp_lock_p->next_abs_offset, final_offset);
	}
	else
	{
	    lock_req_p->wait_abs_offset = final_offset;
	    ret = 0;
	    gossip_debug(GOSSIP_LOCK_DEBUG, "Waiting until %Ld.\n",
			 lock_req_p->wait_abs_offset);
	}
	resp_lock_p->request_finished = 0;
    }
    else
    {
	gossip_err("add_lock_req: add_locks() returned an error!\n");
    }
    
  add_unlock_exit:
    gen_mutex_unlock(lock_file_table_mutex);
    return ret;
}

/* revise_lock_req - Removes all locked bytes up to final_offset from
 * the lock req.  If lock_type == PVFS_SERVER_REMOVE_ALL, remove the
 * entire lock request.  Return -1 for an error. */
int revise_lock_req(PVFS_object_ref *object_ref_p,
		    enum PVFS_server_lock_type lock_type,
		    PVFS_id_gen_t req_id, 
		    PVFS_offset final_offset,
		    struct PVFS_servresp_lock *resp_lock_p,
		    lock_node_t **lock_node_p_p)
{
    int ret = 0;
    enum PVFS_io_type io_type;
    struct qlist_head *hash_link_p = NULL, *pos = NULL, 
	*scratch = NULL, *lock_head_p = NULL;
    itree_t *del_itree_p = NULL;
    rbtree_t *rbtree_p = &RBTREE_NIL, *del_rbtree_p = NULL;
    lock_req_t *lock_req_p = NULL;
    PVFS_size released_bytes = 0;
    linked_itree_t *linked_itree_p = NULL;
    lock_node_t *lock_node_p = NULL;
    PVFS_offset final_local_offset;
    PINT_request_file_data *fdata_p;
    lock_t *tmp_lock_p;

    if (!LOCK_FILE_TABLE_INITIALIZED())
    {
	fprintf(stdout, "revise_lock_req: Impossible lock_file_table not "
		"initialized\n");
	return -1;
    }

    gen_mutex_lock(lock_file_table_mutex);

    /* Find the lock_node for the object_ref_p. */
    hash_link_p = qhash_search(lock_file_table, object_ref_p);
    if (!hash_link_p)
    {
	gossip_debug(GOSSIP_LOCK_DEBUG, 
		     "revise_lock_req: Lock node not found!\n");
	ret = -1;
	goto del_unlock_exit;

    }
    lock_node_p = qlist_entry(hash_link_p, lock_node_t, hash_link);
    *lock_node_p_p = lock_node_p;
    fdata_p = &lock_node_p->fdata;

    /* Since the actual lock req could be either in the queued_req
     * list or the granted_req rbtree, look in the granted_req_rbtree
     * first, then the queued_req_list.  Maintain this note through
     * the call */
    rbtree_p = rbtree_search(lock_node_p->granted_req,
			     &RBTREE_NIL, req_id);
    if (rbtree_p != &RBTREE_NIL)
	lock_req_p = rbtree_entry(rbtree_p, lock_req_t, granted_req_link);
    else
    {
	lock_req_t *tmp_lock_req_p = NULL;

	gossip_debug(GOSSIP_LOCK_DEBUG, "revise_lock_req: req_id=%Ld not "
		     "found in granted list\n", req_id);

	qhash_for_each(pos, &(lock_node_p->queued_req))
	{
	    tmp_lock_req_p = qlist_entry(pos, lock_req_t, queued_req_link); 
	    if (tmp_lock_req_p->req_id == req_id)
		lock_req_p = tmp_lock_req_p;
	}
	if (lock_req_p == NULL)
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, "revise_lock_req: req_id=%Ld not "
			 "found in granted or queued list\n", req_id);
	    ret = -1;
	    goto del_unlock_exit;
	}
    }    
    lock_head_p = &(lock_req_p->lock_head);
    io_type = lock_req_p->io_type;

    /* Delete and/or change locks in the correct lock tree by
     * final_offset. */
    if (lock_type == PVFS_SERVER_RELEASE_ALL) /* Release all bytes. */
    {
	qlist_for_each_safe(pos, scratch, lock_head_p)
	{
	    linked_itree_p = itree_entry(pos, linked_itree_t, list_link);
	    
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "removing (ALL) lock from %Ld to %Ld - req %d "
			 "(should be %Ld)\n",
			 linked_itree_p->itree_link.start,
			 linked_itree_p->itree_link.end,
			 linked_itree_p->lock_id, req_id);

	    released_bytes += linked_itree_p->itree_link.end -
		linked_itree_p->itree_link.start + 1;

	    del_itree_p = itree_delete(
		((io_type == PVFS_IO_READ) ? 
		 &(lock_node_p->read_itree) : &(lock_node_p->write_itree)),
		&ITREE_NIL, &(linked_itree_p->itree_link), 
		linked_itree_cpy_fn);
	    linked_itree_p = 
		itree_entry(del_itree_p, linked_itree_t, itree_link);
	    scratch = linked_itree_p->list_link.next;
	    qlist_del(&(linked_itree_p->list_link));
	    free(linked_itree_p);
	}
    }
    else /* Remove some of the locks (possibly all) */
    {
	struct qlist_head *last_lock_p;

	/* Convert the final_offset to a local offset */
	final_local_offset = 
	    (*fdata_p->dist->methods->logical_to_physical_offset)
	    (fdata_p->dist->params,
	     fdata_p,
	     final_offset);

	while (1) 
	{
	    last_lock_p = lock_head_p->prev;
	    
	    if (last_lock_p == lock_head_p)
	    {
		gossip_debug(GOSSIP_LOCK_DEBUG,
			     "All locks (SOME) were removed!\n");
		break;
	    }

	    linked_itree_p = 
		itree_entry(last_lock_p, linked_itree_t, list_link);
	    
	    /* 3 cases 
	     * 1 - final_local_offset is now safe - not a problem
	     * 2 - Need to break a lock and add a piece to the removed_link
	     * 3 - Remove lock completely */
	    if (final_local_offset > linked_itree_p->itree_link.end)
		break;
	    else if ((final_local_offset <= linked_itree_p->itree_link.end) &&
		     (final_local_offset > linked_itree_p->itree_link.start))
	    {
		tmp_lock_p = malloc(sizeof(lock_t));
		if (!tmp_lock_p)
		{
		    gossip_err("revise_lock_req: malloc tmp_lock_p failed\n");
		    break;
		}
		tmp_lock_p->start = final_local_offset;
		tmp_lock_p->end = linked_itree_p->itree_link.end;

		qlist_add_tail(&(tmp_lock_p->lock_link),
			       &(lock_req_p->removed_link));

		released_bytes += linked_itree_p->itree_link.end -
		    (final_local_offset - 1);

		gossip_debug(GOSSIP_LOCK_DEBUG, 
			     "removing (SOME) partial lock from %Ld to %Ld - "
			     "req %d (should be %Ld) - final_local_off=%Ld\n",
			     final_local_offset,
			     linked_itree_p->itree_link.end,
			     linked_itree_p->lock_id, req_id,
			     final_local_offset);
		
		linked_itree_p->itree_link.end = final_local_offset - 1;
		
		/* Now, fix your own max and go up the tree */
		itree_max_update_self(&(linked_itree_p->itree_link),
				      &ITREE_NIL);
		itree_max_update_parent(&(linked_itree_p->itree_link),
					&ITREE_NIL);
		break;
	    }
	    else
	    {
		tmp_lock_p = malloc(sizeof(lock_t));
		if (!tmp_lock_p)
		{
		    gossip_err("revise_lock_req: malloc tmp_lock_p failed\n");
		    break;
		}
		tmp_lock_p->start = linked_itree_p->itree_link.start;
		tmp_lock_p->end = linked_itree_p->itree_link.end;
		    
		qlist_add_tail(&(tmp_lock_p->lock_link),
			       &(lock_req_p->removed_link));

		released_bytes += linked_itree_p->itree_link.end -
		    linked_itree_p->itree_link.start + 1;

		gossip_debug(GOSSIP_LOCK_DEBUG, 
			     "removing (SOME) lock from %Ld to %Ld - req %d "
			     "(should be %Ld) - final_local_off=%Ld\n",
			     linked_itree_p->itree_link.start,
			     linked_itree_p->itree_link.end,
			     linked_itree_p->lock_id, req_id,
			     final_local_offset);

		qlist_del(last_lock_p);
		del_itree_p = itree_delete(
		    ((io_type == PVFS_IO_READ) ? &(lock_node_p->read_itree) :
		     &(lock_node_p->write_itree)), &ITREE_NIL,
		    &(linked_itree_p->itree_link),
		    linked_itree_cpy_fn);
		linked_itree_p = 
		    itree_entry(del_itree_p, linked_itree_t, itree_link);
		qlist_del(&(linked_itree_p->list_link));
		free(linked_itree_p);
		last_lock_p = lock_head_p->prev;
	    }
	}
    }

    lock_req_p->actual_locked_bytes -= released_bytes;
    /* Don't try to add locks for it in the future */
    lock_req_p->wait_abs_offset = -1;

    /* Remove lock_req from its respective queue if final_offset == -1.
     * If the lock_req was ALL_LOCKS_GRANTED, change its status to
     * INCOMPLETE, and add it to the queued_list in the proper
     * location (i.e. search the all_req queue to see where to add
     * it). */
    if (lock_type == PVFS_SERVER_RELEASE_ALL)
    {
	if (lock_req_p->lock_req_status == INCOMPLETE)
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "Remove all locks - req %Ld removed from the"
			 " queued list.\n",
			 lock_req_p->req_id);
	    qlist_del_init(&(lock_req_p->queued_req_link));
	}
	else
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "Remove all locks - req %Ld removed from the"
			 " granted list.\n",
			 lock_req_p->req_id);

	    del_rbtree_p = 
		rbtree_delete(&lock_node_p->granted_req, &RBTREE_NIL,
			      &(lock_req_p->granted_req_link), 
			      rbtree_lock_req_cpy_fn);
	    lock_req_p =
		rbtree_entry(del_rbtree_p, lock_req_t, granted_req_link);
	}

	/* Delete the lock req from the all_req queue and possibly
	 * delete the lock_node if there are no more entries for
	 * it. */
	qlist_del_init(&(lock_req_p->all_req_link));
	
	free_lock_req(lock_req_p);
	if (qlist_empty(&(lock_node_p->all_req)))
	{
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "No lock request in node, removing lock node\n");
	    
	    qlist_del(&(lock_node_p->hash_link));
	    PINT_dist_free(lock_node_p->fdata.dist);
	    free(lock_node_p);
	    *lock_node_p_p = NULL;
	}
	/* Todo: Should also be a way to check if the table is empty to free
	 * that as well. */
    }
    else
    {
	if (lock_req_p->lock_req_status == ALL_LOCKS_GRANTED)
	{
	    
	    lock_req_t *tmp_lock_req_p = NULL, *found_lock_req_p = NULL;
	    struct qlist_head *tmp_req_p = NULL;

	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "Remove some locks - req %Ld removed from the"
			 " granted list.\n",
			 lock_req_p->req_id);
	    del_rbtree_p =
		rbtree_delete(&(lock_node_p->granted_req), &RBTREE_NIL,
			      &(lock_req_p->granted_req_link), 
			      rbtree_lock_req_cpy_fn);
	     lock_req_p =
		 rbtree_entry(del_rbtree_p, lock_req_t, granted_req_link);
	     tmp_req_p = &(lock_req_p->all_req_link);

	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "Remove some locks - req %Ld added back to the"
			 " queued list.\n",
			 lock_req_p->req_id);

	    /* Find out where in the queued list it belongs.  Search
	     * the all_req queue after it to see what is next and
	     * where it should be added.  If the next one can't be
	     * found, add itself to the end.  */
	    while (tmp_req_p->next != &(lock_node_p->all_req))
	    {
		tmp_lock_req_p = qlist_entry(&(tmp_req_p->next), lock_req_t,
					     all_req_link);
		if (tmp_lock_req_p->lock_req_status == INCOMPLETE)
		{
		    found_lock_req_p = tmp_lock_req_p;
		    break;
		}
	    }

	    lock_req_p->lock_req_status = INCOMPLETE;	    
	    if (found_lock_req_p)
		qlist_add(&(lock_req_p->queued_req_link), 
			  found_lock_req_p->queued_req_link.prev);
	    else
		qlist_add_tail(&(lock_req_p->queued_req_link), 
			       &(lock_node_p->queued_req));
	}
	else {
	    gossip_debug(GOSSIP_LOCK_DEBUG, 
			 "Remove some locks - req %Ld already in "
			 "queued list.\n",
			 lock_req_p->req_id);
	}
    }

    /* Set the resp fields for last_abs_offset and next_abs_offset */
    if (lock_type == PVFS_SERVER_RELEASE_ALL)
    {	
	resp_lock_p->last_abs_offset_locked = -1;
	resp_lock_p->next_abs_offset = -1;
    }
    else
    {
	resp_lock_p->lock_id = lock_req_p->req_id;

	/* Since at least one lock should be removed, this shouldn't happen */
	if (lock_req_p->lock_head.prev == &lock_req_p->lock_head)
	    resp_lock_p->last_abs_offset_locked = -1;
	else
	{
	    linked_itree_p = qlist_entry(lock_req_p->lock_head.prev, 
					 linked_itree_t, list_link);
	    resp_lock_p->last_abs_offset_locked =  
		(*fdata_p->dist->methods->physical_to_logical_offset)
		(fdata_p->dist->params,
		 fdata_p,
		 linked_itree_p->itree_link.end);
	}

	if (lock_req_p->removed_link.prev == &lock_req_p->removed_link)
	{
	    /* search the request and process if necessary */
	    if ((lock_req_p->seg_num == 
		 (lock_req_p->lock_result_p->segs - 1)) &&
		(lock_req_p->seg_bytes_used == 
		 lock_req_p->lock_result_p->offset_array[lock_req_p->seg_num]))
	    {
		if (!PINT_REQUEST_DONE(lock_req_p->file_req_state))
		{
		    lock_req_p->lock_result_p->segs = 0;
		    lock_req_p->lock_result_p->bytes = 0;

		    ret = PINT_process_request(lock_req_p->file_req_state,
					       NULL,
					       &lock_node_p->fdata,
					       lock_req_p->lock_result_p,
					       PINT_SERVER);
		    
		}
	    }
	    
	    if (PINT_REQUEST_DONE(lock_req_p->file_req_state))
		resp_lock_p->next_abs_offset = -1;	    
	    else
	    {
		resp_lock_p->next_abs_offset =
		    (*fdata_p->dist->methods->physical_to_logical_offset)
		    (fdata_p->dist->params,
		     fdata_p,
		     lock_req_p->lock_result_p->offset_array[
			 lock_req_p->seg_num] + lock_req_p->seg_bytes_used);
	    }
	}
	else
	{
	    tmp_lock_p = qlist_entry(lock_req_p->removed_link.prev, 
				     lock_t, lock_link);
	    resp_lock_p->next_abs_offset = 
		(*fdata_p->dist->methods->physical_to_logical_offset)
		(fdata_p->dist->params,
		 fdata_p,
		 tmp_lock_p->start);
	}
    }

  del_unlock_exit:
    gen_mutex_unlock(lock_file_table_mutex);

    resp_lock_p->bytes_accessed = -released_bytes;
    if (released_bytes == 0)
	gossip_err("revise_lock_req: Warning..0 bytes released!\n");

    if (ret == 0)
	resp_lock_p->request_finished = 1;
    else /* There was a problem. */ 
	resp_lock_p->request_finished = 0;
    
    /* If any bytes have been revoked, all queued locks need to be
     * progress as far as possible (advance their blocking bytes and
     * possibly set jobs to complete), which happens in a later state
     * machine */

    return ret;
}

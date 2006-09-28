/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Written by Avery Ching - Northwestern University
 * See COPYING in top-level directory.
 * 
 */

#include "lock-storage.h"

/* Processing scratch space */
#define MAX_OL_PAIRS 64
static PINT_Request_result lock_result;

/* Replace one node with another */
static void qlist_replace(struct qlist_head * dest_p,
			  struct qlist_head * src_p)
{
    /* Fix dest_p's links */
    dest_p->next = src_p->next;
    dest_p->prev = src_p->prev;

    /* Fix dest_p's neighbors' links */
    if (src_p->next)
	src_p->next->prev = dest_p;
    if (src_p->prev)
	src_p->prev->next = dest_p;
}

typedef struct {
    struct qlist_head list_link;
    itree_t itree_link;
} linked_itree_t;

typedef struct {
    /* Pointer to the first lock */
    struct qlist_head lock_head;

    enum PVFS_lock_req_status lock_req_status;
    /* Numerous links to the same objects */
    rbtree_t granted_req_link;
    struct qlist_head queued_req_link;
    struct qlist_head all_req_link;
    
    PVFS_id_gen_t req_id;

    PVFS_size req_bytes;      /* Total bytes requested by the application. */
    PVFS_size total_bytes   ; /* Total bytes in the entire req. */
    enum PVFS_io_type io_type;
    PINT_Request_state *file_req_state; /* Where we are in the file_req */
    PINT_Request *file_req;
    PVFS_offset file_req_offset;

 } lock_req_t;

typedef struct {
    itree_t *write_itree; /* head of write lock tree */
    itree_t *read_itree;  /* head of read lock tree */

    rbtree_t *granted_req;          /* head of granted lock requests */
    struct qlist_head queued_req; /* head of queue lock requests */
    struct qlist_head all_req;    /* all lock requests */

    PVFS_object_ref refn;        /* object infofrmation */
    struct qlist_head hash_link; /* location in the hash linked list */
} lock_node_t;

/* Sentinel creation for both itree.h and rbtree.h */

ITREE_NIL(ITREE_NIL);
RBTREE_NIL(RBTREE_NIL);

int linked_itree_cpy_fn(itree_t *dest_p, itree_t *src_p)
{
    linked_itree_t *tmp_dest_p =
	itree_entry(dest_p, linked_itree_t, itree_link);
    linked_itree_t *tmp_src_p =
        itree_entry(src_p, linked_itree_t, itree_link);
    
    qlist_replace(&(tmp_dest_p->list_link), &(tmp_src_p->list_link));
    return 0;

}

int rbtree_lock_req_cpy_fn(rbtree_t *dest_p, rbtree_t *src_p)
{
    lock_req_t *tmp_dest_p =
	rbtree_entry(dest_p, lock_req_t, queued_req_link);
    lock_req_t *tmp_src_p =
	rbtree_entry(dest_p, lock_req_t, queued_req_link);
    
    qlist_replace(&(tmp_dest_p->lock_head), &(tmp_src_p->lock_head));
    tmp_dest_p->lock_req_status = tmp_src_p->lock_req_status;
    qlist_replace(&(tmp_dest_p->queued_req_link), 
		  &(tmp_src_p->queued_req_link));
    qlist_replace(&(tmp_dest_p->all_req_link), 
		  &(tmp_src_p->all_req_link));
    tmp_dest_p->req_id = tmp_src_p->req_id;
    tmp_dest_p->req_bytes = tmp_src_p->req_bytes;
    tmp_dest_p->total_bytes = tmp_src_p->total_bytes;
    tmp_dest_p->io_type = tmp_src_p->io_type;
    tmp_dest_p->file_req_state = tmp_src_p->file_req_state;
    tmp_dest_p->file_req = tmp_src_p->file_req;
    tmp_dest_p->file_req_offset = tmp_src_p->file_req_offset;

    tmp_src_p->file_req_state = NULL;
    tmp_src_p->file_req = NULL;
    return 0;
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
	
	lock_result.segmax = MAX_OL_PAIRS;
	lock_result.bytemax = INT_MAX;
	lock_result.offset_array = (PVFS_offset *) 
	    calloc(MAX_OL_PAIRS, sizeof(PVFS_offset));
	if (!lock_result.offset_array)
	{
	    qhash_finalize(lock_file_table);
	    lock_file_table = NULL;
	    gen_mutex_destroy(lock_file_table_mutex);
	    lock_file_table_mutex = NULL;
	    return -PVFS_ENOMEM;
	}
	lock_result.size_array = (PVFS_size *) 
	    calloc(MAX_OL_PAIRS, sizeof(PVFS_size));
	if (!lock_result.size_array)
	{
	    qhash_finalize(lock_file_table);
	    lock_file_table = NULL;
	    gen_mutex_destroy(lock_file_table_mutex);
	    lock_file_table_mutex = NULL;
	    free(lock_result.offset_array);
	    return -PVFS_ENOMEM;
	}
    }
    else
	fprintf(stdout, "init_lock_file_table: already exists!\n");

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
	free(lock_result.offset_array);
	free(lock_result.size_array);
    }
    else
	fprintf(stdout, "free_lock_file_table_all: Already NULL!\n");
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
	gen_mutex_lock(lock_file_table_mutex);
	for (i = 0; i < lock_file_table->table_size; i++)
	{
	    struct qlist_head *tmp_req_p = NULL;
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
		    fprintf(stdout, "{req_id=%Ld} ", tmp_lock_req_p->req_id);
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
		itree_inorder_tree_print(lock_node_p->write_itree, 
					 &ITREE_NIL);
		fprintf(stdout, "\n  read_itree: ");
		itree_inorder_tree_print(lock_node_p->read_itree,
					 &ITREE_NIL);
		fprintf(stdout, "\n");
	    }
	    
	    fprintf(stdout, "\n");
	}
	gen_mutex_unlock(lock_file_table_mutex);
    }
    else
	fprintf(stdout, "print_lock_file_table_all: NULL!\n");
}

/* add_lock_req - Adds a number of locks to the object_ref_p.
 * Assumptions made are that all lock_reqs have been granted as many
 * locks as they can hold at this point.  Therefore, writes can
 * proceed as far as possible, but reads can proceed as far as
 * possible only if a write is not in front of it.  Otherwise it must
 * undergo the same checks as the writes in the queue. */
int add_lock_req(PVFS_object_ref *object_ref_p, 
		 enum PVFS_io_type io_type, 
		 PINT_Request *file_req,
		 PVFS_offset file_req_offset,
		 PINT_request_file_data *fdata_p,
		 PVFS_size nb_bytes,
		 PVFS_size bb_bytes,
		 PVFS_size aggregate_size,
		 PVFS_id_gen_t *req_id,
		 PVFS_size *granted_bytes_p)
{
    int i, ret = 0;
    struct qhash_head *hash_link_p = NULL;
    lock_node_t *lock_node_p = NULL;
    lock_req_t *lock_req_p = NULL;
    linked_itree_t *linked_itree_p = NULL;
    itree_t *itree_p = &ITREE_NIL;
    PVFS_size nb_bytes_granted = 0;
    PINT_Request_state *file_req_state;
    PVFS_offset final_physical_off = 0;

    QLIST_HEAD(tmp_lock_head);
    
    if (!LOCK_FILE_TABLE_INITIALIZED())
    {
	fprintf(stdout, "add_lock_req: Impossible lock_file_table not "
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
	    fprintf(stdout, "add_lock_req: calloc lock_node_p failed\n");
	    return -PVFS_ENOMEM;
	}
	ITREE_HEAD_PTR_INIT(lock_node_p->write_itree, ITREE_NIL);
	ITREE_HEAD_PTR_INIT(lock_node_p->read_itree, ITREE_NIL);
	RBTREE_HEAD_PTR_INIT(lock_node_p->granted_req, RBTREE_NIL);
	INIT_QLIST_HEAD(&(lock_node_p->queued_req));
	INIT_QLIST_HEAD(&(lock_node_p->all_req));

	lock_node_p->refn.fs_id = object_ref_p->fs_id;
	lock_node_p->refn.handle = object_ref_p->handle;
	qhash_add(lock_file_table, &(lock_node_p->refn), 
		  &(lock_node_p->hash_link));
    }
    else
	lock_node_p = qlist_entry(hash_link_p, lock_node_t, hash_link);
    
    /* Add all the locks to the correct lock tree - nb style.  First,
     * setup the PVFS state.  */
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

    do {
	lock_result.segs = 0;
	lock_result.bytes = 0;
	lock_result.bytemax = aggregate_size - nb_bytes_granted;

	ret = PINT_process_request(file_req_state,
				   NULL,
				   fdata_p,
				   &lock_result,
				   PINT_SERVER);
	if (ret < 0)
	{
	    gossip_err("add_lock_req: Failed to process file request\n");
	    goto add_unlock_exit;
	}
	
	for (i = 0; i < lock_result.segs; i++)
	{
	    itree_p = itree_interval_search(
		lock_node_p->write_itree, &ITREE_NIL, 
		lock_result.offset_array[i], 
		lock_result.offset_array[i] + lock_result.size_array[i]);
	    if (itree_p != &ITREE_NIL)
	    {
		ret = -PVFS_EINVAL;
		break;
	    }
	    
	    /* Writes have to check both the write and read interval
	     * trees */
	    if (io_type == PVFS_IO_WRITE)
	    {
		itree_p = itree_interval_search(
		    lock_node_p->read_itree, &ITREE_NIL, 
		    lock_result.offset_array[i], 
		    lock_result.offset_array[i] + lock_result.size_array[i]);
		if (itree_p != &ITREE_NIL)
		{
		    ret = -PVFS_EINVAL;
		    break;
		}
	    }
	    else
	    {
		/* Reads need to see whether any writes are ahead of
		 * them before just going ahead (if you want some kind
		 * of guarantee to remove starvation). */
	    }
	    
	    /* Add the lock to the write/read interval tree */
	    if ((linked_itree_p = (linked_itree_t *) 
		 calloc(1, sizeof(linked_itree_t))) == NULL)
	    {
		ret =  -PVFS_ENOMEM;
		goto add_unlock_exit;
	    }		
	    
	    linked_itree_p->itree_link.start = lock_result.offset_array[i];
	    linked_itree_p->itree_link.end = 
		lock_result.offset_array[i] + 
		lock_result.size_array[i] - 1;
	    if (linked_itree_p->itree_link.start > 
		linked_itree_p->itree_link.end)
	    {
		fprintf(stdout, "Invalid lock (start=%Ld,end=%Ld)\n",
			linked_itree_p->itree_link.start,
			linked_itree_p->itree_link.end);
		ret = -EINVAL;
		goto add_unlock_exit;
	    }
	    
	    if (io_type == PVFS_IO_WRITE)
		ret = itree_insert(&(lock_node_p->write_itree), &ITREE_NIL, 
				   &(linked_itree_p->itree_link));
	    else
		ret = itree_insert(&(lock_node_p->read_itree), &ITREE_NIL, 
				   &(linked_itree_p->itree_link));
	    if (ret != 0)
	    {
		fprintf(stdout, "itree_insert of lock (int=%Ld,%Ld,rw=%s) "
			"failed\n", linked_itree_p->itree_link.start,
			linked_itree_p->itree_link.end,
			((io_type == PVFS_IO_READ) ? "r" : "w"));
		ret = -EINVAL;
		goto add_unlock_exit;
	    }
	    
	    /* Add the lock to the tmp_lock_head (later properly
	     * linked to the lock request)*/
	    qlist_add_tail(&(linked_itree_p->list_link), &tmp_lock_head);
	    final_physical_off = 
		lock_result.offset_array[i] + lock_result.size_array[i];
	
	    nb_bytes_granted += lock_result.size_array[i];
	}
    } while ((ret == 0) && (!PINT_REQUEST_DONE(file_req_state)));
    
    /* Add the req to the all_req queue and the secondary appropriate
     * queue (queued or granted) */
    *granted_bytes_p = nb_bytes_granted;
    gossip_debug(GOSSIP_LOCK_DEBUG, "add_lock_req: granted %Ld actual bytes "
		 "of %Ld requested bytes and reached logical offset %Ld\n",
		 *granted_bytes_p, aggregate_size, 
		 (*fdata_p->dist->methods->physical_to_logical_offset)
		 (fdata_p->dist->params,
		  fdata_p,
		  final_physical_off));
    if (nb_bytes_granted > nb_bytes)
    {
	fprintf(stdout, "Error - nb_bytes_granted (%Ld) > nb_bytes (%Ld)\n",
		nb_bytes_granted, nb_bytes);
	ret = -1;
	goto add_unlock_exit;
    }
    lock_req_p = (lock_req_t *) calloc(1, sizeof(lock_req_t));
    lock_req_p->req_id = lock_req_id++;
    *req_id = lock_req_id - 1;
    lock_req_p->req_bytes = nb_bytes;
    lock_req_p->total_bytes = aggregate_size;
    lock_req_p->io_type = io_type;
    lock_req_p->file_req_state = file_req_state;
    lock_req_p->file_req = file_req;
    lock_req_p->file_req_offset = file_req_offset;
    qlist_add_tail(&(lock_req_p->all_req_link), &(lock_node_p->all_req));
    qlist_replace(&(lock_req_p->lock_head), &tmp_lock_head);
    if (ret == 0 && (PINT_REQUEST_DONE(file_req_state)))
    {
	lock_req_p->granted_req_link.key = lock_req_p->req_id;
	rbtree_insert(&(lock_node_p->granted_req), &RBTREE_NIL,
		      &(lock_req_p->granted_req_link));
	lock_req_p->lock_req_status = ALL_LOCKS_GRANTED;
    }
    else
    {
	/* Reset the state to where we last left off */
	PVFS_offset final_logical_off = 
	    (*fdata_p->dist->methods->physical_to_logical_offset)
	    (fdata_p->dist->params,
	     fdata_p,
	     final_physical_off);
	PINT_REQUEST_STATE_RESET(lock_req_p->file_req_state);
	PINT_REQUEST_STATE_SET_TARGET(lock_req_p->file_req_state,
				      final_logical_off);
	
	qlist_add_tail(&(lock_req_p->queued_req_link), 
		       &(lock_node_p->queued_req));
	lock_req_p->lock_req_status = INCOMPLETE;
    }
    
  add_unlock_exit:
    gen_mutex_unlock(lock_file_table_mutex);
    
    return ret;
}

/* del_lock_req - Remove a certain amount of nb_bytes from the lock
 * req.  If nb_bytes == -1, remove the entire lock request.  Return -1
 * for en.  If all n*/

int del_lock_req(PVFS_object_ref *object_ref_p,
		 PVFS_id_gen_t req_id, 
		 PVFS_size nb_bytes,
		 PVFS_size *total_bytes_released_p)
{
    int ret = 0;
    enum PVFS_io_type io_type;
    struct qlist_head *hash_link_p = NULL, *pos = NULL, 
	*scratch = NULL, *lock_head_p = NULL;
    rbtree_t *rbtree_p = &RBTREE_NIL;

    lock_req_t *lock_req_p = NULL;
    PVFS_size bytes_released = 0;
    linked_itree_t *linked_itree_p = NULL;
    lock_node_t *lock_node_p = NULL;


    if (!LOCK_FILE_TABLE_INITIALIZED())
    {
	fprintf(stdout, "del_lock_req: Impossible lock_file_table not "
		"initialized\n");
	return -1;
    }

    gen_mutex_lock(lock_file_table_mutex);

    /* Find the lock_node for the object_ref_p. */
    hash_link_p = qhash_search(lock_file_table, object_ref_p);
    if (!hash_link_p)
    {
	gossip_debug(GOSSIP_LOCK_DEBUG, 
		     "del_lock_req: Lock node not found!\n");
	ret = -1;
	goto del_unlock_exit;

    }
    lock_node_p = qlist_entry(hash_link_p, lock_node_t, hash_link);

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

	gossip_debug(GOSSIP_LOCK_DEBUG, "del_lock_req: req_id=%Ld not "
		     "found in granted list\n", req_id);

	qhash_for_each(pos, &(lock_node_p->queued_req))
	{
	    tmp_lock_req_p = qlist_entry(pos, lock_req_t, queued_req_link); 
	    if (tmp_lock_req_p->req_id == req_id)
		lock_req_p = tmp_lock_req_p;
	}
	if (lock_req_p == NULL)
	{
	    fprintf(stdout, "del_lock_req: req_id=%Ld not found in "
		    "granted or queued list\n", req_id);
	    ret = -1;
	    goto del_unlock_exit;
	}
    }    
    lock_head_p = &(lock_req_p->lock_head);
    io_type = lock_req_p->io_type;
    
    /* Delete and/or change locks in the correct lock tree by
     * nb_bytes. */
    if (nb_bytes == -1)
    {
	qlist_for_each_safe(pos, scratch, lock_head_p)
	{
	    linked_itree_p = itree_entry(pos, linked_itree_t, list_link);
	    qlist_del(&(linked_itree_p->list_link));
	    itree_delete(
		((io_type == PVFS_IO_READ) ? 
		 &(lock_node_p->read_itree) : &(lock_node_p->write_itree)),
		&ITREE_NIL, &(linked_itree_p->itree_link), 
		linked_itree_cpy_fn);
	    free(linked_itree_p);
	}
    }
    else
    {
	struct qlist_head *tmp_lock_p = NULL, 
	    *last_lock_p = lock_head_p->prev;
	while (bytes_released < nb_bytes)
	{
	    linked_itree_p = 
		itree_entry(last_lock_p, linked_itree_t, list_link);
	    
	    if ((nb_bytes - bytes_released) >= 
		(linked_itree_p->itree_link.end - 
		 linked_itree_p->itree_link.start + 1))
	    
	    {
		bytes_released += linked_itree_p->itree_link.end -
		    linked_itree_p->itree_link.start + 1;
		tmp_lock_p = last_lock_p->prev;
		qlist_del(last_lock_p);
		itree_delete(
		    ((io_type == PVFS_IO_READ) ? &(lock_node_p->read_itree) :
		     &(lock_node_p->write_itree)), &ITREE_NIL,
		    &(linked_itree_p->itree_link),
		    linked_itree_cpy_fn);
		free(last_lock_p);
		last_lock_p = tmp_lock_p;

	    }
	    else
	    {
		linked_itree_p->itree_link.end -=
		    nb_bytes - bytes_released;
		
		/* Now, fix your own max and go up the tree */
		itree_max_update_self(&(linked_itree_p->itree_link),
				      &ITREE_NIL);
		itree_max_update_parent(&(linked_itree_p->itree_link),
					&ITREE_NIL);
		bytes_released = nb_bytes;
	    }
	}
    }

    /* Remove lock_req from its respective queue if nb_bytes == -1.
     * If the lock_req was ALL_LOCKS_GRANTED, change its status to
     * INCOMPLETE, and add it to the queued_list in the proper
     * location (i.e. search the all_req queue to see where to add
     * it). */
    if (nb_bytes == -1)
    {
	if (lock_req_p->lock_req_status == INCOMPLETE)
	    qlist_del(&(lock_req_p->queued_req_link));
	else
	    rbtree_delete(&lock_node_p->granted_req, &RBTREE_NIL,
			  &(lock_req_p->granted_req_link), 
			  rbtree_lock_req_cpy_fn);

	/* Delete the lock req from the all_req queue and possibly
	 * delete the lock_node if there are no more entries for
	 * it. */
	qlist_del(&(lock_req_p->all_req_link));
	free(lock_req_p);
	if (qlist_empty(&(lock_node_p->all_req)))
	    qlist_del(&(lock_node_p->hash_link));
    }
    else
    {
	if (lock_req_p->lock_req_status == ALL_LOCKS_GRANTED)
	{
	    lock_req_t *tmp_lock_req_p = NULL, *found_lock_req_p = NULL;
	    struct qlist_head *tmp_req_p = &(lock_req_p->all_req_link);
	    lock_req_p->lock_req_status = INCOMPLETE;
	    rbtree_delete(&(lock_node_p->granted_req), &RBTREE_NIL,
			  &(lock_req_p->granted_req_link), 
			  rbtree_lock_req_cpy_fn);
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
			  &(lock_node_p->queued_req));
	    else
		qlist_add_tail(&(lock_req_p->queued_req_link), 
			       &(lock_node_p->queued_req));
	}
    }
    
  del_unlock_exit:
    gen_mutex_unlock(lock_file_table_mutex);

    if (nb_bytes == -1 && ret == 0)
	*total_bytes_released_p = -1;
    else
	*total_bytes_released_p = bytes_released;
    
    /* If any bytes have been revoked, all queued locks need to be
     * progress as far as possible */
    if (nb_bytes > 0)
    {
	/* This function will make more sense in PVFS2. */
    }
    return ret;
}

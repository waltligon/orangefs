/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * functions to handle storage of  
 * operation info structures for the BMI
 *
 * This is built on top of the quicklist.[ch] files
 */

/*
 * NOTE: I am not locking any data structures in here!  It is assumed
 * that the calling process is locking the appropriate operation
 * structures before calling any of the functions provided here.
 */

#include <stdlib.h>
#include <errno.h>

#include "bmi-method-support.h"
#include "op-list.h"
#include "gossip.h"


/***************************************************************
 * Function prototypes
 */

static void gossip_print_op(method_op_p print_op);
static int op_list_cmp_key(struct op_list_search_key *my_key,
			   method_op_p my_op);

/***************************************************************
 * Visible functions
 */

/*
 * op_list_dump()
 *
 * dumps the contents of the op list to stderr
 *
 * returns 0 on success, -errno on failure
 */
void op_list_dump(op_list_p olp)
{
    op_list_p tmp_entry = NULL;

    gossip_err("op_list_dump():\n");
    qlist_for_each(tmp_entry, olp)
    {
	gossip_print_op(qlist_entry(tmp_entry, struct method_op,
				    op_list_entry));
    }
}


/*
 * op_list_count()
 *
 * counts the number of entries in the op_list
 *
 * returns integer number of items on success, -errno on failure
 */
int op_list_count(op_list_p olp)
{
    int count = 0;
    op_list_p tmp_entry = NULL;
    qlist_for_each(tmp_entry, olp)
    {
	count++;
    }
    return (count);
}


/*
 * op_list_new()
 *
 * creates a new operation list.
 *
 * returns pointer to an empty list or NULL on failure.
 */
op_list_p op_list_new(void)
{
    struct qlist_head *tmp_op_list = NULL;

    tmp_op_list = (struct qlist_head *) malloc(sizeof(struct qlist_head));
    if (tmp_op_list)
    {
	INIT_QLIST_HEAD(tmp_op_list);
    }

    return (tmp_op_list);
}

/*
 * op_list_add()
 *
 * adds an operation to the list.  
 *
 * returns 0 on success, -1 on failure
 */
void op_list_add(op_list_p olp,
		 method_op_p oip)
{
    /* note we are adding to tail:
     * most modules will want to preserve FIFO ordering when searching
     * through op_lists for work to do.
     */
    qlist_add_tail(&(oip->op_list_entry), olp);
}

/*
 * op_list_cleanup()
 *
 * frees up the list and all data associated with it.
 *
 * no return values
 */
void op_list_cleanup(op_list_p olp)
{
    op_list_p iterator = NULL;
    op_list_p scratch = NULL;
    method_op_p tmp_method_op = NULL;

    qlist_for_each_safe(iterator, scratch, olp)
    {
	tmp_method_op = qlist_entry(iterator, struct method_op,
				    op_list_entry);
	bmi_dealloc_method_op(tmp_method_op);
    }
    free(olp);
    olp = NULL;
}

/* op_list_empty()
 *
 * checks to see if the operation list is empty or not.
 *
 * returns 1 if empty, 0 if items are present.
 */
int op_list_empty(op_list_p olp)
{
    return (qlist_empty(olp));
}


/*
 * op_list_remove()
 *
 * Removes the network operation from the given list.
 * DOES NOT destroy the operation.
 *
 * returns 0 on success, -errno on failure.
 */
void op_list_remove(method_op_p oip)
{
    qlist_del(&(oip->op_list_entry));
}


/* op_list_search()
 *
 * Searches the operation list based on parameters in the
 * op_list_search_key structure.  Returns first match.
 *
 * returns pointer to operation on success, NULL on failure.
 */
method_op_p op_list_search(op_list_p olp,
			   struct op_list_search_key *key)
{
    op_list_p tmp_entry = NULL;
    qlist_for_each(tmp_entry, olp)
    {
	if (!(op_list_cmp_key(key, qlist_entry(tmp_entry, struct method_op,
					       op_list_entry))))
	{
	    return (qlist_entry(tmp_entry, struct method_op, op_list_entry));
	}
    }
    return (NULL);
}


/* op_list_shownext()
 *
 * shows the next entry in an op list; does not remove the entry
 *
 * returns pointer to method op on success, NULL on failure
 */
method_op_p op_list_shownext(op_list_p olp)
{
    if (olp->next == olp)
    {
	return (NULL);
    }
    return (qlist_entry(olp->next, struct method_op, op_list_entry));
}

/****************************************************************
 * Internal utility functions
 */


/* 
 * op_list_cmp_key()
 *
 * compares a key structure against an operation to see if they match.  
 *
 * returns 0 if match is found, 1 otherwise
 */
static int op_list_cmp_key(struct op_list_search_key *my_key,
			   method_op_p my_op)
{

    if (my_key->msg_tag_yes && (my_key->msg_tag != my_op->msg_tag))
    {
	return (1);
    }
    if (my_key->op_id_yes && (my_key->op_id != my_op->op_id))
    {
	return (1);
    }
    if (my_key->method_addr_yes)
    {
        if(my_key->method_addr == my_op->addr)
        {
            /* normal case */
        }
        else if(my_op->addr->primary && 
            my_key->method_addr == my_op->addr->primary)
        {
            /* swap address in the op to match addr we are using */
            my_op->addr = my_op->addr->primary;
        }
        else if(my_op->addr->secondary &&
            my_key->method_addr == my_op->addr->secondary)
        {
            /* swap address in the op to match addr we are using */
            my_op->addr = my_op->addr->secondary;
        }
        else
        {
            return(1);
        }
    }
    if (my_key->class_yes && (my_key->class != my_op->class))
    {
        return(1);
    }
    return (0);
}


static void gossip_print_op(method_op_p print_op)
{

    gossip_err("Operation:\n------------\n");
    gossip_err("  op_id: %ld\n", (long) print_op->op_id);
    gossip_err("  send_recv: %d\n", (int) print_op->send_recv);
    gossip_err("  msg_tag: %d\n", (int) print_op->msg_tag);
    gossip_err("  error_code: %d\n", (int) print_op->error_code);
    gossip_err("  amt_complete: %ld\n", (long) print_op->amt_complete);
    gossip_err("  buffer: %p\n", print_op->buffer);
    gossip_err("  actual size: %ld\n", (long) print_op->actual_size);
    gossip_err("  expected size: %ld\n", (long) print_op->expected_size);
    gossip_err("  addr: %p\n", print_op->addr);
    gossip_err("  mode: %d\n", (int) print_op->mode);

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

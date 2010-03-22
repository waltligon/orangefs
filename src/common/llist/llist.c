/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include "llist.h"

/* PINT_llist_new() - returns a pointer to an empty list
 */
PINT_llist_p PINT_llist_new(
    void)
{
    PINT_llist_p l_p;

    if (!(l_p = (PINT_llist_p) malloc(sizeof(PINT_llist))))
	return (NULL);
    l_p->next = l_p->item = NULL;
    return (l_p);
}

/* PINT_llist_empty() - determines if a list is empty
 *
 * Returns 0 if not empty, 1 if empty
 */
int PINT_llist_empty(
    PINT_llist_p l_p)
{
    if (l_p->next == NULL)
	return (1);
    return (0);
}

/* PINT_llist_add_to_tail() - adds an item to a list
 *
 * Requires that a list have already been created
 * Puts item at tail of list
 * Returns 0 on success, -1 on failure
 */
int PINT_llist_add_to_tail(
    PINT_llist_p l_p,
    void *item)
{
    PINT_llist_p new_p;

    if (!l_p)	/* not a list */
	return (-1);

    /* NOTE: first "item" pointer in list is _always_ NULL */

    if ((new_p = (PINT_llist_p) malloc(sizeof(PINT_llist))) == NULL)
	return -1;
    new_p->next = NULL;
    new_p->item = item;
    while (l_p->next)
	l_p = l_p->next;
    l_p->next = new_p;
    return (0);
}

/* PINT_llist_add_to_head() - adds an item to a list
 *
 * Requires that a list have already been created
 * Puts item at head of list
 * Returns 0 on success, -1 on failure
 */
int PINT_llist_add_to_head(
    PINT_llist_p l_p,
    void *item)
{
    PINT_llist_p new_p;

    if (!l_p)	/* not a list */
	return (-1);

    /* NOTE: first "item" pointer in list is _always_ NULL */

    if ((new_p = (PINT_llist_p) malloc(sizeof(PINT_llist))) == NULL)
	return -1;
    new_p->next = l_p->next;
    new_p->item = item;
    l_p->next = new_p;
    return (0);
}

/* PINT_llist_head() - returns a pointer to the item at the head of the
 * list
 *
 * Returns NULL on error or if no items are in list
 */
void *PINT_llist_head(
    PINT_llist_p l_p)
{
    if (!l_p || !l_p->next)
	return (NULL);
    return (l_p->next->item);
}

/* PINT_llist_tail() - returns pointer to the item at the tail of the list
 * 
 * Returns NULL on error or if no items are in list
 */
void *PINT_llist_tail(
    PINT_llist_p l_p)
{
    if (!l_p || !l_p->next)
	return (NULL);
    while (l_p->next)
	l_p = l_p->next;
    return (l_p->item);
}

/* PINT_llist_search() - finds first match from list and returns pointer
 *
 * Returns NULL on error or if no match made
 * Returns pointer to item if found
 */
void *PINT_llist_search(
    PINT_llist_p l_p,
    void *key,
    int (*comp) (void *,
		 void *))
{
    if (!l_p || !l_p->next || !comp)	/* no or empty list */
	return (NULL);

    for (l_p = l_p->next; l_p; l_p = l_p->next)
    {
	/* NOTE: "comp" function must return _0_ if a match is made */
	if (!(*comp) (key, l_p->item))
	    return (l_p->item);
    }
    return (NULL);
}

/* PINT_llist_rem() - removes first match from list
 *
 * Returns NULL on error or not found, or a pointer to item if found
 * Removes item from list, but does not attempt to free memory
 *   allocated for item
 */
void *PINT_llist_rem(
    PINT_llist_p l_p,
    void *key,
    int (*comp) (void *,
		 void *))
{
    if (!l_p || !l_p->next || !comp)	/* no or empty list */
	return (NULL);

    for (; l_p->next; l_p = l_p->next)
    {
	/* NOTE: "comp" function must return _0_ if a match is made */
	if (!(*comp) (key, l_p->next->item))
	{
	    void *i_p = l_p->next->item;
	    PINT_llist_p rem_p = l_p->next;

	    l_p->next = l_p->next->next;
	    free(rem_p);
	    return (i_p);
	}
    }
    return (NULL);
}

/* PINT_llist_count()
 *
 * counts items in the list
 * NOTE: this is a slow count- it works by iterating through the whole
 * list
 *
 * returns count on success, -errno on failure
 */
int PINT_llist_count(
    PINT_llist_p l_p)
{
    int count = 0;

    if (!l_p)
	return (-1);

    for (l_p = l_p->next; l_p; l_p = l_p->next)
    {
	count++;
    }

    return (count);
}

/* PINT_llist_doall() - passes through list calling function "fn" on all 
 *    items in the list
 *
 * Returns -1 on error, 0 on success
 */
int PINT_llist_doall(
    PINT_llist_p l_p,
    int (*fn) (void *))
{
    PINT_llist_p tmp_p;

    if (!l_p || !l_p->next || !fn)
	return (-1);
    for (l_p = l_p->next; l_p;)
    {
	tmp_p = l_p->next;	/* save pointer to next element in case the
				 * function destroys the element pointed to
				 * by l_p...
				 */
	(*fn) (l_p->item);
	l_p = tmp_p;
    }
    return (0);
}

/* PINT_llist_doall_arg() - passes through list calling function "fn" on all 
 *    items in the list; passes through an argument to the function
 *
 * Returns -1 on error, 0 on success
 */
int PINT_llist_doall_arg(
    PINT_llist_p l_p,
    int (*fn) (void *item,
	       void *arg),
    void *arg)
{
    PINT_llist_p tmp_p;

    if (!l_p || !l_p->next || !fn)
	return (-1);
    for (l_p = l_p->next; l_p;)
    {
	tmp_p = l_p->next;	/* save pointer to next element in case the
				 * function destroys the element pointed to
				 * by l_p...
				 */
	(*fn) (l_p->item, arg);
	l_p = tmp_p;
    }
    return (0);
}

/* PINT_llist_free() - frees all memory associated with a list
 *
 * Relies on passed function to free memory for an item
 */
void PINT_llist_free(
    PINT_llist_p l_p,
    void (*fn) (void *))
{
    PINT_llist_p tmp_p;

    if (!l_p || !fn)
	return;

    /* There is never an item in first entry */
    tmp_p = l_p;
    l_p = l_p->next;
    free(tmp_p);
    while (l_p)
    {
	(*fn) (l_p->item);
	tmp_p = l_p;
	l_p = l_p->next;
	free(tmp_p);
    }
}

/*
 * PINT_llist_next()
 * 
 * returns the next list entry in the list.  WARNING- use this function
 * carefully- it is sortof a hack around the interface.
 *
 * returns a pointer to the next entry on success, NULL on end or
 * failure.
 */
PINT_llist_p PINT_llist_next(
    PINT_llist_p entry)
{

    if (!entry)
    {
	return (NULL);
    }

    return (entry->next);
}


/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 * End:
 */

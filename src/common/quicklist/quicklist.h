/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Derived from linked qlist code taken from linux 2.4.3 (list.h) */

/*
 * Simple doubly linked qlist implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole qlists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

#ifndef QUICKLIST_H
#define QUICKLIST_H

struct qlist_head {
	struct qlist_head *next, *prev;
};

#define QLIST_HEAD_INIT(name) { &(name), &(name) }

#define QLIST_HEAD(name) \
	struct qlist_head name = QLIST_HEAD_INIT(name)

#define INIT_QLIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/*
 * Insert a new entry between two known consecutive entries. 
 *
 * This is only for internal qlist manipulation where we know
 * the prev/next entries already!
 */
static __inline__ void __qlist_add(struct qlist_head * new,
	struct qlist_head * prev,
	struct qlist_head * next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/**
 * qlist_add - add a new entry
 * @new: new entry to be added
 * @head: qlist head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static __inline__ void qlist_add(struct qlist_head *new, struct qlist_head *head)
{
	__qlist_add(new, head, head->next);
}

/**
 * qlist_add_tail - add a new entry
 * @new: new entry to be added
 * @head: qlist head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static __inline__ void qlist_add_tail(struct qlist_head *new, struct qlist_head *head)
{
	__qlist_add(new, head->prev, head);
}

/*
 * Delete a qlist entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal qlist manipulation where we know
 * the prev/next entries already!
 */
static __inline__ void __qlist_del(struct qlist_head * prev,
				  struct qlist_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * qlist_del - deletes entry from qlist.
 * @entry: the element to delete from the qlist.
 * Note: qlist_empty on entry does not return true after this, the entry is in an undefined state.
 */
static __inline__ void qlist_del(struct qlist_head *entry)
{
	__qlist_del(entry->prev, entry->next);
}

/**
 * qlist_del_init - deletes entry from qlist and reinitialize it.
 * @entry: the element to delete from the qlist.
 */
static __inline__ void qlist_del_init(struct qlist_head *entry)
{
	__qlist_del(entry->prev, entry->next);
	INIT_QLIST_HEAD(entry); 
}

/**
 * qlist_empty - tests whether a qlist is empty
 * @head: the qlist to test.
 */
static __inline__ int qlist_empty(struct qlist_head *head)
{
	return head->next == head;
}

/**
 * qlist_splice - join two qlists
 * @qlist: the new qlist to add.
 * @head: the place to add it in the first qlist.
 */
static __inline__ void qlist_splice(struct qlist_head *qlist, struct qlist_head *head)
{
	struct qlist_head *first = qlist->next;

	if (first != qlist) {
		struct qlist_head *last = qlist->prev;
		struct qlist_head *at = head->next;

		first->prev = head;
		head->next = first;

		last->next = at;
		at->prev = last;
	}
}

/**
 * qlist_entry - get the struct for this entry
 * @ptr:	the &struct qlist_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the qlist_struct within the struct.
 */
#define qlist_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * qlist_for_each	-	iterate over a qlist
 * @pos:	the &struct qlist_head to use as a loop counter.
 * @head:	the head for your qlist.
 */
#define qlist_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#endif /* QUICKLIST_H */

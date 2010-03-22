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

#include <stdlib.h>

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
 * qlist_pop - pop the first item off the list and return it
 * @head: qlist to modify
 */
static __inline__ struct qlist_head* qlist_pop(struct qlist_head *head)
{
    struct qlist_head *item = NULL;

    if (!qlist_empty(head))
    {
        item = head->next;
        qlist_del(item);
    }

    return item;
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
    ((type *)((char *)(ptr)-(unsigned long)((&((type *)0)->member))))

/**
 * qlist_for_each	-	iterate over a qlist
 * @pos:	the &struct qlist_head to use as a loop counter.
 * @head:	the head for your qlist.
 */
#define qlist_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * list_for_each_safe - iterate over a list safe against 
 *     removal of list entry
 * @pos:  the &struct list_head to use as a loop counter.
 * @n:    another &struct list_head to use as temporary storage
 * @head: the head for your list.
 */
#define qlist_for_each_safe(pos, scratch, head) \
    for (pos = (head)->next, scratch = pos->next; pos != (head);\
         pos = scratch, scratch = pos->next)

/**
 * qlist_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define qlist_for_each_entry(pos, head, member)				\
    for (pos = qlist_entry((head)->next, typeof(*pos), member);	\
         &pos->member != (head); 					\
         pos = qlist_entry(pos->member.next, typeof(*pos), member))	\

/**
 * qlist_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop counter.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define qlist_for_each_entry_safe(pos, n, head, member)			\
    for (pos = qlist_entry((head)->next, typeof(*pos), member),	\
         n = qlist_entry(pos->member.next, typeof(*pos), member);	\
         &pos->member != (head); 					\
         pos = n, n = qlist_entry(n->member.next, typeof(*n), member))

static inline int qlist_exists(struct qlist_head *list, struct qlist_head *qlink)
{
    struct qlist_head *pos;

    if(qlist_empty(list)) return 0;

    qlist_for_each(pos, list)
    {
        if(pos == qlink)
        {
            return 1;
        }
    }
    return 0;
}

static inline int qlist_count(struct qlist_head *list)
{
    struct qlist_head *pos;
    int count = 0;

    pos = list->next;

    while(pos != list)
    {
        ++count;
        pos = pos->next;
    }

    return count;
}

static inline struct qlist_head * qlist_find(
    struct qlist_head *list,
    int (*compare)(struct qlist_head *, void *),
    void *ptr)
{
    struct qlist_head *pos;
    qlist_for_each(pos, list)
    {
        if(compare(pos, ptr))
        {
            return pos;
        }
    }
    return NULL;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif /* QUICKLIST_H */

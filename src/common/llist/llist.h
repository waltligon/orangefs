/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#ifndef LLIST_H
#define LLIST_H

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#define llist_add(__llist_p, __void_p) \
	llist_add_to_head((__llist_p), (__void_p))

/* STRUCTURES */
typedef struct llist llist, *llist_p;

struct llist
{
    void *item;
    llist_p next;
};

/* PROTOTYPES */
llist_p llist_new(
    void);
int llist_empty(
    llist_p);
int llist_add_to_head(
    llist_p,
    void *);
int llist_add_to_tail(
    llist_p,
    void *);
int llist_doall(
    llist_p,
    int (*fn) (void *));
int llist_doall_arg(
    llist_p l_p,
    int (*fn) (void *item,
	       void *arg),
    void *arg);
void llist_free(
    llist_p,
    void (*fn) (void *));
void *llist_search(
    llist_p,
    void *,
    int (*comp) (void *,
		 void *));
void *llist_rem(
    llist_p,
    void *,
    int (*comp) (void *,
		 void *));
void *llist_head(
    llist_p);
void *llist_tail(
    llist_p);
int llist_count(
    llist_p l_p);
llist_p llist_next(
    llist_p entry);


#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 * End:
 */

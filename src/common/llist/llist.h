/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#ifndef LLIST_H
#define LLIST_H

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif


#define PINT_llist_add(__llist_p, __void_p) \
	PINT_llist_add_to_head((__llist_p), (__void_p))

/* STRUCTURES */
typedef struct PINT_llist PINT_llist, *PINT_llist_p;

struct PINT_llist
{
    void *item;
    PINT_llist_p next;
};

/* PROTOTYPES */
PINT_llist_p PINT_llist_new(
    void);
int PINT_llist_empty(
    PINT_llist_p);
int PINT_llist_add_to_head(
    PINT_llist_p,
    void *);
int PINT_llist_add_to_tail(
    PINT_llist_p,
    void *);
int PINT_llist_doall(
    PINT_llist_p,
    int (*fn) (void *));
int PINT_llist_doall_arg(
    PINT_llist_p l_p,
    int (*fn) (void *item,
	       void *arg),
    void *arg);
void PINT_llist_free(
    PINT_llist_p,
    void (*fn) (void *));
void *PINT_llist_search(
    PINT_llist_p,
    void *,
    int (*comp) (void *,
		 void *));
void *PINT_llist_rem(
    PINT_llist_p,
    void *,
    int (*comp) (void *,
		 void *));
void *PINT_llist_head(
    PINT_llist_p);
void *PINT_llist_tail(
    PINT_llist_p);
int PINT_llist_count(
    PINT_llist_p l_p);
PINT_llist_p PINT_llist_next(
    PINT_llist_p entry);


#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 * End:
 */

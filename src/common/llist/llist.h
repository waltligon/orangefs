/*
 * copyright (c) 2001 Clemson University, all rights reserved.
 *
 * Written by Rob Ross and Phil Carns.
 *
 * This program is free software; you can redistribute it and/or
 * modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Rob Ross rross@mcs.anl.gov
 *           Phil Carns  pcarns@parl.clemson.edu
 */

#ifndef LLIST_H
#define LLIST_H

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <gen-locks.h>

#define llist_add(__llist_p, __void_p) \
	llist_add_to_head((__llist_p), (__void_p))

/* STRUCTURES */
typedef struct llist llist, *llist_p;

struct llist {
	void *item;
	llist_p next;
	gen_mutex_t* mut_llist;
	int count;  /* note this is only valid in the first link! */
};

/* PROTOTYPES */
llist_p llist_new(void);
llist_p llist_new_block(int);
void llist_free_block(llist_p, void (*fn)(void*),int);
int llist_empty(llist_p);
int llist_count(llist_p);
int llist_add_to_head(llist_p, void *);
int llist_add_to_tail(llist_p, void *);
int llist_doall(llist_p, int (*fn)(void *));
void llist_free(llist_p, void (*fn)(void*));
void *llist_search(llist_p, void *, int (*comp)(void *, void *));
int llist_search_retval(llist_p, void *, int (*comp)(void *, void *),void **);
void *llist_rem(llist_p, void *, int (*comp)(void *, void *));
void *llist_head(llist_p);
int llist_append(llist_p dest, llist_p src);
llist_p llist_next(llist_p entry);
void* llist_item_from_entry(llist_p entry);


#endif

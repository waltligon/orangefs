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

#include <llist.h>

/****************************************************************
 * visible functions
 */


/* 
 * llist_new()
 * 
 * creates a new list
 *
 * returns a pointer to the new list on success, NULL on failure
 */
llist_p llist_new(void)
{
	llist_p l_p;

	if (!(l_p = (llist_p) malloc(sizeof(llist)))) return(NULL);
	l_p->next = l_p->item = NULL;
	l_p->count = 0;

	l_p->mut_llist = gen_mutex_build();
	if(!l_p->mut_llist){
		free(l_p);
		return(NULL);
	}

	return(l_p);
}

/*llist_new_block( )
 * 
 *Allocates a block of memory of specified size
 *
 *returns a pointer to the memory block
 */
llist_p llist_new_block(int size)
{
	llist_p l_p;
	llist_p tmp_p;
	int index=0;

	if (!(l_p = (llist_p)calloc(size,sizeof(llist)))) 
		return(NULL);
	tmp_p=l_p;
	for(index=0;index < size; index++,tmp_p++)
	{
		tmp_p->next = tmp_p->item = NULL;
		tmp_p->count = 0;

		tmp_p->mut_llist = gen_mutex_build();
		if(!tmp_p->mut_llist)
		{
			free(l_p);
			return(NULL);
		}
	}
	return(l_p);
}


/* 
 * llist_free_block() 
 * 
 * frees all memory associated with a list - relies on passed 
 * function to free memory for an item.Different from the
 * free function in that this does not free the start_p
 * as that is part of a larger chunk of memory that will
 * be freed later as a whole
 * 
 * no return value
 */
void llist_free_block(llist_p l_p, void (*fn)(void*),int alloc_flag)
{
	llist_p tmp_p;
	llist_p start_p;

	if (!l_p || !fn) return;
	start_p = l_p;

	/* There is never an item in first entry */
	gen_mutex_lock(l_p->mut_llist);
	l_p = l_p->next;
	while(l_p) {
		(*fn)(l_p->item);
		tmp_p = l_p;
		l_p = l_p->next;
		free(tmp_p);
		start_p->count--;
	}
	gen_mutex_unlock(start_p->mut_llist);
	gen_mutex_destroy(start_p->mut_llist);
}	


/* 
 * llist_empty() 
 *
 * determines if a list is empty
 *
 * Returns 0 if not empty, 1 if empty
 */
int llist_empty(llist_p l_p)
{

	if(l_p->count == 0){
		return(1);
	}
	return(0);
}

/*
 * llist_count()
 *
 * determines how many items are in the list
 *
 * returns count on success, -errno on failure
 */
int llist_count(llist_p l_p){

	return(l_p->count);
}

/* 
 * llist_add_to_tail() 
 * 
 * adds an item to the tail of a list
 *
 * Returns 0 on success, -errno on failure
 */
int llist_add_to_tail(llist_p l_p, void *item)
{
	llist_p new_p = NULL;
	llist_p tmp_p = NULL;

	if (!l_p) /* not a list */ return(-EINVAL);

	/* NOTE: first "item" pointer in list is _always_ NULL */

	new_p       = (llist_p) malloc(sizeof(llist));
	if(!new_p){
		return(-ENOMEM);
	}
	tmp_p = l_p;
	new_p->next = NULL;
	new_p->item = item;
	gen_mutex_lock(l_p->mut_llist);
	new_p->mut_llist = l_p->mut_llist;
	while (tmp_p->next) tmp_p = tmp_p->next;
	tmp_p->next   = new_p;
	l_p->count++;
	gen_mutex_unlock(l_p->mut_llist);
	return(0);
}

/* 
 * llist_add_to_head() 
 * 
 * adds an item to the head of a list
 *
 * Returns 0 on success, -errno on failure
 */
int llist_add_to_head(llist_p l_p, void *item)
{
	llist_p new_p = NULL;

	if (!l_p) /* not a list */ return(-EINVAL);

	/* NOTE: first "item" pointer in list is _always_ NULL */

	new_p       = (llist_p) malloc(sizeof(llist));
	if(!new_p){
		return(-ENOMEM);
	}
	new_p->item = item;

	gen_mutex_lock(l_p->mut_llist);
	new_p->mut_llist = l_p->mut_llist;
	new_p->next = l_p->next;
	l_p->next   = new_p;
	l_p->count++;
	gen_mutex_unlock(l_p->mut_llist);
	return(0);
}

/* 
 * llist_head() 
 * 
 * returns a pointer to the item at the head of the list
 *
 * returns a pointer to the item on success, NULL on failure
 */
void *llist_head(llist_p l_p)
{
	if (!l_p || !l_p->next) return(NULL);
	gen_mutex_lock(l_p->mut_llist);
	l_p = l_p->next;
	gen_mutex_unlock(l_p->mut_llist);
	return(l_p->item);
}

/* 
 * llist_search() 
 * 
 * finds first match from list and returns pointer - determines whether
 * or not a match exists by using the passed in comparison function
 *
 * Returns pointer to item on success, NULL on failure
 */
void *llist_search(llist_p l_p, void *key, int (*comp)(void *, void *))
{
	llist_p start_p = l_p;

	if (!l_p || !l_p->next || !comp) /* no or empty list */ return(NULL);

	gen_mutex_lock(l_p->mut_llist);
	for (l_p = l_p->next; l_p; l_p = l_p->next) {
		/* NOTE: "comp" function must return _0_ if a match is made */
		if (!(*comp)(key, l_p->item)){
			gen_mutex_unlock(l_p->mut_llist);
			return(l_p->item);
		}
	}
	gen_mutex_unlock(start_p->mut_llist);
	return(NULL);
}

/* 
 * llist_search_retval() 
 * 
 * finds first match from list and returns pointer - determines whether
 * or not a match exists by using the passed in comparison function
 *
 * Returns 0 if search succeeds or fails and 1 if an error occurs.
 * If search was successful then item_found contains a pointer to
 * the item else it has a NULL
 */
int llist_search_retval(llist_p l_p, void *key, int (*comp)(void *, void *),void **item_found)
{
	llist_p start_p = l_p;

	if (!l_p || !l_p->next || !comp) /* no or empty list */ 
		return(1);

	gen_mutex_lock(l_p->mut_llist);
	for (l_p = l_p->next; l_p; l_p = l_p->next) {
		/* NOTE: "comp" function must return _0_ if a match is made */
		if (!(*comp)(key, l_p->item)){
			gen_mutex_unlock(l_p->mut_llist);
			*item_found=l_p->item;/*if item found fill up the found var*/
			return(0);
		}
	}
	gen_mutex_unlock(start_p->mut_llist);
	*item_found=NULL;  /*If item not found fill NULL*/
	return(0);
}

/* 
 * llist_rem() 
 * 
 * removes first match from list - does not destroy it
 *
 * returns a pointer to the item on success, NULL on failure
 */
void *llist_rem(llist_p l_p, void *key, int (*comp)(void *, void *))
{
	llist_p start_p = l_p;

	if (!l_p || !l_p->next || !comp) /* no or empty list */ return(NULL);
	
	gen_mutex_lock(l_p->mut_llist);
	for (; l_p->next; l_p = l_p->next) {
		/* NOTE: "comp" function must return _0_ if a match is made */
		if (!(*comp)(key, l_p->next->item)) {
			void *i_p = l_p->next->item;
			llist_p rem_p = l_p->next;

			l_p->next = l_p->next->next;
			free(rem_p);
			start_p->count--;
			gen_mutex_unlock(l_p->mut_llist);
			return(i_p);
		}
	}
	gen_mutex_unlock(l_p->mut_llist);
	return(NULL);
}

/* 
 * llist_doall() 
 *
 * passes through list calling function "fn" on all items in the list
 *
 * Returns 0 on success, -errno on failure
 */
int llist_doall(llist_p l_p, int (*fn)(void *))
{
	llist_p tmp_p;
	llist_p orig_p = l_p;

	if (!l_p || !l_p->next || !fn) return(-1);
	gen_mutex_lock(orig_p->mut_llist);
	for (l_p = l_p->next; l_p;) {
		tmp_p = l_p->next; /* save pointer to next element in case the
		                    * function destroys the element pointed to
								  * by l_p...
								  */
		(*fn)(l_p->item);
		l_p = tmp_p;
	}
	gen_mutex_unlock(orig_p->mut_llist);
	return(0);
}

/* 
 * llist_free() 
 * 
 * frees all memory associated with a list - relies on passed 
 * function to free memory for an item
 *
 * no return value
 */
void llist_free(llist_p l_p, void (*fn)(void*))
{
	llist_p tmp_p;
	llist_p start_p;

	if (!l_p || !fn) return;
	start_p = l_p;

	/* There is never an item in first entry */
	gen_mutex_lock(l_p->mut_llist);
	l_p = l_p->next;
	while(l_p) {
		(*fn)(l_p->item);
		tmp_p = l_p;
		l_p = l_p->next;
		free(tmp_p);
		start_p->count--;
	}
	gen_mutex_unlock(start_p->mut_llist);
	gen_mutex_destroy(start_p->mut_llist);
	free(start_p);
}

/*
 * llist_append()
 *
 * combines two lists into one list.
 *
 * returns 0 on success, -errno on failure
 */
int llist_append(llist_p dest, llist_p src){

	llist_p end_p = NULL;
	int tmp_count = 0;

	if((!dest) || (!src)){
		return(-EINVAL);
	}

	tmp_count = llist_count(src);

	/* does the src list have anything in it? */
	if(tmp_count < 1){
		/* nothing to do, except destroy the src list */
		gen_mutex_destroy(src->mut_llist);
		free(src);
		return(0);
	}

	gen_mutex_lock(dest->mut_llist);
	gen_mutex_lock(src->mut_llist);
	/* traverse to the end destination list */
	end_p = dest;
	while(end_p->next){
		end_p = end_p->next;
	}

	end_p->next = src->next;
	dest->count += tmp_count;

	gen_mutex_unlock(src->mut_llist);
	gen_mutex_unlock(dest->mut_llist);
	gen_mutex_destroy(src->mut_llist);

	free(src);

	return(0);
}                      

/*
 * llist_next()
 * 
 * returns the next list entry in the list.  WARNING- use this function
 * carefully- it is sortof a hack around the interface.
 *
 * returns a pointer to the next entry on success, NULL on end or
 * failure.
 */
llist_p llist_next(llist_p entry){

	if(!entry){
		return(NULL);
	}

	return(entry->next);
}

/*
 * llist_item_from_entry()
 *
 * returns the item stored in a given list entry.  WARNING- use
 * carefully.  Like the above function it is a bit of a hack around the
 * llist interface.
 *
 * returns a pointer to the item on success, NULL on failure.
 */
void* llist_item_from_entry(llist_p entry){

	if(!entry){
		return(NULL);
	}

	return(entry->item);
}

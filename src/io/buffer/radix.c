#include <stdlib.h>
#include <stdio.h>
#include "radix.h"

/*
 * This file is download at http://www.cosc.canterbury.ac.nz/~tad/alg/dict/
 *  
 * The disclaimer information of this file can be found at 
 * Disclaimer
 *   You agree that this repository contains non-commercially developed
 *   algorithms and that the source code and related files may contain errors.
 *   In no event will the provider of this repository be held responsible
 *   for any damages resulting from the source code and related files
 *   supplied by this repository or the use thereof.  You acknowledge that
 *   you use all source code and related files supplied by this repository
 *   at you own risk.
 *
 *  Tadao Takaoka, Professor, at Department of Computer Science,
 *  University of Canterbury, Private Bag 4800, Christchurch, New Zealand
 *  maintains the above links.
 */


/* Modifications made by Jiesheng Wu are as follows:
 * (1) bug fixes in rst_delete;
 * (2) use "unsigned long index" as the search key.
 * TODO: (1) to remove malloc and free in insert and free 
 * as much as possible.
 *      (2) This is not an efficient implementation. 
 */


/* Prototypes for functions only visible within this file. */
void rst_free_dfs(rst_node_t *p);

/* rst_alloc() - Allocates space for a radix search tree and returns a
 * pointer to it. 
 */
rst_t *rst_alloc(unsigned long (* get_value)(const void *), int max_b)
{
    rst_t *t;
    rst_node_t *r;
    
    t = malloc(sizeof(rst_t));
    r = t->root = malloc(sizeof(rst_node_t));
    r->child_links = 0;
    r->a[0] = r->a[1] = NULL;
    t->n = 0;
    t->max_b = max_b;
    t->stack = malloc(max_b * sizeof(rst_node_t *));
    t->path_info = malloc(max_b * sizeof(int));
    t->get_value = get_value;

    return t;
}

/* rst_free() - Frees space used by the radix search trie pointed to by t.
 */
void rst_free(rst_t *t) {
    rst_free_dfs(t->root);
    free(t);
}

void rst_free_dfs(rst_node_t *p) {
    if(p->child_links & 1) rst_free_dfs(p->a[0]);
    if(p->child_links & 2) rst_free_dfs(p->a[1]);
    free(p);
}


/* rst_insert() - Inserts an item into the radix search tree pointed 
 * to by t, according to its key.  The key of an item in the radix
 * search tree must be unique among items in the tree.  If an item with
 * the same key already exists in the tree, a pointer to that item is 
 * returned. 
 * Otherwise, NULL is returned, indicating insertion was successful.
 */
void *rst_insert(rst_t *t, unsigned long key, void *item)
{
    unsigned long key2;
    unsigned int j, mask, stop_mask;
    int i;
    rst_node_t *x, *p;
    void *item2;

    //fprintf(stderr, "rst_insert: key=%ld, item=%p\n", key, item);

    mask = 1 << (i = t->max_b - 1);
    
    /* Locate the insertion position. */
    p = t->root;    
    for(;;) {
	/* Traverse left or right branch, depending on the current key bit, j.
	 * If the branch does not exist, then the insertion position has
	 * been located.  Note that the (j+1)th bit in p->child_links
	 * indicates the kind of pointer for p->a[j].
	 */
	j = (key & mask) >> i;
	if(!(p->child_links & (j+1))) break;
	p = p->a[j];

	/* Move to the next bit. */
	mask >>= 1;
	i--;
    }

    if((item2 = p->a[j])) {
	/* Check that the same item does not already exist in the tree. */
	key2 = t->get_value(item2);
	if(key2 == key) {
	    fprintf(stderr, "rst_insert: error, key2=key=%ld (item=%p, exist=%p)\n", key, item, item2);
	    return p->a[j];  /* Insert failed. */
        }

	/* Create new nodes as necessary, in order to distinguish the key of
	 * the inserted item from the key of the existing item.
	 * The variable stop_mask is used for determining where the 
	 * bits of the two mkeys differ.
	 */
	stop_mask = key ^ key2;  /* Exclusive OR */
	x = malloc(sizeof(rst_node_t));
	p->a[j] = x;
	p->child_links = p->child_links | (j+1);  /* Set bit j. */
	for(;;) {
	    p = x;
	    
	    /* Move to the next bit. */
	    mask >>= 1;
	    i--;
	    j = (key & mask) >> i;

	    /* If the current bit value is different in key and key2, then
	     * no more new nodes need to be created.
	     */
	    if(mask & stop_mask) break;

	    x = malloc(sizeof(rst_node_t));
	    p->a[j] = x;
	    p->a[!j] = NULL;
	    p->child_links = (j+1);  /* Only bit j is set. */
	}

	p->a[j] = item;
	p->a[!j] = item2;
        p->child_links = 0;
    }
    else {
	p->a[j] = item;
    }

    t->n++;

    return NULL;
}


/* rst_find() - Find an item in the radix search tree with the same key as
 * the item pointed to by `key_item'.  Returns a pointer to the item found, or
 * NULL if no item was found.
 */
void *rst_find(rst_t *t, unsigned long index) 
{
    unsigned int j, mask;
    int i;
    rst_node_t *p;

    mask = 1 << (i = t->max_b - 1);
    
    /* Search for the item with key `key'. */
    p = t->root;
    for(;;) {
	/* Traverse left or right branch, depending on the current bit. */
	j = (index & mask) >> i;
	if(!(p->child_links & (j+1))) break;
	p = p->a[j];

	/* Move to the next bit. */
	mask >>= 1;
	i--;
    }

    if(!p->a[j] || t->get_value(p->a[j]) != index ) return NULL;
    
    return p->a[j];
}


/* rst_find() - Find an item in the radix search trie with the same key as
 * the item pointed to by `key_item'.  Returns a pointer to the item found, or
 * NULL if no item was found.
 */
void *rst_find_min(rst_t *t)
{
    unsigned int j;
    rst_node_t *p;

    p = t->root;
    for(;;) {
	j = p->a[0] ? 0 : 1;
	if(!(p->child_links & (j+1))) break;
	p = p->a[j];
    }

    return p->a[j];
}



/* rst_delete() - Delete the first item found in the radix search trie with
 * the same key as the item pointed to by `key_item'.  Returns a pointer to the
 * deleted item, and NULL if no item was found.
 */
void *rst_delete(rst_t *t, unsigned long key) 
{
    rst_node_t *p;
    unsigned int mask, i, j;
    rst_node_t **stack;
    int *path_info, tos;
    void *y, *return_item;

    //fprintf(stderr, "------rst_delete: key=%ld\n", key);
    
    mask = 1 << (i = t->max_b - 1);
    stack = t->stack;
    path_info = t->path_info;
    tos = 0;
    
    /* Search for the item with key `key'. */
    p = t->root;
    for(;;) {
	
	/* Traverse left or right branch, depending on the current bit. */
	j = (key & mask) >> i;
	stack[tos] = p;
	path_info[tos] = j;
	tos++;
	if(!(p->child_links & (j+1))) break;
	p = p->a[j];

	/* Move to the next bit. */
	mask >>= 1;
	i--;
    }

    /* The delete operation fails if the tree contains no items, or no mathcing
     * item was found.
     */
    if(!p->a[j] || t->get_value(p->a[j]) != key) return NULL;
    return_item = p->a[j];

    /* Currently p->a[j] points to the item which is to be removed.  After
     * removing the deleted item, the parent node must also be deleted if its
     * other child pointer is NULL.  This deletion can propagate up to the next
     * level.
     */
    tos--;
    for(;;) {
	if(tos == 0) {  /* Special case: deleteing a child of the root node. */
	    p->a[j] = NULL;
	    p->child_links = p->child_links & ~(j+1);  /* Clear bit j. */
	    return return_item;
	}
	
        if(p->a[!j]) break;
	
	free(p);
        p = stack[--tos];
	j = path_info[tos];
    }
    
    /* For the current node, p, the child pointer p->a[!j] is not NULL.
     * If p->a[!j] points to a node, we set p->a[j] to NULL.  Otherwise if
     * p->a[!j] points to an item, we keep a pointer to the item, and
     * continue to delete parent nodes if thier other child pointer is NULL.
     */
    if(p->child_links & (!j+1)) {  /* p->a[!j] points to a node. */
        p->a[j] = NULL;
    }
    else {  /* p->a[!j] points to an item. */

        /* Delete p, and parent nodes for which the other child pointer is
	 * NULL.
	 */
        y = p->a[!j];
        do {
	    free(p);
            p = stack[--tos];
	    j = path_info[tos];
	    if(p->a[!j]) break;
        } while(tos != 0);

        /* For the current node, p, p->a[!j] is not NULL.  We assign item y to
	 * p->a[j].
	 */
	p->a[j] = y;
    }
    p->child_links = p->child_links & ~(j+1);  /* Clear bit j. */

    return return_item;
}


/* new functions */
void rst_init(rst_t *t, unsigned long (* get_value)(const void *), int max_b)
{
    rst_node_t *r;
     
    r = t->root = malloc(sizeof(rst_node_t));
    r->child_links = 0;
    r->a[0] = r->a[1] = NULL;
    t->n = 0;
    t->max_b = max_b; 
    t->stack = malloc(max_b * sizeof(rst_node_t *));
    t->path_info = malloc(max_b * sizeof(int));
    t->get_value = get_value;

    return;
}

void rst_finalize(rst_t *t) {
    rst_free_dfs(t->root);
}


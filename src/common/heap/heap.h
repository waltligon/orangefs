#ifndef HEAP_SORT_H
#define HEAP_SORT_H

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/* From Introduction To Algorithms by Cormen, Leiserson, and Rivest */

/* Write your own cpy_fn, swap_fn, print_fn, insert_fn, extract_fn to
 * get this to work */

typedef struct heap_node_s {
    int64_t key;
    void *user_p;
} heap_node_t;

typedef struct heap_s {
    heap_node_t *nodes;
    int size; /* Used space */
    int max_size; /* Allocated space */
} heap_t;

static inline int parent(int i) 
{
    return (((i + 1) / 2) - 1);
}

static inline int left(int i) 
{
    return ((2 * (i + 1)) - 1);
}

static inline int right(int i) 
{
    return (2 * (i + 1));
}

static inline int create_heap(heap_t *heap_p, int size) 
{
    heap_p->size = 0;
    heap_p->max_size = size;
    heap_p->nodes = (heap_node_t *) calloc (size, sizeof(heap_node_t));
    if (heap_p->nodes == NULL)
	return -1;
    else
	return 0;
}

static inline void free_heap(heap_t *heap_p) 
{
    free(heap_p->nodes);
}

static inline void heapify(heap_t *heap_p, int i,
			   void (*swap_fn) (heap_node_t *dest_p,
					    heap_node_t *src_p)) 
{
    int l, r, smallest;
    heap_node_t *nodes;

    nodes = heap_p->nodes;

    l = left(i);
    r = right(i);

    if ((l < heap_p->size) && (nodes[l].key < nodes[i].key))
	smallest = l;
    else
	smallest = i;

    if ((r < heap_p->size) && (nodes[r].key < nodes[smallest].key))
	smallest = r;

    if (smallest != i) 
    {
	swap_fn(&nodes[i], &nodes[smallest]);
	heapify(heap_p, smallest, swap_fn);
    }
}

static inline void build_heap(heap_t *heap_p,
			      void (*swap_fn) (heap_node_t *dest_p,
					       heap_node_t *src_p)) 
{
    int i;

    for (i = ((heap_p->size / 2) - 1); i >= 0; i--)
	heapify(heap_p, i, swap_fn);
}

static inline heap_node_t * heap_insert(heap_t *heap_p, int64_t key, 
					void (*cpy_fn) (heap_node_t *dest_p, 
							heap_node_t *src_p))
{
    heap_node_t *nodes;
    int i;

    nodes = heap_p->nodes;    
    (heap_p->size)++;
    i = heap_p->size - 1;
    while ((i > 0) && (nodes[parent(i)].key > key)) 
    {
	cpy_fn(&nodes[i], &nodes[parent(i)]);
	i = parent(i);
    }

    nodes[i].key = key;
    return &nodes[i];
}

static inline void heap_extract_min(heap_t *heap_p, int64_t *key_p,
				    void (*cpy_fn) (heap_node_t *dest_p,
						    heap_node_t *src_p),
				    void (*swap_fn) (heap_node_t *dest_p,
						     heap_node_t *src_p))
{
    heap_node_t *nodes;
    nodes = heap_p->nodes;
    
    assert(heap_p->size > 0);
    *key_p = nodes[0].key;
    cpy_fn(&nodes[0], &nodes[heap_p->size-1]);
    heap_p->size--;
    heapify(heap_p, 0, swap_fn);
}

static inline void heap_min(heap_t *heap_p, int64_t *key_p)
{
    heap_node_t *nodes;
    nodes = heap_p->nodes;

    assert(heap_p->size > 0);

    *key_p = nodes[0].key;
}

static int exp2_fn(int n)
{
    int i = 0, ret = 1;
    
    assert(n >= 0);

    for (i = 0; i < n; i++)
	ret *= 2;

    return ret;
}

static inline void print_heap(heap_t *heap_p, 
			      void (*print_fn) (heap_node_t *heap_node_p)) 
{
    int i;
    double level = 0;
    int next_level_idx = 1;

    printf("heap size = %d\n", heap_p->size);
    printf("(key,proc):\n");
    for (i = 0; i < heap_p->size; i++) {
	print_fn(&heap_p->nodes[i]);
	
	if ((i + 1) == next_level_idx) {
	    printf("\n");
	    next_level_idx += exp2_fn(level+1);
	    level++;
	}
	else if (i == heap_p->size - 1)
	    printf("\n");
    }
    printf("\n");
}

#endif

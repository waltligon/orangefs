#ifndef RST_H
#define RST_H

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
 *  Tadao Takaoka, Professor at Department of Computer Science,
 *  University of Canterbury, Private Bag 4800, Christchurch, New Zealand
 *  maintains the above links.
 */

/* Structure type definition for nodes in the radix search tree.  The
 * (j+1)th bit in p->child_links indicates the kind of pointer for p->a[j].
 * For example, if bit 0 is 1 then p->a[0] points to another rst_note
 * structure.  Otherwise p->a[0] is NULL or points to a data item.
 */
typedef struct rst_node {
    int child_links;
    void *a[2];
} rst_node_t;

/* Structure type definition for a radix search trie. */
typedef struct radix_tree_root {
    rst_node_t *root;
    int n;
    int max_b;
    rst_node_t **stack;
    unsigned long (* get_value)(const void *);
    int *path_info;
} rst_t;


rst_t *rst_alloc(unsigned long (* get_value)(const void *), int max_b);
void rst_free(rst_t *t);

void *rst_insert(rst_t *t, unsigned long index, void *item);
void *rst_find(rst_t *t, unsigned long index);
void *rst_delete(rst_t *t, unsigned long index);


/* modifications */

void rst_init(rst_t *t, unsigned long (* get_value)(const void *), int max_b);

static inline void *radix_tree_lookup( struct radix_tree_root *tree, 
									   unsigned long index )
{
	return rst_find(tree, index);
}

static inline int radix_tree_insert(struct radix_tree_root *tree, 
									unsigned long index, void *item)
{
	void *ret;

    ret = rst_insert(tree, index, item);

    if ( ret == NULL ) return 0;

    return -1;
}

static inline void radix_tree_delete(struct radix_tree_root *tree,
									 unsigned long index)
{
	rst_delete(tree, index);
    return;
}

static inline void init_single_radix_tree( struct radix_tree_root *tree, 
					   unsigned long (* get_value)(const void *), int max_b )
{
	
	rst_init(tree, get_value, max_b);

}

#define RADIX_MAX_BITS (24)

#endif

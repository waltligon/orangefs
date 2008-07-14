/* Derived from CLR's red-black tree algorithm */

#ifndef RBTREE_H
#define RBTREE_H

#include <assert.h>

enum rbtree_color
{
    RBTREE_RED,
    RBTREE_BLACK,
    RBTREE_NONE
};

typedef struct rbtree_s rbtree_t;

struct rbtree_s {
    enum rbtree_color color;
    /* While it would be nice to make the key void to be a string,
     * int, etc., this requires get_key and cmp_key functions, which
     * will slow down the code. */
    int64_t key;
    
    rbtree_t *parent, *left, *right;
};

#define RBTREE_HEAD_INIT(color, key, parent, left, right) \
        { (color), (key), (parent), (left), (right) } 

#define RBTREE_HEAD_PTR(name) \
        rbtree_t *name = NULL

#define RBTREE_HEAD_PTR_INIT(name, NIL) \
        name = &(NIL)

/* Sentinel (dummy) node */
#define RBTREE_NIL(name) \
        rbtree_t name = \
        RBTREE_HEAD_INIT(RBTREE_BLACK, -1, &(name), &(name), &(name))

/* rbtree_search - Find whether a node with the key already exists. */
static inline rbtree_t * rbtree_search(rbtree_t *head_p,
				       rbtree_t *NIL,
				       int64_t key)
{
    rbtree_t *x_p = head_p;
    while (x_p != NIL)
    {
        if (key == x_p->key)
	    return x_p;
	else if (key < x_p->key)
	    x_p = x_p->left;
        else 
            x_p = x_p->right;
    }
    
    return NIL;
}

/* rbtree_insert - Returns a true if the node was added.  False if the
 * node was not added. */
static inline int rbtree_insert_node(rbtree_t **head_p_p, 
				     rbtree_t *NIL,
				     rbtree_t *z_p)
{
    rbtree_t *y_p = NIL;
    rbtree_t *x_p = *head_p_p;

    /* Initialized links to point to the sentinel */
    z_p->parent = z_p->left = z_p->right = NIL;

    while (x_p != NIL)
    {
	y_p = x_p;
	if (z_p->key < x_p->key)
	    x_p = x_p->left;
	else
	    x_p = x_p->right;
    }
    z_p->parent = y_p;
    
    if (y_p == NIL)
	*head_p_p = z_p;
    else 
    {
	if (z_p->key < y_p->key)
	    y_p->left = z_p;
	else
	    y_p->right = z_p;
    }

    return 0;
}

static inline int rbtree_left_rot(rbtree_t **head_p_p,
				  rbtree_t *NIL,
				  rbtree_t *x_p)

{
    rbtree_t *y_p = x_p->right;

    assert(x_p != NIL);
    assert(y_p != NIL);
    
    x_p->right = y_p->left;
    if (y_p->left != NIL)
	y_p->left->parent = x_p;
   
    y_p->parent = x_p->parent;
    if (x_p->parent == NIL)
	*head_p_p = y_p;
    else 
    {
	if (x_p == x_p->parent->left)
	    x_p->parent->left = y_p;
	else
	    x_p->parent->right = y_p;
    }

    y_p->left = x_p;
    x_p->parent = y_p;
    return 0;
}

static inline int rbtree_right_rot(rbtree_t **head_p_p,
				   rbtree_t *NIL,
				   rbtree_t *x_p)
    
{
    rbtree_t *y_p = x_p->left;

    assert(x_p != NIL);
    assert(y_p != NIL);
    
    x_p->left = y_p->right;
    if (y_p->right != NIL)
	y_p->right->parent = x_p;
   
    y_p->parent = x_p->parent;
    if (x_p->parent == NIL)
	*head_p_p = y_p;
    else 
    {
	if (x_p == x_p->parent->right)
	    x_p->parent->right = y_p;
	else
	    x_p->parent->left = y_p;
    }

    y_p->right = x_p;
    x_p->parent = y_p;
    return 0;
}

/* rbtree_insert - Insert a node into the red-black tree fully. */
static inline int rbtree_insert(rbtree_t **head_p_p,
				rbtree_t *NIL,
				rbtree_t *x_p)
{
    rbtree_t *y_p = NIL;
    
    if (rbtree_insert_node(head_p_p, NIL, x_p) != 0)
	return -1;
    x_p->color = RBTREE_RED;
    while (x_p != *head_p_p && x_p->parent->color == RBTREE_RED)
    {
	if (x_p->parent == x_p->parent->parent->left)
	{
	    y_p = x_p->parent->parent->right;
	    if (y_p->color == RBTREE_RED)
	    {
		x_p->parent->color = RBTREE_BLACK;
		y_p->color = RBTREE_BLACK;
		x_p->parent->parent->color = RBTREE_RED;
		x_p = x_p->parent->parent;
	    }
	    else 
	    {
		if (x_p == x_p->parent->right)
		{
		    x_p = x_p->parent;
		    rbtree_left_rot(head_p_p, NIL, x_p);
		}
		x_p->parent->color = RBTREE_BLACK;
		x_p->parent->parent->color = RBTREE_RED;
		rbtree_right_rot(head_p_p, NIL, x_p->parent->parent);
	    }
	}
	else
	{
	    y_p = x_p->parent->parent->left;
	    if (y_p->color == RBTREE_RED)
	    {
		x_p->parent->color = RBTREE_BLACK;
		y_p->color = RBTREE_BLACK;
		x_p->parent->parent->color = RBTREE_RED;
		x_p = x_p->parent->parent;
	    }
	    else 
	    {
		if (x_p == x_p->parent->left)
		{
		    x_p = x_p->parent;
		    rbtree_right_rot(head_p_p, NIL, x_p);
		}
		x_p->parent->color = RBTREE_BLACK;
		x_p->parent->parent->color = RBTREE_RED;
		rbtree_left_rot(head_p_p, NIL, x_p->parent->parent);
	    }

	}
	
    }
    (*head_p_p)->color = RBTREE_BLACK;
    return 0;
}

static inline rbtree_t * rbtree_minimum(rbtree_t *x_p,
					rbtree_t *NIL)
{
    while (x_p->left != NIL)
	x_p = x_p->left;
    
    return x_p;
}

static inline rbtree_t * rbtree_maximum(rbtree_t *x_p,
				 rbtree_t *NIL)
{
    while (x_p->right != NIL)
	x_p = x_p->right;
    
    return x_p;
}

static inline rbtree_t * rbtree_sucessor(rbtree_t **head_p_p,
					 rbtree_t *NIL,
					 rbtree_t *x_p)
{
    rbtree_t *y_p = NIL;

    if (x_p->right != NIL)
	return rbtree_minimum(x_p->right, NIL);
    
    y_p = x_p->parent;
    while (y_p != NIL && x_p == y_p->right)
    {
	x_p = y_p;
	y_p = y_p->parent;
    }
    return y_p;
}

static inline int rbtree_fixup(rbtree_t **head_p_p,
			       rbtree_t *NIL,
			       rbtree_t *x_p)
{
    rbtree_t *w_p = NIL;

    while (x_p != *head_p_p && x_p->color == RBTREE_BLACK)
    {
	if (x_p == x_p->parent->left)
	{
	    w_p = x_p->parent->right;
	    if (w_p->color == RBTREE_RED)
	    {
		w_p->color = RBTREE_BLACK;
		x_p->parent->color = RBTREE_RED;
		rbtree_left_rot(head_p_p, NIL, x_p->parent);
		w_p = x_p->parent->right;
	    }
	    if (w_p->left->color == RBTREE_BLACK && 
		w_p->right->color == RBTREE_BLACK)
	    {
		w_p->color = RBTREE_RED;
		x_p = x_p->parent;
	    }
	    else 
	    {
		if (w_p->right->color == RBTREE_BLACK)
		{
		    w_p->left->color = RBTREE_BLACK;
		    w_p->color = RBTREE_RED;
		    rbtree_right_rot(head_p_p, NIL, w_p);
		    w_p = x_p->parent->right;
		}
		w_p->color = x_p->parent->color;
		x_p->parent->color = RBTREE_BLACK;
		w_p->right->color = RBTREE_BLACK;
		rbtree_left_rot(head_p_p, NIL, x_p->parent);
		x_p = *head_p_p;
		
	    }
	}
	else
	{
	    w_p = x_p->parent->left;
	    if (w_p->color == RBTREE_RED)
	    {
		w_p->color = RBTREE_BLACK;
		x_p->parent->color = RBTREE_RED;
		rbtree_right_rot(head_p_p, NIL, x_p->parent);
		w_p = x_p->parent->left;
	    }
	    if (w_p->right->color == RBTREE_BLACK && 
		w_p->left->color == RBTREE_BLACK)
	    {
		w_p->color = RBTREE_RED;
		x_p = x_p->parent;
	    }
	    else 
	    {
		if (w_p->left->color == RBTREE_BLACK)
		{
		    w_p->right->color = RBTREE_BLACK;
		    w_p->color = RBTREE_RED;
		    rbtree_left_rot(head_p_p, NIL, w_p);
		    w_p = x_p->parent->left;
		}
		w_p->color = x_p->parent->color;
		x_p->parent->color = RBTREE_BLACK;
		w_p->left->color = RBTREE_BLACK;
		rbtree_right_rot(head_p_p, NIL, x_p->parent);
		x_p = *head_p_p;
	    }
	}
    }
    x_p->color = RBTREE_BLACK;
    return 0;
}

/* rbtree_delete - Remove a node from the red-black tree, by splicing it
 * out.  Returns a pointer to the removed node for later freeing. */
static inline rbtree_t * rbtree_delete(rbtree_t **head_p_p,
				rbtree_t *NIL,
				rbtree_t *z_p,
				int (*cpy_fn) (rbtree_t *,
					       rbtree_t *))
{
    rbtree_t *x_p = NIL;
    rbtree_t *y_p = NIL;

    if (z_p->left == NIL || z_p->right == NIL)
	y_p = z_p;
    else 
    {
	y_p = rbtree_sucessor(head_p_p, NIL, z_p);
	assert(y_p != NIL);
    }

    if (y_p->left != NIL)
	x_p = y_p->left;
    else
	x_p = y_p->right;
    
    x_p->parent = y_p->parent;
    if (y_p->parent == NIL)
	*head_p_p = x_p;
    else 
    {
	if (y_p == y_p->parent->left)
	    y_p->parent->left = x_p;
	else
	    y_p->parent->right = x_p;
    }

    if (y_p != z_p)
    {
	/* Copy over all elements from the encapsulating structure. */
	cpy_fn(z_p, y_p);
	z_p->key = y_p->key;
    }
    if (y_p->color == RBTREE_BLACK)
	rbtree_fixup(head_p_p, NIL, x_p);

    return y_p;
}

/* Circular queue for doing rbtree_breadth_print. */
typedef struct rbtree_qnode_s rbtree_qnode;

struct rbtree_qnode_s
{
    int level;
    rbtree_t *rbtree_p;
    rbtree_qnode *next;
};

static inline int rbtree_qnode_add(rbtree_qnode **q_head_p_p, 
			    int level,
			    rbtree_t *rbtree_p)
{
    rbtree_qnode *rbtree_qnode_p = (rbtree_qnode *) 
	calloc(1, sizeof(rbtree_qnode));
    if (rbtree_qnode_p == NULL)
    {
	fprintf(stderr, "Calloc of rbtree_qnode_p failed\n");
	return -1;
    }
    rbtree_qnode_p->level = level;
    rbtree_qnode_p->rbtree_p = rbtree_p;

    if (*q_head_p_p == NULL)
	rbtree_qnode_p->next = rbtree_qnode_p;
    else
    {
	rbtree_qnode_p->next = (*q_head_p_p)->next;
	(*q_head_p_p)->next = rbtree_qnode_p;
    }
    *q_head_p_p = rbtree_qnode_p;
    return 0;
}

static inline rbtree_qnode * rbtree_qnode_pop(rbtree_qnode **q_head_p_p)
{
    rbtree_qnode *rbtree_qnode_p = NULL;
    
    if (*q_head_p_p == NULL)
    {
	fprintf(stdout, "rbtree_qnode_pop: head is empty!\n");
	return NULL;
    }

    rbtree_qnode_p = (*q_head_p_p)->next;
    if (rbtree_qnode_p == *q_head_p_p)
	*q_head_p_p = NULL;
    else
	(*q_head_p_p)->next = rbtree_qnode_p->next;
    
    rbtree_qnode_p->next = NULL;
    return rbtree_qnode_p;
}

/* rbtree_breadth_print - Debugging function to print out all nodes of
 * the interval tree in levels. */
static inline void rbtree_breadth_print(rbtree_t *head_p,
				 rbtree_t *NIL)
{
    int old_level = -1;
    rbtree_qnode *q_head_p = NULL;
    rbtree_qnode *q_pop_p = NULL;
    fprintf(stdout, "\nrbtree_breadth_print:");
    if (head_p != NIL)
    {
	rbtree_qnode_add(&(q_head_p), 0, head_p);
    }
    while (q_head_p != NULL)
    {
	q_pop_p = rbtree_qnode_pop(&(q_head_p));
	if (q_pop_p->rbtree_p->left != NIL)
	    rbtree_qnode_add(&(q_head_p), q_pop_p->level + 1, 
		       q_pop_p->rbtree_p->left);
	if (q_pop_p->rbtree_p->right != NIL)
	    rbtree_qnode_add(&(q_head_p), q_pop_p->level + 1, 
		       q_pop_p->rbtree_p->right);
	
	if (old_level != q_pop_p->level)
	{
	    old_level =  q_pop_p->level;
	    fprintf(stdout, "\nlevel %d: ", old_level);
	}
	
	fprintf(stdout, "{%lld,%s} ", 
		lld(q_pop_p->rbtree_p->key),
		(q_pop_p->rbtree_p->color == RBTREE_RED ? "r": "b"));
	free(q_pop_p);
    }
    fprintf(stdout, "\n\n");
}

/* rbtree_inorder_tree_print - Debugging function to print out all
 * nodes of the red-black tree in order. */
static inline void rbtree_inorder_tree_print(rbtree_t *head_p,
					     rbtree_t *NIL)
{
    if (head_p != NIL)
    {
	rbtree_inorder_tree_print(head_p->left, NIL);
	fprintf(stdout, "{%lld,%s} ", lld(head_p->key), 
		head_p->color == RBTREE_RED ? "r": "b");
	rbtree_inorder_tree_print(head_p->right, NIL);
    }
}

/* rbtree_inorder_tree_check - Look for incorrectly ordered nodes and
 * wrong maxes. */
static inline int rbtree_inorder_tree_check(rbtree_t *head_p,
				     rbtree_t *NIL)
{
    int ret = 0;
    if (head_p != NIL)
    {
	ret = rbtree_inorder_tree_check(head_p->left, NIL);
	if (ret != 0)
	    return ret;
	
	/* Order checks. */
	if (head_p->parent != NIL)
	{
	    if (head_p->key > head_p->parent->key &&
		head_p->parent->left == head_p)
	    {
		fprintf(stdout, "(left) node(%lld,%s) has a  "
			"greater key than parent (%lld)\n",
			lld(head_p->key), (head_p->color == RBTREE_RED ? "r": "b"),
			lld(head_p->parent->key));
		return -1;
	    }
	    if (head_p->key < head_p->parent->key &&
		head_p->parent->right == head_p)
	    {
		fprintf(stdout, "(right) node(%lld,,%s) has a  "
			"greater key than parent (%lld)\n",
			lld(head_p->key), (head_p->color == RBTREE_RED ? "r": "b"),
			lld(head_p->parent->key));
		return -1;
	    }
	}
	if (head_p->left != NIL)
	    if (head_p->key < head_p->left->key)
	    {
		fprintf(stdout, "node(%lld,%s) has a  "
			"lesser key than left child (%lld)\n",
			lld(head_p->key), (head_p->color == RBTREE_RED ? "r": "b"),
			lld(head_p->left->key));
		return -1;
	    }
	if (head_p->right != NIL)
	    if (head_p->key > head_p->right->key)
	    {
		fprintf(stdout, "node(%lld,%s) has a  "
			"greater key than right child (%lld)\n",
			lld(head_p->key), (head_p->color == RBTREE_RED ? "r": "b"),
			lld(head_p->right->key));
		return -1;
	    }
	
	ret = rbtree_inorder_tree_check(head_p->right, NIL);
	if (ret != 0)
	    return ret;
    }
    return ret;
}

/* rbtree_entry - get the struct for this entry
 * @ptr:        the rbtree pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the rbtree_struct within the struct. */
#define rbtree_entry(ptr, type, member) \
        ((type *)((char *)(ptr)-(unsigned long)((&((type *)0)->member))))


#endif

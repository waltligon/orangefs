/* Derived from CLR's interval tree algorithm */

#ifndef ITREE_H
#define ITREE_H

#include <assert.h>

enum itree_color
{
    ITREE_RED,
    ITREE_BLACK,
    ITREE_NONE
};

typedef struct itree_s itree_t;

struct itree_s
{
    enum itree_color color;
    /* end must be greater than or equal to start */
    int64_t start, end, max;
    itree_t *parent, *left, *right;
};

#define ITREE_HEAD_INIT(color, start, end, max, parent, left, right) \
        { (color), (start), (end), (max), (parent), (left), (right) } 

#define ITREE_HEAD_PTR(name) \
        itree_t *name = NULL

#define ITREE_HEAD_PTR_INIT(name, NIL) \
        name = &(NIL)

/* Sentinel (dummy) node that should be initialized in the file using
 * interval_tree.h */
#define ITREE_NIL(name) \
        itree_t name = \
        ITREE_HEAD_INIT(ITREE_NONE, -1, -1, -1, &(name), &(name), &(name))

/* itree_interval_search - Finds an interval which overlaps tested
 * interval */ 
static inline itree_t * itree_interval_search(itree_t *head_p, 
					      itree_t *NIL,
					      int64_t start,
					      int64_t end)
{
    itree_t *x_p = head_p;

    while (x_p != NIL && (start > x_p->end || end < x_p->start))
    {
	if (x_p->left != NIL && x_p->left->max >= start)
	    x_p = x_p->left;
	else
	    x_p = x_p->right;
    }

    return x_p;
}

/* itree_insert - Insert a node into the tree */
static inline int itree_insert_node(itree_t **head_p_p, 
				    itree_t *NIL,
				    itree_t *z_p)
{
    itree_t *y_p = NIL;
    itree_t *x_p = *head_p_p;

    /* Initialized links to point to the sentinel */
    z_p->parent = z_p->left = z_p->right = NIL;
    z_p->max = z_p->end;
    assert(z_p->start <= z_p->end);

    while (x_p != NIL)
    {
	y_p = x_p;

	/* Additional code for updating max */
	if (x_p->max < z_p->max)
	    x_p->max = z_p->max;

	if (z_p->start < x_p->start)
	    x_p = x_p->left;
	else
	    x_p = x_p->right;

    }
    z_p->parent = y_p;
 
    if (y_p == NIL)
	*head_p_p = z_p;
    else 
    {
	if (z_p->start < y_p->start)
	    y_p->left = z_p;
	else
	    y_p->right = z_p;
    }

    return 0;
}

static inline int itree_left_rot(itree_t **head_p_p,
				 itree_t *NIL,
				 itree_t *x_p)
{
    itree_t *y_p = x_p->right;

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

    /* Additional code for updating max */
    y_p->max = x_p->max;
    if (x_p->end > x_p->left->max)
	x_p->max = x_p->end;
    else
	x_p->max = x_p->left->max;
    if (x_p->max < x_p->right->max)
	x_p->max = x_p->right->max;

    return 0;
}

static inline int itree_right_rot(itree_t **head_p_p,
				  itree_t *NIL,
				  itree_t *x_p)
{
    itree_t *y_p = x_p->left;

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

    /* Additional code for updating max */
    y_p->max = x_p->max;
    if (x_p->end > x_p->right->max)
	x_p->max = x_p->end;
    else
	x_p->max = x_p->right->max;
    if (x_p->max < x_p->left->max)
	x_p->max = x_p->left->max;

    return 0;
}

/* itree_insert - Insert a node into the interval tree fully. */
static inline int itree_insert(itree_t **head_p_p,
			       itree_t *NIL,
			       itree_t *x_p)
{
    itree_t *y_p = NIL;

    if (itree_insert_node(head_p_p, NIL, x_p) != 0)
	return -1;
    x_p->color = ITREE_RED;
    while (x_p != *head_p_p && x_p->parent->color == ITREE_RED)
    {
	if (x_p->parent == x_p->parent->parent->left)
	{
	    y_p = x_p->parent->parent->right;
	    if (y_p->color == ITREE_RED)
	    {
		x_p->parent->color = ITREE_BLACK;
		y_p->color = ITREE_BLACK;
		x_p->parent->parent->color = ITREE_RED;
		x_p = x_p->parent->parent;
	    }
	    else 
	    {
		if (x_p == x_p->parent->right)
		{
		    x_p = x_p->parent;
		    itree_left_rot(head_p_p, NIL, x_p);
		}
		x_p->parent->color = ITREE_BLACK;
		x_p->parent->parent->color = ITREE_RED;
		itree_right_rot(head_p_p, NIL, x_p->parent->parent);
	    }
	}
	else
	{
	    y_p = x_p->parent->parent->left;
	    if (y_p->color == ITREE_RED)
	    {
		x_p->parent->color = ITREE_BLACK;
		y_p->color = ITREE_BLACK;
		x_p->parent->parent->color = ITREE_RED;
		x_p = x_p->parent->parent;
	    }
	    else 
	    {
		if (x_p == x_p->parent->left)
		{
		    x_p = x_p->parent;
		    itree_right_rot(head_p_p, NIL, x_p);
		}
		x_p->parent->color = ITREE_BLACK;
		x_p->parent->parent->color = ITREE_RED;
		itree_left_rot(head_p_p, NIL, x_p->parent->parent);
	    }

	}
	
    }
    (*head_p_p)->color = ITREE_BLACK;
    return 0;
}

static inline itree_t * itree_minimum(itree_t *x_p,
				      itree_t *NIL)
{
    while (x_p->left != NIL)
	x_p = x_p->left;
    
    return x_p;
}

static inline itree_t * itree_maximum(itree_t *x_p,
				      itree_t *NIL)
{
    while (x_p->right != NIL)
	x_p = x_p->right;
    
    return x_p;
}

static inline itree_t * itree_sucessor(itree_t **head_p_p,
				       itree_t *NIL,
				       itree_t *x_p)
{
    itree_t *y_p = NIL;
    
    if (x_p->right != NIL)
	return itree_minimum(x_p->right, NIL);
    
    y_p = x_p->parent;
    while (y_p != NIL && x_p == y_p->right)
    {
	x_p = y_p;
	y_p = y_p->parent;
    }
    return y_p;
}

static inline int itree_fixup(itree_t **head_p_p,
			      itree_t *NIL,
			      itree_t *x_p)
{
    itree_t *w_p = NIL;

    while (x_p != *head_p_p && x_p->color == ITREE_BLACK)
    {
	if (x_p == x_p->parent->left)
	{
	    w_p = x_p->parent->right;
	    if (w_p->color == ITREE_RED)
	    {
		w_p->color = ITREE_BLACK;
		x_p->parent->color = ITREE_RED;
		itree_left_rot(head_p_p, NIL, x_p->parent);
		w_p = x_p->parent->right;
	    }
	    if (w_p->left->color == ITREE_BLACK && 
		w_p->right->color == ITREE_BLACK)
	    {
		w_p->color = ITREE_RED;
		x_p = x_p->parent;
	    }
	    else 
	    {
		if (w_p->right->color == ITREE_BLACK)
		{
		    w_p->left->color = ITREE_BLACK;
		    w_p->color = ITREE_RED;
		    itree_right_rot(head_p_p, NIL, w_p);
		    w_p = x_p->parent->right;
		}
		w_p->color = x_p->parent->color;
		x_p->parent->color = ITREE_BLACK;
		w_p->right->color = ITREE_BLACK;
		itree_left_rot(head_p_p, NIL, x_p->parent);
		x_p = *head_p_p;
		
	    }
	}
	else
	{
	    w_p = x_p->parent->left;
	    if (w_p->color == ITREE_RED)
	    {
		w_p->color = ITREE_BLACK;
		x_p->parent->color = ITREE_RED;
		itree_right_rot(head_p_p, NIL, x_p->parent);
		w_p = x_p->parent->left;
	    }
	    if (w_p->right->color == ITREE_BLACK && 
		w_p->left->color == ITREE_BLACK)
	    {
		w_p->color = ITREE_RED;
		x_p = x_p->parent;
	    }
	    else 
	    {
		if (w_p->left->color == ITREE_BLACK)
		{
		    w_p->right->color = ITREE_BLACK;
		    w_p->color = ITREE_RED;
		    itree_left_rot(head_p_p, NIL, w_p);
		    w_p = x_p->parent->left;
		}
		w_p->color = x_p->parent->color;
		x_p->parent->color = ITREE_BLACK;
		w_p->left->color = ITREE_BLACK;
		itree_right_rot(head_p_p, NIL, x_p->parent);
		x_p = *head_p_p;
	    }
	}
    }
    x_p->color = ITREE_BLACK;
    return 0;
}

/* itree_max_update_self - Fix the max value of self, assuming that
 * max is incorrect, but that start and end are correct. */
static inline int itree_max_update_self(itree_t *node_p,
					itree_t *NIL)
{
    int64_t new_max = -1;

    if (node_p->end > node_p->left->max)
	new_max = node_p->end;
    else
	new_max = node_p->left->max;
    if (new_max < node_p->right->max)
	new_max = node_p->right->max;
	
    node_p->max = new_max;

    return 0;
}


/* itree_max_update_parent - Fix the max values up the tree, starting
 * from node_p's parent. */
static inline int itree_max_update_parent(itree_t *node_p,
					  itree_t *NIL)
{
    itree_t *tmp_p = node_p;
    int64_t new_max = -1;
    
    while (tmp_p->parent != NIL)
    {
	tmp_p = tmp_p->parent;
	if (tmp_p->end > tmp_p->left->max)
	    new_max = tmp_p->end;
	else
	    new_max = tmp_p->left->max;
	if (new_max < tmp_p->right->max)
	    new_max = tmp_p->right->max;
	
	if (new_max == tmp_p->max)
	    break;
	else
	    tmp_p->max = new_max;
    }

    return 0;
}

/* itree_delete - Remove a node from the interval tree, by splicing it
 * out.  Returns a pointer to the removed node for later freeing. */
static inline itree_t * itree_delete(itree_t **head_p_p,
			      itree_t *NIL,
			      itree_t *z_p,
			      int (*cpy_fn) (itree_t *, 
					     itree_t *))
{
    itree_t *x_p = NIL;
    itree_t *y_p = NIL;

    if (z_p->left == NIL || z_p->right == NIL)
	y_p = z_p;
    else 
    {
	y_p = itree_sucessor(head_p_p, NIL, z_p);
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
	z_p->start = y_p->start;
	z_p->end = y_p->end;
	
	/* Additional code for updating max */
	if (y_p->end > z_p->max)
	    z_p->max = y_p->end;
	itree_max_update_parent(z_p, NIL);
    }

    /* Additional code for updating max */
    itree_max_update_parent(y_p, NIL);

    if (y_p->color == ITREE_BLACK)
	itree_fixup(head_p_p, NIL, x_p);

    return y_p;
}

/* Circular queue for doing itree_breadth_print. */
typedef struct itree_qnode_s itree_qnode;

struct itree_qnode_s
{
    int level;
    itree_t *itree_p;
    itree_qnode *next;
};

static inline int itree_qnode_add(itree_qnode **q_head_p_p, 
				  int level,
				  itree_t *itree_p)
{
    itree_qnode *itree_qnode_p = (itree_qnode *) 
	calloc(1, sizeof(itree_qnode));
    if (itree_qnode_p == NULL)
    {
	fprintf(stderr, "Calloc of itree_qnode_p failed\n");
	return -1;
    }
    itree_qnode_p->level = level;
    itree_qnode_p->itree_p = itree_p;

    if (*q_head_p_p == NULL)
	itree_qnode_p->next = itree_qnode_p;
    else
    {
	itree_qnode_p->next = (*q_head_p_p)->next;
	(*q_head_p_p)->next = itree_qnode_p;
    }
    *q_head_p_p = itree_qnode_p;
    return 0;
}

static inline itree_qnode * itree_qnode_pop(itree_qnode **q_head_p_p)
{
    itree_qnode *itree_qnode_p = NULL;
    
    if (*q_head_p_p == NULL)
    {
	fprintf(stdout, "itree_qnode_pop: head is empty!\n");
	return NULL;
    }

    itree_qnode_p = (*q_head_p_p)->next;
    if (itree_qnode_p == *q_head_p_p)
	*q_head_p_p = NULL;
    else
	(*q_head_p_p)->next = itree_qnode_p->next;
    
    itree_qnode_p->next = NULL;
    return itree_qnode_p;
}

/* itree_breadth_print - Debugging function to print out all nodes of
 * the interval tree in levels. */
static inline void itree_breadth_print(itree_t *head_p,
				       itree_t *NIL)
{
    int old_level = -1, count = 0;
    itree_qnode *q_head_p = NULL;
    itree_qnode *q_pop_p = NULL;
    fprintf(stdout, "\nitree_breadth_print:");
    if (head_p != NIL)
    {
	itree_qnode_add(&(q_head_p), 0, head_p);
    }
    while (q_head_p != NULL)
    {
	q_pop_p = itree_qnode_pop(&(q_head_p));
	if (q_pop_p->itree_p->left != NIL)
	    itree_qnode_add(&(q_head_p), q_pop_p->level + 1, 
		       q_pop_p->itree_p->left);
	if (q_pop_p->itree_p->right != NIL)
	    itree_qnode_add(&(q_head_p), q_pop_p->level + 1, 
		       q_pop_p->itree_p->right);
	
	if (old_level != q_pop_p->level)
	{
	    old_level =  q_pop_p->level;
	    fprintf(stdout, "\nlevel %d: ", old_level);
	}
	
	fprintf(stdout, "{%lld,%lld,%lld,%s} ", 
		lld(q_pop_p->itree_p->start), lld(q_pop_p->itree_p->end),
		lld(q_pop_p->itree_p->max), 
		(q_pop_p->itree_p->color == ITREE_RED ? "r": "b"));
	free(q_pop_p);
	count++;
    }
    fprintf(stdout, "\n%d total nodes\n\n", count);
}

/* itree_breadth_print_fn - Debugging function to print out
 * function-defined informations of each node of the interval tree in
 * levels. */
static inline void itree_breadth_print_fn(itree_t *head_p,
					  itree_t *NIL,
					  void (*print_fn) (itree_t *))
{
    int old_level = -1, count = 0;
    itree_qnode *q_head_p = NULL;
    itree_qnode *q_pop_p = NULL;
    fprintf(stdout, "\nitree_breadth_print:");
    if (head_p != NIL)
    {
	itree_qnode_add(&(q_head_p), 0, head_p);
    }
    while (q_head_p != NULL)
    {
	q_pop_p = itree_qnode_pop(&(q_head_p));
	if (q_pop_p->itree_p->left != NIL)
	    itree_qnode_add(&(q_head_p), q_pop_p->level + 1, 
		       q_pop_p->itree_p->left);
	if (q_pop_p->itree_p->right != NIL)
	    itree_qnode_add(&(q_head_p), q_pop_p->level + 1, 
		       q_pop_p->itree_p->right);
	
	if (old_level != q_pop_p->level)
	{
	    old_level =  q_pop_p->level;
	    fprintf(stdout, "\nlevel %d: ", old_level);
	}
	
	print_fn(q_pop_p->itree_p);
	free(q_pop_p);
	count++;
    }
    fprintf(stdout, "\n%d total nodes\n\n", count);
}

/* itree_inorder_tree_print - Debugging function to print out all nodes of
 * the interval tree in order. */
static inline void itree_inorder_tree_print(itree_t *head_p,
					    itree_t *NIL)
{
    if (head_p != NIL)
    {
	itree_inorder_tree_print(head_p->left, NIL);
	fprintf(stdout, "{%lld,%lld,%lld,%s} ", 
		lld(head_p->start), lld(head_p->end), lld(head_p->max), 
		(head_p->color == ITREE_RED ? "r": "b"));
	itree_inorder_tree_print(head_p->right, NIL);
    }
}

/* itree_inorder_tree_print_fn - Debugging function to print out
 * function-defined information for all nodes of the interval tree in
 * order. */
static inline void itree_inorder_tree_print_fn(itree_t *head_p,
					       itree_t *NIL,
					       void (*print_fn) (itree_t *))
{
    if (head_p != NIL)
    {
	itree_inorder_tree_print_fn(head_p->left, NIL, print_fn);
	print_fn(head_p);
	itree_inorder_tree_print_fn(head_p->right, NIL, print_fn);
    }
}

/* itree_nil_check - Debugging function to figure out what is
 * happening with the NIL node. */
static inline int itree_nil_check(itree_t *NIL)
{
    if (NIL->color != ITREE_NONE ||
	NIL->start != -1 ||
	NIL->end != -1 ||
	NIL->max != -1 ||
	NIL->left != NIL ||
	NIL->right != NIL ||
	NIL->parent != NIL)
    {
	fprintf(stdout, 
		"Error: Nil is wrong.\n"
		"Color = %d, (should be %d)\n"
		"Start = %lld, End = %lld, Max = %lld (all should be -1)\n"
		"Left = %lx, Right = %lx, Parent = %lx (all should be %lx)\n",
		NIL->color, ITREE_NONE, lld(NIL->start), lld(NIL->end), 
		lld(NIL->max), 
		(unsigned long) NIL->left, 
		(unsigned long) NIL->right, 
		(unsigned long) NIL->parent,
		(unsigned long) NIL);
    }

    return 0;
}

/* itree_inorder_tree_check - Look for incorrectly ordered nodes and
 * wrong maxes. */
static inline int itree_inorder_tree_check(itree_t *head_p,
					   itree_t *NIL)
{
    int ret = 0;
    if (head_p != NIL)
    {
	ret = itree_inorder_tree_check(head_p->left, NIL);
	if (ret != 0)
	    return ret;
	
	/* Order checks. */
	if (head_p->parent != NIL)
	{
	    if (head_p->start > head_p->parent->start &&
		head_p->parent->left == head_p)
	    {
		fprintf(stdout, "(left) node(%lld,%lld,%lld,%s) has a  "
			"greater start than parent (%lld)\n",
			lld(head_p->start), lld(head_p->end), lld(head_p->max),
			(head_p->color == ITREE_RED ? "r": "b"),
			lld(head_p->parent->start));
		return -1;
	    }
	    if (head_p->start < head_p->parent->start &&
		head_p->parent->right == head_p)
	    {
		fprintf(stdout, "(right) node(%lld,%lld,%lld,%s) has a  "
			"greater start than parent (%lld)\n",
			lld(head_p->start), lld(head_p->end), lld(head_p->max),
			(head_p->color == ITREE_RED ? "r": "b"),
			lld(head_p->parent->start));
		return -1;
	    }
	}
	if (head_p->left != NIL)
	    if (head_p->start < head_p->left->start)
	    {
		fprintf(stdout, "node(%lld,%lld,%lld,%s) has a  "
			"lesser start than left child (%lld)\n",
			lld(head_p->start), lld(head_p->end), lld(head_p->max),
			(head_p->color == ITREE_RED ? "r": "b"),
			lld(head_p->left->start));
		return -1;
	    }
	if (head_p->right != NIL)
	    if (head_p->start > head_p->right->start)
	    {
		fprintf(stdout, "node(%lld,%lld,%lld,%s) has a  "
			"greater start than right child (%lld)\n",
			lld(head_p->start), lld(head_p->end), lld(head_p->max),
			(head_p->color == ITREE_RED ? "r": "b"),
			lld(head_p->right->start));
		return -1;
	    }
	/* Max checks. */
	if (head_p->max < head_p->end)
	{
	    fprintf(stdout, "node(%lldd,%lld,%lld,%s) has a  "
		    "greater end (%lld)\n",
		    lld(head_p->start), lld(head_p->end), lld(head_p->max), 
		    (head_p->color == ITREE_RED ? "r": "b"), 
		    lld(head_p->end));
	    return -1;
	}
	if (head_p->max < head_p->left->max)
	{
	    fprintf(stdout, "node(%lld,%lld,%lld,%s) has a left child "
		    "with a greater max (%lld)\n",
		    lld(head_p->start), lld(head_p->end), lld(head_p->max), 
		    (head_p->color == ITREE_RED ? "r": "b"), 
		    lld(head_p->left->max));
	    return -1;
	}
	if (head_p->max < head_p->right->max)
	{
	    fprintf(stdout, "node(%lld,%lld,%lld,%s) has a right child "
		    "with a greater max (%lld)\n",
		    lld(head_p->start), lld(head_p->end), lld(head_p->max), 
		    (head_p->color == ITREE_RED ? "r": "b"), 
		    lld(head_p->right->max));
	    return -1;
	}
	if (head_p->max != head_p->left->max &&
	    head_p->max != head_p->right->max &&
	    head_p->max != head_p->end)
	{
	    fprintf(stdout, "node(%lld,%lld,%lld,%s) has a max not equal to "
		    "its end (%lld) or its childrens' maxes (%lld,%lld)\n",
		    lld(head_p->start), lld(head_p->end), lld(head_p->max), 
		    (head_p->color == ITREE_RED ? "r": "b"), 
		    lld(head_p->end), lld(head_p->left->max), lld(head_p->right->max));
	    return -1;
	}
	
	ret = itree_inorder_tree_check(head_p->right, NIL);
	if (ret != 0)
	    return ret;
    }
    return ret;
}

/* itree_entry - get the struct for this entry
 * @ptr:        the itree pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the itree_struct within the struct. */
#define itree_entry(ptr, type, member) \
        ((type *)((char *)(ptr)-(unsigned long)((&((type *)0)->member))))


#endif

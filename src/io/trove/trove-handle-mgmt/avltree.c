/*
 *  avltree.c:  from http://purists.org/avltree/
 */

#include <stdlib.h>
#include "trove-extentlist.h"		/* declares the AVL types  */
#include "avltree.h"

/*
 *  avlrotleft: perform counterclockwise rotation
 *
 *  robl: also update pointers to parent nodes
 *
 *  Parameters:
 *
 *    n           Address of a pointer to a node
 */
void
avlrotleft(struct avlnode **n)
{
	struct avlnode *tmp = *n;

	*n = (*n)->right;
	tmp->right = (*n)->left;
	(*n)->left = tmp;
}

/*
 *  avlrotright: perform clockwise rotation
 *
 *  Parameters:
 *
 *    n           Address of a pointer to a node
 */
void
avlrotright(struct avlnode **n)
{
	struct avlnode *tmp = *n;	 

	*n = (*n)->left;		
	tmp->left = (*n)->right; 
	(*n)->right = tmp;	
}

/*
 *  avlleftgrown: helper function for avlinsert
 *
 *  Parameters:
 *
 *    n           Address of a pointer to a node. This node's left 
 *                subtree has just grown due to item insertion; its 
 *                "skew" flag needs adjustment, and the local tree 
 *                (the subtree of which this node is the root node) may 
 *                have become unbalanced.
 *
 *  Return values:
 *
 *    OK          The local tree could be rebalanced or was balanced 
 *                from the start. The parent activations of the avlinsert 
 *                activation that called this function may assume the 
 *                entire tree is valid.
 *
 *    BALANCE     The local tree was balanced, but has grown in height.
 *                Do not assume the entire tree is valid.
 */
enum AVLRES
avlleftgrown(struct avlnode **n)
{
	switch ((*n)->skew) {
	case LEFT:
		if ((*n)->left->skew == LEFT) {
			(*n)->skew = (*n)->left->skew = NONE;
			avlrotright(n);
		}	
		else {
			switch ((*n)->left->right->skew) {
			case LEFT:
				(*n)->skew = RIGHT;
				(*n)->left->skew = NONE;
				break;

			case RIGHT:
				(*n)->skew = NONE;
				(*n)->left->skew = LEFT;
				break;

			default:
				(*n)->skew = NONE;
				(*n)->left->skew = NONE;
			}
			(*n)->left->right->skew = NONE;
			avlrotleft(& (*n)->left);
			avlrotright(n);
		}
		return OK;

	case RIGHT:
		(*n)->skew = NONE;
		return OK;
	
	default:
		(*n)->skew = LEFT;
		return BALANCE;
	}
}

/*
 *  avlrightgrown: helper function for avlinsert
 *
 *  See avlleftgrown for details.
 */
enum AVLRES
avlrightgrown(struct avlnode **n)
{
	switch ((*n)->skew) {
	case LEFT:					
		(*n)->skew = NONE;
		return OK;

	case RIGHT:
		if ((*n)->right->skew == RIGHT) {	
			(*n)->skew = (*n)->right->skew = NONE;
			avlrotleft(n);
		}
		else {
			switch ((*n)->right->left->skew) {
			case RIGHT:
				(*n)->skew = LEFT;
				(*n)->right->skew = NONE;
				break;

			case LEFT:
				(*n)->skew = NONE;
				(*n)->right->skew = RIGHT;
				break;

			default:
				(*n)->skew = NONE;
				(*n)->right->skew = NONE;
			}
			(*n)->right->left->skew = NONE;
			avlrotright(& (*n)->right);
			avlrotleft(n);
		}
		return OK;

	default:
		(*n)->skew = RIGHT;
		return BALANCE;
	}
}

/*
 *  avlinsert: insert a node into the AVL tree.
 *
 *  Parameters:
 *
 *    n           Address of a pointer to a node.
 *
 *    d           Item to be inserted.
 *
 *  Return values:
 *
 *    nonzero     The item has been inserted. The excact value of 
 *                nonzero yields is of no concern to user code; when
 *                avlinsert recursively calls itself, the number 
 *                returned tells the parent activation if the AVL tree 
 *                may have become unbalanced; specifically:
 *
 *      OK        None of the subtrees of the node that n points to 
 *                has grown, the AVL tree is valid.
 *
 *      BALANCE   One of the subtrees of the node that n points to 
 *                has grown, the node's "skew" flag needs adjustment,
 *                and the AVL tree may have become unbalanced.
 *
 *    zero        The datum provided could not be inserted, either due 
 *                to AVLKEY collision (the tree already contains another
 *                item with which the same AVLKEY is associated), or
 *                due to insufficient memory.
 */   
enum AVLRES
avlinsert(struct avlnode **n, AVLDATUM d)
{
	enum AVLRES tmp;

	if (!(*n)) {
		if (!((*n) = malloc(sizeof(struct avlnode)))) {
			return ERROR;
		}
		(*n)->left = (*n)->right = NULL;
		(*n)->d = d;
		(*n)->skew = NONE;
		return BALANCE;
	}
	
	if (AVLKEY(d) < AVLKEY((*n)->d)) {
		if ((tmp = avlinsert(& (*n)->left, d)) == BALANCE) {
			return avlleftgrown(n);
		}
		return tmp;
	}
	if (AVLKEY(d) > AVLKEY((*n)->d)) {
		if ((tmp = avlinsert(& (*n)->right,  d)) == BALANCE) {
			return avlrightgrown(n);
		}
		return tmp;
	}
	return ERROR;
}

/*
 *  avlleftshrunk: helper function for avlremove and avlfindlowest
 *
 *  Parameters:
 *
 *    n           Address of a pointer to a node. The node's left
 *                subtree has just shrunk due to item removal; its
 *                "skew" flag needs adjustment, and the local tree
 *                (the subtree of which this node is the root node) may
 *                have become unbalanced.
 *
 *   Return values:
 *
 *    OK          The parent activation of the avlremove activation
 *                that called this function may assume the entire
 *                tree is valid.
 *
 *    BALANCE     Do not assume the entire tree is valid.
 */                
enum AVLRES
avlleftshrunk(struct avlnode **n)
{
	switch ((*n)->skew) {
	case LEFT:
		(*n)->skew = NONE;
		return BALANCE;

	case RIGHT:
		if ((*n)->right->skew == RIGHT) {
			(*n)->skew = (*n)->right->skew = NONE;
			avlrotleft(n);
			return BALANCE;
		}
		else if ((*n)->right->skew == NONE) {
			(*n)->skew = RIGHT;
			(*n)->right->skew = LEFT;
			avlrotleft(n);
			return OK;
		}
		else {
			switch ((*n)->right->left->skew) {
			case LEFT:
				(*n)->skew = NONE;
				(*n)->right->skew = RIGHT;
				break;

			case RIGHT:
				(*n)->skew = LEFT;
				(*n)->right->skew = NONE;
				break;

			default:
				(*n)->skew = NONE;
				(*n)->right->skew = NONE;
			}
			(*n)->right->left->skew = NONE;
			avlrotright(& (*n)->right);
			avlrotleft(n);
			return BALANCE;
		}

	default:
		(*n)->skew = RIGHT;
		return OK;
	}
}

/*
 *  avlrightshrunk: helper function for avlremove and avlfindhighest
 *
 *  See avlleftshrunk for details.
 */
enum AVLRES
avlrightshrunk(struct avlnode **n)
{
	switch ((*n)->skew) {
	case RIGHT:
		(*n)->skew = NONE;
		return BALANCE;

	case LEFT:
		if ((*n)->left->skew == LEFT) {
			(*n)->skew = (*n)->left->skew = NONE;
			avlrotright(n);
			return BALANCE;
		}
		else if ((*n)->left->skew == NONE) {
			(*n)->skew = LEFT;
			(*n)->left->skew = RIGHT;
			avlrotright(n);
			return OK;
		}
		else {
			switch ((*n)->left->right->skew) {
			case LEFT:
				(*n)->skew = RIGHT;
				(*n)->left->skew = NONE;
				break;

			case RIGHT:
				(*n)->skew = NONE;
				(*n)->left->skew = LEFT;	
				break;
			
			default:
				(*n)->skew = NONE;
				(*n)->left->skew = NONE;
			}
			(*n)->left->right->skew = NONE;
			avlrotleft(& (*n)->left);
			avlrotright(n);
			return BALANCE;
		}

	default:
		(*n)->skew = LEFT;
		return OK;
	}
}

/*
 *  avlfindhighest: replace a node with a subtree's highest-ranking item.
 *
 *  Parameters:
 *
 *    target      Pointer to node to be replaced.
 *
 *    n           Address of pointer to subtree.
 *
 *    res         Pointer to variable used to tell the caller whether
 *                further checks are necessary; analog to the return
 *                values of avlleftgrown and avlleftshrunk (see there). 
 *
 *  Return values:
 *
 *    1           A node was found; the target node has been replaced.
 *
 *    0           The target node could not be replaced because
 *                the subtree provided was empty.
 *
 */
int
avlfindhighest(struct avlnode *target, struct avlnode **n, enum AVLRES *res)
{
	struct avlnode *tmp;

	*res = BALANCE;
	if (!(*n)) {
		return 0;
	}
	if ((*n)->right) {
		if (!avlfindhighest(target, &(*n)->right, res)) {
			return 0;
		}
		if (*res == BALANCE) {
			*res = avlrightshrunk(n);
		}
		return 1;
	}
	free(target->d);
	target->d  = (*n)->d;
	tmp = *n;
	*n = (*n)->left;
	free(tmp);
	return 1;
}

/*
 *  avlfindlowest: replace node with a subtree's lowest-ranking item.
 *
 *  See avlfindhighest for the details.
 */
int
avlfindlowest(struct avlnode *target, struct avlnode **n, enum AVLRES *res)
{
	struct avlnode *tmp;

	*res = BALANCE;
	if (!(*n)) {
		return 0;
	}
	if ((*n)->left) {
		if (!avlfindlowest(target, &(*n)->left, res)) {
			return 0;
		}
		if (*res == BALANCE) {
			*res =  avlleftshrunk(n);
		}
		return 1;
	}
	free(target->d);
	target->d = (*n)->d;
	tmp = *n;
	*n = (*n)->right;
	free(tmp);
	return 1;
}

/*
 *  avlremove: remove an item from the tree.
 *
 *  Parameters:
 *
 *    n           Address of a pointer to a node.
 *
 *    key         AVLKEY of item to be removed.
 *
 *  Return values:
 *
 *    nonzero     The item has been removed. The exact value of 
 *                nonzero yields if of no concern to user code; when
 *                avlremove recursively calls itself, the number
 *                returned tells the parent activation if the AVL tree
 *                may have become unbalanced; specifically:
 *
 *      OK        None of the subtrees of the node that n points to
 *                has shrunk, the AVL tree is valid.
 *
 *      BALANCE   One of the subtrees of the node that n points to
 *                has shrunk, the node's "skew" flag needs adjustment,
 *                and the AVL tree may have become unbalanced.
 *
 *   zero         The tree does not contain an item yielding the
 *                AVLKEY value provided by the caller.
 */
enum AVLRES
avlremove(struct avlnode **n, int key)
{
	enum AVLRES tmp = BALANCE;

	if (!(*n)) {
		return ERROR;
	}
	if (key < AVLKEY((*n)->d)) {
		if ((tmp = avlremove(& (*n)->left, key)) == BALANCE) {
			return avlleftshrunk(n);
		}
		return tmp;
	}
	if (key > AVLKEY((*n)->d)) {
		if ((tmp = avlremove(& (*n)->right, key)) == BALANCE) {
			return avlrightshrunk(n);
		}
		return tmp;
	}
	if ((*n)->left) {
		if (avlfindhighest(*n, &((*n)->left), &tmp)) {
			if (tmp == BALANCE) {
				tmp = avlleftshrunk(n);
			}
			return tmp;
		}
	}
	if ((*n)->right) {
		if (avlfindlowest(*n, &((*n)->right), &tmp)) {
			if (tmp == BALANCE) {
				tmp = avlrightshrunk(n);
			}
			return tmp;
		}
	}
	free((*n)->d);
	(*n)->d = NULL;
	free(*n);
	*n = NULL;
	return BALANCE;
}

/*
 *  avlaccess: retrieve the datum corresponding to a given AVLKEY.
 *
 *  Parameters:
 *
 *    n           Pointer to the root node.
 *
 *    key         TKEY of item to be accessed.
 *
 *  Return values:
 *
 *    non-NULL    An item yielding the AVLKEY provided has been found,
 *                the return value points to the AVLKEY attached to it.
 *
 *    NULL        The item could not be found.
 */
AVLDATUM *
avlaccess(struct avlnode *n, int key)
{
        if (!n) {
                return NULL;
        }
        if (key < AVLKEY((n)->d)) {
                return avlaccess(n->left, key);
        }
        if (key > AVLKEY((n)->d)) {
                return avlaccess(n->right, key);
        }
        return &(n->d);
}

#ifdef AVLALTKEY
/*
 *  avlaltaccess: retrieve the datum corresponding to a given AVLALTKEY.
 *
 *  Parameters:
 *
 *    n           Pointer to the root node.
 *
 *    key         TKEY of item to be accessed.
 *
 *  Return values:
 *
 *    non-NULL    An item yielding the AVLALTKEY provided has been found,
 *                the return value points to the AVLALTKEY attached to it.
 *
 *    NULL        The item could not be found.
 */
AVLDATUM *
avlaltaccess(struct avlnode *n, int key)
{
        if (!n) {
                return NULL;
        }
        if (key < AVLALTKEY((n)->d)) {
                return avlaltaccess(n->left, key);
        }
        if (key > AVLALTKEY((n)->d)) {
                return avlaltaccess(n->right, key);
        }
        return &(n->d);
}
#endif

/*
 * avlgethighest: retrieve the datum from the highest weighted node
 *
 * Parameters:
 *
 *   n		pointer to the root node
 *
 * Return values:
 *
 *   non-NULL	the return value points to the AVLKEY attached to the node
 *
 *   NULL	tree has no nodes
 */
AVLDATUM *
avlgethighest(struct avlnode *n)
{
	if (!n) {
		return NULL;
	}
	if (n->right) {
		return avlgethighest(n->right);
	}
	return &(n->d);
}

/*
 *  avldepthfirst: depth-first tree traversal.
 *
 *  Parameters:
 *
 *    n          Pointer to the root node.
 *
 *    f          Worker function to be called for every node.
 *
 *    param      Additional parameter to be passed to the
 *               worker function
 *
 *    depth      Recursion depth indicator. Allows the worker function
 *               to determine how many levels the node being processed
 *               is below the root node. Can be used, for example,
 *               for selecting the proper indentation width when
 *               avldepthfirst ist used to print a tree dump to
 *               the screen.
 *
 *               Most of the time, you will want to call avldepthfirst
 *               with a "depth" value of zero.
 */
void
avldepthfirst(struct avlnode *n, AVLWORKER *f, int param, int depth)
{
	if (!n) return;
	avldepthfirst(n->left, f, param, depth + 1);
	(*f)(n, param, depth);
	avldepthfirst(n->right, f, param, depth +1);
}

/*
 *  avlpostorder: post-order tree traversal.
 *
 *  Parameters:
 *
 *    n          Pointer to the root node.
 *
 *    f          Worker function to be called for every node.
 *
 *    param      Additional parameter to be passed to the
 *               worker function
 *
 *    depth      Recursion depth indicator. Allows the worker function
 *               to determine how many levels the node being processed
 *               is below the root node. Can be used, for example,
 *               for selecting the proper indentation width when
 *               avldepthfirst ist used to print a tree dump to
 *               the screen.
 *
 *               Most of the time, you will want to call avlpostorder 
 *               with a "depth" value of zero.
 */
void
avlpostorder(struct avlnode *n, AVLWORKER *f, int param, int depth)
{
	if (!n) return;
	avlpostorder(n->left, f, param, depth + 1);
	avlpostorder(n->right, f, param, depth +1);
	(*f)(n, param, depth);
}

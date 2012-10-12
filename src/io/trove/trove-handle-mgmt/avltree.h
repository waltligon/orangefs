/*
 *  avltree.h : from http://purists.org/avltree/
 */

#ifndef AVLTREE_H
#define AVLTREE_H

#ifndef AVLDATUM
#error AVLDATUM undefined!
#endif

#ifndef AVLKEY_TYPE
#error AVLKEY_TYPE undefined
#endif

#ifndef AVLKEY
#error AVLKEY undefined!
#endif

/*
 *  Which of a given node's subtrees is higher?
 */
enum AVLSKEW 
{
    NONE,	
    LEFT,
    RIGHT
};

/*
 *  Did a given insertion/deletion succeed, and what do we do next?
 */
enum AVLRES
{
    ERROR = 0,
    OK,
    BALANCE,
};

/*
 *  AVL tree node structure
 */
struct avlnode
{
    struct avlnode *left, *right;
    AVLDATUM d;
    enum AVLSKEW skew;
};

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
avlinsert(struct avlnode **n, AVLDATUM d);

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
avlremove(struct avlnode **n, AVLKEY_TYPE key);

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
avlaccess(struct avlnode *n, AVLKEY_TYPE key);

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
avlaltaccess(struct avlnode *n, AVLKEY_TYPE key);
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
avlgethighest(struct avlnode *n);

/*
 *  Function to be called by the tree traversal functions.
 *
 *  Parameters:
 *
 *    n           Pointer to a node.
 *
 *    param       Value provided by the traversal function's caller.
 *
 *    depth       Recursion depth indicator. Allows the function to
 *                determine how many levels the node bein processed is
 *                below the root node. Can be used, for example,
 *                for selecting the proper indentation width when
 *                avldepthfirst is used to print a tree dump to 
 *                the screen.
 */
typedef void AVLWORKER(struct avlnode *n, int param, int depth);

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
avldepthfirst(struct avlnode *n, AVLWORKER *f, int param, int depth);

/*
 *  avlpreorder: depth-first tree traversal.
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
avlpostorder(struct avlnode *n, AVLWORKER *f, int param, int depth);
#endif


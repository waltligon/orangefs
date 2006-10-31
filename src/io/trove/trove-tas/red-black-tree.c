/*
 * Author: Julian Kunkel 2005
 */
 
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "red-black-tree.h"

int compare_int64(
    RBData * data,
    RBKey * key)
{
    int64_t ikey1 = *((int64_t *) data);
    int64_t ikey2 = *((int64_t *) key);

    if (ikey1 > ikey2)
    {   /* take right neigbour */
        return +1;
    }
    else if (ikey1 < ikey2)
    {   /* take left neigbour */
        return -1;
    }
    return 0;
}

static void inorderWalkthrough(
    void (*callback) (RBData * data,
                      void *funcData),
    tree_node * node,
    void *funcData)
{
    if (node->left != NULL)
    {
        inorderWalkthrough(callback, node->left, funcData);
    }
    (*callback) (node->data, funcData);
    if (node->right != NULL)
    {
        inorderWalkthrough(callback, node->right, funcData);
    }
}

void iterate_red_black_tree(
    void (*callback) (RBData * data,
                      void *funcData),
    red_black_tree * tree,
    void *funcData)
{
    if (tree == NULL || tree->head == NULL)
        return;
    inorderWalkthrough(callback, tree->head, funcData);
}

/* take care of memory managment of the data by yourself ! */
tree_node *lookup_tree(
    RBKey * key,
    red_black_tree * tree)
{
    tree_node *actNode = tree->head;
    while (actNode != NULL)
    {
        int compret = (tree->compare) (actNode->data, key);
        if (compret > 0)
        {       /* take right neigbour */
            actNode = actNode->right;
        }
        else if (compret < 0)
        {       /* take left neigbour */
            actNode = actNode->left;
        }
        else
        {       /* found key */
            return actNode;
        }
    }

    return NULL;
}


red_black_tree *new_red_black_tree(
    int (*compare) (RBData * data,
                    RBKey * key),
    int (*compare2) (RBData * data,
                     RBData * data2))
{
    red_black_tree *tree = malloc(sizeof(red_black_tree));
    tree->head = NULL;
    tree->compare = compare;
    tree->compare2 = compare2;
    return tree;
}

void free_empty_red_black_tree(
    red_black_tree ** tree)
{
    if (*tree != NULL)
    {
        free(*tree);
        *tree = NULL;
    }
}

inline tree_node *nodesibling(
    tree_node * node)
{
    tree_node *parent = node->parent;
    if (parent == NULL)
        return NULL;
    return parent->left == node ? parent->right : parent->left;
}

inline void rotateleft(
    tree_node * node,
    red_black_tree * tree)
{
    tree_node *parent = node->parent;
    tree_node *sibling = node->right;

    if (parent != NULL)
    {
        if (parent->left == node)
        {
            parent->left = sibling;
        }
        else
        {
            parent->right = sibling;
        }
    }
    else
    {   /* grandparent == NULL */
        tree->head = sibling;
    }
    sibling->parent = parent;
    /* connection to parent tree is now correct... */
    node->right = sibling->left;
    if (sibling->left != NULL)
    {
        sibling->left->parent = node;
    }

    sibling->left = node;
    node->parent = sibling;
}

inline void rotateright(
    tree_node * node,
    red_black_tree * tree)
{
    tree_node *sibling = node->left;
    if (node->parent != NULL)
    {
        tree_node *parent = node->parent;

        if (parent->left == node)
        {
            parent->left = sibling;
        }
        else
        {
            parent->right = sibling;
        }
    }
    else
    {   /* node is head of tree */
        tree->head = sibling;
    }
    sibling->parent = node->parent;
    /* connection to parent tree is now correct... */

    node->left = sibling->right;
    if (sibling->right != NULL)
        sibling->right->parent = node;

    sibling->right = node;
    node->parent = sibling;
}


/* recursive function to recreate RB tree condition */
static void rebuildRBTree(
    red_black_tree * tree,
    tree_node * node)
{
    if (node == tree->head)
    {
        node->color = BLACK;
        return;
    }
    tree_node *parent = node->parent;
    /* No path from the root to a leaf may contain two consecutive nodes colored red */
    if (parent->color == RED)
    {   /* condition not held ! 
         * check cases !!! Decision depends on the grandpa of the new node position... */
        tree_node *grandparent = parent->parent;
        /* printf("Condition not held key:%d parent:%d grandpa:%d !\n",node->key, parent->key, grandparent->key); */
        tree_node *uncle = grandparent->right;
        if (parent == grandparent->right)
        {
            uncle = grandparent->left;
        }

        if (uncle != NULL && uncle->color == RED)
        {
            /* case LLr or LRb */
            grandparent->color = RED;
            uncle->color = BLACK;
            parent->color = BLACK;
            /* recursivly call function */
            rebuildRBTree(tree, grandparent);
            return;
        }
        if (parent == grandparent->left)
        {   /* grandparent->color must be BLACK because parent is RED */
            if (parent->left == node)
            {   /* case LLb */
                rotateright(grandparent, tree);
                grandparent->color = RED;
                parent->color = BLACK;
                return;
            }
            else
            {   /* case LRb */
                rotateleft(parent, tree);
                rotateright(grandparent, tree);
                grandparent->color = RED;
                node->color = BLACK;
                return;
            }
        }
        else
        {
            if (parent->left == node)
            {   /* case RLb */
                rotateright(parent, tree);
                rotateleft(grandparent, tree);
                grandparent->color = RED;
                node->color = BLACK;
                return;
            }
            else
            {   /* case RRb */
                rotateleft(grandparent, tree);
                grandparent->color = RED;
                parent->color = BLACK;
                return;
            }
        }


    }
}

tree_node *insert_key_into_tree(
    RBData * data,
    red_black_tree * tree)
{
    tree_node *new_node = (tree_node *) malloc(sizeof(tree_node));
    new_node->data = data;

    if (tree->head == NULL)
    {   /* if the tree is empty */ 
        tree->head = new_node;
        new_node->parent = NULL;
        new_node->left = NULL;
        new_node->right = NULL;
        new_node->color = BLACK;
        return new_node;
    }

    tree_node *actNode = tree->head;
    tree_node *parentNode = NULL;


    while (actNode != NULL)
    {
        parentNode = actNode;
        int compret = (tree->compare2) (actNode->data, data);
        if (compret > 0)
        {       /* take right neigbour */
            actNode = actNode->right;
        }
        else if (compret < 0)
        {       /* take left neigbour */
            actNode = actNode->left;
        }
        else
        {       /* found key, whoops data already exits! */
            return NULL;
        }
    }

    /* found position for insert !  
     * binary tree insertion always at a leaf node: */
    if ((tree->compare2) (parentNode->data, data) > 0)
    {   /* take right neigbour */
        parentNode->right = new_node;
    }
    else
    {   /* take left neigbour */
        parentNode->left = new_node;
    }
    new_node->parent = parentNode;
    new_node->left = NULL;
    new_node->right = NULL;
    new_node->color = RED;

    /* check if red-black-tree conditions are held */   
    rebuildRBTree(tree, new_node);
    /* insert RedBlack condition is now held. */
    return new_node;
}

static void rebuildTreeAfterDeletion(
    red_black_tree * tree,
    tree_node * node)
{
    tree_node *parent = node->parent;
    if (node->color == BLACK)
    {
        /* complex cases:  */
        if (parent != NULL)
        {
            /* delete case 2 */                         
            tree_node *sibling = nodesibling(node);
            if (sibling != NULL && sibling->color == RED)
            {
                parent->color = RED;
                sibling->color = BLACK;
                if (node == parent->left)
                {
                    rotateleft(parent, tree);
                }
                else
                {
                    rotateright(parent, tree);
                }
                sibling = nodesibling(node);
            }
            /* delete case 3  */
            if (parent->color == BLACK
                && (sibling == NULL
                    || (sibling->color == BLACK
                        && (sibling->left == NULL
                            || sibling->left->color == BLACK)
                        && (sibling->right == NULL
                            || sibling->right->color == BLACK))))
            {
                if (sibling != NULL)
                    sibling->color = RED;
                rebuildTreeAfterDeletion(tree, node->parent);
            }
            else
            {
                /* delete case 4  */
                if (parent->color == RED
                    && (sibling == NULL
                        || (sibling->color == BLACK
                            && (sibling->left == NULL
                                || sibling->left->color == BLACK)
                            && (sibling->right == NULL
                                || sibling->right->color == BLACK))))
                {
                    sibling->color = RED;
                    parent->color = BLACK;
                }
                else
                {
                    /* delete case 5  */
                    if (parent->left == node && sibling->color == BLACK &&
                        sibling->left != NULL && sibling->left->color == RED &&
                        (sibling->right == NULL
                         || sibling->right->color == BLACK))
                    {
                        sibling->color = RED;
                        sibling->left->color = BLACK;
                        rotateright(sibling, tree);
                    }
                    else if (parent->right == node && sibling->color == BLACK &&
                             sibling->right != NULL
                             && sibling->right->color == RED
                             && (sibling->left == NULL
                                 || sibling->left->color == BLACK))
                    {
                        sibling->color = RED;
                        sibling->right->color = BLACK;
                        rotateleft(sibling, tree);
                    }
                    /* delete case 6  */
                    parent = node->parent;
                    sibling = nodesibling(node);
                    sibling->color = parent->color;
                    parent->color = BLACK;
                    if (node == parent->left)
                    {
                        if (sibling->right != NULL)
                            sibling->right->color = BLACK;
                        rotateleft(parent, tree);
                    }
                    else
                    {
                        if (sibling->left != NULL)
                            sibling->left->color = BLACK;
                        rotateright(parent, tree);
                    }
                }
            }
        }       /* else new node is tree root, do nothing...  */
    }   
}

void delete_node_from_tree(
    tree_node * node,
    red_black_tree * tree)
{
    tree_node *changed = delete_node_from_tree2(node, tree);
    if (changed != NULL)
    {
        delete_node_from_tree2(changed, tree);
    }
}

tree_node *delete_node_from_tree2(
    tree_node * node,
    red_black_tree * tree)
{
    if (node->left != NULL && node->right != NULL)
    {   /* node has two children...
         * look for maximum value in the left subtree, O(log(N))  */
        tree_node *act_node;
        for (act_node = node->left; act_node->right != NULL;
             act_node = act_node->right);
        node->data = act_node->data;

        /* delete old node which has only one child  */
        return act_node;
    }

    tree_node *child = node->left == NULL ? node->right : node->left;
    int createdChild = 0;
    /* delete node which now has at most one child
     * use node as dummy child  */
    if (node->color == BLACK && node->parent != NULL && child == NULL)
    {
        child = node;
        child->data = NULL;
        createdChild = 1;
    }

    /* relink nodes  */
    if (node->parent != NULL)
    {
        if (node->parent->left == node)
        {
            node->parent->left = child;
        }
        else
        {
            node->parent->right = child;
        }
    }
    else
    {   /* node was tree head !!! */
        tree->head = child;
    }
    if (child != NULL)
    {
        child->parent = node->parent;
        if (node->color == BLACK)
        {
            if (child->color == RED)
            {
                child->color = BLACK;
            }
            else
            {
                /* start to rebuild tree...  */
                rebuildTreeAfterDeletion(tree, child);
            }
        }
    }

    if (createdChild)
    {
        tree_node *parent = child->parent;

        if (parent != NULL)
        {
            if (parent->left == child)
            {
                parent->left = NULL;
            }
            else
            {
                parent->right = NULL;
            }
        }
    }

    /* give node memory free */
    free(node);
    return NULL;
}

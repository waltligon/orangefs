/*
 * Author: Julian Kunkel 2005
 */
#ifndef REDBLACKTREE_H_
#define REDBLACKTREE_H_

#include <stdlib.h>

enum node_color
{
    RED = 0, BLACK = 1
};

typedef void RBData;
typedef void RBKey;


struct red_black_tree_node
{
    struct red_black_tree_node *parent;
    struct red_black_tree_node *left;
    struct red_black_tree_node *right;

    RBData *data;
    enum node_color color;
};

typedef struct red_black_tree_node tree_node;

struct red_black_tree_
{
    tree_node *head;
    int (*compare) (RBData * data,RBKey * key);
    int (*compare2) ( RBData * data, RBData * data2);
};

typedef struct red_black_tree_ red_black_tree;

/* functions */
tree_node *lookup_tree(
    RBKey * key,
    red_black_tree * tree);
    
red_black_tree *new_red_black_tree(
    int (*compare) (RBData * data,
                    RBKey * key),
    int (*compare2) (RBData * data,
                     RBData * data2));
                     
void free_empty_red_black_tree(
    red_black_tree ** tree);

tree_node *insert_key_into_tree(
    RBData * data,
    red_black_tree * tree);

void delete_node_from_tree(
    tree_node * node,
    red_black_tree * tree);
    
/* this function returns NULL if the node is deleted, or the node which 
 * changed position
 * this is necessary if the data contains references to the node :) */
tree_node *delete_node_from_tree2(
    tree_node * node,
    red_black_tree * tree);


void iterate_red_black_tree(
    void (*callback) (RBData * data, void *funcData),
    red_black_tree * tree,
    void *funcData);

/* example compare functions: */
int compare_int64(
    RBData * data,
    RBKey * key);

#endif /*REDBLACKTREE_H_ */

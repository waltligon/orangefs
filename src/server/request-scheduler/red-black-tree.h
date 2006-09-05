/*
 * Author: Its me..
 */
#ifndef REDBLACKTREE_H_
#define REDBLACKTREE_H_

#include <stdlib.h>

typedef void RBData;
typedef void RBKey;


struct red_black_tree_node{
	struct red_black_tree_node* parent;	
	struct red_black_tree_node* left;
	struct red_black_tree_node* right;

	RBData * data;
	char color;	
};

typedef struct red_black_tree_node tree_node;

struct red_black_tree_{
	tree_node * head;
	int (*compare)(RBData * data, RBKey * key);
	int (*compare2)(RBData * data, RBData * data2);	
};

typedef struct red_black_tree_ red_black_tree;

/* functions */
tree_node* lookupTree(RBKey *key, red_black_tree* tree);
red_black_tree *newRedBlackTree(int (*compare)(RBData * data, RBKey * key),
    int (*compare2)(RBData * data, RBData* data2));
void initRedBlackTree(red_black_tree * tree, 
    int (*compare)(RBData * data, RBKey * key),
    int (*compare2)(RBData * data, RBData* data2));

void freeEmptyRedBlackTree(red_black_tree ** tree);
 
tree_node * insertKeyIntoTree(RBData * data, red_black_tree* tree);

void deleteNodeFromTree(tree_node*node , red_black_tree* tree);
/* this function returns NULL if the node is deleted, or the node which changed position
   this is necessary if the data contains references to the node */
tree_node * deleteNodeFromTree2(tree_node*node , red_black_tree* tree);


/* callback function returns 0 if it wants to continue iterating */ 
int iterateRedBlackTree(int (*callback)(RBData* data, void* funcData), red_black_tree* tree,void* funcData);

/* example compare functions: */
int compareInt64(RBData * data, RBKey * key);

#endif /*REDBLACKTREE_H_*/


#include<stdlib.h>
#include<stdio.h>
#include<assert.h>
#include<math.h>

#include "red-black-tree.h"

int64_t numexists=0;

static int inorder_check(tree_node* node,int depth, int * nodes, int redNodes, int blackNodes, int * leafBlackNodes){
	if(node == NULL) return 0;
	int maxdepth = depth+1;
	int retdepth =0;

	if(depth == 0){ /* initialise leaf black nodes */
		*leafBlackNodes = -1;
	}	
	
	(*nodes)++;
	if(node->color == RED)
		redNodes++;
	else
		blackNodes++;
	if(node->left != NULL){
		if(node->color == RED && node->left->color == RED){
			printf("WARNING two red nodes ...");
			assert(0);
		}
		retdepth=inorder_check(node->left,depth+1, nodes,redNodes,blackNodes, leafBlackNodes);
		if(retdepth > maxdepth) maxdepth = retdepth;
		if(node->left->parent != node){
			printf("ERROR wrong parent node %lld parent %lld", *((int64_t*) node->left->data), *((int64_t*) node->data));
			assert(0);
		}
		if(compare_int64(node->left->data,node->data) != +1){
			printf("ERROR Left key!\n");
			assert(0);
		}
	}
	if(node->right != NULL){
		if(node->color == RED && node->right->color == RED){
			printf("WARNING two red nodes node and right son...");
			assert(0);
		}		
		if(compare_int64(node->right->data,node->data) != -1){
			printf("ERROR right key!\n");
			assert(0);
		}
		if(node->right->parent != node){
			printf("ERROR wrong parent node %lld parent %lld", *((int64_t*) node->right->data), *((int64_t*) node->data));
			assert(0);			
		}
		
		retdepth=inorder_check(node->right,depth+1, nodes,redNodes,blackNodes,leafBlackNodes);
		if(retdepth > maxdepth) maxdepth = retdepth;
	}	
	
	if(node->left == NULL && node->right == NULL){ /* leaf, check if blackNodeCondition is held */
		 if(*leafBlackNodes == -1){  /* first leaf is reached */
		 	*leafBlackNodes = blackNodes;
		 }
		 if(blackNodes != *leafBlackNodes){
		 	printf(" WARNING  blackNodes to leafs are different now:%d should be:%d condition not held\n",blackNodes, *leafBlackNodes);
		 	assert(0);
		 }
	}
	
	if(depth == 0){
		double nld2 = log((double) *nodes)/log(2.0)*2+1;
		if( maxdepth > nld2 )
			printf(" WARNING tree with %d nodes to deep should be max:%f is %d\n", *nodes, nld2, maxdepth);
	}
	return maxdepth;
}


static void create_random_tree_node(red_black_tree * tree){
	int num=rand()%10000;
	int64_t * data = malloc(sizeof(int64_t));
	*data = num;
    insert_key_into_tree( (RBData*)data, tree);
	numexists = num;
}

int main(int argc, char ** argv){
	
 	red_black_tree * tree = new_red_black_tree(compare_int64,compare_int64);
	int i;
	int nodecount=0,nodecountA;
	int blackNodes;
	int depth=0,depthA;
	
	RBData * data = malloc(sizeof(int));
	int one=1;
 	data = (RBData*) &one;
 	int two=2;
    printf("Testing default comparision function\n");
	printf("CompareInt64 1,2 result:%d\n",compare_int64((RBData*) &one,(RBData*) &two));
	printf("CompareInt64 2,1 result:%d\n",compare_int64((RBData*) &two,(RBData*) &one));
	printf("CompareInt64 1,1 result:%d\n",compare_int64((RBData*) &one,(RBData*) &one));	
	
	int seed=3;
	
	if(argc == 2){
		sscanf(argv[1],"%d",&seed);
	}
	
	if(seed == 0){
		printf("Syntax: %s randomSeedNum\n",argv[0]);
		exit(1);
	}
	/* for debugging purpose take argv as initialisation value */
	srand(seed);
	
	for(i=0; i < 5000; i++){
		create_random_tree_node(tree);
	    nodecountA = 0;		
	    depthA=inorder_check(tree->head,0,& nodecountA,0,0,& blackNodes);
	}
	printf("depth for %d nodes is %d\n",nodecountA,depthA);
	nodecount = -1;
	depth = -1;
	printf("Try to delete existing num %llu, %p\n", numexists, lookup_tree((void*)&numexists,tree) );
	
	for(i=0; i < 5000; i++){
		int64_t num=rand()%10000;
		if(lookup_tree((void*)&num,tree) == NULL) continue;
		tree_node * node = lookup_tree((void*)&num,tree);
		free(node->data);
    	delete_node_from_tree(node,tree);
	    nodecount = 0;
	    depth=inorder_check(tree->head,0,& nodecount,0,0,& blackNodes);
	}

	printf("Test correctly done\n");
	printf("depth for %d nodes is %d\n",nodecountA,depthA);
	printf("depth now for %d nodes is %d\n" ,nodecount,depth);

 	return 0;
}

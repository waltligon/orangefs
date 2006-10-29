#include "tas-handle-manager.h"
#include <stdio.h>
#include <assert.h>

static gen_mutex_t tas_handlemanager_mutex;
red_black_tree * collectionTree=NULL;
static int handleManagerInititalised = 0;

struct tas_handle_manager_{
	red_black_tree * handleTree;
	
	gen_mutex_t tree_mutex;
};

typedef struct tas_handle_manager_ tas_handle_manager;

/*
 * Tree for mapping the collection id to the handle_managers
 */
struct tas_handle_manager_collections_{
	TROVE_coll_id coll_id;
	TROVE_handle seperatorHandleNo;
	tas_handle_manager * firstHandleType;
	tas_handle_manager * secondHandleType;	
};

typedef struct tas_handle_manager_collections_ tas_handle_manager_collections;

/*
 * Comparision functions:
 */
static int compareCollections(RBData * data, RBKey * key){
	TROVE_coll_id ikey1=((tas_handle_manager_collections*)data)->coll_id;
	TROVE_coll_id ikey2=*((TROVE_coll_id *) key);	
	if(ikey1 > ikey2 ){ //take right neigbour
		return +1;
	}else if(ikey1 < ikey2){//take left neigbour
		return -1;
	}
	return 0;
}

static int compareCollections2(RBData * data, RBData * data2){
	TROVE_coll_id ikey1=((tas_handle_manager_collections*)data)->coll_id;
	TROVE_coll_id ikey2=((tas_handle_manager_collections*)data2)->coll_id;
	if(ikey1 > ikey2 ){ //take right neigbour
		return +1;
	}else if(ikey1 < ikey2){//take left neigbour
		return -1;
	}
	return 0;
}

//for comparision of handleRanges:
static int compareRanges2(RBData * data, RBData * data2){
	tas_handleRange * ikey1=(tas_handleRange *) data;
	tas_handleRange * ikey2=(tas_handleRange *) data2;
	
	//is no overlapping possible ?
	if(ikey1->last < ikey2->first ){ 
		return -1;
	}else if(ikey1->first > ikey2->last){
		return +1;
	}
	//it does overlapp !!!
	return 0;
}

/*
 * Helper functions:
 */
static inline void insertHandleExtend(tas_handle_manager* manager,TROVE_handle lowerBound, TROVE_handle upperBound){
	tas_handleRange* handles = (tas_handleRange*) malloc(sizeof(tas_handleRange));
	handles->first = lowerBound;
	handles->last = upperBound;
	insertKeyIntoTree(handles,manager->handleTree);
};

static inline tas_handle_manager* insertHandleRangeManager(TROVE_handle lowerBound, TROVE_handle upperBound) {
	tas_handle_manager* manager=(tas_handle_manager*) malloc(sizeof(tas_handle_manager));
	gen_mutex_init(& manager->tree_mutex);
	manager->handleTree = newRedBlackTree(compareRanges2,compareRanges2);
	insertHandleExtend(manager, lowerBound,upperBound);
	return manager;
}

static inline tas_handle_manager* lookupCollection(TROVE_coll_id coll_id, TROVE_handle handle){
  	tree_node * node =lookupTree(& coll_id,collectionTree);
  	if(node == NULL) return NULL;
  	tas_handle_manager_collections * collection = (tas_handle_manager_collections*) node->data;
  	if(handle > collection->seperatorHandleNo){
  		return collection->secondHandleType;
  	}else{
  		return collection->firstHandleType;
  	}
}
	
/*
 * 
 */
void initialiseHandleManager(void){
	if(!handleManagerInititalised){
		gen_mutex_init(& tas_handlemanager_mutex);
		gen_mutex_lock(& tas_handlemanager_mutex);
		collectionTree = newRedBlackTree(compareCollections,compareCollections2);
		gen_mutex_unlock(& tas_handlemanager_mutex);	
	}
	handleManagerInititalised = 1;
}


void initialiseCollection(TROVE_coll_id coll_id,TROVE_handle lowerBound, 
	TROVE_handle upperBound,TROVE_handle lowerBound2, TROVE_handle upperBound2){
		
	if(handleManagerInititalised != 1){
		printf("ERROR Handle manager is not initialised !\n");
		assert(0);
	}
	gen_mutex_lock(& tas_handlemanager_mutex);
	if( lookupTree(& coll_id, collectionTree) != NULL){
		printf("ERROR: collection already initialised !\n");
		assert(0);
	}
			
	tas_handle_manager_collections * coll_manager = 
		(tas_handle_manager_collections*) malloc(sizeof(tas_handle_manager_collections));
	coll_manager->coll_id = coll_id;
	coll_manager->seperatorHandleNo = upperBound;

	coll_manager->firstHandleType = insertHandleRangeManager(lowerBound, upperBound);
	if(lowerBound2 == 0){
		coll_manager->secondHandleType = NULL;
	}else{
		coll_manager->secondHandleType = insertHandleRangeManager(lowerBound2, upperBound2);		
	}
	coll_manager->coll_id = coll_id;

  	insertKeyIntoTree(coll_manager,collectionTree);
	gen_mutex_unlock(& tas_handlemanager_mutex);		
}

/*
 * Remove handle from available handles and return it !
 * Assume the handles will be never used up....
 */
TROVE_handle getHandle(TROVE_coll_id coll_id,TROVE_handle_extent_array * extends){
	tas_handle_manager* manager = lookupCollection(coll_id, extends->extent_array[0].first);
	if(manager == NULL) return TROVE_HANDLE_NULL;
	
	int i;
	TROVE_handle handle=TROVE_HANDLE_NULL;
	tree_node * node;
	gen_mutex_lock(& manager->tree_mutex);
	for(i=0; i < extends->extent_count ; i++){
		 node= lookupTree(& extends->extent_array[i],manager->handleTree);
		 if(node != NULL){
		 	//found a overlapping extend, take first :)
		 	
		 	tas_handleRange * nodeRange = (tas_handleRange *) node->data;
		 	handle=nodeRange->first;
		 	nodeRange->first++;
		 	if(nodeRange->first > nodeRange->last){ 
		 		//remove handle range:
		 		deleteNodeFromTree(node,manager->handleTree);
		 		free(nodeRange);
		 	}
		 	
		 	break;
		 }
	}
	gen_mutex_unlock(& manager->tree_mutex);	
	return handle;
}

TROVE_handle getArbitraryHandle(TROVE_coll_id coll_id,TROVE_handle handleSep){
	tas_handle_manager* manager = lookupCollection(coll_id,handleSep);
	if(manager == NULL) return TROVE_HANDLE_NULL;

	TROVE_handle handle=TROVE_HANDLE_NULL;
	gen_mutex_lock(& manager->tree_mutex);
   	tas_handleRange * nodeRange = (tas_handleRange *) manager->handleTree->head->data;
   	handle=nodeRange->first;
 	nodeRange->first++;
 	if(nodeRange->first > nodeRange->last){
 		//remove handle range:
 		deleteNodeFromTree(manager->handleTree->head,manager->handleTree);
 		free(nodeRange);
 	}
	gen_mutex_unlock(& manager->tree_mutex);
	return handle;
}

/*
 * May split handle range into two parts
 */
TROVE_handle getfixedHandle(TROVE_coll_id coll_id,TROVE_handle handle){
	tas_handle_manager* manager = lookupCollection(coll_id, handle);
	if(manager == NULL) return TROVE_HANDLE_NULL;
	PVFS_handle_extent handleExtend;
	handleExtend.first = handle;
	handleExtend.last = handle;

	tree_node * node;
	gen_mutex_lock(& manager->tree_mutex);
	node= lookupTree(& handleExtend, manager->handleTree);
	if(node != NULL){
	 	//found a extend which contains handle
	 	tas_handleRange * nodeRange = (tas_handleRange *) node->data;
	 	
	 	if(nodeRange->first < handle && nodeRange->last > handle){
	 		//split handle range into two parts ...
	 		//change data of current node (does not destroy tree order)
	 		TROVE_handle tmpRangeFirst = nodeRange->first;
	 		nodeRange->first = handle+1;
	 		insertHandleExtend(manager, tmpRangeFirst, handle-1);
	 	}else{
		 	if(nodeRange->first == handle){
		 		nodeRange->first++;
		 	}else if(nodeRange->last == handle){
		 		nodeRange->last--;
		 	}
			if(nodeRange->first > nodeRange->last){
				//remove handle range:
				deleteNodeFromTree(node,manager->handleTree);
				free(nodeRange);
			}
	 	}
 		gen_mutex_unlock(& manager->tree_mutex);	
 		return handle;
	}
	gen_mutex_unlock(& manager->tree_mutex);	
	return TROVE_HANDLE_NULL;
}

/*
 * Free the handle by adding it to a handle range !
 * Maybe fix entries together in handle manager...
 */
int freeHandle(TROVE_coll_id coll_id,TROVE_handle handle){
	return freeHandleRange(coll_id, handle,handle);
}

int freeHandleRange(TROVE_coll_id coll_id,TROVE_handle lowerhandle,TROVE_handle upperhandle){
	tas_handle_manager* manager = lookupCollection(coll_id, upperhandle);
	if(manager == NULL) return -1;
	
	tree_node * node;
	PVFS_handle_extent handleExtend;
	handleExtend.first = lowerhandle;
	handleExtend.last = upperhandle;
	gen_mutex_lock(& manager->tree_mutex);
	node= lookupTree(& handleExtend, manager->handleTree);
	if(node != NULL){
		gen_mutex_unlock(& manager->tree_mutex);	
		//at least one handle in range is already free, this should not happen !
		return -1;
	}
	
	//try to find near handles +- 1...
	handleExtend.first = lowerhandle-1;
	handleExtend.last = upperhandle+1;
	node= lookupTree(& handleExtend, manager->handleTree);
	if(node != NULL){
		//found one merge them
	 	tas_handleRange * nodeRange = (tas_handleRange *) node->data;
		if(nodeRange->first == upperhandle+1){
			nodeRange->first = lowerhandle;			
		}else if(nodeRange->last +1 == lowerhandle){
			nodeRange->last = upperhandle;
		}else{
			assert(0);
		}
	 	//try to recursively merge entries
	 	int changed = 1;
	 	while(changed){
			changed = 0;	 		
	 		tree_node * newnode;	 			 		
	 		//find left side extend:
			handleExtend.first = nodeRange->first-1;
			handleExtend.last  = nodeRange->first-1;
			newnode= lookupTree(& handleExtend, manager->handleTree);
			if(newnode != NULL){
				changed = 1;
			 	tas_handleRange * nodeRangeNew = (tas_handleRange *) newnode->data;
				nodeRange->first = nodeRangeNew->first;
				deleteNodeFromTree(newnode,manager->handleTree);				
			}
			
			//find right side extend:
			handleExtend.first = nodeRange->last+1;
			handleExtend.last  = nodeRange->last+1;
			newnode= lookupTree(& handleExtend, manager->handleTree);			
			if(newnode != NULL){
				changed = 1;
			 	tas_handleRange * nodeRangeNew = (tas_handleRange *) newnode->data;
				nodeRange->last = nodeRangeNew->last;
				deleteNodeFromTree(newnode,manager->handleTree);				
			}
	 	}
	 	
	}else{
		//create a new single entry...
		//this should be optimized later
		insertHandleExtend(manager, lowerhandle, upperhandle);
	}
	
	gen_mutex_unlock(& manager->tree_mutex);	
	
	
	return 0;
}

int lookupHandle(TROVE_coll_id coll_id,TROVE_handle handle){
	tas_handle_manager* manager = lookupCollection(coll_id, handle);
	if(manager == NULL) return 0;
	
	tree_node * node;
	PVFS_handle_extent handleExtend;
	handleExtend.first = handle;
	handleExtend.last = handle;
	gen_mutex_lock(& manager->tree_mutex);
	node= lookupTree(& handleExtend, manager->handleTree);
	gen_mutex_unlock(& manager->tree_mutex);
	if(node == NULL) return 0;
	return 1;
}


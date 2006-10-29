#include<stdio.h>
#include "tas-queue.h"

tas_queue *newTasQueue(void){
	tas_queue * ret=malloc(sizeof(tas_queue));
	*ret = NULL;
	return ret;
}

void relinkFront(queue_elem * elem, tas_queue* queue){
		if(elem->prev != NULL){ 
			//put found element at front of list...
			queue_elem * tmp = elem->prev;				
			(*queue)->prev = elem; //is NULL before
			if(elem->next != NULL){
				elem->next->prev = elem->prev;
			}
			tmp->next = elem->next;
			//now make found the first element...
			elem->next = *queue;
			elem->prev = NULL;
			*queue = elem;
		}
}

void *  insertKeyIntoQueue(queueKey key, void * data, tas_queue* queue){
	queue_elem * elem = lookupQueue(key, queue);
	void * olddata=NULL;
	if(elem == NULL){
		pushFrontKey(key, data,queue);
	}else{
		//replace existing data
		olddata = elem->data;
		elem->data = data;
		relinkFront(elem,queue);
	}
	return olddata;
}

queue_elem * pushFrontKey(queueKey key, void * data, tas_queue* queue){
	queue_elem * elem = malloc(sizeof(queue_elem));
	elem->data = data;
	elem->key = key;
	elem->prev = NULL;
	elem->next = *queue;
	if(*queue != NULL){
		(*queue)->prev = elem;
	}
	*queue = elem;
	return elem;
}

queue_elem * lookupQueue(queueKey key, tas_queue * queue){
	queue_elem * found=*queue;
	int i=0;
	for(; found != NULL ; found = found->next){
		if(found->key == key){
			if( i > WARNING_NUM){
				printf(" WARNING: lookupQueue takes %d iterations\n",i);
			}
			return found;
		}
		i++;
	}
	if( i > WARNING_NUM){
		printf(" WARNING: lookupQueue takes %d iterations and did not find handle\n",i);
	}	
	return NULL;
}

void deleteElementFromList(queue_elem * found, tas_queue* queue){
		if(found->prev == NULL){ //first entry
			*queue = found->next;
			if(found->next != NULL){
				found->next->prev = NULL;
			}
		}else{ //not first entry
			found->prev->next = found->next;
			if(found->next != NULL){
				found->next->prev = found->prev;
			}
		}
}

void * deleteKeyFromList(queueKey key, tas_queue* queue){
	queue_elem * found=lookupQueue(key,queue);
	if(found == NULL) return NULL;
	deleteElementFromList(found,queue);
	void * data;			
	data = found->data;
	free(found); 
	return data;
}




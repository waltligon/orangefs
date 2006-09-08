#include <stdio.h>
#include "simplequeue.h"

simplequeue *new_simple_queue(void){
	simplequeue * ret=malloc(sizeof(simplequeue));
	ret->head = NULL;
	ret->tail = NULL;	
	return ret;
}

void init_simple_queue(simplequeue * queue){
    queue->head = NULL;
    queue->tail = NULL;   
}

void relink_front_simple(simplequeue_elem * elem, simplequeue* queue){
		if(elem->prev != NULL){ 
			/*put found element at front of list...*/
			simplequeue_elem * tmp = elem->prev;				
			queue->head->prev = elem; /*is NULL before*/
			if(elem->next != NULL){
				elem->next->prev = elem->prev;
			}else{
				queue->tail = elem->prev;
			}
			tmp->next = elem->next;
			//now make found the first element...
			elem->next = queue->head;
			elem->prev = NULL;
			queue->head = elem;
		}
}

void * pop_back_simple(simplequeue* queue){
	if(queue->tail == NULL) return NULL;
	void * data=queue->tail->data;
	if(queue->tail->prev == NULL){
		queue->head = NULL;
        free(queue->tail);
		queue->tail = NULL;
	}else{
		queue->tail = queue->tail->prev;
		free(queue->tail->next);
		queue->tail->next = NULL;
	}
	return data;
}

int simple_queue_is_empty(simplequeue* queue)
{
    return queue->head == NULL;
}

simplequeue_elem * push_front_simple(void * data, simplequeue* queue){
	simplequeue_elem * elem = malloc(sizeof(simplequeue_elem));
    if( ! elem )
    {
        return NULL;
    }
	elem->data = data;
	elem->prev = NULL;
	elem->next = queue->head;
	if(queue->head != NULL){
		queue->head->prev = elem;
	}else{
		queue->tail = elem;
	}
	queue->head = elem;
	return elem;
}

void delete_element_from_simple(simplequeue_elem * found, simplequeue* queue){
		if(found->prev == NULL){ //first entry
			queue->head = found->next;
			if(found->next != NULL){
				found->next->prev = NULL;
			}else{
				queue->tail = found->prev;
			}
		}else{ /*not first entry*/
			found->prev->next = found->next;
			if(found->next != NULL){
				found->next->prev = found->prev;
			}else{
				queue->tail = found->prev;
			}
		}
}


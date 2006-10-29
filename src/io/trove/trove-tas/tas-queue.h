/*
 * Author Julian Kunkel
 * Simple double linked list with puts object 
 * 	to front of list when it is accessed.
 */

#ifndef TASQUEUE_H_
#define TASQUEUE_H_

#include<stdlib.h>

/*
 * Print a warning if lookup takes more than WARNING_NUM iterations
 * Performance decreases the more elements need to be inspected...
 */
#define WARNING_NUM 40

typedef int64_t queueKey;

struct tas_queue_elem_{
	struct tas_queue_elem_ * next;
 	struct tas_queue_elem_ * prev;

	queueKey key;
 	void * data;
};

typedef struct tas_queue_elem_ queue_elem;

//currently queue and queue_elem are equal,
//but maybe some elements will be added later so:
typedef struct tas_queue_elem_* tas_queue;


//functions:
tas_queue *newTasQueue(void);

queue_elem * lookupQueue(queueKey key, tas_queue* queue);

void deleteElementFromList(queue_elem * elem, tas_queue* queue);
void * deleteKeyFromList(queueKey key, tas_queue* queue);
//lookup key and replace data if possible, else pushfront.
//returns replaced data (if key exists)
//Queue element is put to head of queue
void * insertKeyIntoQueue(queueKey key, void * data, tas_queue* queue);
queue_elem * pushFrontKey(queueKey key, void * data, tas_queue* queue);

//remove data from current possition and push it to head of queue,
void relinkFront(queue_elem * elem, tas_queue* queue);


#define iterate_tas_queue(func,elem,queue){ 		\
	queue_elem * elem=*queue; 					    \
	for(; elem != NULL ; elem = elem->next){        \
		(func); 									\
	} 											    \
}                                         

#endif /*TASQUEUE_H_*/

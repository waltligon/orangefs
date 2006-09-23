#ifndef ALTSIMPLEQUEUE_H_
#define ALTSIMPLEQUEUE_H_

#include <stdlib.h>

struct simplequeue_elem_{
	struct simplequeue_elem_ * next;
 	struct simplequeue_elem_ * prev;

 	void * data;
};

typedef struct simplequeue_elem_ simplequeue_elem;

struct simplequeue_{
	simplequeue_elem* head;
	simplequeue_elem* tail;
};
typedef struct simplequeue_ simplequeue;


/* functions: */
simplequeue *new_simple_queue(void);
void init_simple_queue(simplequeue * queue);

void delete_element_from_simple(simplequeue_elem * elem, simplequeue* queue);

simplequeue_elem * push_front_simple(void * data, simplequeue* queue);
int simple_queue_is_empty(simplequeue* queue);

/* returns data: */
void * pop_back_simple(simplequeue* queue);

/*remove data from current possition and push it to head of queue,*/
void relink_front_simple(simplequeue_elem * elem, simplequeue* queue);

#define ITERATE_SIMPLEQUE(queue, q_elem, x) \
    for(q_elem = queue.head; q_elem != NULL; q_elem = q_elem->next) \
    { x \
    }

#endif /*ALTSIMPLEQUEUE_H_*/

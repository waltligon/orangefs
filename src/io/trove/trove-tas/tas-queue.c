#include<stdio.h>
#include "tas-queue.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

tas_queue *new_tas_queue(
    void)
{
    tas_queue *ret = malloc(sizeof(tas_queue));
    *ret = NULL;
    return ret;
}

void relink_front(
    queue_elem * elem,
    tas_queue * queue)
{
    if (elem->prev != NULL)
    {
        /* put found element at front of list...  */
        queue_elem *tmp = elem->prev;
        (*queue)->prev = elem;  /* is NULL before  */
        if (elem->next != NULL)
        {
            elem->next->prev = elem->prev;
        }
        tmp->next = elem->next;
        /* now make found the first element...  */
        elem->next = *queue;
        elem->prev = NULL;
        *queue = elem;
    }
}

void *insert_key_into_queue(
    queue_key key,
    void *data,
    tas_queue * queue)
{
    queue_elem *elem = lookup_queue(key, queue);
    void *olddata = NULL;
    if (elem == NULL)
    {
        push_front_key(key, data, queue);
    }
    else
    {
        /* replace existing data  */
        olddata = elem->data;
        elem->data = data;
        relink_front(elem, queue);
    }
    return olddata;
}

queue_elem *push_front_key(
    queue_key key,
    void *data,
    tas_queue * queue)
{
    queue_elem *elem = malloc(sizeof(queue_elem));
    elem->data = data;
    elem->key = key;
    elem->prev = NULL;
    elem->next = *queue;
    if (*queue != NULL)
    {
        (*queue)->prev = elem;
    }
    *queue = elem;
    return elem;
}

queue_elem *lookup_queue(
    queue_key key,
    tas_queue * queue)
{
    queue_elem *found = *queue;
    int i = 0;
    for (; found != NULL; found = found->next)
    {
        if (found->key == key)
        {
            if (i > WARNING_NUM)
            {
                printf(" WARNING: lookupQueue takes %d iterations\n", i);
            }
            return found;
        }
        i++;
    }
    if (i > WARNING_NUM)
    {
        printf
            (" WARNING: lookupQueue takes %d iterations and did not find handle\n",
             i);
    }
    return NULL;
}

void delete_element_from_list(
    queue_elem * found,
    tas_queue * queue)
{
    if (found->prev == NULL)
    {   /* first entry  */
        *queue = found->next;
        if (found->next != NULL)
        {
            found->next->prev = NULL;
        }
    }
    else
    {   /* not first entry  */
        found->prev->next = found->next;
        if (found->next != NULL)
        {
            found->next->prev = found->prev;
        }
    }
}

void *delete_key_from_list(
    queue_key key,
    tas_queue * queue)
{
    queue_elem *found = lookup_queue(key, queue);
    if (found == NULL)
        return NULL;
    delete_element_from_list(found, queue);
    void *data;
    data = found->data;
    free(found);
    return data;
}

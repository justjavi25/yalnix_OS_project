/* Queue.h */
#ifndef QUEUE_H
#define QUEUE_H

#include "process.h"


typedef struct Queue {
    pcb_t *head;
    pcb_t *tail;
    int    size;
} Queue_t;

/*
 * Initialize an empty queue.
 */
void Queue_init(Queue_t *q);

/*
 * Return 1 if queue is empty, 0 otherwise.
 */
int Queue_empty(Queue_t *q);

/*
 * Add process to the back of the queue.
 *
 */
void Queue_push(Queue_t *q, pcb_t *proc);

/*
 * Remove and return process from the front.
 */
pcb_t *Queue_pop(Queue_t *q);

/*
 * Remove a specific process from anywhere in the queue.
 */

 void Queue_remove(Queue_t *q, pcb_t *proc);

#endif /* QUEUE_H */

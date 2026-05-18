/* Queue.h */
#ifndef QUEUE_H
#define QUEUE_H

#include "process.h"

/*
 * Simple doubly linked queue for PCBs.
 *
 * Used for:
 *   - ready queue
 *   - blocked queue
 *   - delay queue
 *   - per-terminal read/write queues
 *
 * Design pseudocode:
 *
 * struct Queue {
 *     pcb_t *head;
 *     pcb_t *tail;
 *     int size;
 * }
 *
 * PCBs are linked via their next/prev pointers.
 */

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
 * Pseudocode:
 *   - Set proc->prev = q->tail.
 *   - Set proc->next = NULL.
 *   - If queue empty: head = proc.
 *   - Else: tail->next = proc.
 *   - tail = proc.
 *   - size++.
 */
void Queue_push(Queue_t *q, pcb_t *proc);

/*
 * Remove and return process from the front.
 *
 * Pseudocode:
 *   - If empty: return NULL.
 *   - front = head.
 *   - head = front->next.
 *   - If head: head->prev = NULL.
 *   - Else: tail = NULL.
 *   - size--.
 *   - Return front.
 */
pcb_t *Queue_pop(Queue_t *q);

/*
 * Remove a specific process from anywhere in the queue.
 *
 * Pseudocode:
 *   - Find proc in queue.
 *   - Relink prev and next neighbors.
 *   - Update head/tail if needed.
 *   - size--.
 */
void Queue_remove(Queue_t *q, pcb_t *proc);

#endif /* QUEUE_H */

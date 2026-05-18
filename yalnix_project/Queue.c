/* Queue.c */
#include "Queue.h"

/*
 * TODO Checkpoint 2:
 * Implement all queue operations.
 * See pseudocode in Queue.h.
 */

void Queue_init(Queue_t *q)
{
    if (q == NULL) return;
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

int Queue_empty(Queue_t *q)
{
    if (q == NULL) return 1;
    return (q->size == 0);
}

void Queue_push(Queue_t *q, pcb_t *proc)
{
    (void) q;
    (void) proc;
    TracePrintf(1, "Queue_push: stub\n");
}

pcb_t *Queue_pop(Queue_t *q)
{
    (void) q;
    TracePrintf(1, "Queue_pop: stub\n");
    return NULL;
}

void Queue_remove(Queue_t *q, pcb_t *proc)
{
    (void) q;
    (void) proc;
    TracePrintf(1, "Queue_remove: stub\n");
}

#include "queue.h"
#include "process.h"
#include <stdlib.h>
#include <ylib.h>

//initialize a process queue to be empty.
void InitProcessQueue(process_queue_t *queue)
{
    //start with no processes in the queue.
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

//add a process to the back of the ready queue.
void EnqueueProcess(process_queue_t *queue, pcb_t *proc)
{
    //allocate a node to hold this process.
    queue_node_t *node = (queue_node_t *)malloc(sizeof(queue_node_t));

    if (node == NULL) {
        //cannot allocate queue node, process cannot be queued.
        TracePrintf(0, "EnqueueProcess: malloc failed for PID %d\n", proc->pid);
        return;
    }

    TracePrintf(1, "EnqueueProcess: Adding PID %d, queue count before=%d\n", proc->pid, queue->count);

    //store the process pointer in the node.
    node->process = proc;
    node->next = NULL;

    if (queue->head == NULL) {
        //queue is empty, this is the first process.
        queue->head = node;
        queue->tail = node;
    } else {
        //add to the back of the queue.
        queue->tail->next = node;
        queue->tail = node;
    }

    //increment the process count.
    queue->count++;
}

//peek at the front of the queue without removing it.
pcb_t *PeekProcess(process_queue_t *queue)
{
    //check if queue is empty.
    if (queue->head == NULL) {
        return NULL;
    }

    //return the process from the front without removing the node.
    return queue->head->process;
}

//remove a process from the front of the ready queue.
pcb_t *DequeueProcess(process_queue_t *queue)
{
    //check if queue is empty.
    if (queue->head == NULL) {
        TracePrintf(1, "DequeueProcess: queue empty, count=%d\n", queue->count);
        return NULL;
    }

    //get the front node.
    queue_node_t *node = queue->head;

    //get the process from the front.
    pcb_t *proc = node->process;

    TracePrintf(1, "DequeueProcess: Removing PID %d, queue count=%d\n", proc->pid, queue->count);

    //move head to the next node.
    queue->head = node->next;

    if (queue->head == NULL) {
        //queue is now empty, update tail.
        queue->tail = NULL;
    }

    //free the node.
    free(node);

    //decrement the process count.
    queue->count--;

    //return the process.
    return proc;
}

//return the number of processes in the queue.
int GetProcessQueueCount(process_queue_t *queue)
{
    return queue->count;
}

//check if a specific process is in the queue.
int IsProcessInQueue(process_queue_t *queue, pcb_t *proc)
{
    //walk through the queue looking for the process.
    queue_node_t *node = queue->head;

    while (node != NULL) {
        if (node->process == proc) {
            //found the process in the queue.
            return 1;
        }
        node = node->next;
    }

    //process not found in queue.
    return 0;
}

//remove a specific process from the queue (for blocking).
int RemoveProcessFromQueue(process_queue_t *queue, pcb_t *proc)
{
    //check if queue is empty.
    if (queue->head == NULL) {
        return 0;
    }

    //special case: removing the head.
    if (queue->head->process == proc) {
        queue_node_t *node = queue->head;
        queue->head = node->next;

        if (queue->head == NULL) {
            //queue is now empty.
            queue->tail = NULL;
        }

        free(node);
        queue->count--;
        return 1;
    }

    //search through the rest of the queue.
    queue_node_t *prev = queue->head;
    queue_node_t *curr = queue->head->next;

    while (curr != NULL) {
        if (curr->process == proc) {
            //found it, remove from middle or end.
            prev->next = curr->next;

            if (curr == queue->tail) {
                //was at the tail, update tail.
                queue->tail = prev;
            }

            free(curr);
            queue->count--;
            return 1;
        }

        prev = curr;
        curr = curr->next;
    }

    //process not found in queue.
    return 0;
}

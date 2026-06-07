#ifndef QUEUE_H
#define QUEUE_H

//forward declaration to avoid circular dependency.
typedef struct pcb pcb_t;

//node in the process queue.
typedef struct queue_node {
    //pointer to a PCB.
    pcb_t *process;
    //pointer to next node in queue.
    struct queue_node *next;
} queue_node_t;

//process queue for ready processes.
typedef struct {
    //head of the queue (next to dequeue).
    queue_node_t *head;
    //tail of the queue (where to enqueue).
    queue_node_t *tail;
    //number of processes in queue.
    int count;
} process_queue_t;

//initialize a process queue to be empty.
void InitProcessQueue(process_queue_t *queue);

//add a process to the back of the ready queue.
void EnqueueProcess(process_queue_t *queue, pcb_t *proc);

//peek at the front of the queue without removing it.
pcb_t *PeekProcess(process_queue_t *queue);

//remove a process from the front of the ready queue.
pcb_t *DequeueProcess(process_queue_t *queue);

//return the number of processes in the queue.
int GetProcessQueueCount(process_queue_t *queue);

//check if a specific process is in the queue.
int IsProcessInQueue(process_queue_t *queue, pcb_t *proc);

//remove a specific process from the queue (for blocking).
int RemoveProcessFromQueue(process_queue_t *queue, pcb_t *proc);

#endif

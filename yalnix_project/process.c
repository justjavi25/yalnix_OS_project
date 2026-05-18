/* process.c */
#include "process.h"

pcb_t *current_process = NULL;
pcb_t *idle_process    = NULL;

/*
 * TODO Checkpoint 2 and beyond:
 * Implement all functions declared in process.h.
 * See pseudocode in each function comment.
 */

void process_init(void)
{
    TracePrintf(1, "process_init: stub\n");
}

pcb_t *alloc_pcb(void)
{
    TracePrintf(1, "alloc_pcb: stub\n");
    return NULL;
}

void free_pcb(pcb_t *proc)
{
    (void) proc;
    TracePrintf(1, "free_pcb: stub\n");
}

void context_switch(pcb_t *next, UserContext *uctxt)
{
    (void) next;
    (void) uctxt;
    TracePrintf(1, "context_switch: stub\n");
}

void clone_kernel_context(pcb_t *new_proc)
{
    (void) new_proc;
    TracePrintf(1, "clone_kernel_context: stub\n");
}




KernelContext *KCSwitch(KernelContext *kc_in,
                        void *curr_pcb_p,
                        void *next_pcb_p)
{
  

    (void) curr_pcb_p;
    (void) next_pcb_p;

    TracePrintf(1, "KCSwitch: stub\n");

    return kc_in;
}

KernelContext *KCCopy(KernelContext *kc_in,
                      void *new_pcb_p,
                      void *not_used)
{
  

    (void) new_pcb_p;
    (void) not_used;

    TracePrintf(1, "KCCopy: stub\n");

    return kc_in;
}

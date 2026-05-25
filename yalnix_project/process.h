#ifndef PROCESS_H
#define PROCESS_H

#include <hardware.h>

//minimal process control block needed for checkpoint 2.
typedef struct pcb {
    //process id assigned by helper_new_pid.
    int pid;

    //Region 1 page table for this process.
    pte_t *region1_pt;

    //saved user-mode context for this process.
    UserContext user_context;

    //physical pages currently used for this process's kernel stack.
    int kernel_stack_pages[KERNEL_STACK_MAXSIZE / PAGESIZE];
} pcb_t;

//the process currently running or about to return to user mode.
extern pcb_t *current_process;

//the special idle process.
extern pcb_t *idle_process;

//initializes process-level globals.
void InitProcessSystem(void);

//creates the checkpoint-2 idle process.
pcb_t *CreateIdleProcess(UserContext *boot_context);

//idle loop used as the first user-mode target for checkpoint 2.
void DoIdle(void);

#endif

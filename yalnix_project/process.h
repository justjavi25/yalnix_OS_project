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

    //saved kernel-mode context for this process (used by KernelContextSwitch).
    KernelContext kernel_context;

    //physical pages currently used for this process's kernel stack.
    int kernel_stack_pages[KERNEL_STACK_MAXSIZE / PAGESIZE];
} pcb_t;

//the process currently running or about to return to user mode.
extern pcb_t *current_process;

//the special idle process.
extern pcb_t *idle_process;

//the special init process.
extern pcb_t *init_process;

//initializes process-level globals.
void InitProcessSystem(void);

//creates the checkpoint-2 idle process.
pcb_t *CreateIdleProcess(UserContext *boot_context);

//idle loop used as the first user-mode target for checkpoint 2.
void DoIdle(void);

//creates the init process PCB and loads its program into Region 1.
pcb_t *CreateInitProces(UserContext *init_context, char *name, char **args);

//loads a Linux executable into a process's Region 1 address space.
int LoadProgram(char *name, char *args[], pcb_t *proc);

//KCSFunc_t for cloning the current kernel context+stack into a new process.
KernelContext *KCCopy(KernelContext *kc_in, void *new_pcb_p, void *not_used);

#endif

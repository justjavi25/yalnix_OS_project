#ifndef PROCESS_H
#define PROCESS_H

#include <hardware.h>
#include "queue.h"

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

    //lowest Region 1 page not used by the loaded program's data/heap.
    int brk_page;

    //lowest legal heap break page for this process.
    int min_brk_page;

    //first Region 1 page currently mapped for the user stack.
    int stack_base_page;

    //nonzero when the process is blocked in Delay.
    int delayed;

    //clock tick on or after which a delayed process can run again.
    int wake_tick;

    //parent process pointer for Wait syscall.
    struct pcb *parent;

    //nonzero if this process has exited and is waiting to be reaped.
    int is_zombie;

    //exit status saved for parent to retrieve via Wait.
    int exit_status;

    //nonzero if parent is blocked waiting for this child.
    int parent_waiting;

    //nonzero if this process is blocked in Wait.
    int wait_blocked;

    //user-space status pointer supplied to Wait while blocked.
    int *wait_status_ptr;

    //saved status to copy out when a blocked Wait resumes.
    int wait_status_ready;
    int wait_status_value;

    //nonzero if this process has actually run on the hardware yet.
    int has_run;

    //child side of Fork should force the syscall return value to 0.
    int fork_return_zero;

    //nonzero if blocked on terminal output.
    int tty_write_blocked;

    //terminal output bookkeeping for TtyWrite.
    int tty_id;
    char *tty_write_buf;
    int tty_write_len;
    int tty_write_offset;

    //nonzero if blocked on terminal input.
    int tty_read_blocked;

    //terminal input bookkeeping for TtyRead.
    int tty_read_id;
    void *tty_read_buf;
    int tty_read_len;

    //nonzero if blocked on a pipe read/write.
    int pipe_read_blocked;
    int pipe_write_blocked;
    int pipe_id;
    void *pipe_read_buf;
    int pipe_read_len;
    char *pipe_write_buf;
    int pipe_write_len;
    int pipe_write_offset;

    //nonzero if blocked on a lock or condition variable.
    int lock_blocked;
    int cvar_blocked;
    int waiting_lock_id;
    int waiting_cvar_id;
    int cvar_wait_lock_id;
} pcb_t;

//the process currently running or about to return to user mode.
extern pcb_t *current_process;

//the special idle process.
extern pcb_t *idle_process;

//the special init process.
extern pcb_t *init_process;

//the ready queue of processes available to run.
extern process_queue_t ready_queue;

//all processes that have not been fully reaped/freed.
extern process_queue_t all_processes;

//initializes process-level globals.
void InitProcessSystem(void);

//creates the checkpoint-2 idle process.
pcb_t *CreateIdleProcess(UserContext *boot_context);

//idle loop used as the first user-mode target for checkpoint 2.
void DoIdle(void);

//creates the init process PCB and loads its program into Region 1.
pcb_t *CreateInitProcess(UserContext *init_context, char *name, char **args);

//loads a Linux executable into a process's Region 1 address space.
int LoadProgram(char *name, char *args[], pcb_t *proc);

//KCSFunc_t for cloning the current kernel context+stack into a new process.
KernelContext *KCCopy(KernelContext *kc_in, void *new_pcb_p, void *not_used);

//KCSFunc_t for switching from one existing process to another.
KernelContext *KCSwitch(KernelContext *kc_in, void *old_pcb_p, void *new_pcb_p);

//create a new process by cloning the current process (for Fork).
pcb_t *CloneProcess(pcb_t *parent);

//free all resources used by a process (for process death).
void FreeProcess(pcb_t *proc);

//record/remove a process in the global process list.
void RegisterProcess(pcb_t *proc);
void UnregisterProcess(pcb_t *proc);

#endif

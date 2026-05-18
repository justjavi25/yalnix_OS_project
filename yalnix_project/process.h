/* process.h */
#ifndef PROCESS_H
#define PROCESS_H

#include <hardware.h>
#include <yalnix.h>

/*
 * Process states.
 */
typedef enum {
    PROC_RUNNING = 0,
    PROC_READY,
    PROC_BLOCKED,
    PROC_ZOMBIE,
    PROC_DEAD
} proc_state_t;

/*
 * Process Control Block (PCB).
 *
 * Design pseudocode:
 *
 * Each process needs:
 *
 * - pid
 *     Unique process identifier.
 *     Obtained at creation via helper_new_pid(region1_pt).
 *     Released at death via helper_retire_pid(pid).
 *
 * - state
 *     Current lifecycle state of the process.
 *
 * - user_context
 *     Full copy of UserContext.
 *     Saved on every trap entry.
 *     Restored on every trap exit.
 *
 * - kernel_context
 *     Full copy of KernelContext.
 *     Saved/restored by KernelContextSwitch.
 *     Must be stored by value (not pointer) in PCB.
 *
 * - region1_pt
 *     Pointer to this process's Region 1 page table.
 *     Loaded into REG_PTBR1 when this process runs.
 *
 * - kernel_stack_frames[2]
 *     Physical frame numbers of the two kernel stack pages.
 *     Must be remapped into the kernel stack virtual range
 *     on every context switch.
 *
 * - parent
 *     Pointer to parent PCB. NULL if orphan or init.
 *
 * - children
 *     Linked list of child PCBs.
 *
 * - exit_status
 *     Saved exit status after process dies.
 *     Collected by parent via Wait.
 *
 * - delay_ticks
 *     Number of clock ticks remaining in a Delay syscall.
 *
 * - next / prev
 *     Links for use in queues.
 */
typedef struct pcb {
    int            pid;
    proc_state_t   state;

    UserContext    user_context;
    KernelContext  kernel_context;

    struct pte    *region1_pt;
    int            kernel_stack_frames[2];

    struct pcb    *parent;
    struct pcb    *children;
    struct pcb    *next_sibling;

    int            exit_status;
    int            delay_ticks;

    struct pcb    *next;
    struct pcb    *prev;
} pcb_t;

/* Global pointers */
extern pcb_t *current_process;
extern pcb_t *idle_process;

/*
 * Initialize the process subsystem.
 *
 * Pseudocode:
 *   - Set current_process = NULL.
 *   - Set idle_process = NULL.
 *   - Initialize ready queue.
 *   - Initialize blocked queue.
 *   - Initialize delay queue.
 */
void process_init(void);

/*
 * Allocate and initialize a blank PCB.
 *
 * Pseudocode:
 *   - malloc a pcb_t.
 *   - Zero all fields.
 *   - Allocate Region 1 page table.
 *   - Allocate kernel stack frames.
 *   - Assign pid via helper_new_pid(region1_pt).
 *   - Set state = PROC_READY.
 *   - Return pointer.
 */
pcb_t *alloc_pcb(void);

/*
 * Free a PCB and all its resources.
 *
 * Pseudocode:
 *   - Free user pages.
 *   - Destroy Region 1 page table.
 *   - Free kernel stack frames.
 *   - Call helper_retire_pid(pid).
 *   - Free pcb_t struct.
 */
void free_pcb(pcb_t *proc);

/*
 * Context switch from current process to next.
 *
 * Pseudocode:
 *   - Save current UserContext into current_process->user_context.
 *   - Call KernelContextSwitch(KCSwitch, current_process, next_process).
 *   - After switch returns, we are running as next_process.
 *   - Restore UserContext from current_process->user_context into *uctxt.
 */
void context_switch(pcb_t *next, UserContext *uctxt);

/*
 * Clone current kernel context into a new PCB (used by Fork).
 *
 * Pseudocode:
 *   - Call KernelContextSwitch(KCCopy, new_pcb, NULL).
 *   - KCCopy will:
 *       - Copy KernelContext from kc_in into new_pcb->kernel_context.
 *       - Copy current kernel stack contents into new_pcb's stack frames.
 *       - Return kc_in unchanged.
 */
void clone_kernel_context(pcb_t *new_proc);

/*
 * KCSwitch - passed to KernelContextSwitch for context switching.
 *
 * Pseudocode:
 *   - Copy kc_in into curr_pcb->kernel_context.
 *   - Remap kernel stack pages to next_pcb's frames.
 *   - Flush kernel stack TLB.
 *   - Return pointer to next_pcb->kernel_context.
 */
KernelContext *KCSwitch(KernelContext *kc_in,
                        void *curr_pcb_p,
                        void *next_pcb_p);

/*
 * KCCopy - passed to KernelContextSwitch for cloning a process.
 *
 * Pseudocode:
 *   - Copy kc_in into new_pcb->kernel_context.
 *   - Copy current kernel stack pages into new_pcb's kernel stack frames.
 *       (Temporarily map destination frame into a scratch page to do copy.)
 *   - Return kc_in.
 */
KernelContext *KCCopy(KernelContext *kc_in,
                      void *new_pcb_p,
                      void *not_used);

#endif /* PROCESS_H */

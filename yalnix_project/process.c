#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>
#include <ylib.h>
#include "memory.h"
#include "process.h"

//the process currently considered running by the kernel.
pcb_t *current_process = NULL;

//the special idle process used when there is no real user process to run.
pcb_t *idle_process = NULL;

//initialize checkpoint-2 process bookkeeping.
void InitProcessSystem(void)
{
    //there is no current process until KernelStart creates idle.
    current_process = NULL;

    //there is no idle process until CreateIdleProcess builds it.
    idle_process = NULL;
}

//the checkpoint-2 idle function that runs after KernelStart returns to user mode.
void DoIdle(void)
{
    while (1) {
        TracePrintf(1, "DoIdle\n");
        Pause();
    }
}

//create the first process: idle.
pcb_t *CreateIdleProcess(UserContext *boot_context)
{
    //allocate storage for the idle PCB from the kernel heap.
    pcb_t *idle = (pcb_t *)malloc(sizeof(pcb_t));

    //stop immediately if the kernel cannot allocate its first PCB.
    if (idle == NULL) {
        //a kernel without an idle PCB cannot complete checkpoint 2.
        helper_abort("CreateIdleProcess: malloc failed");
    }

    //clear the PCB so all fields start in a known state.
    memset(idle, 0, sizeof(pcb_t));

    //allocate an empty Region 1 page table for idle.
    idle->region1_pt = CreateRegion1PageTable();

    //stop if the Region 1 page table could not be allocated.
    if (idle->region1_pt == NULL) {
        //idle needs Region 1 so it can have a user-mode stack.
        helper_abort("CreateIdleProcess: CreateRegion1PageTable failed");
    }

    //allocate one physical frame for idle's user stack.
    int stack_pfn = AllocFrame();

    //stop if physical memory is exhausted before idle can run.
    if (stack_pfn == ERROR) {
        //without a stack frame, returning into idle would trap immediately.
        helper_abort("CreateIdleProcess: idle stack allocation failed");
    }

    //map the top Region 1 virtual page as idle's user stack.
    if (MapPage(idle->region1_pt, MAX_PT_LEN - 1, stack_pfn,
                PROT_READ | PROT_WRITE) == ERROR) {
        //give the frame back because the page-table mapping failed.
        FreeFrame(stack_pfn);

        //stop because idle cannot run without a mapped stack.
        helper_abort("CreateIdleProcess: idle stack mapping failed");
    }

    //record the boot kernel stack's first page for checkpoint-2 identity bookkeeping.
    idle->kernel_stack_pages[0] = KERNEL_STACK_BASE >> PAGESHIFT;

    //record the boot kernel stack's second page for checkpoint-2 identity bookkeeping.
    idle->kernel_stack_pages[1] = (KERNEL_STACK_BASE >> PAGESHIFT) + 1;

    //ask the Yalnix helper code for a pid tied to this Region 1 page table.
    idle->pid = helper_new_pid(idle->region1_pt);

    //copy the boot UserContext so we preserve the framework-provided starting state.
    memcpy(&idle->user_context, boot_context, sizeof(UserContext));

    //make the return-to-user-mode PC point at the idle loop in kernel text.
    idle->user_context.pc = DoIdle;

    //put the idle user stack pointer at the top of Region 1.
    idle->user_context.sp = (void *)VMEM_1_LIMIT - sizeof(void*);

    //save the global idle process pointer for later scheduler code.
    idle_process = idle;

    //return the completed idle PCB to KernelStart.
    return idle;
}

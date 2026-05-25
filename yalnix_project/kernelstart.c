#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>
#include <ylib.h>
#include "memory.h"
#include "process.h"
#include "trap.h"

//KernelStart: Entry point for the Yalnix kernel.
//initialize free frame tracking
//calculate and save size of physical memory globally
//initialize and create interrupt vector table
//write address of the interrupt vector table to REG_VECTOR_BASE register
//build Region 0 page table (kernel address space)
//build Region 1 page table for idle
//enable virtual memory.
//transition to user mode
void KernelStart(char *cmd_args[], unsigned int pmem_size, UserContext *uctxt)
{
    //cmd_args is unused in checkpoint 2 because idle is hard-coded instead of loaded from a file.
    (void)cmd_args;

    //print a boot message so we can see KernelStart has begun.
    TracePrintf(0, "KernelStart: booting\n");

    //initialize the physical frame bitmap using the memory size provided by the hardware.
    InitPhysicalMemory(pmem_size);

    //initialize process globals before creating the idle process.
    InitProcessSystem();

    //fill the trap vector with checkpoint-2 handlers.
    init_trap_vector();

    //tell the hardware where the trap vector table lives.
    WriteRegister(REG_VECTOR_BASE, (unsigned int)trap_vector);

    //build the Region 0 page table that maps the kernel address space.
    pte_t *region0_pt = BuildRegion0PageTable();

    //stop immediately if Region 0 setup failed.
    if (region0_pt == NULL) {
        //the kernel cannot enable virtual memory without a Region 0 page table.
        helper_abort("KernelStart: BuildRegion0PageTable failed");
    }

    //map any kernel heap pages requested by malloc before VM is turned on.
    SyncKernelBrkBeforeVM(region0_pt);

    //create the idle PCB, including its Region 1 page table and user stack.
    pcb_t *idle = CreateIdleProcess(uctxt);
    //stop immediately if idle creation failed.
    if (idle == NULL) {
        //checkpoint 2 cannot finish without an idle process.
        helper_abort("KernelStart: CreateIdleProcess failed");
    }

    //create the init pcb along with its region 1 page table and user stack
    pcb_t *init = CreateInitProcess(uctx, "init", cmd_args);  
    //stop immediately if init creation failed.
    if (init == NULL) {
        //checkpoint 2 cannot finish without an idle process.
        helper_abort("KernelStart: CreateIdleProcess failed");
    }

    //make init the current process for the first return to user mode.
    current_process = init;

    // Region 0 page table base register.
    WriteRegister(REG_PTBR0, (unsigned int)region0_pt);

    //Region 0 page table length register.
    WriteRegister(REG_PTLR0, MAX_PT_LEN);

    //idle's Region 1 page table base register.
    WriteRegister(REG_PTBR1, (unsigned int)idle->region1_pt);

    //Region 1 page table length register.
    WriteRegister(REG_PTLR1, MAX_PT_LEN);

    //record in kernel bookkeeping that virtual memory is about to be active.
    vm_enabled = 1;

    //tell the hardware to begin translating addresses through the page tables.
    WriteRegister(REG_VM_ENABLE, 1);

    //flush all stale translations before enabling virtual memory.
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_ALL);


    //copy idle's modified context into the hardware-provided return context.
    memcpy(uctxt, &idle->user_context, sizeof(UserContext));

    //print checkpoint-recommended message before returning into idle.
    TracePrintf(0, "KernelStart: leaving KernelStart\n");
}

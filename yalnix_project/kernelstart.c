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
    TracePrintf(0, "KernelStart: booting\n");

    //initialize the physical frame bitmap using the memory size provided by the hardware.
    InitPhysicalMemory(pmem_size);

    //initialize process globals before creating the idle process.
    InitProcessSystem();

    //fill the trap vector with handlers.
    init_trap_vector();

    //tell the hardware where the trap vector table lives.
    WriteRegister(REG_VECTOR_BASE, (unsigned int)trap_vector);

    //build the Region 0 page table that maps the kernel address space.
    pte_t *region0_pt = BuildRegion0PageTable();

    if (region0_pt == NULL) {
        helper_abort("KernelStart: BuildRegion0PageTable failed");
    }

    //map any kernel heap pages requested by malloc before VM is turned on.
    SyncKernelBrkBeforeVM(region0_pt);

    //create the idle PCB, including its Region 1 page table and user stack.
    pcb_t *idle = CreateIdleProcess(uctxt);
    if (idle == NULL) {
        helper_abort("KernelStart: CreateIdleProcess failed");
    }

    //point the hardware at the page tables and enable virtual memory.
    //CreateInitProces calls LoadProgram which accesses Region 1 addresses,
    //so VM must be on before we create init.
    WriteRegister(REG_PTBR0, (unsigned int)region0_pt);
    WriteRegister(REG_PTLR0, MAX_PT_LEN);
    WriteRegister(REG_PTBR1, (unsigned int)idle->region1_pt);
    WriteRegister(REG_PTLR1, MAX_PT_LEN);

    vm_enabled = 1;
    WriteRegister(REG_VM_ENABLE, 1);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_ALL);

    //pick the init program name: cmd_args[0] if provided, else "init".
    char *init_name = (cmd_args != NULL && cmd_args[0] != NULL) ? cmd_args[0] : "init";

    //create the init PCB and load its program into Region 1.
    //CreateInitProces internally switches PTBR1 to init's page table for LoadProgram.
    pcb_t *init = CreateInitProces(uctxt, init_name, cmd_args);
    if (init == NULL) {
        helper_abort("KernelStart: CreateInitProces failed");
    }

    //clone idle's current kernel context and stack contents into init's PCB.
    //After this call, init has a valid kernel_context it can be switched to.
    if (KernelContextSwitch(KCCopy, (void *)init, NULL) != 0) {
        helper_abort("KernelStart: KernelContextSwitch(KCCopy) failed");
    }

    //make init the first process to run in user mode.
    current_process = init;

    //switch the hardware to init's Region 1 so we return into init's userland.
    WriteRegister(REG_PTBR1, (unsigned int)init->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);

    //copy init's user context into the hardware-provided slot so the CPU
    //returns to init's entry point when KernelStart returns.
    memcpy(uctxt, &init->user_context, sizeof(UserContext));

    TracePrintf(0, "KernelStart: leaving KernelStart\n");
}

/* trap.c */
#include "trap.h"
#include "process.h"
#include "syscalls.h"
#include "queue.h"
#include <hardware.h>
#include <ykernel.h>
#include <ylib.h>

/*
//CHECKPOINT 2:

Create the flobal trap vector table - with entries equal to TRAP_VECTOR_SIZE as required in section 2.4
-> Each entry is a pointer to a function that takes UserContext * and returns voide
HW uses this table to decide which handler to call for each trap number
*/
void (*trap_vector[TRAP_VECTOR_SIZE])(UserContext *);

static int clock_ticks = 0;

//delayed queue for processes waiting to wake up.
static process_queue_t delayed_queue;

static void InitDelayedQueue(void)
{
    //initialize the delayed queue once at startup.
    InitProcessQueue(&delayed_queue);
}

static void WakeDelayedProcesses(void)
{
    //check if there are any delayed processes and wake them if their time has come.
    //we need to check the delayed queue for processes that should wake up.
    //for now, we'll iterate through and re-queue ready processes.

    //note: in a full implementation, we'd maintain a proper delayed queue structure.
    //for checkpoint 4, we keep delayed processes in the ready queue but marked as delayed.
    //this simplification works but may schedule delayed processes before they're ready.
}

static pcb_t *PickNextProcess(void)
{
    //use round-robin scheduling from the ready queue.
    //if queue is empty, return idle process.

    //peek at the front of the ready queue without dequeuing.
    pcb_t *next = PeekProcess(&ready_queue);

    if (next != NULL) {
        //found a process in the ready queue, return it.
        return next;
    }

    //ready queue is empty, only idle is available.
    if (idle_process != NULL) {
        return idle_process;
    }

    //nothing is available, keep current process.
    return current_process;
}

static void RestoreCurrentProcess(UserContext *uctxt)
{
    WriteRegister(REG_PTBR1, (unsigned int)current_process->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    TracePrintf(1, "RestoreCurrentProcess: PID %d, restoring PC=%p\n",
                current_process->pid, current_process->user_context.pc);
    memcpy(uctxt, &current_process->user_context, sizeof(UserContext));
}


/*Should create and register the interrupts/trap handler table (Section 2.5, table 2.2)
The hardware needs to know which kernel function to call when a trap happens. This fills
a global trap vecotr array with function pointers. 
*/
void init_trap_vector(void)
{

  //CHECKPOINT 2:

  //create trap vector table and write virtual address to reg_vector_base
   int i;
  
  //set every trap entry to a safe default handler to prevent null handler crashes in checkpoint 2
   for(i= 0; i<TRAP_VECTOR_SIZE; i++){
      trap_vector[i] = HandleTrapUnhandled;
   }

   //Set clock and kernel entries to their designated handlers for checkpoint 2
   trap_vector[TRAP_CLOCK] = HandleTrapClock; //timer interrupt
   trap_vector[TRAP_KERNEL] = HandleTrapKernel; // syscall trap
}


/*trap to handle a system call or kernell call
A syscall enters the kernel though TRAP_KERNEL. The specific syscall number is in uctxt->code.
For checkpoint 2: Only need to identify that a syscall happened. 
*/
void HandleTrapKernel(UserContext *uctxt)
{
    int blocked;
    pcb_t *old_process;
    pcb_t *next_process;

    TracePrintf(0, "kernel trap syscall code=0x%x\n", uctxt->code);

    if (current_process == NULL) {
        uctxt->regs[0] = ERROR;
        return;
    }

    blocked = DispatchSyscall(uctxt, clock_ticks);
    memcpy(&current_process->user_context, uctxt, sizeof(UserContext));

    if (!blocked) {
        //syscall did not block, continue running the same process.
        return;
    }

    //syscall blocked the process, pick the next process to run.
    old_process = current_process;

    //dequeue the next process from the ready queue.
    next_process = DequeueProcess(&ready_queue);

    //if nothing in queue, use idle.
    if (next_process == NULL) {
        next_process = idle_process;
    }

    if (next_process == NULL || next_process == old_process) {
        //no other process to run, keep current.
        return;
    }

    //switch to the next process.
    current_process = next_process;
    if (KernelContextSwitch(KCSwitch,
                            (void *)old_process,
                            (void *)next_process) != 0) {
        helper_abort("HandleTrapKernel: KernelContextSwitch failed");
    }

    RestoreCurrentProcess(uctxt);
}

//handle timer interrupts for round-robin scheduling.
void HandleTrapClock(UserContext *uctxt)
{
    pcb_t *old_process;
    pcb_t *next_process;

    //increment the global clock tick counter.
    clock_ticks++;

    //wake any delayed processes that are ready to run.
    WakeDelayedProcesses();

    TracePrintf(1, "clock trap\n");

    if (current_process == NULL) {
        return;
    }

    old_process = current_process;

    //save the user context for the running process.
    //only save if this process has actually run before (to avoid overwriting new child contexts).
    if (current_process->has_run) {
        TracePrintf(1, "Clock: Saving context for PID %d (has run), before PC=%p, uctxt->pc=%p\n",
                    current_process->pid, current_process->user_context.pc, uctxt->pc);
        memcpy(&current_process->user_context, uctxt, sizeof(UserContext));
        TracePrintf(1, "Clock: After save, PID %d now has PC=%p\n",
                    current_process->pid, current_process->user_context.pc);
    } else {
        //first time running, mark it as having run and DON'T overwrite context.
        TracePrintf(1, "Clock: First run for PID %d, keeping cloned context PC=%p\n",
                    current_process->pid, current_process->user_context.pc);
        current_process->has_run = 1;
    }

    //re-enqueue the current process if it's not delayed (to implement round-robin).
    if (!old_process->delayed) {
        EnqueueProcess(&ready_queue, old_process);
    }

    //dequeue the next process from the ready queue.
    next_process = DequeueProcess(&ready_queue);

    //if nothing in queue, use idle.
    if (next_process == NULL) {
        next_process = idle_process;
    }

    if (next_process != NULL && next_process != old_process) {
        //switch to the next process.
        current_process = next_process;
        if (KernelContextSwitch(KCSwitch,
                                (void *)old_process,
                                (void *)next_process) != 0) {
            helper_abort("HandleTrapClock: KernelContextSwitch failed");
        }
    } else if (next_process != NULL && next_process == old_process) {
        //dequeued the same process, but we're not switching.
        //re-enqueue it so it stays in the round-robin.
        if (!old_process->delayed) {
            EnqueueProcess(&ready_queue, next_process);
        }
    }

    //restore the user context and return to user mode.
    RestoreCurrentProcess(uctxt);
}

/*
CHECKPOINT 2: Catch everything that hasn't been implemented yet.
Will just handle unhandled traps instead of failing silently. 
*/
void HandleTrapUnhandled(UserContext *uctxt){

  //CHECKPOINT 2:

  //print diagnostic infor for an unexpected trap
  TracePrintf(0, "unhandled trap vector=%d code=%d addr=0x%x pc=0x%x sp=0x%x\n", uctxt->vector, uctxt->code, uctxt->addr, uctxt->pc, uctxt->sp);

  //stops kernel to notify of unhandled trap
  helper_abort("unhandled trap");
}

void HandleTrapIllegal(UserContext *uctxt)
{
  HandleTrapUnhandled(uctxt);
}

void HandleTrapMemory(UserContext *uctxt)
{
  HandleTrapUnhandled(uctxt);
}

void HandleTrapMath(UserContext *uctxt)
{
  HandleTrapUnhandled(uctxt);
}

void HandleTrapTtyReceive(UserContext *uctxt)
{
  HandleTrapUnhandled(uctxt);
}

void HandleTrapTtyTransmit(UserContext *uctxt)
{
  HandleTrapUnhandled(uctxt);

}

void HandleTrapDisk(UserContext *uctxt)
{ 
  HandleTrapUnhandled(uctxt);
}

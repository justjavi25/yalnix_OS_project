/* trap.c */
#include "trap.h"
#include "process.h"
#include "syscalls.h"


void init_trap_vector(void)
{
  //create trap vector table and write virtual address to reg_vector_base
}


//trap to handle a system call or kernell call
void HandleTrapKernel(UserContext *uctxt)
{
     // Save uctxt into current_process->user_context.
     // find what syscall is being invoked
     // verify all arguments to syscall are valid
     // place return valie into regs field of current context
     // Copy current_process->user_context back to *uctxt.
     // return to user mode
}

void HandleTrapClock(UserContext *uctxt)
{
  // Save uctxt into current_process->user_context.
  // Chceck our round robin queue and perform a context switch if there are other processes waiting
  // If there are no other processes waiting we dispatch our idle process
  // Copy current_process->user_context back to *uctxt.
  // return to user mode

}

void HandleTrapIllegal(UserContext *uctxt)
{
  // Save uctxt into current_process->user_context.
  // Kill any process going on
  // Print why it killed requested process
  // Copy current_process->user_context back to *uctxt.
  // return to user mode

}

void HandleTrapMemory(UserContext *uctxt)
{
  // Save uctxt into current_process->user_context.
  //  Determine if addr in current process represent request to grow the user stack
  //    if adress is in region one and under the current stack we allow it
  //    we make sure at least one unmapped page remains between user heap and user stack
  //  If memory access violates the above protections we abort this process
  // Copy current_process->user_context back to *uctxt.
  // return to user mode
}

void HandleTrapMath(UserContext *uctxt)
{
  // Save uctxt into current_process->user_context.
  // Kill any process going on
  // Print why it killed requested process
  // Copy current_process->user_context back to *uctxt.
  // return to user mode
}

void HandleTrapTtyReceive(UserContext *uctxt)
{
  // Save uctxt into current_process->user_context.
  // Identify what terminal generted this trap, we get from code field of user_context
  // We use TtyReceive hardware instruction to copy input from terminal into kernel buffer 
  // Copy current_process->user_context back to *uctxt.
  // return to user mode
}

void HandleTrapTtyTransmit(UserContext *uctxt)
{
  // Save uctxt into current_process->user_context.
  //Identify what process was waiting for this transmission to complete and move back into queue
  //If other processes were waiting to write to this terminal, we initate the next TtyTransmit
  // Copy current_process->user_context back to *uctxt.
  // return to user mode

}

void HandleTrapDisk(UserContext *uctxt)
{ 
//we ignore for now
}

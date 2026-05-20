/* trap.c */
#include "trap.h"
#include "process.h"
#include "syscalls.h"
#include <ykernel.h>

/*
//CHECKPOINT 2:

Create the flobal trap vector table - with entries equal to TRAP_VECTOR_SIZE as required in section 2.4
-> Each entry is a pointer to a function that takes UserContext * and returns voide
HW uses this table to decide which handler to call for each trap number
*/
void (*trap_vector[TRAP_VECTOR_SIZE])(UserContext *);


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
    //CHECKPOINT 2:

    //print syscall number
    TracePrintf(0, "kernel trap syscall code=0x%x\n", uctxt->code);
    //return value for syscall put in regs[0] as specified in section 3.2
    //We haven't implemented syscalls yet, so just return error.
    uctxt->regs[0] = ERROR;
}

/*
Handle timer interrupts. For checkpoint 2: just trace and return
*/
void HandleTrapClock(UserContext *uctxt)
{

  //CHECKPOINT 2:

  //Log that a clock interrupt occured, does not need scheduling yet
  TracePrintf(1, "clock trap\n");

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

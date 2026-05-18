/* syscalls.c */
#include "syscalls.h"
#include "process.h"


int KernelFork(void)
{
 //instantiate new PCB and get new PID helper_new_pid()
 //clone age table for the child from its parents memeory
 //use KernelContextSwitch and KCCopu to copy parents kernel stack and registers into the childs PCB
 //Make sure child context returns 0 and parent returns childs PID
}

int KernelExec(char *filename, char **argvec)
{
  //First we validate that the filename and the argvec arguments are valid pointers in user space adresses
  //Use provided LoadProgram function to wipe the current region1 memeory and load new program
  //Serup up new UserContext with PC pointed at the porgram's entry point and SP at top of new user stack
}

void KernelExit(int status)
{
  //if parent is waiting on child exit we save our status and PID waiting to be reaped
  //if parent is not waiting we mark the process a zombie
  //if the process itself has children we orphan them
  //deallocate all region 1 memory for this process and deallocate its PCB
  //call Halt() if this was init process to shut down kernel
}


int KernelWait(int *status_ptr)
{
  //check if current process has any children
  //  if not return error
  //check if any child has exited
  //  if it has collect its status 
  //  free remaining resources
  //  return its PID
  //if children are still running but none are zombies
  //  block parent from running until a child exits
  //  once child exits repeat earlier steps
}

int KernelGetPid(void)
{
  //Look at current running process's PCB
  //Return PID value in it
}

int KernelBrk(void *addr)
{
  //Round requested addr up to next page boundary
  //Check if new break overlaps into user stack or falls bellow original data seg
  //Allocate or deallocate physical frames to match new size
  //Update region1 page table for current process
  //flush tlb
}

int KernelDelay(int clock_ticks)
{
  //If ticks is 0 return
  //If ticks is less than 0 return error
  //Sleep the current process
  //Store tah wakeup time(current ticks+delay ticks) into the PCB
  //Call kernel context switch to run next ready process
}

int KernelTtyRead(int tty_id, void *buf, int len)
{
  //check if the specified terminal has a buffered input available
  //if the buffer is empty
  //  block process
  //  place into wait queue
  //when data arrives via our TRAP_TTY_RECEIVE we copy up to len number of bytes into the user buf
  //we finally return the number of bytes read
}

int KernelTtyWrite(int tty_id, void *buf, int len)
{
//copy suer data into a  kernel buffer
//if terminal is busy with something else we block this process until it is not
//we call given TtyTransmit hardware instruction
//This process stays blocked until a TRAP_TTY_TRANSMIT lets us know the operation has completed and we can keep going forward
}

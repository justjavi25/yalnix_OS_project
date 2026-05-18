#include <hardware.h>
#include <yalnix.h>
#include "memory.h"
#include "process.h"
#include "trap.h"
#include "idle.h"



//KernelStart: Entry point for the Yalnix kernel.
  //Initialize free frame tracking
  //Calculate and save suze of physical memory globally
  //Initialize and create interrupt vector table
  //Write address of the interrupt vector table to REG_VECTOR_BASE registor
  // Set kernelbreak
  // Build Region 0 page table (kernel address space)
  // Build region 1 page table for idle
  // Enable virtual memory.
  // Transition to user mode


void KernelStart(char *cmd_args[], unsigned int pmem_size, UserContext *uctxt)
{


}

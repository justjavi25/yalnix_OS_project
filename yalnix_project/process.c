#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>
#include <ylib.h>
#include "memory.h"
#include "process.h"
#include <fcntl.h>
#include <unistd.h>
#include <ykernel.h>
#include <load_info.h>

//GLOBALS
//the process currently considered running by the kernel.
pcb_t *current_process = NULL;
//the special idle process used when there is no real user process to run.
pcb_t *idle_process = NULL;
//the special init process
pcb_t *init_process = NULL;





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






/*----------------------------------------------------------------------------------*/
//LoadProgram()













/*
 *  Load a program into an existing address space.  The program comes from
 *  the Linux file named "name", and its arguments come from the array at
 *  "args", which is in standard argv format.  The argument "proc" points
 *  to the process or PCB structure for the process into which the program
 *  is to be loaded. 
 */

/*
 * ==>> Declare the argument "proc" to be a pointer to the PCB of 
 * ==>> the current process. 
 */
int
LoadProgram(char *name, char *args[], pcb_t* proc) 

{
  int fd;
  int (*entry)();
  struct load_info li;
  int i;
  char *cp;
  char **cpp;
  char *cp2;
  int argcount;
  int size;
  int text_pg1;
  int data_pg1;
  int data_npg;
  int stack_npg;
  long segment_size;
  char *argbuf;

  current_process = proc;

  /*
   * Open the executable file 
   */
  if ((fd = open(name, O_RDONLY)) < 0) {
    TracePrintf(0, "LoadProgram: can't open file '%s'\n", name);
    return ERROR;
  }

  if (LoadInfo(fd, &li) != LI_NO_ERROR) {
    TracePrintf(0, "LoadProgram: '%s' not in Yalnix format\n", name);
    close(fd);
    return (-1);
  }

  if (li.entry < VMEM_1_BASE) {
    TracePrintf(0, "LoadProgram: '%s' not linked for Yalnix\n", name);
    close(fd);
    return ERROR;
  }

  /*
   * Figure out in what region 1 page the different program sections
   * start and end
   */
  text_pg1 = (li.t_vaddr - VMEM_1_BASE) >> PAGESHIFT;
  data_pg1 = (li.id_vaddr - VMEM_1_BASE) >> PAGESHIFT;
  data_npg = li.id_npg + li.ud_npg;
  /*
   *  Figure out how many bytes are needed to hold the arguments on
   *  the new stack that we are building.  Also count the number of
   *  arguments, to become the argc that the new "main" gets called with.
   */
  size = 0;
  for (i = 0; args[i] != NULL; i++) {
    TracePrintf(3, "counting arg %d = '%s'\n", i, args[i]);
    size += strlen(args[i]) + 1;
  }
  argcount = i;

  TracePrintf(2, "LoadProgram: argsize %d, argcount %d\n", size, argcount);
  
  /*
   *  The arguments will get copied starting at "cp", and the argv
   *  pointers to the arguments (and the argc value) will get built
   *  starting at "cpp".  The value for "cpp" is computed by subtracting
   *  off space for the number of arguments (plus 3, for the argc value,
   *  a NULL pointer terminating the argv pointers, and a NULL pointer
   *  terminating the envp pointers) times the size of each,
   *  and then rounding the value *down* to a double-word boundary.
   */
  cp = ((char *)VMEM_1_LIMIT) - size;

  cpp = (char **)
    (((int)cp - 
      ((argcount + 3 + POST_ARGV_NULL_SPACE) *sizeof (void *))) 
     & ~7);

  /*
   * Compute the new stack pointer, leaving INITIAL_STACK_FRAME_SIZE bytes
   * reserved above the stack pointer, before the arguments.
   */
  cp2 = (caddr_t)cpp - INITIAL_STACK_FRAME_SIZE;

  TracePrintf(1, "prog_size %d, text %d data %d bss %d pages\n",
	      li.t_npg + data_npg, li.t_npg, li.id_npg, li.ud_npg);

  /* 
   * Compute how many pages we need for the stack */
  stack_npg = (VMEM_1_LIMIT - DOWN_TO_PAGE(cp2)) >> PAGESHIFT;

  TracePrintf(1, "LoadProgram: heap_size %d, stack_size %d\n",
	      li.t_npg + data_npg, stack_npg);


  /* leave at least one page between heap and stack */
  if (stack_npg + data_pg1 + data_npg >= MAX_PT_LEN) {
    close(fd);
    return ERROR;
  }

  /*
   * This completes all the checks before we proceed to actually load
   * the new program.  From this point on, we are committed to either
   * loading succesfully or killing the process.
   */

  /*
   * Set the new stack pointer value in the process's UserContext
   */

  /* 
   * ==>> (rewrite the line below to match your actual data structure) 
        proc->uc.sp = cp2; 
   */
  proc->user_context.sp = cp2;

  /*
   * Now save the arguments in a separate buffer in region 0, since
   * we are about to blow away all of region 1.
   */
  cp2 = argbuf = (char *)malloc(size);

  /* 
   * ==>> You should perhaps check that malloc returned valid space 
   */
  if (argbuf == NULL) {
        close(fd);
        return ERROR;
  }

  for (i = 0; args[i] != NULL; i++) {
    TracePrintf(3, "saving arg %d = '%s'\n", i, args[i]);
    strcpy(cp2, args[i]);
    cp2 += strlen(cp2) + 1;
  }

  /*
   * Set up the page tables for the process so that we can read the
   * program into memory.  Get the right number of physical pages
   * allocated, and set them all to writable.
   */

  /* ==>> Throw away the old region 1 virtual address space by
   * ==>> curent process by walking through the R1 page table and,
   * ==>> for every valid page, free the pfn and mark the page invalid.
   */

  for (i = 0; i < MAX_PT_LEN; i++) {
        if (proc->region1_pt[i].valid) {
            FreeFrame(proc->region1_pt[i].pfn);
            proc->region1_pt[i].valid = 0;
        }
    }

  /*
   * ==>> Then, build up the new region1.  
   * ==>> (See the LoadProgram diagram in the manual.)
   */

  /*
   * ==>> First, text. Allocate "li.t_npg" physical pages and map them starting at
   * ==>> the "text_pg1" page in region 1 address space.
   * ==>> These pages should be marked valid, with a protection of
   * ==>> (PROT_READ | PROT_WRITE).
   */
   for(i = 0; i < li.t_npg; i++){
      int pfn = AllocFrame();
      if(pfn == ERROR) return ERROR;

      MapPage(proc->region1_pt, text_pg1 + i, pfn, PROT_READ | PROT_WRITE);
    }

  /*
   * ==>> Then, data. Allocate "data_npg" physical pages and map them starting at
   * ==>> the  "data_pg1" in region 1 address space.
   * ==>> These pages should be marked valid, with a protection of
   * ==>> (PROT_READ | PROT_WRITE).
   */
  for(i = 0; i < data_npg; i++){
      int pfn = AllocFrame();
      if(pfn == ERROR) return ERROR;

      MapPage(proc->region1_pt, data_pg1 + i, pfn, PROT_READ | PROT_WRITE);
    }

  /* 
   * ==>> Then, stack. Allocate "stack_npg" physical pages and map them to the top
   * ==>> of the region 1 virtual address space.
   * ==>> These pages should be marked valid, with a
   * ==>> protection of (PROT_READ | PROT_WRITE).
   */
  for(i = 0; i < stack_npg; i++){
      int pfn = AllocFrame();
      if(pfn == ERROR) return ERROR;

      MapPage(proc->region1_pt, MAX_PT_LEN - i - 1, pfn, PROT_READ | PROT_WRITE);
  }

  /*
   * ==>> (Finally, make sure that there are no stale region1 mappings left in the TLB!)
   */
  WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);

  /*
   * All pages for the new address space are now in the page table.  
   */

  /*
   * Read the text from the file into memory.
   */
  lseek(fd, li.t_faddr, SEEK_SET);
  segment_size = li.t_npg << PAGESHIFT;
  if (read(fd, (void *) li.t_vaddr, segment_size) != segment_size) {
    close(fd);
    return KILL;   // see ykernel.h
  }

  /*
   * Read the data from the file into memory.
   */
  lseek(fd, li.id_faddr, 0);
  segment_size = li.id_npg << PAGESHIFT;

  if (read(fd, (void *) li.id_vaddr, segment_size) != segment_size) {
    close(fd);
    return KILL;
  }


  close(fd);			/* we've read it all now */

  /*
   * ==>> Above, you mapped the text pages as writable, so this code could write
   * ==>> the new text there.
   *
   * ==>> But now, you need to change the protections so that the machine can execute
   * ==>> the text.
   *
   * ==>> For each text page in region1, change the protection to (PROT_READ | PROT_EXEC).
   * ==>> If any of these page table entries is also in the TLB, 
   * ==>> you will need to flush the old mapping. 
   */
  for (i = 0; i < li.t_npg; i++) {
        proc->region1_pt[text_pg1 + i].prot =
            PROT_READ | PROT_EXEC;
    }

    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
  
  /*
   * Zero out the uninitialized data area
   */
  bzero((void*)li.id_end, li.ud_end - li.id_end);

  /*
   * Set the entry point in the process's UserContext
   */

  /* 
   * ==>> (rewrite the line below to match your actual data structure) 
   * ==>> proc->uc.pc = (caddr_t) li.entry;
   */
  proc->user_context.pc = (void*)li.entry;
  /*
   * Now, finally, build the argument list on the new stack.
   */

  memset(cpp, 0x00, VMEM_1_LIMIT - ((int) cpp));

  *cpp++ = (char *)argcount;		/* the first value at cpp is argc */
  cp2 = argbuf;
  for (i = 0; i < argcount; i++) {      /* copy each argument and set argv */
    *cpp++ = cp;
    strcpy(cp, cp2);
    cp += strlen(cp) + 1;
    cp2 += strlen(cp2) + 1;
  }
  free(argbuf);
  *cpp++ = NULL;			/* the last argv is a NULL pointer */
  *cpp++ = NULL;			/* a NULL pointer for an empty envp */

  return SUCCESS;
}












/*----------------------------------------------------------------------------------*/
//Checkpoint 3





//now that we have our idle process, we then clone a new init proces after idling
pcb_t *CreateInitProces(UserContext *init_context, char *name, char **args){

    //first we set up our pcb for init
    pcb_t *init = malloc(sizeof(pcb_t));

    //stop immediately if the kernel cannot allocate more memory to this
    if (init == NULL) {
        helper_abort("CreateInitProcess: malloc failed");
    }
     //clear the PCB so all fields start in a known state.
    memset(init, 0, sizeof(pcb_t));

    /*------------------------------------------------------------------------*/

    //allocate an empty Region 1 page table for init
    init->region1_pt = CreateRegion1PageTable();

    //stop if the Region 1 page table could not be allocated.
    if (init->region1_pt == NULL) {
        //idle needs Region 1 so it can have a user-mode stack.
        helper_abort("CreateInitProcess: CreateRegion1PageTable failed");
    }

    //allocate one physical frame for inits user stack.
    int stack_pfn = AllocFrame();
    //stop if physical memory is exhausted before init can run.
    if (stack_pfn == ERROR) {
        //without a stack frame, returning into idle would trap immediately.
        helper_abort("CreateInitProcess: init stack allocation failed");
    }
    //map the top Region 1 virtual page as inits's user stack and check to make sure it got mapped correctly
    //without throwing an error
    if (MapPage(init->region1_pt, MAX_PT_LEN - 1, stack_pfn,
                PROT_READ | PROT_WRITE) == ERROR) {
        //give the frame back because the page-table mapping failed.
        FreeFrame(stack_pfn);
        //stop because init cannot run without a mapped stack.
        helper_abort("CreateIdleProcess: init stack mapping failed");
    }

    //since were loading a new program we allocate new frames to each new stack page
    init->kernel_stack_pages[0] = AllocFrame();
    init->kernel_stack_pages[1] = AllocFrame();

    //ask the Yalnix helper code for a pid tied to this Region 1 page table.(pg43 of manual)
    init->pid = helper_new_pid(init->region1_pt);

    //copy the init UserContext to the provided init_context so we preserve the starting state.
    memcpy(&init->user_context, init_context, sizeof(UserContext));

    //SWITCH TO INIT ADDRESS SPACE
    WriteRegister(REG_PTBR1, (unsigned int)init->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);

    //load program
    if (LoadProgram(name, args, init) != SUCCESS) {
        helper_abort("CreateInitProcess: LoadProgram failed");
    }

    //save the global init process pointer for later scheduler code.
    init_process = init;

    //return the completed idle PCB to KernelStart.
    return init;

}


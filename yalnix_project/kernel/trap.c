/* trap.c */
#include "trap.h"
#include "process.h"
#include "syscalls.h"
#include "queue.h"
#include "tty.h"
#include "memory.h"
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

static void WakeDelayedProcesses(void)
{
    queue_node_t *node = all_processes.head;

    while (node != NULL) {
        pcb_t *proc = node->process;
        if (proc != NULL && proc->delayed && proc->wake_tick <= clock_ticks &&
            !proc->is_zombie && !proc->wait_blocked &&
            !proc->tty_read_blocked && !proc->tty_write_blocked) {
            proc->delayed = 0;
            if (proc != current_process &&
                !IsProcessInQueue(&ready_queue, proc)) {
                EnqueueProcess(&ready_queue, proc);
            }
        }
        node = node->next;
    }
}

static int IsRunnable(pcb_t *proc)
{
    if (proc == NULL) {
        return 0;
    }

    if (proc == idle_process) {
        return 1;
    }

    /* CP6 calls block by setting PCB flags and moving the process onto an
     * object-specific wait queue.  The scheduler must see all of those flags
     * clear before returning the process to user mode. */
    return !proc->delayed && !proc->wait_blocked &&
           !proc->tty_read_blocked && !proc->tty_write_blocked &&
           !proc->pipe_read_blocked && !proc->pipe_write_blocked &&
           !proc->lock_blocked && !proc->cvar_blocked &&
           !proc->is_zombie;
}

static pcb_t *DequeueRunnableProcess(void)
{
    pcb_t *next = DequeueProcess(&ready_queue);

    while (next != NULL && !IsRunnable(next)) {
        next = DequeueProcess(&ready_queue);
    }

    if (next != NULL) {
        return next;
    }

    return idle_process;
}

static void ReapDetachedZombies(void)
{
    queue_node_t *node = all_processes.head;

    while (node != NULL) {
        queue_node_t *next = node->next;
        pcb_t *proc = node->process;

        if (proc != NULL && proc != current_process && proc != idle_process &&
            proc != init_process && proc->is_zombie && proc->parent == NULL) {
            UnregisterProcess(proc);
            FreeProcess(proc);
        }

        node = next;
    }
}

static void RestoreCurrentProcess(UserContext *uctxt)
{
    WriteRegister(REG_PTBR1, (unsigned int)current_process->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    if (current_process->wait_status_ready) {
        if (current_process->wait_status_ptr != NULL) {
            *(current_process->wait_status_ptr) =
                current_process->wait_status_value;
        }
        current_process->wait_status_ptr = NULL;
        current_process->wait_status_ready = 0;
    }
    TracePrintf(1, "RestoreCurrentProcess: PID %d, restoring PC=%p\n",
                current_process->pid, current_process->user_context.pc);
    memcpy(uctxt, &current_process->user_context, sizeof(UserContext));
}

static void SwitchAwayFromCurrent(UserContext *uctxt, char *where)
{
    pcb_t *old_process = current_process;
    pcb_t *next_process = DequeueRunnableProcess();

    if (next_process == NULL || next_process == old_process) {
        return;
    }

    current_process = next_process;
    if (KernelContextSwitch(KCSwitch,
                            (void *)old_process,
                            (void *)next_process) != 0) {
        helper_abort(where);
    }

    RestoreCurrentProcess(uctxt);
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
   trap_vector[TRAP_ILLEGAL] = HandleTrapIllegal;
   trap_vector[TRAP_MEMORY] = HandleTrapMemory;
   trap_vector[TRAP_MATH] = HandleTrapMath;
   trap_vector[TRAP_TTY_RECEIVE] = HandleTrapTtyReceive;
   trap_vector[TRAP_TTY_TRANSMIT] = HandleTrapTtyTransmit;
   trap_vector[TRAP_DISK] = HandleTrapDisk;
}


/*trap to handle a system call or kernell call
A syscall enters the kernel though TRAP_KERNEL. The specific syscall number is in uctxt->code.
For checkpoint 2: Only need to identify that a syscall happened. 
*/
void HandleTrapKernel(UserContext *uctxt)
{
    int blocked;

    TracePrintf(1, "kernel trap syscall code=0x%x\n", uctxt->code);

    if (current_process == NULL) {
        uctxt->regs[0] = ERROR;
        return;
    }

    blocked = DispatchSyscall(uctxt, clock_ticks);
    if (current_process->fork_return_zero) {
        current_process->user_context.regs[0] = 0;
        memcpy(uctxt, &current_process->user_context, sizeof(UserContext));
        current_process->fork_return_zero = 0;
    } else {
        memcpy(&current_process->user_context, uctxt, sizeof(UserContext));
    }
    if (!current_process->is_zombie) {
        current_process->has_run = 1;
    }

    if (!blocked) {
        //syscall did not block, continue running the same process.
        return;
    }

    SwitchAwayFromCurrent(uctxt, "HandleTrapKernel: KernelContextSwitch failed");
    ReapDetachedZombies();
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
    ReapDetachedZombies();

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
    if (old_process != idle_process && IsRunnable(old_process)) {
        EnqueueProcess(&ready_queue, old_process);
    }

    next_process = DequeueRunnableProcess();

    if (next_process != NULL && next_process != old_process) {
        //switch to the next process.
        current_process = next_process;
        if (KernelContextSwitch(KCSwitch,
                                (void *)old_process,
                                (void *)next_process) != 0) {
            helper_abort("HandleTrapClock: KernelContextSwitch failed");
        }
    }

    //restore the user context and return to user mode.
    RestoreCurrentProcess(uctxt);
    ReapDetachedZombies();
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
  TracePrintf(0, "aborting PID %d after illegal trap\n",
              current_process == NULL ? -1 : current_process->pid);
  KernelExitProcess(ERROR);
  SwitchAwayFromCurrent(uctxt, "HandleTrapIllegal: KernelContextSwitch failed");
  ReapDetachedZombies();
}

void HandleTrapMemory(UserContext *uctxt)
{
  unsigned int addr;
  int fault_page;
  int pfn;

  if (current_process != NULL && current_process->region1_pt != NULL) {
    addr = (unsigned int)uctxt->addr;
    if (addr >= VMEM_1_BASE && addr < VMEM_1_LIMIT) {
      fault_page = (addr - VMEM_1_BASE) >> PAGESHIFT;
      /* Treat faults just below the current stack mapping as stack growth, but
       * do not let the stack cross the heap/red-zone boundary. */
      if (fault_page < current_process->stack_base_page &&
          fault_page > current_process->brk_page) {
        for (int vpn = current_process->stack_base_page - 1;
             vpn >= fault_page; vpn--) {
          pfn = AllocFrame();
          if (pfn == ERROR ||
              MapPage(current_process->region1_pt, vpn, pfn,
                      PROT_READ | PROT_WRITE) == ERROR) {
            if (pfn != ERROR) {
              FreeFrame(pfn);
            }
            KernelExitProcess(ERROR);
            SwitchAwayFromCurrent(uctxt, "HandleTrapMemory: stack grow failed");
            ReapDetachedZombies();
            return;
          }
        }
        current_process->stack_base_page = fault_page;
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
        return;
      }
    }
  }

  TracePrintf(0, "aborting PID %d after memory trap addr=0x%x\n",
              current_process == NULL ? -1 : current_process->pid,
              uctxt->addr);
  KernelExitProcess(ERROR);
  SwitchAwayFromCurrent(uctxt, "HandleTrapMemory: KernelContextSwitch failed");
  ReapDetachedZombies();
}

void HandleTrapMath(UserContext *uctxt)
{
  TracePrintf(0, "aborting PID %d after math trap\n",
              current_process == NULL ? -1 : current_process->pid);
  KernelExitProcess(ERROR);
  SwitchAwayFromCurrent(uctxt, "HandleTrapMath: KernelContextSwitch failed");
  ReapDetachedZombies();
}

void HandleTrapTtyReceive(UserContext *uctxt)
{
  HandleTtyReceive(uctxt->code);
}

void HandleTrapTtyTransmit(UserContext *uctxt)
{
  HandleTtyTransmit(uctxt->code);
}

void HandleTrapDisk(UserContext *uctxt)
{ 
  HandleTrapUnhandled(uctxt);
}

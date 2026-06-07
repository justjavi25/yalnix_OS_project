#include "syscalls.h"
#include "memory.h"
#include "process.h"
#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>

static int AddrToRegion1Page(void *addr)
{
    unsigned int vaddr = UP_TO_PAGE(addr);

    if (vaddr < VMEM_1_BASE || vaddr > VMEM_1_LIMIT) {
        return ERROR;
    }

    return (vaddr - VMEM_1_BASE) >> PAGESHIFT;
}

static void ClearRegion1Page(pte_t *pte)
{
    pte->valid = 0;
    pte->prot = PROT_NONE;
    pte->pfn = 0;
}

static int KernelGetPid(void)
{
    if (current_process == NULL) {
        return ERROR;
    }

    return current_process->pid;
}

static int KernelBrk(void *addr)
{
    int new_brk_page;

    if (current_process == NULL || current_process->region1_pt == NULL) {
        return ERROR;
    }

    new_brk_page = AddrToRegion1Page(addr);
    if (new_brk_page == ERROR) {
        return ERROR;
    }

    if (new_brk_page < current_process->min_brk_page) {
        return ERROR;
    }

    if (new_brk_page >= current_process->stack_base_page - 1) {
        return ERROR;
    }

    if (new_brk_page > current_process->brk_page) {
        for (int vpn = current_process->brk_page; vpn < new_brk_page; vpn++) {
            int pfn = AllocFrame();
            if (pfn == ERROR) {
                for (int undo = current_process->brk_page; undo < vpn; undo++) {
                    if (current_process->region1_pt[undo].valid) {
                        FreeFrame(current_process->region1_pt[undo].pfn);
                    }
                    ClearRegion1Page(&current_process->region1_pt[undo]);
                }
                return ERROR;
            }

            if (MapPage(current_process->region1_pt, vpn, pfn,
                        PROT_READ | PROT_WRITE) == ERROR) {
                FreeFrame(pfn);
                for (int undo = current_process->brk_page; undo < vpn; undo++) {
                    if (current_process->region1_pt[undo].valid) {
                        FreeFrame(current_process->region1_pt[undo].pfn);
                    }
                    ClearRegion1Page(&current_process->region1_pt[undo]);
                }
                return ERROR;
            }
        }
    } else if (new_brk_page < current_process->brk_page) {
        for (int vpn = new_brk_page; vpn < current_process->brk_page; vpn++) {
            if (current_process->region1_pt[vpn].valid) {
                FreeFrame(current_process->region1_pt[vpn].pfn);
            }
            ClearRegion1Page(&current_process->region1_pt[vpn]);
        }
    }

    current_process->brk_page = new_brk_page;
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    return SUCCESS;
}

static int KernelDelay(int clock_ticks, int current_tick)
{
    if (clock_ticks < 0) {
        return ERROR;
    }

    if (clock_ticks == 0) {
        return SUCCESS;
    }

    if (current_process == NULL) {
        return ERROR;
    }

    //mark process as delayed.
    current_process->delayed = 1;
    current_process->wake_tick = current_tick + clock_ticks;

    //remove from ready queue since it's blocked.
    RemoveProcessFromQueue(&ready_queue, current_process);

    return SUCCESS;
}


/*----------------------------------------------------------------------------------*/
//CHECKPOINT 4: Fork, Exec, Wait

static pcb_t *FindChild(pcb_t *parent, int want_zombie)
{
    queue_node_t *node = all_processes.head;

    while (node != NULL) {
        pcb_t *proc = node->process;
        if (proc != NULL && proc->parent == parent &&
            (!want_zombie || proc->is_zombie)) {
            return proc;
        }
        node = node->next;
    }

    return NULL;
}

static void OrphanChildren(pcb_t *parent)
{
    queue_node_t *node = all_processes.head;

    while (node != NULL) {
        queue_node_t *next = node->next;
        pcb_t *proc = node->process;
        if (proc != NULL && proc->parent == parent) {
            if (proc->is_zombie) {
                UnregisterProcess(proc);
                FreeProcess(proc);
            } else {
                proc->parent = NULL;
            }
        }
        node = next;
    }
}

static void WakeWaitingParent(pcb_t *child)
{
    pcb_t *parent = child->parent;

    if (parent == NULL || !parent->wait_blocked) {
        return;
    }

    parent->user_context.regs[0] = child->pid;
    parent->wait_status_value = child->exit_status;
    parent->wait_status_ready = 1;
    parent->wait_blocked = 0;
    child->parent = NULL;

    if (!IsProcessInQueue(&ready_queue, parent)) {
        EnqueueProcess(&ready_queue, parent);
    }
}

void KernelExitProcess(int status)
{
    if (current_process == NULL) {
        return;
    }

    if (current_process == init_process) {
        TracePrintf(0, "init process exited with status %d; halting\n", status);
        Halt();
    }

    TracePrintf(1, "KernelExit: PID %d status %d\n",
                current_process->pid, status);

    OrphanChildren(current_process);
    RemoveProcessFromQueue(&ready_queue, current_process);
    current_process->exit_status = status;
    current_process->is_zombie = 1;
    current_process->delayed = 0;
    current_process->wait_blocked = 0;
    WakeWaitingParent(current_process);
}


//fork syscall: create a child process that is an exact copy of the current process.
static int KernelFork(void)
{
    //cannot fork if there is no current process.
    if (current_process == NULL) {
        return ERROR;
    }

    //clone the current process to create a child.
    pcb_t *child = CloneProcess(current_process);

    if (child == NULL) {
        //not enough memory or resources to create child.
        return ERROR;
    }

    //set up the child's kernel context with its own kernel stack.
    if (KernelContextSwitch(KCCopy, (void *)child, NULL) != 0) {
        //failed to set up child's kernel context.
        UnregisterProcess(child);
        FreeProcess(child);
        return ERROR;
    }

    if (current_process != NULL && current_process->fork_return_zero) {
        current_process->fork_return_zero = 0;
        return 0;
    }

    //debug: check child's PC after KCCopy and PCB addresses.
    TracePrintf(0, "KernelFork: parent PCB=%p, child PCB=%p, parent user_context=%p, child user_context=%p\n",
                current_process, child, &current_process->user_context, &child->user_context);
    TracePrintf(1, "KernelFork: after KCCopy, child PC=%p\n", child->user_context.pc);

    //in the child, Fork returns 0.
    //we set this up now, but it will be restored when the child runs.
    child->user_context.regs[0] = 0;

    //debug: check child's PC after setting regs[0].
    TracePrintf(1, "KernelFork: after regs[0]=0, child PC=%p\n", child->user_context.pc);

    //add the child to the ready queue so it can be scheduled.
    EnqueueProcess(&ready_queue, child);

    //return the child's PID to the parent.
    return child->pid;
}


//exec syscall: replace the current process's program with a new executable.
static int KernelExec(char *filename, char *argv[])
{
    //buffer to hold filename copied from user space.
    char kernel_filename[256];
    int i;

    //cannot exec if there is no current process.
    if (current_process == NULL) {
        return ERROR;
    }

    //validate the filename pointer.
    if (filename == NULL) {
        return ERROR;
    }

    //copy filename from user space to kernel space.
    //the filename pointer is a user space address, so we need to copy it carefully.
    for (i = 0; i < 255; i++) {
        kernel_filename[i] = filename[i];
        if (kernel_filename[i] == '\0') {
            break;
        }
    }

    //ensure null termination.
    kernel_filename[255] = '\0';

    //validate the argv pointer.
    if (argv == NULL) {
        return ERROR;
    }

    //temporarily switch to the process's Region 1 address space.
    WriteRegister(REG_PTBR1, (unsigned int)current_process->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);

    //load the new program into the current process's Region 1.
    //use the kernel space copy of the filename, not the user space pointer.
    int result = LoadProgram(kernel_filename, argv, current_process);

    if (result != SUCCESS) {
        //LoadProgram failed, the process is now in an inconsistent state.
        //return ERROR to indicate failure.
        return result;
    }

    //exec never returns on success - the new program starts executing.
    //but since we're in a syscall context, we need to signal that this
    //process should not continue from where it trapped.
    //the PC will already be set by LoadProgram.

    //return SUCCESS to indicate the load succeeded (process will restart).
    return SUCCESS;
}


//wait syscall: wait for a child process to exit and retrieve its exit status.
static int KernelWait(int *status_ptr)
{
    pcb_t *child;
    int child_pid;

    if (current_process == NULL) {
        return ERROR;
    }

    child = FindChild(current_process, 1);
    if (child != NULL) {
        child_pid = child->pid;
        if (status_ptr != NULL) {
            *status_ptr = child->exit_status;
        }
        UnregisterProcess(child);
        FreeProcess(child);
        return child_pid;
    }

    child = FindChild(current_process, 0);
    if (child == NULL) {
        return ERROR;
    }

    current_process->wait_blocked = 1;
    current_process->wait_status_ptr = status_ptr;
    current_process->wait_status_ready = 0;
    RemoveProcessFromQueue(&ready_queue, current_process);
    return SUCCESS;
}

static int KernelWaitDispatch(int *status_ptr, int *blocked)
{
    int rc = KernelWait(status_ptr);

    if (current_process != NULL && current_process->wait_blocked) {
        *blocked = 1;
    }

    return rc;
}

int DispatchSyscall(UserContext *uctxt, int current_tick)
{
    int blocked = 0;

    switch (uctxt->code) {
    case YALNIX_GETPID:
        uctxt->regs[0] = KernelGetPid();
        break;

    case YALNIX_BRK:
        uctxt->regs[0] = KernelBrk((void *)uctxt->regs[0]);
        break;

    case YALNIX_DELAY:
        uctxt->regs[0] = KernelDelay((int)uctxt->regs[0], current_tick);
        blocked = (uctxt->regs[0] == SUCCESS &&
                   current_process != NULL &&
                   current_process->delayed);
        break;

    case YALNIX_EXIT:
        KernelExitProcess((int)uctxt->regs[0]);
        blocked = 1;
        break;

    case YALNIX_FORK:
        //fork syscall: child gets 0, parent gets child's PID.
        memcpy(&current_process->user_context, uctxt, sizeof(UserContext));
        uctxt->regs[0] = KernelFork();
        break;

    case YALNIX_EXEC:
        //exec syscall: does not return on success, returns ERROR on failure.
        uctxt->regs[0] = KernelExec((char *)uctxt->regs[0], (char **)uctxt->regs[1]);
        if (uctxt->regs[0] == KILL) {
            KernelExitProcess(ERROR);
            blocked = 1;
            break;
        }
        //after exec succeeds, update uctxt with the new program's context.
        //LoadProgram sets the PC and SP in current_process->user_context.
        if (uctxt->regs[0] == SUCCESS) {
            memcpy(uctxt, &current_process->user_context, sizeof(UserContext));
        }
        break;

    case YALNIX_WAIT:
        //wait syscall: parent waits for child to exit.
        uctxt->regs[0] = KernelWaitDispatch((int *)uctxt->regs[0], &blocked);
        break;

    default:
        TracePrintf(0, "unsupported syscall code=0x%x\n", uctxt->code);
        uctxt->regs[0] = ERROR;
        break;
    }

    return blocked;
}

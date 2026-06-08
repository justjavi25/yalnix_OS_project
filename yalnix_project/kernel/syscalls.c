#include "syscalls.h"
#include "memory.h"
#include "process.h"
#include "tty.h"
#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>
#include <string.h>

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
        WriteRegister(REG_PTBR1, (unsigned int)current_process->region1_pt);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
        return 0;
    }

    // Keep this at trace level 1; it is useful when diagnosing fork context copies.
    TracePrintf(1, "KernelFork: parent PCB=%p, child PCB=%p, parent user_context=%p, child user_context=%p\n",
                current_process, child, &current_process->user_context, &child->user_context);
    TracePrintf(1, "KernelFork: after KCCopy, child PC=%p\n", child->user_context.pc);

    //in the child, Fork returns 0.
    //we set this up now, but it will be restored when the child runs.
    child->user_context.regs[0] = 0;

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

/*----------------------------------------------------------------------------------*/
/* Checkpoint 6: pipes, locks, condition variables, and Reclaim.
 *
 * These kernel objects live in fixed-size tables.  User programs only see
 * integer handles, while the kernel keeps the actual buffers and waiter queues
 * here in Region 0.  A blocked syscall removes its process from ready_queue and
 * leaves enough state in the PCB for the eventual wakeup path to finish the call.
 */

#define MAX_PIPES 256

typedef struct {
    int id;
    int valid;

    /* FIFO contents are kept in a circular buffer. */
    char buffer[PIPE_BUFFER_LEN];
    int read_pos;
    int write_pos;
    int filled;

    /* Readers wait here when the pipe is empty; writers wait when it is full. */
    process_queue_t read_queue;
    process_queue_t write_queue;
} pipe_t;

typedef struct {
    int id;
    int valid;

    /* owner_pid is meaningful only while held is nonzero. */
    int owner_pid;
    int held;

    /* FIFO queue of processes blocked in Acquire. */
    process_queue_t wait_queue;
} lock_t;

typedef struct {
    int id;
    int valid;

    /* Waiters have released their lock and must reacquire it before returning. */
    process_queue_t wait_queue;
} cvar_t;

static pipe_t pipes[MAX_PIPES];
static int next_pipe_id = 0;

static lock_t locks[MAX_PIPES];
static int next_lock_id = 0;

static cvar_t cvars[MAX_PIPES];
static int next_cvar_id = 0;

/* Initialize all CP6 object tables before the first user process can run. */
void InitPipeSystem(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].valid = 0;
        pipes[i].id = -1;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].filled = 0;
        InitProcessQueue(&pipes[i].read_queue);
        InitProcessQueue(&pipes[i].write_queue);
    }

    /* Use disjoint handle ranges so Reclaim can diagnose the target type cheaply. */
    next_pipe_id = 1000;

    for (int i = 0; i < MAX_PIPES; i++) {
        locks[i].valid = 0;
        locks[i].id = -1;
        locks[i].owner_pid = -1;
        locks[i].held = 0;
        InitProcessQueue(&locks[i].wait_queue);
    }
    next_lock_id = 2000;

    for (int i = 0; i < MAX_PIPES; i++) {
        cvars[i].valid = 0;
        cvars[i].id = -1;
        InitProcessQueue(&cvars[i].wait_queue);
    }
    next_cvar_id = 3000;
}

static pipe_t *FindPipe(int pipe_id)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].valid && pipes[i].id == pipe_id) {
            return &pipes[i];
        }
    }
    return NULL;
}

static int AllocPipe(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].valid) {
            pipes[i].valid = 1;
            pipes[i].id = next_pipe_id++;
            pipes[i].read_pos = 0;
            pipes[i].write_pos = 0;
            pipes[i].filled = 0;
            return pipes[i].id;
        }
    }

    return ERROR;
}

/* PipeInit creates an empty kernel FIFO and copies its handle to user space. */
static int KernelPipeInit(int *pipe_idp)
{
    if (pipe_idp == NULL || current_process == NULL) {
        return ERROR;
    }

    if (AddrToRegion1Page(pipe_idp) == ERROR) {
        return ERROR;
    }

    int pipe_id = AllocPipe();
    if (pipe_id == ERROR) {
        return ERROR;
    }

    *pipe_idp = pipe_id;
    return SUCCESS;
}

/* PipeRead returns immediately when data is buffered, otherwise blocks. */
static int KernelPipeRead(int pipe_id, void *buf, int len, int *blocked)
{
    pipe_t *pipe;
    int available;
    int to_copy;

    if (buf == NULL || len < 0 || blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    pipe = FindPipe(pipe_id);
    if (pipe == NULL) {
        return ERROR;
    }

    // check how much data available
    available = pipe->filled;

    if (available == 0) {
        /* Save the user buffer in the PCB so a future writer can finish this read. */
        current_process->pipe_read_blocked = 1;
        current_process->pipe_read_id = pipe_id;
        current_process->pipe_read_buf = buf;
        current_process->pipe_read_len = len;
        RemoveProcessFromQueue(&ready_queue, current_process);
        EnqueueProcess(&pipe->read_queue, current_process);
        *blocked = 1;
        return SUCCESS;
    }

    to_copy = (len < available) ? len : available;

    for (int i = 0; i < to_copy; i++) {
        ((char *)buf)[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_LEN;
    }

    // update filled count
    pipe->filled -= to_copy;

    /* A read may free enough space for one blocked writer to make progress. */
    pcb_t *writer = DequeueProcess(&pipe->write_queue);
    if (writer != NULL && !writer->pipe_write_blocked) {
        EnqueueProcess(&ready_queue, writer);
    } else if (writer != NULL) {
        int write_available = PIPE_BUFFER_LEN - pipe->filled;
        if (write_available >= PIPE_BUFFER_LEN / 2) {
            int to_write = (writer->pipe_write_len < write_available) ?
                          writer->pipe_write_len : write_available;

            for (int i = 0; i < to_write; i++) {
                pipe->buffer[pipe->write_pos] = ((char *)writer->pipe_write_buf)[i + writer->pipe_write_offset];
                pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_LEN;
            }

            pipe->filled += to_write;
            writer->pipe_write_offset += to_write;

            if (writer->pipe_write_offset >= writer->pipe_write_len) {
                writer->pipe_write_blocked = 0;
                writer->user_context.regs[0] = writer->pipe_write_len;
                EnqueueProcess(&ready_queue, writer);
            } else {
                EnqueueProcess(&pipe->write_queue, writer);
            }
        } else {
            EnqueueProcess(&pipe->write_queue, writer);
        }
    }

    return to_copy;
}

/* PipeWrite buffers as much data as possible and blocks for any remainder. */
static int KernelPipeWrite(int pipe_id, void *buf, int len, int *blocked)
{
    pipe_t *pipe;
    int available;
    int to_write;

    if (buf == NULL || len < 0 || blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    pipe = FindPipe(pipe_id);
    if (pipe == NULL) {
        return ERROR;
    }

    // check available space
    available = PIPE_BUFFER_LEN - pipe->filled;

    if (len > 0 && available == 0) {
        /* Keep the original user buffer and offset so later reads can resume it. */
        current_process->pipe_write_blocked = 1;
        current_process->pipe_write_id = pipe_id;
        current_process->pipe_write_buf = buf;
        current_process->pipe_write_len = len;
        current_process->pipe_write_offset = 0;
        RemoveProcessFromQueue(&ready_queue, current_process);
        EnqueueProcess(&pipe->write_queue, current_process);
        *blocked = 1;
        return SUCCESS;
    }

    to_write = (len < available) ? len : available;

    for (int i = 0; i < to_write; i++) {
        pipe->buffer[pipe->write_pos] = ((char *)buf)[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_LEN;
    }

    // update filled count
    pipe->filled += to_write;

    /* If readers were sleeping, satisfy one read from the newly buffered data. */
    pcb_t *reader = DequeueProcess(&pipe->read_queue);
    if (reader != NULL) {
        int available_data = pipe->filled;
        int to_give = (reader->pipe_read_len < available_data) ?
                     reader->pipe_read_len : available_data;

        for (int i = 0; i < to_give; i++) {
            ((char *)reader->pipe_read_buf)[i] = pipe->buffer[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_LEN;
        }

        pipe->filled -= to_give;
        reader->pipe_read_blocked = 0;
        reader->user_context.regs[0] = to_give;
        EnqueueProcess(&ready_queue, reader);
    }

    if (to_write < len) {
        /* The caller returns only after the full requested length is written. */
        current_process->pipe_write_blocked = 1;
        current_process->pipe_write_id = pipe_id;
        current_process->pipe_write_buf = ((char *)buf) + to_write;
        current_process->pipe_write_len = len - to_write;
        current_process->pipe_write_offset = 0;
        RemoveProcessFromQueue(&ready_queue, current_process);
        EnqueueProcess(&pipe->write_queue, current_process);
        *blocked = 1;
        return SUCCESS;
    }

    return to_write;
}

static lock_t *FindLock(int lock_id);
static cvar_t *FindCvar(int cvar_id);

/* Reclaim invalidates a CP6 object and wakes blocked users with ERROR. */
static int KernelReclaim(int id)
{
    pcb_t *proc;

    pipe_t *pipe = FindPipe(id);
    if (pipe != NULL) {
        while ((proc = DequeueProcess(&pipe->read_queue)) != NULL) {
            proc->pipe_read_blocked = 0;
            proc->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, proc);
        }

        while ((proc = DequeueProcess(&pipe->write_queue)) != NULL) {
            proc->pipe_write_blocked = 0;
            proc->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, proc);
        }

        pipe->valid = 0;
        return SUCCESS;
    }

    lock_t *lock = FindLock(id);
    if (lock != NULL) {
        while ((proc = DequeueProcess(&lock->wait_queue)) != NULL) {
            proc->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, proc);
        }

        lock->valid = 0;
        return SUCCESS;
    }

    cvar_t *cvar = FindCvar(id);
    if (cvar != NULL) {
        while ((proc = DequeueProcess(&cvar->wait_queue)) != NULL) {
            proc->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, proc);
        }

        cvar->valid = 0;
        return SUCCESS;
    }

    return ERROR;
}

static lock_t *FindLock(int lock_id)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (locks[i].valid && locks[i].id == lock_id) {
            return &locks[i];
        }
    }
    return NULL;
}

static int AllocLock(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!locks[i].valid) {
            locks[i].valid = 1;
            locks[i].id = next_lock_id++;
            locks[i].owner_pid = -1;
            locks[i].held = 0;
            InitProcessQueue(&locks[i].wait_queue);
            return locks[i].id;
        }
    }

    return ERROR;
}

/* LockInit returns a new unlocked mutex handle. */
static int KernelLockInit(int *lock_idp)
{
    if (lock_idp == NULL || current_process == NULL) {
        return ERROR;
    }

    if (AddrToRegion1Page(lock_idp) == ERROR) {
        return ERROR;
    }

    int lock_id = AllocLock();
    if (lock_id == ERROR) {
        return ERROR;
    }

    *lock_idp = lock_id;
    return SUCCESS;
}

/* Acquire either takes the lock immediately or queues the caller. */
static int KernelAcquire(int lock_id, int *blocked)
{
    lock_t *lock;

    if (blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    lock = FindLock(lock_id);
    if (lock == NULL) {
        return ERROR;
    }

    if (!lock->held) {
        lock->held = 1;
        lock->owner_pid = current_process->pid;
        return SUCCESS;
    }

    current_process->lock_blocked = 1;
    current_process->lock_blocked_id = lock_id;
    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&lock->wait_queue, current_process);
    *blocked = 1;
    return SUCCESS;
}

/* Release hands ownership directly to the next waiter when one exists. */
static int KernelRelease(int lock_id)
{
    lock_t *lock;
    pcb_t *waiter;

    lock = FindLock(lock_id);
    if (lock == NULL) {
        return ERROR;
    }

    if (!lock->held || lock->owner_pid != current_process->pid) {
        return ERROR;
    }

    waiter = DequeueProcess(&lock->wait_queue);
    if (waiter != NULL) {
        lock->owner_pid = waiter->pid;
        waiter->lock_blocked = 0;
        EnqueueProcess(&ready_queue, waiter);
    } else {
        lock->held = 0;
        lock->owner_pid = -1;
    }

    return SUCCESS;
}

static cvar_t *FindCvar(int cvar_id)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (cvars[i].valid && cvars[i].id == cvar_id) {
            return &cvars[i];
        }
    }
    return NULL;
}

static int AllocCvar(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!cvars[i].valid) {
            cvars[i].valid = 1;
            cvars[i].id = next_cvar_id++;
            InitProcessQueue(&cvars[i].wait_queue);
            return cvars[i].id;
        }
    }

    return ERROR;
}

/* CvarInit returns a handle for a condition variable wait queue. */
static int KernelCvarInit(int *cvar_idp)
{
    if (cvar_idp == NULL || current_process == NULL) {
        return ERROR;
    }

    if (AddrToRegion1Page(cvar_idp) == ERROR) {
        return ERROR;
    }

    int cvar_id = AllocCvar();
    if (cvar_id == ERROR) {
        return ERROR;
    }

    *cvar_idp = cvar_id;
    return SUCCESS;
}

/* CvarWait releases lock_id, sleeps, then returns only after reacquiring it. */
static int KernelCvarWait(int cvar_id, int lock_id, int *blocked)
{
    cvar_t *cvar;
    lock_t *lock;

    if (blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    cvar = FindCvar(cvar_id);
    lock = FindLock(lock_id);
    if (cvar == NULL || lock == NULL) {
        return ERROR;
    }

    if (!lock->held || lock->owner_pid != current_process->pid) {
        return ERROR;
    }

    /* Save lock_id so Signal/Broadcast can move the process through Acquire. */
    current_process->cvar_wait_lock_id = lock_id;

    lock->held = 0;
    lock->owner_pid = -1;

    current_process->cvar_blocked = 1;
    current_process->cvar_blocked_id = cvar_id;
    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&cvar->wait_queue, current_process);
    *blocked = 1;
    return SUCCESS;
}

/* CvarSignal wakes one waiter, then makes it compete for its saved lock. */
static int KernelCvarSignal(int cvar_id)
{
    cvar_t *cvar;
    lock_t *lock;
    pcb_t *waiter;
    int lock_id;

    cvar = FindCvar(cvar_id);
    if (cvar == NULL) {
        return ERROR;
    }

    waiter = DequeueProcess(&cvar->wait_queue);
    if (waiter != NULL) {
        lock_id = waiter->cvar_wait_lock_id;
        lock = FindLock(lock_id);
        if (lock == NULL) {
            waiter->cvar_blocked = 0;
            waiter->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, waiter);
            return SUCCESS;
        }

        waiter->cvar_blocked = 0;
        if (!lock->held) {
            lock->held = 1;
            lock->owner_pid = waiter->pid;
            EnqueueProcess(&ready_queue, waiter);
        } else {
            waiter->lock_blocked = 1;
            waiter->lock_blocked_id = lock_id;
            EnqueueProcess(&lock->wait_queue, waiter);
        }
    }

    return SUCCESS;
}

/* CvarBroadcast applies the Signal wakeup rule to every waiting process. */
static int KernelCvarBroadcast(int cvar_id)
{
    cvar_t *cvar;
    lock_t *lock;
    pcb_t *waiter;
    int lock_id;

    cvar = FindCvar(cvar_id);
    if (cvar == NULL) {
        return ERROR;
    }

    while ((waiter = DequeueProcess(&cvar->wait_queue)) != NULL) {
        lock_id = waiter->cvar_wait_lock_id;
        lock = FindLock(lock_id);
        if (lock == NULL) {
            waiter->cvar_blocked = 0;
            waiter->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, waiter);
            continue;
        }

        waiter->cvar_blocked = 0;
        if (!lock->held) {
            lock->held = 1;
            lock->owner_pid = waiter->pid;
            EnqueueProcess(&ready_queue, waiter);
        } else {
            waiter->lock_blocked = 1;
            waiter->lock_blocked_id = lock_id;
            EnqueueProcess(&lock->wait_queue, waiter);
        }
    }

    return SUCCESS;
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

    case YALNIX_TTY_READ:
        uctxt->regs[0] = KernelTtyRead((int)uctxt->regs[0],
                                        (void *)uctxt->regs[1],
                                        (int)uctxt->regs[2],
                                        &blocked);
        break;

    case YALNIX_TTY_WRITE:
        uctxt->regs[0] = KernelTtyWrite((int)uctxt->regs[0],
                                         (void *)uctxt->regs[1],
                                         (int)uctxt->regs[2],
                                         &blocked);
        break;

    case YALNIX_PIPE_INIT:
        uctxt->regs[0] = KernelPipeInit((int *)uctxt->regs[0]);
        break;

    case YALNIX_PIPE_READ:
        uctxt->regs[0] = KernelPipeRead((int)uctxt->regs[0],
                                        (void *)uctxt->regs[1],
                                        (int)uctxt->regs[2],
                                        &blocked);
        break;

    case YALNIX_PIPE_WRITE:
        uctxt->regs[0] = KernelPipeWrite((int)uctxt->regs[0],
                                         (void *)uctxt->regs[1],
                                         (int)uctxt->regs[2],
                                         &blocked);
        break;

    case YALNIX_RECLAIM:
        uctxt->regs[0] = KernelReclaim((int)uctxt->regs[0]);
        break;

    case YALNIX_LOCK_INIT:
        uctxt->regs[0] = KernelLockInit((int *)uctxt->regs[0]);
        break;

    case YALNIX_LOCK_ACQUIRE:
        uctxt->regs[0] = KernelAcquire((int)uctxt->regs[0], &blocked);
        break;

    case YALNIX_LOCK_RELEASE:
        uctxt->regs[0] = KernelRelease((int)uctxt->regs[0]);
        break;

    case YALNIX_CVAR_INIT:
        uctxt->regs[0] = KernelCvarInit((int *)uctxt->regs[0]);
        break;

    case YALNIX_CVAR_WAIT:
        uctxt->regs[0] = KernelCvarWait((int)uctxt->regs[0],
                                        (int)uctxt->regs[1],
                                        &blocked);
        break;

    case YALNIX_CVAR_SIGNAL:
        uctxt->regs[0] = KernelCvarSignal((int)uctxt->regs[0]);
        break;

    case YALNIX_CVAR_BROADCAST:
        uctxt->regs[0] = KernelCvarBroadcast((int)uctxt->regs[0]);
        break;

    default:
        TracePrintf(0, "unsupported syscall code=0x%x\n", uctxt->code);
        uctxt->regs[0] = ERROR;
        break;
    }

    return blocked;
}

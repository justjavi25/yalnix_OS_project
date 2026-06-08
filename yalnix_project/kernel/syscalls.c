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

/*----------------------------------------------------------------------------------*/
//CHECKPOINT 6: Pipes, Locks, Condition Variables

#define MAX_PIPES 256

// pipe data structure
typedef struct {
    int id;
    int valid;
    // circular buffer for pipe data
    char buffer[PIPE_BUFFER_LEN];
    int read_pos;
    int write_pos;
    int filled;
    // queues for blocked processes
    process_queue_t read_queue;
    process_queue_t write_queue;
} pipe_t;

// lock data structure
typedef struct {
    int id;
    int valid;
    // owner process id
    int owner_pid;
    // whether lock is currently held
    int held;
    // queue of processes waiting for lock
    process_queue_t wait_queue;
} lock_t;

// condition variable data structure
typedef struct {
    int id;
    int valid;
    // queue of processes waiting on this cvar
    process_queue_t wait_queue;
} cvar_t;

// global pipe table
static pipe_t pipes[MAX_PIPES];
// next pipe id to assign
static int next_pipe_id = 0;

// global lock table
static lock_t locks[MAX_PIPES];
// next lock id to assign
static int next_lock_id = 0;

// global cvar table
static cvar_t cvars[MAX_PIPES];
// next cvar id to assign
static int next_cvar_id = 0;

// initialize pipe system
void InitPipeSystem(void)
{
    // clear all pipes
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].valid = 0;
        pipes[i].id = -1;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].filled = 0;
        InitProcessQueue(&pipes[i].read_queue);
        InitProcessQueue(&pipes[i].write_queue);
    }
    // start pipe ids at 1000 to avoid conflicts
    next_pipe_id = 1000;

    // clear all locks
    for (int i = 0; i < MAX_PIPES; i++) {
        locks[i].valid = 0;
        locks[i].id = -1;
        locks[i].owner_pid = -1;
        locks[i].held = 0;
        InitProcessQueue(&locks[i].wait_queue);
    }
    // start lock ids at 2000
    next_lock_id = 2000;

    // clear all cvars
    for (int i = 0; i < MAX_PIPES; i++) {
        cvars[i].valid = 0;
        cvars[i].id = -1;
        InitProcessQueue(&cvars[i].wait_queue);
    }
    // start cvar ids at 3000
    next_cvar_id = 3000;
}

// find pipe by id
static pipe_t *FindPipe(int pipe_id)
{
    // search pipe table for matching id
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].valid && pipes[i].id == pipe_id) {
            return &pipes[i];
        }
    }
    return NULL;
}

// allocate new pipe from global table
static int AllocPipe(void)
{
    // find first free pipe slot
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].valid) {
            // mark as valid and initialize
            pipes[i].valid = 1;
            pipes[i].id = next_pipe_id++;
            pipes[i].read_pos = 0;
            pipes[i].write_pos = 0;
            pipes[i].filled = 0;
            return pipes[i].id;
        }
    }
    // table full
    return ERROR;
}

// syscall: create new pipe
static int KernelPipeInit(int *pipe_idp)
{
    // validate args
    if (pipe_idp == NULL || current_process == NULL) {
        return ERROR;
    }

    // allocate pipe
    int pipe_id = AllocPipe();
    if (pipe_id == ERROR) {
        return ERROR;
    }

    // return id to userland
    *pipe_idp = pipe_id;
    return SUCCESS;
}

// syscall: read from pipe
static int KernelPipeRead(int pipe_id, void *buf, int len, int *blocked)
{
    pipe_t *pipe;
    int available;
    int to_copy;

    // validate args
    if (buf == NULL || len < 0 || blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    // find pipe
    pipe = FindPipe(pipe_id);
    if (pipe == NULL) {
        return ERROR;
    }

    // check how much data available
    available = pipe->filled;

    // if empty, block reader on this pipe
    if (available == 0) {
        current_process->pipe_read_blocked = 1;
        current_process->pipe_read_id = pipe_id;
        current_process->pipe_read_buf = buf;
        current_process->pipe_read_len = len;
        RemoveProcessFromQueue(&ready_queue, current_process);
        EnqueueProcess(&pipe->read_queue, current_process);
        *blocked = 1;
        return SUCCESS;
    }

    // copy requested amount or what's available
    to_copy = (len < available) ? len : available;

    // copy data from circular buffer
    for (int i = 0; i < to_copy; i++) {
        ((char *)buf)[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_LEN;
    }

    // update filled count
    pipe->filled -= to_copy;

    // wake any blocked writers
    pcb_t *writer = DequeueProcess(&pipe->write_queue);
    if (writer != NULL && !writer->pipe_write_blocked) {
        // put writer back on ready queue
        EnqueueProcess(&ready_queue, writer);
    } else if (writer != NULL) {
        // try to complete partial write
        int write_available = PIPE_BUFFER_LEN - pipe->filled;
        if (write_available >= PIPE_BUFFER_LEN / 2) {
            // enough space to continue write
            int to_write = (writer->pipe_write_len < write_available) ?
                          writer->pipe_write_len : write_available;

            // copy data from writer's buffer
            for (int i = 0; i < to_write; i++) {
                pipe->buffer[pipe->write_pos] = ((char *)writer->pipe_write_buf)[i + writer->pipe_write_offset];
                pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_LEN;
            }

            // update pipe state
            pipe->filled += to_write;
            writer->pipe_write_offset += to_write;

            // check if write complete
            if (writer->pipe_write_offset >= writer->pipe_write_len) {
                // write done, wake writer
                writer->pipe_write_blocked = 0;
                writer->user_context.regs[0] = writer->pipe_write_len;
                EnqueueProcess(&ready_queue, writer);
            } else {
                // more to write, put back on queue
                EnqueueProcess(&pipe->write_queue, writer);
            }
        } else {
            // not enough space yet, requeue writer
            EnqueueProcess(&pipe->write_queue, writer);
        }
    }

    return to_copy;
}

// syscall: write to pipe
static int KernelPipeWrite(int pipe_id, void *buf, int len, int *blocked)
{
    pipe_t *pipe;
    int available;
    int to_write;

    // validate args
    if (buf == NULL || len < 0 || blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    // find pipe
    pipe = FindPipe(pipe_id);
    if (pipe == NULL) {
        return ERROR;
    }

    // check available space
    available = PIPE_BUFFER_LEN - pipe->filled;

    // if full and want to write, block writer
    if (len > 0 && available == 0) {
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

    // write what fits
    to_write = (len < available) ? len : available;

    // copy to circular buffer
    for (int i = 0; i < to_write; i++) {
        pipe->buffer[pipe->write_pos] = ((char *)buf)[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_LEN;
    }

    // update filled count
    pipe->filled += to_write;

    // wake blocked readers
    pcb_t *reader = DequeueProcess(&pipe->read_queue);
    if (reader != NULL) {
        // give reader what's available
        int available_data = pipe->filled;
        int to_give = (reader->pipe_read_len < available_data) ?
                     reader->pipe_read_len : available_data;

        // copy data to reader
        for (int i = 0; i < to_give; i++) {
            ((char *)reader->pipe_read_buf)[i] = pipe->buffer[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_LEN;
        }

        // update pipe and wake reader
        pipe->filled -= to_give;
        reader->pipe_read_blocked = 0;
        reader->user_context.regs[0] = to_give;
        EnqueueProcess(&ready_queue, reader);
    }

    // if partial write, block for remainder
    if (to_write < len) {
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

// syscall: destroy pipe and wake all blocked processes
static int KernelReclaim(int id)
{
    // find pipe
    pipe_t *pipe = FindPipe(id);
    if (pipe == NULL) {
        return ERROR;
    }

    // wake all blocked readers with error
    pcb_t *proc;
    while ((proc = DequeueProcess(&pipe->read_queue)) != NULL) {
        proc->pipe_read_blocked = 0;
        proc->user_context.regs[0] = ERROR;
        EnqueueProcess(&ready_queue, proc);
    }

    // wake all blocked writers with error
    while ((proc = DequeueProcess(&pipe->write_queue)) != NULL) {
        proc->pipe_write_blocked = 0;
        proc->user_context.regs[0] = ERROR;
        EnqueueProcess(&ready_queue, proc);
    }

    // mark pipe as invalid
    pipe->valid = 0;
    return SUCCESS;
}

// lock data structure
typedef struct {
    int id;
    int valid;
    int owner_pid;
    int held;
    process_queue_t wait_queue;
} lock_t;

// global lock table
static lock_t locks[MAX_PIPES];
// next lock id to assign
static int next_lock_id = 0;

// find lock by id
static lock_t *FindLock(int lock_id)
{
    // search lock table for matching id
    for (int i = 0; i < MAX_PIPES; i++) {
        if (locks[i].valid && locks[i].id == lock_id) {
            return &locks[i];
        }
    }
    return NULL;
}

// allocate new lock from global table
static int AllocLock(void)
{
    // find first free lock slot
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!locks[i].valid) {
            // mark as valid and initialize
            locks[i].valid = 1;
            locks[i].id = next_lock_id++;
            locks[i].owner_pid = -1;
            locks[i].held = 0;
            InitProcessQueue(&locks[i].wait_queue);
            return locks[i].id;
        }
    }
    // table full
    return ERROR;
}

// syscall: create new lock
static int KernelLockInit(int *lock_idp)
{
    // validate args
    if (lock_idp == NULL || current_process == NULL) {
        return ERROR;
    }

    // allocate lock
    int lock_id = AllocLock();
    if (lock_id == ERROR) {
        return ERROR;
    }

    // return id to userland
    *lock_idp = lock_id;
    return SUCCESS;
}

// syscall: acquire lock
static int KernelAcquire(int lock_id, int *blocked)
{
    lock_t *lock;

    // validate args
    if (blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    // find lock
    lock = FindLock(lock_id);
    if (lock == NULL) {
        return ERROR;
    }

    // if lock not held, acquire it
    if (!lock->held) {
        lock->held = 1;
        lock->owner_pid = current_process->pid;
        return SUCCESS;
    }

    // lock is held, block process
    current_process->lock_blocked = 1;
    current_process->lock_blocked_id = lock_id;
    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&lock->wait_queue, current_process);
    *blocked = 1;
    return SUCCESS;
}

// syscall: release lock
static int KernelRelease(int lock_id)
{
    lock_t *lock;
    pcb_t *waiter;

    // find lock
    lock = FindLock(lock_id);
    if (lock == NULL) {
        return ERROR;
    }

    // check if current process owns lock
    if (!lock->held || lock->owner_pid != current_process->pid) {
        return ERROR;
    }

    // wake first waiter if any
    waiter = DequeueProcess(&lock->wait_queue);
    if (waiter != NULL) {
        // give lock to waiter
        lock->owner_pid = waiter->pid;
        waiter->lock_blocked = 0;
        EnqueueProcess(&ready_queue, waiter);
    } else {
        // no waiters, release lock
        lock->held = 0;
        lock->owner_pid = -1;
    }

    return SUCCESS;
}

// condition variable data structure
typedef struct {
    int id;
    int valid;
    process_queue_t wait_queue;
} cvar_t;

// global cvar table
static cvar_t cvars[MAX_PIPES];
// next cvar id to assign
static int next_cvar_id = 0;

// find cvar by id
static cvar_t *FindCvar(int cvar_id)
{
    // search cvar table for matching id
    for (int i = 0; i < MAX_PIPES; i++) {
        if (cvars[i].valid && cvars[i].id == cvar_id) {
            return &cvars[i];
        }
    }
    return NULL;
}

// allocate new cvar from global table
static int AllocCvar(void)
{
    // find first free cvar slot
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!cvars[i].valid) {
            // mark as valid and initialize
            cvars[i].valid = 1;
            cvars[i].id = next_cvar_id++;
            InitProcessQueue(&cvars[i].wait_queue);
            return cvars[i].id;
        }
    }
    // table full
    return ERROR;
}

// syscall: create new cvar
static int KernelCvarInit(int *cvar_idp)
{
    // validate args
    if (cvar_idp == NULL || current_process == NULL) {
        return ERROR;
    }

    // allocate cvar
    int cvar_id = AllocCvar();
    if (cvar_id == ERROR) {
        return ERROR;
    }

    // return id to userland
    *cvar_idp = cvar_id;
    return SUCCESS;
}

// syscall: wait on cvar
static int KernelCvarWait(int cvar_id, int lock_id, int *blocked)
{
    cvar_t *cvar;
    lock_t *lock;

    // validate args
    if (blocked == NULL || current_process == NULL) {
        return ERROR;
    }

    // find cvar and lock
    cvar = FindCvar(cvar_id);
    lock = FindLock(lock_id);
    if (cvar == NULL || lock == NULL) {
        return ERROR;
    }

    // check if current process owns lock
    if (!lock->held || lock->owner_pid != current_process->pid) {
        return ERROR;
    }

    // save lock id for re-acquire on wake
    current_process->cvar_wait_lock_id = lock_id;

    // release the lock
    lock->held = 0;
    lock->owner_pid = -1;

    // block on cvar
    current_process->cvar_blocked = 1;
    current_process->cvar_blocked_id = cvar_id;
    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&cvar->wait_queue, current_process);
    *blocked = 1;
    return SUCCESS;
}

// syscall: signal cvar (wake one waiter)
static int KernelCvarSignal(int cvar_id)
{
    cvar_t *cvar;
    lock_t *lock;
    pcb_t *waiter;
    int lock_id;

    // find cvar
    cvar = FindCvar(cvar_id);
    if (cvar == NULL) {
        return ERROR;
    }

    // wake first waiter if any
    waiter = DequeueProcess(&cvar->wait_queue);
    if (waiter != NULL) {
        // get the lock that waiter needs to re-acquire
        lock_id = waiter->cvar_wait_lock_id;
        lock = FindLock(lock_id);
        if (lock == NULL) {
            // lock was deleted, give error to waiter
            waiter->cvar_blocked = 0;
            waiter->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, waiter);
            return SUCCESS;
        }

        // block waiter on lock acquisition
        waiter->cvar_blocked = 0;
        if (!lock->held) {
            // lock is free, give it to waiter
            lock->held = 1;
            lock->owner_pid = waiter->pid;
            EnqueueProcess(&ready_queue, waiter);
        } else {
            // lock is held, block waiter
            waiter->lock_blocked = 1;
            waiter->lock_blocked_id = lock_id;
            EnqueueProcess(&lock->wait_queue, waiter);
        }
    }

    return SUCCESS;
}

// syscall: broadcast cvar (wake all waiters)
static int KernelCvarBroadcast(int cvar_id)
{
    cvar_t *cvar;
    lock_t *lock;
    pcb_t *waiter;
    int lock_id;

    // find cvar
    cvar = FindCvar(cvar_id);
    if (cvar == NULL) {
        return ERROR;
    }

    // wake all waiters
    while ((waiter = DequeueProcess(&cvar->wait_queue)) != NULL) {
        // get the lock that waiter needs to re-acquire
        lock_id = waiter->cvar_wait_lock_id;
        lock = FindLock(lock_id);
        if (lock == NULL) {
            // lock was deleted, give error to waiter
            waiter->cvar_blocked = 0;
            waiter->user_context.regs[0] = ERROR;
            EnqueueProcess(&ready_queue, waiter);
            continue;
        }

        // block waiter on lock acquisition
        waiter->cvar_blocked = 0;
        if (!lock->held) {
            // lock is free, give it to waiter
            lock->held = 1;
            lock->owner_pid = waiter->pid;
            EnqueueProcess(&ready_queue, waiter);
        } else {
            // lock is held, block waiter
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

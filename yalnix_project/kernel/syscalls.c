#include "syscalls.h"
#include "memory.h"
#include "process.h"
#include "tty.h"
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

typedef struct pipe_state {
    int id;
    int valid;
    char buf[PIPE_BUFFER_LEN];
    int head;
    int len;
    process_queue_t readers;
    process_queue_t writers;
    struct pipe_state *next;
} pipe_state_t;

typedef struct lock_state {
    int id;
    int valid;
    pcb_t *owner;
    process_queue_t waiters;
    struct lock_state *next;
} lock_state_t;

typedef struct cvar_state {
    int id;
    int valid;
    process_queue_t waiters;
    struct cvar_state *next;
} cvar_state_t;

static int next_resource_id = 1;
static pipe_state_t *pipes = NULL;
static lock_state_t *locks = NULL;
static cvar_state_t *cvars = NULL;

static int UserReadable(void *buf, int len)
{
    return UserBufferValidFor(current_process, buf, len, PROT_READ);
}

static int UserWritable(void *buf, int len)
{
    return UserBufferValidFor(current_process, buf, len, PROT_WRITE);
}

static void CopyToProcess(pcb_t *proc, void *dst, void *src, int len)
{
    unsigned int saved_ptbr1;

    if (proc == NULL || len <= 0) {
        return;
    }

    saved_ptbr1 = (current_process == NULL || current_process->region1_pt == NULL)
        ? 0
        : (unsigned int)current_process->region1_pt;

    WriteRegister(REG_PTBR1, (unsigned int)proc->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    memcpy(dst, src, len);

    if (saved_ptbr1 != 0) {
        WriteRegister(REG_PTBR1, saved_ptbr1);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    }
}

static int CopyIntToUser(int *dst, int value)
{
    if (!UserWritable(dst, sizeof(int))) {
        return ERROR;
    }
    *dst = value;
    return SUCCESS;
}

static int AllocResourceId(void)
{
    return next_resource_id++;
}

static pipe_state_t *FindPipe(int id)
{
    pipe_state_t *pipe = pipes;
    while (pipe != NULL) {
        if (pipe->valid && pipe->id == id) {
            return pipe;
        }
        pipe = pipe->next;
    }
    return NULL;
}

static lock_state_t *FindLock(int id)
{
    lock_state_t *lock = locks;
    while (lock != NULL) {
        if (lock->valid && lock->id == id) {
            return lock;
        }
        lock = lock->next;
    }
    return NULL;
}

static cvar_state_t *FindCvar(int id)
{
    cvar_state_t *cvar = cvars;
    while (cvar != NULL) {
        if (cvar->valid && cvar->id == id) {
            return cvar;
        }
        cvar = cvar->next;
    }
    return NULL;
}

static void ReadyProcess(pcb_t *proc)
{
    if (proc != NULL && proc != current_process &&
        proc != idle_process && !proc->is_zombie &&
        !IsProcessInQueue(&ready_queue, proc)) {
        EnqueueProcess(&ready_queue, proc);
    }
}

static void WakeLockWaiter(lock_state_t *lock)
{
    pcb_t *waiter;

    if (lock == NULL || lock->owner != NULL) {
        return;
    }

    waiter = DequeueProcess(&lock->waiters);
    if (waiter == NULL) {
        return;
    }

    lock->owner = waiter;
    waiter->lock_blocked = 0;
    waiter->waiting_lock_id = 0;
    waiter->user_context.regs[0] = SUCCESS;
    ReadyProcess(waiter);
}

static void ReleaseLockInternal(lock_state_t *lock)
{
    if (lock == NULL) {
        return;
    }
    lock->owner = NULL;
    WakeLockWaiter(lock);
}

static void GrantLockOrBlock(pcb_t *proc, int lock_id)
{
    lock_state_t *lock = FindLock(lock_id);

    if (proc == NULL || lock == NULL) {
        if (proc != NULL) {
            proc->cvar_blocked = 0;
            proc->lock_blocked = 0;
            proc->user_context.regs[0] = ERROR;
            ReadyProcess(proc);
        }
        return;
    }

    if (lock->owner == NULL) {
        lock->owner = proc;
        proc->cvar_blocked = 0;
        proc->lock_blocked = 0;
        proc->waiting_lock_id = 0;
        proc->user_context.regs[0] = SUCCESS;
        ReadyProcess(proc);
        return;
    }

    proc->cvar_blocked = 0;
    proc->lock_blocked = 1;
    proc->waiting_lock_id = lock_id;
    EnqueueProcess(&lock->waiters, proc);
}

static int PipeReadBytes(pipe_state_t *pipe, pcb_t *reader)
{
    int count;
    char tmp[PIPE_BUFFER_LEN];

    if (pipe == NULL || reader == NULL || pipe->len <= 0) {
        return 0;
    }

    count = reader->pipe_read_len;
    if (count > pipe->len) {
        count = pipe->len;
    }
    if (count > PIPE_BUFFER_LEN) {
        count = PIPE_BUFFER_LEN;
    }

    for (int i = 0; i < count; i++) {
        tmp[i] = pipe->buf[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_BUFFER_LEN;
        pipe->len--;
    }

    CopyToProcess(reader, reader->pipe_read_buf, tmp, count);
    reader->pipe_read_blocked = 0;
    reader->pipe_read_buf = NULL;
    reader->pipe_read_len = 0;
    reader->user_context.regs[0] = count;
    ReadyProcess(reader);
    return count;
}

static int PipeWriteBytes(pipe_state_t *pipe, pcb_t *writer)
{
    int written = 0;

    if (pipe == NULL || writer == NULL) {
        return 0;
    }

    while (writer->pipe_write_offset < writer->pipe_write_len &&
           pipe->len < PIPE_BUFFER_LEN) {
        int tail = (pipe->head + pipe->len) % PIPE_BUFFER_LEN;
        pipe->buf[tail] = writer->pipe_write_buf[writer->pipe_write_offset++];
        pipe->len++;
        written++;
    }

    if (writer->pipe_write_offset >= writer->pipe_write_len) {
        writer->pipe_write_blocked = 0;
        writer->pipe_id = 0;
        writer->user_context.regs[0] = writer->pipe_write_len;
        free(writer->pipe_write_buf);
        writer->pipe_write_buf = NULL;
        writer->pipe_write_len = 0;
        writer->pipe_write_offset = 0;
        ReadyProcess(writer);
    }

    return written;
}

static void ServicePipe(pipe_state_t *pipe)
{
    int progressed = 1;

    while (pipe != NULL && progressed) {
        progressed = 0;

        while (pipe->len > 0 && PeekProcess(&pipe->readers) != NULL) {
            pcb_t *reader = DequeueProcess(&pipe->readers);
            progressed += PipeReadBytes(pipe, reader);
        }

        while (pipe->len < PIPE_BUFFER_LEN && PeekProcess(&pipe->writers) != NULL) {
            pcb_t *writer = PeekProcess(&pipe->writers);
            int wrote = PipeWriteBytes(pipe, writer);
            progressed += wrote;
            if (!writer->pipe_write_blocked) {
                DequeueProcess(&pipe->writers);
            }
            if (wrote == 0) {
                break;
            }
        }
    }
}

static int KernelPipeInit(int *pipe_idp)
{
    pipe_state_t *pipe;

    if (CopyIntToUser(pipe_idp, 0) == ERROR) {
        return ERROR;
    }

    pipe = (pipe_state_t *)malloc(sizeof(pipe_state_t));
    if (pipe == NULL) {
        return ERROR;
    }

    memset(pipe, 0, sizeof(pipe_state_t));
    pipe->id = AllocResourceId();
    pipe->valid = 1;
    InitProcessQueue(&pipe->readers);
    InitProcessQueue(&pipe->writers);
    pipe->next = pipes;
    pipes = pipe;
    *pipe_idp = pipe->id;
    return SUCCESS;
}

static int KernelPipeRead(int pipe_id, void *buf, int len, int *blocked)
{
    pipe_state_t *pipe = FindPipe(pipe_id);

    if (pipe == NULL || len < 0 || blocked == NULL) {
        return ERROR;
    }
    if (len == 0) {
        return 0;
    }
    if (!UserWritable(buf, len)) {
        return ERROR;
    }

    current_process->pipe_read_blocked = 1;
    current_process->pipe_id = pipe_id;
    current_process->pipe_read_buf = buf;
    current_process->pipe_read_len = len;

    if (pipe->len > 0) {
        PipeReadBytes(pipe, current_process);
        ServicePipe(pipe);
        return current_process->user_context.regs[0];
    }

    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&pipe->readers, current_process);
    ServicePipe(pipe);
    if (current_process->pipe_read_blocked) {
        *blocked = 1;
        return SUCCESS;
    }
    return current_process->user_context.regs[0];
}

static int KernelPipeWrite(int pipe_id, void *buf, int len, int *blocked)
{
    pipe_state_t *pipe = FindPipe(pipe_id);
    char *kernel_buf;

    if (pipe == NULL || len < 0 || blocked == NULL) {
        return ERROR;
    }
    if (len == 0) {
        return 0;
    }
    if (!UserReadable(buf, len)) {
        return ERROR;
    }

    kernel_buf = (char *)malloc(len);
    if (kernel_buf == NULL) {
        return ERROR;
    }
    memcpy(kernel_buf, buf, len);

    current_process->pipe_write_blocked = 1;
    current_process->pipe_id = pipe_id;
    current_process->pipe_write_buf = kernel_buf;
    current_process->pipe_write_len = len;
    current_process->pipe_write_offset = 0;

    PipeWriteBytes(pipe, current_process);
    ServicePipe(pipe);
    if (current_process->pipe_write_blocked) {
        RemoveProcessFromQueue(&ready_queue, current_process);
        EnqueueProcess(&pipe->writers, current_process);
        ServicePipe(pipe);
    }
    if (current_process->pipe_write_blocked) {
        *blocked = 1;
        return SUCCESS;
    }
    return len;
}

static int KernelLockInit(int *lock_idp)
{
    lock_state_t *lock;

    if (CopyIntToUser(lock_idp, 0) == ERROR) {
        return ERROR;
    }

    lock = (lock_state_t *)malloc(sizeof(lock_state_t));
    if (lock == NULL) {
        return ERROR;
    }

    memset(lock, 0, sizeof(lock_state_t));
    lock->id = AllocResourceId();
    lock->valid = 1;
    InitProcessQueue(&lock->waiters);
    lock->next = locks;
    locks = lock;
    *lock_idp = lock->id;
    return SUCCESS;
}

static int KernelAcquire(int lock_id, int *blocked)
{
    lock_state_t *lock = FindLock(lock_id);

    if (lock == NULL || blocked == NULL || lock->owner == current_process) {
        return ERROR;
    }

    if (lock->owner == NULL) {
        lock->owner = current_process;
        return SUCCESS;
    }

    current_process->lock_blocked = 1;
    current_process->waiting_lock_id = lock_id;
    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&lock->waiters, current_process);
    *blocked = 1;
    return SUCCESS;
}

static int KernelRelease(int lock_id)
{
    lock_state_t *lock = FindLock(lock_id);

    if (lock == NULL || lock->owner != current_process) {
        return ERROR;
    }

    ReleaseLockInternal(lock);
    return SUCCESS;
}

static int KernelCvarInit(int *cvar_idp)
{
    cvar_state_t *cvar;

    if (CopyIntToUser(cvar_idp, 0) == ERROR) {
        return ERROR;
    }

    cvar = (cvar_state_t *)malloc(sizeof(cvar_state_t));
    if (cvar == NULL) {
        return ERROR;
    }

    memset(cvar, 0, sizeof(cvar_state_t));
    cvar->id = AllocResourceId();
    cvar->valid = 1;
    InitProcessQueue(&cvar->waiters);
    cvar->next = cvars;
    cvars = cvar;
    *cvar_idp = cvar->id;
    return SUCCESS;
}

static int KernelCvarWait(int cvar_id, int lock_id, int *blocked)
{
    cvar_state_t *cvar = FindCvar(cvar_id);
    lock_state_t *lock = FindLock(lock_id);

    if (cvar == NULL || lock == NULL || blocked == NULL ||
        lock->owner != current_process) {
        return ERROR;
    }

    current_process->cvar_blocked = 1;
    current_process->waiting_cvar_id = cvar_id;
    current_process->cvar_wait_lock_id = lock_id;
    RemoveProcessFromQueue(&ready_queue, current_process);
    EnqueueProcess(&cvar->waiters, current_process);
    ReleaseLockInternal(lock);
    *blocked = 1;
    return SUCCESS;
}

static int KernelCvarSignal(int cvar_id)
{
    cvar_state_t *cvar = FindCvar(cvar_id);
    pcb_t *waiter;

    if (cvar == NULL) {
        return ERROR;
    }

    waiter = DequeueProcess(&cvar->waiters);
    if (waiter != NULL) {
        waiter->waiting_cvar_id = 0;
        GrantLockOrBlock(waiter, waiter->cvar_wait_lock_id);
    }
    return SUCCESS;
}

static int KernelCvarBroadcast(int cvar_id)
{
    cvar_state_t *cvar = FindCvar(cvar_id);
    pcb_t *waiter;

    if (cvar == NULL) {
        return ERROR;
    }

    while ((waiter = DequeueProcess(&cvar->waiters)) != NULL) {
        waiter->waiting_cvar_id = 0;
        GrantLockOrBlock(waiter, waiter->cvar_wait_lock_id);
    }
    return SUCCESS;
}

static void WakeQueueWithError(process_queue_t *queue)
{
    pcb_t *proc;

    while ((proc = DequeueProcess(queue)) != NULL) {
        proc->pipe_read_blocked = 0;
        proc->pipe_write_blocked = 0;
        proc->lock_blocked = 0;
        proc->cvar_blocked = 0;
        proc->user_context.regs[0] = ERROR;
        if (proc->pipe_write_buf != NULL) {
            free(proc->pipe_write_buf);
            proc->pipe_write_buf = NULL;
        }
        ReadyProcess(proc);
    }
}

static int KernelReclaim(int id)
{
    pipe_state_t *pipe = pipes;
    lock_state_t *lock = locks;
    cvar_state_t *cvar = cvars;

    while (pipe != NULL) {
        if (pipe->valid && pipe->id == id) {
            pipe->valid = 0;
            WakeQueueWithError(&pipe->readers);
            WakeQueueWithError(&pipe->writers);
            return SUCCESS;
        }
        pipe = pipe->next;
    }

    while (lock != NULL) {
        if (lock->valid && lock->id == id) {
            lock->valid = 0;
            lock->owner = NULL;
            WakeQueueWithError(&lock->waiters);
            return SUCCESS;
        }
        lock = lock->next;
    }

    while (cvar != NULL) {
        if (cvar->valid && cvar->id == id) {
            cvar->valid = 0;
            WakeQueueWithError(&cvar->waiters);
            return SUCCESS;
        }
        cvar = cvar->next;
    }

    return ERROR;
}

static void CleanupProcessResources(pcb_t *proc)
{
    lock_state_t *lock = locks;

    while (lock != NULL) {
        if (lock->valid) {
            RemoveProcessFromQueue(&lock->waiters, proc);
            if (lock->owner == proc) {
                lock->owner = NULL;
                WakeLockWaiter(lock);
            }
        }
        lock = lock->next;
    }

    for (pipe_state_t *pipe = pipes; pipe != NULL; pipe = pipe->next) {
        RemoveProcessFromQueue(&pipe->readers, proc);
        RemoveProcessFromQueue(&pipe->writers, proc);
    }

    for (cvar_state_t *cvar = cvars; cvar != NULL; cvar = cvar->next) {
        RemoveProcessFromQueue(&cvar->waiters, proc);
    }
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

    CleanupProcessResources(current_process);
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
    TracePrintf(1, "KernelFork: parent PCB=%p, child PCB=%p, parent user_context=%p, child user_context=%p\n",
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

    if (status_ptr != NULL && !UserWritable(status_ptr, sizeof(int))) {
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

    case YALNIX_RECLAIM:
        uctxt->regs[0] = KernelReclaim((int)uctxt->regs[0]);
        break;

    default:
        TracePrintf(0, "unsupported syscall code=0x%x\n", uctxt->code);
        uctxt->regs[0] = ERROR;
        break;
    }

    return blocked;
}

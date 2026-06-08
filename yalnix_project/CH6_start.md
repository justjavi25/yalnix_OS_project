# Checkpoint 6 Notes

This checkpoint adds the IPC and synchronization calls used by the pipe, lock,
cvar, and torture tests.  The current implementation keeps the CP6 object state
inside `kernel/syscalls.c` and initializes it from `KernelStart`.

## Kernel Objects

The kernel stores pipes, locks, and condition variables in fixed-size tables.
User processes receive integer handles only; the buffers, ownership state, and
wait queues stay in Region 0.

Handle ranges:
- Pipes start at `1000`
- Locks start at `2000`
- Condition variables start at `3000`

## Pipes

Implemented syscalls:
- `PipeInit(int *pipe_idp)`
- `PipeRead(int pipe_id, void *buf, int len)`
- `PipeWrite(int pipe_id, void *buf, int len)`

Each pipe uses a circular FIFO buffer of `PIPE_BUFFER_LEN` bytes.  Readers block
when the pipe is empty.  Writers block when the buffer cannot accept the full
write yet.  The PCB stores the blocked read/write buffer and remaining length so
the wakeup path can complete the syscall later.

## Locks

Implemented syscalls:
- `LockInit(int *lock_idp)`
- `Acquire(int lock_id)`
- `Release(int lock_id)`

Locks track the owning PID and a FIFO waiter queue.  `Acquire` returns
immediately if the lock is free; otherwise the process is removed from the ready
queue.  `Release` verifies ownership and transfers the lock directly to the next
waiter when one exists.

## Condition Variables

Implemented syscalls:
- `CvarInit(int *cvar_idp)`
- `CvarWait(int cvar_id, int lock_id)`
- `CvarSignal(int cvar_id)`
- `CvarBroadcast(int cvar_id)`

`CvarWait` requires the caller to hold the supplied lock.  The syscall releases
that lock, blocks on the cvar queue, and records the lock handle in the PCB.
`Signal` and `Broadcast` move waiters back through the lock-acquire path before
they return to user mode.

## Reclaim

`Reclaim(int id)` invalidates any CP6 object handle and wakes processes blocked
on that object with `ERROR`.  The syscall checks the pipe, lock, and cvar tables
in that order.

## Scheduler Touch Points

The scheduler treats CP6 blocking flags the same way it treats `Delay`, `Wait`,
and terminal I/O blocking.  A process is runnable only after all blocking flags
are clear.  This logic lives in `trap.c` in `WakeDelayedProcesses` and
`IsRunnable`.

## Tests

Useful test order:
- `test/lock`
- `test/cvar`
- `test/pipe_r2w`
- `test/pipe_w2r`
- `test/bigstack`
- `test/forkstack`
- `test/torture`

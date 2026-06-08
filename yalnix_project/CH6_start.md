# Checkpoint 6 Implementation Guide

## Overview
CP6 is **not in the official curriculum**, but the project includes test files that require advanced IPC and synchronization features. This document outlines what would need to be implemented.

---

## Required Syscalls for Extra Tests

### 1. PIPES (3 syscalls)
Used by: `pipe_r2w.c`, `pipe_w2r.c`, `torture.c`

```c
int PipeInit(int *pipe_idp)
int PipeRead(int pipe_id, void *buf, int len)
int PipeWrite(int pipe_id, void *buf, int len)
int Reclaim(int id)  // Also destroys pipes
```

**Implementation outline:**
- Global array of pipe structures
- Each pipe: FIFO buffer (min PIPE_BUFFER_LEN bytes), read queue, write queue
- PipeRead: blocks if empty, waits for writer
- PipeWrite: non-blocking if space; blocks if full
- Reclaim: free pipe and wake blocked processes

**Estimated lines of code: 300**

---

### 2. LOCKS (4 syscalls)
Used by: `lock.c`, `torture.c`, `cvar.c`

```c
int LockInit(int *lock_idp)
int Acquire(int lock_id)
int Release(int lock_id)
int Reclaim(int id)  // Destroys locks
```

**Implementation outline:**
- Global array of lock structures
- Each lock: owner PID, waiter queue, held flag
- Acquire: blocks if held by another process
- Release: wakes next waiter
- Reclaim: free lock and wake blocked processes

**Estimated lines of code: 150**

---

### 3. CONDITION VARIABLES (5 syscalls)
Used by: `cvar.c`, `torture.c`

```c
int CvarInit(int *cvar_idp)
int CvarSignal(int cvar_id)
int CvarBroadcast(int cvar_id)
int CvarWait(int cvar_id, int lock_id)
int Reclaim(int id)  // Destroys cvars
```

**Implementation outline:**
- Global array of cvar structures
- Each cvar: waiter queue, associated lock
- CvarWait: release lock, block on cvar, re-acquire lock (Mesa semantics)
- CvarSignal: wake one waiter
- CvarBroadcast: wake all waiters
- Reclaim: free cvar and wake blocked processes

**Estimated lines of code: 200**

---

## Architecture Changes Needed

### 1. Global Synchronization Objects Table
```c
typedef struct {
    int id;           // Unique identifier
    int type;         // PIPE, LOCK, CVAR
    union {
        pipe_t *pipe;
        lock_t *lock;
        cvar_t *cvar;
    } data;
} sync_object_t;

#define MAX_SYNC_OBJECTS 256
sync_object_t sync_objects[MAX_SYNC_OBJECTS];
```

### 2. Process PCB Extensions
```c
struct pcb {
    // ... existing fields ...
    int blocked_on;      // ID of object this process is blocked on (if any)
    int blocked_type;    // PIPE/LOCK/CVAR/TTY
    struct lock *held_lock;  // For MESA semantics in CvarWait
};
```

### 3. New Trap Handling
None - all use TRAP_KERNEL syscalls

---

## Test Files & Requirements

| File | Features | Syscalls Needed |
|------|----------|-----------------|
| pipe_r2w.c | Create pipe, write, read | PipeInit, PipeWrite, PipeRead, Reclaim |
| pipe_w2r.c | Create pipe, read, write | PipeInit, PipeRead, PipeWrite, Reclaim |
| lock.c | Basic mutual exclusion | LockInit, Acquire, Release, Reclaim |
| cvar.c | Producer/consumer with cvars | CvarInit, CvarWait, CvarSignal, LockInit, Acquire, Release, Reclaim |
| torture.c | Complex stress test | All of the above |
| bigstack.c | Stack growth beyond initial | TRAP_MEMORY handler (should already work) |
| forkstack.c | Fork + stack growth | Fork + TRAP_MEMORY handler |

---

## Implementation Steps

1. **Add syscall dispatch entries** in `kernel/syscalls.c`
   - Map new syscall codes to handler functions
   
2. **Implement Pipe subsystem** (~250 lines)
   - `kernel/pipe.c`: PipeInit, PipeRead, PipeWrite
   - Data structures and queue management
   
3. **Implement Lock subsystem** (~150 lines)
   - `kernel/lock.c`: LockInit, Acquire, Release
   - Simple FIFO waiter queue
   
4. **Implement Cvar subsystem** (~200 lines)
   - `kernel/cvar.c`: CvarInit, CvarWait, CvarSignal, CvarBroadcast
   - Mesa semantics (don't re-check condition)
   
5. **Implement Reclaim** (~50 lines)
   - `kernel/syscalls.c`: Unified resource destruction
   - Walk sync_objects table and destroy

6. **Update scheduler** (~50 lines)
   - Handle processes blocked on synchronization objects
   - Wake them when appropriate

---

## Key Design Decisions

### 1. Blocking Semantics
- When process blocks on pipe/lock/cvar:
  - Remove from ready queue
  - Add to object's waiter queue
  - Scheduler skips it until woken

### 2. Lock Release (Acquire/Release)
- Only caller can release own lock
- Wake first waiter in queue
- Waiter directly transitions to ready (no additional lock acquisition)

### 3. Condition Variable Wait (Mesa Semantics)
- Release the associated lock
- Block on the cvar
- When woken: re-acquire lock before returning
- **Caller MUST re-check condition** (not guaranteed condition is still true)

### 4. Pipe Semantics
- Buffer between processes
- Write never blocks if space < PIPE_BUFFER_LEN
- Read blocks if empty
- Multiple readers/writers supported

### 5. Resource Cleanup
- Reclaim() destroys any object type
- Wake all blocked processes (give ERROR)
- Return resources to free pool

---

## Testing Order

1. Test pipes separately (pipe_r2w, pipe_w2r)
2. Test locks separately (lock.c)
3. Test cvars separately (cvar.c)
4. Test combined (torture.c)

---

## Estimated Effort
- **Total new code: ~800-1000 lines**
- **Complexity: Medium** (blocking/waking logic is tricky)
- **Time: 4-8 hours** for experienced student
- **Risk: Low** (contained to new modules, no changes to existing code)


# Checkpoint 4 Testing Guide

## Overview

This guide explains how to compile and test the Fork, Exec, and Wait implementations.

## Step 1: Compile Everything

From the project root directory:

```bash
cd /Volumes/home/COSC58_Workspace/yalnix_OS_project/yalnix_project
make clean
make
```

This will:
- Compile the kernel with Fork, Exec, Wait handlers
- Compile all test programs (cp4_fork, cp4_exec, cp4_wait, cp4_fork_multi, cp4_fork_exec)
- Link everything into the `yalnix` executable

You should see output like:
```
gcc -c kernel/kernelstart.c
gcc -c kernel/memory.c
gcc -c kernel/process.c
gcc -c kernel/syscalls.c
...
gcc -o cp4_fork test/cp4_fork.c
gcc -o cp4_exec test/cp4_exec.c
gcc -o cp4_wait test/cp4_wait.c
...
```

---

## Step 2: Run Individual Tests

### Test 1: Basic Fork (cp4_fork)

**Purpose:** Verify that Fork creates a child process with a different PID

**Command:**
```bash
./yalnix cp4_fork
```

**Expected Output:**
```
cp4_fork: Fork returned [CHILD_PID]
cp4_fork: I am CHILD, my PID is [CHILD_PID]
cp4_fork: PASS if child PID is different from parent
cp4_fork: I am PARENT, my PID is [PARENT_PID]
cp4_fork: PASS if parent and child PIDs are different
```

**Success Criteria:**
- Both parent and child print their messages
- Child PID ≠ Parent PID
- No segmentation faults or crashes
- Both processes loop (call Pause())

**What's being tested:**
- CloneProcess() allocates new PCB correctly
- Child's memory is copied from parent
- Parent and child have different PIDs
- Child's return register (regs[0]) is set to 0

---

### Test 2: Multiple Forks (cp4_fork_multi)

**Purpose:** Verify that multiple Forks work independently and don't interfere

**Command:**
```bash
./yalnix cp4_fork_multi
```

**Expected Output:**
```
cp4_fork_multi: Starting main process PID [PARENT]
cp4_fork_multi: PARENT created child1 PID [CHILD1]
cp4_fork_multi: CHILD1 PID [CHILD1]
cp4_fork_multi: CHILD1 allocated heap
cp4_fork_multi: PARENT created child2 PID [CHILD2]
cp4_fork_multi: CHILD2 PID [CHILD2]
cp4_fork_multi: CHILD2 allocated larger heap
cp4_fork_multi: PARENT now has 2 children
cp4_fork_multi: PASS if all 3 processes have different PIDs
```

**Success Criteria:**
- Three processes created (parent + 2 children)
- All three have different PIDs
- Each child has its own heap (Brk calls succeed)
- No interference between children's memory
- Each process can call syscalls independently

**What's being tested:**
- Multiple children can be created
- Each child has independent memory space
- Heap operations don't affect siblings
- Process list grows correctly

---

### Test 3: Exec (cp4_exec)

**Purpose:** Verify that Exec replaces the current process with a new program

**Command:**
```bash
./yalnix cp4_exec
```

**Expected Output:**
```
cp4_exec: Starting with PID [PID]
cp4_exec: About to call Exec to replace with cp4_fork
cp4_fork: Fork returned [CHILD_PID]
cp4_fork: I am CHILD, my PID is [CHILD_PID]
cp4_fork: I am PARENT, my PID is [PARENT_PID]
```

**Success Criteria:**
- Initial cp4_exec message appears
- Program switches to cp4_fork output mid-run
- NO "Exec FAILED" message
- The SAME process (same PID) now runs different code
- cp4_fork program runs to completion

**What's being tested:**
- LoadProgram() frees old Region 1 pages
- New program is loaded correctly
- Entry point (PC) is set to new program's start
- Process memory is completely replaced
- Child's PID from cp4_fork matches what Exec would have shown

---

### Test 4: Fork + Exec (cp4_fork_exec)

**Purpose:** Verify the common pattern: Fork + Exec to spawn a new program

**Command:**
```bash
./yalnix cp4_fork_exec
```

**Expected Output:**
```
cp4_fork_exec: PARENT PID [PARENT] starting
cp4_fork_exec: CHILD PID [CHILD] before Exec
cp4_fork: Fork returned [GRANDCHILD_PID]
cp4_fork: I am CHILD, my PID is [GRANDCHILD_PID]
cp4_fork: I am PARENT, my PID is [CHILD]
cp4_fork_exec: PARENT PID [PARENT] created child PID [CHILD]
cp4_fork_exec: PARENT continuing while child runs different program
cp4_fork_exec: PASS if child process shows cp4_fork output
```

**Success Criteria:**
- Three processes active (parent + child + grandchild)
- Parent and child have different PIDs
- Child Execs to run cp4_fork
- cp4_fork then Forks, creating a grandchild
- Parent continues while child runs different program

**What's being tested:**
- Fork works correctly
- Exec works correctly
- Both work together in the same execution flow
- Process memory is independent across Fork+Exec

---

### Test 5: Fork + Wait (cp4_wait)

**Purpose:** Verify that Fork works and Wait is prepared for use (currently returns ERROR)

**Command:**
```bash
./yalnix cp4_wait
```

**Expected Output:**
```
cp4_wait: Parent starting with PID [PARENT]
cp4_wait: CHILD PID [CHILD] starting work
cp4_wait: CHILD allocated heap, Brk returned [SUCCESS]
cp4_wait: PARENT created child with PID [CHILD]
cp4_wait: PARENT calling Wait for child
cp4_wait: Wait returned ERROR (not yet fully implemented)
cp4_wait: PARENT exiting
cp4_wait: CHILD exiting
```

**Success Criteria:**
- Child and Parent created with different PIDs
- Child can call Brk successfully
- Wait returns ERROR (expected for CP4)
- No segmentation faults
- Child and parent keep running

**What's being tested:**
- Fork creates child correctly
- PCB has parent pointer set
- Wait syscall is callable (doesn't crash)
- Return values work as expected

---

## Step 3: Verify With Trace Output

If you want detailed kernel trace output, you can modify the kernel to print more:

```bash
# Recompile with verbose tracing
make clean
make

# Run with trace (look at TRACE file output)
./yalnix cp4_fork 2>&1 | head -100
```

This shows:
- Trap handler calls
- Context switches
- Memory allocations
- Page table updates

---

## Debugging Common Issues

### Issue: "undefined reference to `CloneProcess`"

**Solution:** Make sure process.h includes the function declaration:
```c
pcb_t *CloneProcess(pcb_t *parent);
```

And process.c has the full implementation (around line 600+).

### Issue: Exec returns but shouldn't

**Problem:** LoadProgram might not be getting the right region1_pt

**Check:** Ensure in KernelExec you're writing REG_PTBR1:
```c
WriteRegister(REG_PTBR1, (unsigned int)current_process->region1_pt);
```

### Issue: Child has same PID as parent

**Problem:** helper_new_pid might not be working correctly

**Check:**
- Call helper_new_pid for each new PCB
- Store result in child->pid
- Verify helper_retire_pid is called when freeing

### Issue: Memory corruption after Fork

**Problem:** TLB not being flushed properly

**Check:** After copying pages in CloneProcess:
```c
WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);  // Flush scratch pages
```

### Issue: Segmentation fault in CloneProcess

**Problem:** Invalid frame numbers or page table access

**Debug:**
```c
// Add tracing
TracePrintf(1, "CloneProcess: copying page %d\n", vpn);
if (parent_proc->region1_pt[vpn].pfn < 0) {
    TracePrintf(0, "ERROR: invalid pfn\n");
    return NULL;
}
```

---

## Manual Testing Checklist

Use this checklist to verify functionality:

```
[ ] Compile: make clean && make succeeds
[ ] Test 1:  ./yalnix cp4_fork shows both parent and child output
[ ] Test 2:  ./yalnix cp4_fork_multi shows 3 processes with different PIDs
[ ] Test 3:  ./yalnix cp4_exec shows switch from cp4_exec to cp4_fork code
[ ] Test 4:  ./yalnix cp4_fork_exec shows parent, child, and grandchild
[ ] Test 5:  ./yalnix cp4_wait shows Fork + Wait (even if Wait returns ERROR)
[ ] Memory: No "out of memory" errors
[ ] Crash:  No segmentation faults
[ ] PIDs:   Each process has unique PID
[ ] Syscalls: GetPid, Brk, Delay all still work
```

---

## Understanding Test Output Flow

### cp4_fork execution:

```
Parent starts (PID=1)
  │
  ├─ Fork() → returns child_pid to parent
  │
  ├─ Parent continues with child_pid in return register
  │   └─ Prints "I am PARENT"
  │
  └─ Child also continues from Fork call
      └─ Has regs[0]=0 from CloneProcess setup
      └─ if (pid == 0) branch executes
      └─ Prints "I am CHILD"
```

### cp4_exec execution:

```
Process starts as cp4_exec (PID=1)
  │
  └─ Calls Exec("cp4_fork")
      │
      └─ LoadProgram frees old pages, loads new ones
      │
      └─ Hardware restores new PC (entry point of cp4_fork)
      │
      └─ Process now running cp4_fork code with same PID
         └─ But Region 1 is completely different
```

### cp4_fork_multi execution:

```
Main (PID=1)
  │
  ├─ Fork() → creates Child1 (PID=2)
  │   │
  │   └─ Child1: calls Brk (2000)
  │       └─ sleeps in loop
  │
  └─ Main: Fork() → creates Child2 (PID=3)
      │
      └─ Child2: calls Brk (4000)
          └─ sleeps in loop

Result: 3 independent processes, each with own memory
```

---

## Expected Behavior Summary

| Test | Parent | Child | Grandchild |
|------|--------|-------|------------|
| cp4_fork | Prints | Prints | N/A |
| cp4_fork_multi | Prints + Creates 2 | Prints + Brk | N/A |
| cp4_exec | Becomes cp4_fork | N/A | N/A |
| cp4_fork_exec | Prints | Becomes cp4_fork which Forks | Prints |
| cp4_wait | Prints + Fork | Brks + Waits | N/A |

---

## Next Steps (For Full CP4)

When you implement full Wait/Exit:

1. Add Exit syscall handler
2. Implement process queue/list for tracking children
3. Implement Wait blocking mechanism
4. Test with modified cp4_wait to verify parent blocks until child exits

For now, cp4_wait demonstrates that the foundation is ready for these additions.

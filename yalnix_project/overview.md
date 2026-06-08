# Yalnix OS Project Overview

This is a Yalnix OS kernel implementation for the DCS 58 fictional computer architecture, structured in 5 checkpoints.

## Project Status

- **CP1**: Pseudocode planning
- **CP2**: Idle process and virtual memory setup
- **CP3**: Init process loading (95% - ✓ working)
- **CP4**: Fork, Exec, Wait syscalls (85% - Fork verified working)
- **CP5**: Terminal I/O (90% - TtyRead/Write verified working)

## Build & Run Commands

```bash
# Full rebuild
make clean && make

# Run tests (IMPORTANT: use full path with test/)
./yalnix test/cp4_fork           # ✓ Correct
./yalnix cp4_fork                # ✗ Fails - file not found

# Common tests
./yalnix test/cp4_fork           # Fork/child process test
./yalnix test/cp4_fork_multi     # Multiple children
./yalnix test/cp4_fork_exec      # Fork + Exec pattern
./yalnix test/cp4_wait           # Wait for child
./yalnix test/cp5_tty_write      # Terminal output
echo "input" | ./yalnix test/cp5_tty_read  # Terminal input

# Debugging
./yalnix -lk 2 test/cp4_fork     # Kernel trace level 2
./yalnix -W test/cp4_fork        # Dump core on warnings
make kill                         # Kill stray processes
```

## Project Structure

```
kernel/                 # Kernel implementation
├── kernelstart.c      # Boot, VM init, Region 0 setup
├── process.c          # PCB, Fork, Exec, process cloning
├── memory.c           # Frame allocation, page tables
├── syscalls.c         # All syscall handlers
├── trap.c             # Trap vector, Clock/Kernel handlers
├── tty.c              # Terminal I/O with blocking
└── queue.c            # Process queue operations

test/                   # Test programs
├── cp3_*.c           # CP3: GetPid, Brk, Delay
├── cp4_*.c           # CP4: Fork, Exec, Wait
└── cp5_tty_*.c       # CP5: TtyRead, TtyWrite
```

## Architecture Overview

### Process Management (CP3-CP4)

**PCB (Process Control Block)** contains:
- User context (PC, SP, registers) - saved on trap entry
- Kernel context (used by KernelContextSwitch)
- Region 1 page table (user memory)
- Kernel stack frames
- Parent/child pointers
- Blocking state (delayed, wait, TTY I/O)

**Fork flow**: CloneProcess copies parent's PCB and Region 1 pages → KCCopy sets up kernel stack in child → child wakes with regs[0]=0

**Exec flow**: LoadProgram frees old Region 1, loads new executable, sets PC to entry point

**Wait/Exit flow**: Parent blocks on wait, child marks itself zombie on exit, parent wakes when child exits

### Memory Management (CP2-CP3)

- **Region 0**: Kernel code/data/heap (shared, fixed address)
- **Region 1**: User code/data/heap/stack (per-process, created per Fork/Exec)
- **Kernel stack**: Fixed address (KERNEL_STACK_BASE) but different frames per process on context switch
- **Frame tracking**: Bitmap of free/used physical frames
- **TLB critical**: Must flush after ANY page table change or stale mappings cause silent corruption

### Terminal I/O (CP5)

**TtyWrite blocking sequence**:
1. Process calls TtyWrite → KernelTtyWrite marks process blocked, queues write
2. Clock tick dispatcher skips blocked process
3. HandleTtyTransmit (on hardware completion) wakes process, re-enqueues to ready

**TtyRead blocking sequence**:
1. Process calls TtyRead → KernelTtyRead marks blocked, queues read
2. HandleTtyReceive (hardware input ready) unblocks process with data
3. Clock tick re-schedules to ready queue

**Buffering**: Per-terminal line buffers persist between reads; multiple processes can wait on same terminal.

## Important Notes

1. **Full paths required** - Always `./yalnix test/program`, never `./yalnix program`
2. **Output redirection** - TracePrintf → TRACE file, TtyWrite → TTYLOG files (not stdout)
3. **Context switching** - KernelContextSwitch runs on special stack, saves/restores full kernel state
4. **TLB flushing** - Required after page table changes; stale mappings = silent data corruption
5. **Blocking removal** - Blocked processes removed from ready queue; must be re-enqueued when unblocking
6. **Fork memory safety** - Must temporarily map destination frames to copy; scratch page below kernel stack works

## Key Files to Know

- `yalnix_handout.txt`: Full specification (Sections 8.3-8.5 for CP3-5)
- `CP4_TESTING_GUIDE.md`: Fork/Exec/Wait test documentation
- `kernel/process.h`: PCB structure definition
- `kernel/syscalls.c`: All syscall handler implementations

## Debugging Tips

- Check TRACE for "CORRUPTION", "stale", "Illegal PFN"
- Process state: `grep "PID <N>" TRACE | head -20`
- gdb: `b HandleTrapClock` for kernel breakpoints
- Test hang? Check if process is blocked (Delay, TtyRead, Wait)

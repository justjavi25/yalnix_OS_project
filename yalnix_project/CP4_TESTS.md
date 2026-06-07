# Checkpoint 4 Test Notes

This branch implements Checkpoint 4: `Fork`, `Exec`, `Exit`, `Wait`, parent/child tracking, zombie reaping, and round-robin scheduling.

Run these tests from `yalnix_project` on the Linux Yalnix environment.

```sh
make clean
make
```

Most CP4 tests intentionally loop forever after printing their success traces, so run them with `timeout`.

## Basic Fork

```sh
timeout 5 ./yalnix test/cp4_fork
grep -E "cp4_fork|Fork|KernelFork|memory trap|aborting|ERROR" TRACE
```

Expected output includes both parent and child paths:

```text
cp4_fork: Fork returned 2
cp4_fork: I am PARENT, my PID is 1
cp4_fork: PASS if parent and child PIDs are different
cp4_fork: Fork returned 0
cp4_fork: I am CHILD, my PID is 2
cp4_fork: PASS if child PID is different from parent
```

There should be no `memory trap`, `aborting`, or `ERROR`.

## Basic Exec

```sh
timeout 5 ./yalnix test/cp4_exec
grep -E "cp4_exec|cp4_fork|Exec|memory trap|aborting|ERROR" TRACE
```

Expected output:

```text
cp4_exec: Starting with PID 1
cp4_exec: About to call Exec to replace with cp4_fork
Syscall trap Exec
cp4_fork: Fork returned 2
cp4_fork: I am PARENT, my PID is 1
cp4_fork: Fork returned 0
cp4_fork: I am CHILD, my PID is 2
```

`cp4_exec: exec failed` should not appear. There should be no `memory trap`, `aborting`, or `ERROR`.

## Fork Then Exec

```sh
timeout 5 ./yalnix test/cp4_fork_exec
grep -E "cp4_fork_exec|cp4_fork|Fork|Exec|memory trap|aborting|ERROR" TRACE
```

Expected output:

```text
cp4_fork_exec: PARENT PID 1 starting
cp4_fork_exec: PARENT PID 1 created child PID 2
cp4_fork_exec: PARENT continuing while child runs different program
cp4_fork_exec: PASS if child process shows cp4_fork output
cp4_fork_exec: CHILD PID 2 before Exec
Syscall trap Exec
cp4_fork: Fork returned 3
cp4_fork: I am PARENT, my PID is 2
cp4_fork: Fork returned 0
cp4_fork: I am CHILD, my PID is 3
```

There should be no `memory trap`, `aborting`, or `ERROR`.

## Wait and Exit

```sh
timeout 5 ./yalnix test/cp4_wait
grep -E "cp4_wait|Wait|Exit|KernelExit|memory trap|aborting|ERROR|FAIL|PASS" TRACE
```

Expected output:

```text
cp4_wait: Parent starting with PID 1
cp4_wait: PARENT created child with PID 2
cp4_wait: PARENT calling Wait for child
Syscall trap Wait
cp4_wait: CHILD PID 2 starting work
cp4_wait: CHILD exiting with status 42
Syscall trap Exit
KernelExit: PID 2 status 42
cp4_wait: PASS child PID 2 status 42
cp4_wait: PARENT exiting
```

The parent should block in `Wait` until the child exits, then resume with the correct child PID and status. `FAIL`, `memory trap`, `aborting`, or unexpected `ERROR` output indicates a problem.

Note: `cp4_wait` may print `Brk returned -1` for its child heap experiment. That is not a Wait failure; the pass condition is receiving child PID `2` and status `42`.

## Quick Full CP4 Run

```sh
make clean
make
timeout 5 ./yalnix test/cp4_fork
grep -E "cp4_fork|memory trap|aborting|ERROR" TRACE
timeout 5 ./yalnix test/cp4_exec
grep -E "cp4_exec|cp4_fork|memory trap|aborting|ERROR" TRACE
timeout 5 ./yalnix test/cp4_fork_exec
grep -E "cp4_fork_exec|cp4_fork|memory trap|aborting|ERROR" TRACE
timeout 5 ./yalnix test/cp4_wait
grep -E "cp4_wait|KernelExit|PASS|FAIL|memory trap|aborting|ERROR" TRACE
```

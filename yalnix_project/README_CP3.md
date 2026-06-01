# Checkpoint 3 Tests

These tests are for the minimal Checkpoint 3 target from the Yalnix handout:

- load an `init` program into userland
- switch between `init` and `idle`
- handle `Brk`, `GetPid`, and `Delay`

The CP3 tests intentionally do not call `Exit`, `Fork`, `Exec`, or `Wait`, because those belong to later checkpoints in this repo. Each test prints its result and then loops on `Pause()`.

## Build

From the project root in the Linux/Yalnix environment:

```sh
make clean
make
```

The Makefile builds these user programs:

- `init`
- `test/cp3_getpid`
- `test/cp3_delay`
- `test/cp3_brk`

## Test 1: Default Init

Run:

```sh
./yalnix
```

Expected trace pattern:

```text
KernelStart: booting
KernelStart: leaving KernelStart
init: started pid=<positive pid> argc=1 argv0=init
init: loop 0 before delay
DoIdle
...
init: loop woke from delay
```

Pass condition:

- `init` is loaded without passing an explicit program name.
- `GetPid()` returns a positive pid.
- `Delay(2)` blocks init.
- `DoIdle` appears while init is delayed.
- init later wakes and prints `init: loop woke from delay`.

## Test 2: GetPid

Run:

```sh
./yalnix test/cp3_getpid
```

Expected trace pattern:

```text
cp3_getpid: start argc=1 argv0=test/cp3_getpid
cp3_getpid: GetPid returned <positive pid>
cp3_getpid: PASS if pid is positive
```

Pass condition:

- The returned pid is positive.
- The kernel does not report `unsupported syscall`.

## Test 3: Delay

Run:

```sh
./yalnix test/cp3_delay
```

Expected trace pattern:

```text
cp3_delay: before Delay(3)
DoIdle
...
cp3_delay: after Delay(3), rc=0
cp3_delay: before Delay(0)
cp3_delay: after Delay(0), rc=0
cp3_delay: before Delay(-1)
cp3_delay: after Delay(-1), rc=-1
cp3_delay: PASS if rc values are 0, 0, -1 and idle runs during Delay(3)
```

Pass condition:

- `Delay(3)` returns `0` after at least a few clock ticks.
- `DoIdle` runs while the process is delayed.
- `Delay(0)` returns `0` immediately.
- `Delay(-1)` returns `-1`.

## Test 4: Brk

Run:

```sh
./yalnix test/cp3_brk
```

Expected trace pattern:

```text
cp3_brk: base=<addr> one_page=<addr> two_pages=<addr>
cp3_brk: Brk(one_page) rc=0
cp3_brk: wrote first grown page: A Z
cp3_brk: Brk(two_pages) rc=0
cp3_brk: wrote second grown page: B Y
cp3_brk: shrink back to one_page rc=0
cp3_brk: PASS if all Brk rc values above are 0 and writes printed expected letters
```

Pass condition:

- All three `Brk` calls return `0`.
- Writes into the newly allocated heap pages succeed.
- The kernel does not take an unexpected `TRAP_MEMORY`.

If this test fails before `main` starts, the user heap startup path is probably calling `Brk` earlier than expected. If it fails during the writes, the `Brk` page mapping or Region 1 TLB flush is likely wrong.

## Current CP3 Scope Notes

The current minimal CP3 implementation has only `idle` and one real user process. More general ready queues, process exit, fork/exec/wait, terminal I/O, and full trap handling are expected in later checkpoints.

`TRAP_MEMORY` stack growth is not part of these tests. If one of these tests unexpectedly faults due to normal stack use, implement the minimal stack-growth handler next.

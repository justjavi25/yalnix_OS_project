#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <hardware.h>

// dispatches one TRAP_KERNEL syscall. Returns nonzero if the caller blocked.
int DispatchSyscall(UserContext *uctxt, int current_tick);

// terminate the current process with the supplied status.
void KernelExitProcess(int status);

// initialize CP6 pipes, locks, and condition variables.
void InitPipeSystem(void);

#endif

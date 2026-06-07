#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <hardware.h>

//dispatches one TRAP_KERNEL syscall. Returns nonzero if the caller blocked.
int DispatchSyscall(UserContext *uctxt, int current_tick);

#endif

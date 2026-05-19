/* syscalls.h */
#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <hardware.h>
#include <yalnix.h>


void KernelFork(UserContext *uctxt);


void KernelExec(UserContext *uctxt);


void KernelExit(UserContext *uctxt);


void KernelWait(UserContext *uctxt);


void KernelGetPid(UserContext *uctxt);

void KernelBrk(UserContext *uctxt);

void KernelDelay(UserContext *uctxt);

void KernelTtyRead(UserContext *uctxt);

void KernelTtyWrite(UserContext *uctxt);

#endif /* SYSCALLS_H */

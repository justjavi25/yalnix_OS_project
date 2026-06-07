#ifndef TTY_H
#define TTY_H

#include <hardware.h>

//initialize terminal I/O state.
void InitTtySystem(void);

//kernel-side implementation of the TtyWrite syscall.
int KernelTtyWrite(int tty_id, void *buf, int len, int *blocked);

//handle a TRAP_TTY_TRANSMIT interrupt.
void HandleTtyTransmit(int tty_id);

#endif

#ifndef TRAP_H
#define TRAP_H

#include <hardware.h>

//global trap vector installed through REG_VECTOR_BASE.
extern void (*trap_vector[TRAP_VECTOR_SIZE])(UserContext *);

//initializes all trap vector entries for checkpoint 2.
void init_trap_vector(void);

//handles TRAP_KERNEL syscall traps.
void HandleTrapKernel(UserContext *uctxt);

//handles TRAP_CLOCK timer interrupts.
void HandleTrapClock(UserContext *uctxt);

//handles traps that are not implemented yet.
void HandleTrapUnhandled(UserContext *uctxt);

void HandleTrapIllegal(UserContext *uctxt);

void HandleTrapMemory(UserContext *uctxt);

void HandleTrapMath(UserContext *uctxt);

void HandleTrapTtyReceive(UserContext *uctxt);

void HandleTrapTtyTransmit(UserContext *uctxt);

void HandleTrapDisk(UserContext *uctxt);

#endif

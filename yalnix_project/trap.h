/* trap.h */
#ifndef TRAP_H
#define TRAP_H

#include <hardware.h>
#include <yalnix.h>


void init_trap_vector(void);

void HandleTrapKernel(UserContext *uctxt);
void HandleTrapClock(UserContext *uctxt);
void HandleTrapIllegal(UserContext *uctxt);
void HandleTrapMemory(UserContext *uctxt);
void HandleTrapMath(UserContext *uctxt);
void HandleTrapTtyReceive(UserContext *uctxt);
void HandleTrapTtyTransmit(UserContext *uctxt);
void HandleTrapDisk(UserContext *uctxt);


#endif /* TRAP_H */

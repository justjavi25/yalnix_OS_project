/* idle.h */
#ifndef IDLE_H
#define IDLE_H

#include <hardware.h>
#include <yalnix.h>

/*
 * DoIdle - the idle process function.
 *
 * Runs in userland when no other process is ready.
 * Uses Pause() to yield the CPU until the next clock interrupt,
 * to avoid busy-waiting on the host machine.
 *
 * The idle PCB's user_context.pc is set to point to this function
 * before KernelStart returns.
 */
void DoIdle(void);

#endif /* IDLE_H */

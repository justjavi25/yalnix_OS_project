/* idle.c */
#include "idle.h"

/*
 * DoIdle - verbatim from the Yalnix handout.
 *
 * This function lives in kernel text but runs in userland.
 * This is allowed as a shortcut for Checkpoint 2.
 * By Checkpoint 3 we will use LoadProgram to load a real init.
 */
void DoIdle(void)
{
    while (1) {
        TracePrintf(1, "DoIdle\n");
        Pause();
    }
}

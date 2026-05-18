/* init.h */
#ifndef INIT_H
#define INIT_H

#include <hardware.h>
#include <yalnix.h>

/*
 * init.h - declarations for init process setup.
 *
 * The init process is the first real userland process.
 * It is loaded from an executable file in Checkpoint 3.
 *
 * Pseudocode for Checkpoint 3:
 *
 * create_init_process(cmd_args):
 *   1. Allocate PCB via alloc_pcb().
 *   2. Allocate Region 1 page table.
 *   3. Allocate kernel stack frames.
 *   4. Use LoadProgram(cmd_args[0], cmd_args, pcb) to
 *      load the executable into Region 1.
 *   5. Set state = PROC_READY.
 *   6. Push onto ready queue.
 *   7. Clone kernel context via KernelContextSwitch(KCCopy,...).
 *
 * Note from handout:
 *   - Use cmd_args[0] as the filename for init.
 *   - Default to "init" if cmd_args is empty.
 */

void create_init_process(char *cmd_args[]);

#endif /* INIT_H */

/* syscalls.h */
#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <hardware.h>
#include <yalnix.h>

/*
 * Syscall dispatcher.
 *
 * Called from HandleTrapKernel.
 *
 * Pseudocode:
 *   switch (uctxt->code) {
 *     case YALNIX_FORK:     KernelFork(uctxt);    break;
 *     case YALNIX_EXEC:     KernelExec(uctxt);    break;
 *     case YALNIX_EXIT:     KernelExit(uctxt);    break;
 *     case YALNIX_WAIT:     KernelWait(uctxt);    break;
 *     case YALNIX_GETPID:   KernelGetPid(uctxt);  break;
 *     case YALNIX_BRK:      KernelBrk(uctxt);     break;
 *     case YALNIX_DELAY:    KernelDelay(uctxt);   break;
 *     case YALNIX_TTY_READ: KernelTtyRead(uctxt); break;
 *     case YALNIX_TTY_WRITE:KernelTtyWrite(uctxt);break;
 *     default:
 *       TracePrintf unknown syscall.
 *       uctxt->regs[0] = ERROR.
 *   }
 */
void syscall_dispatch(UserContext *uctxt);

/*
 * Fork - create a new child process.
 *
 * Pseudocode:
 *   1. Allocate child PCB via alloc_pcb().
 *   2. Copy parent's Region 1 pages into new frames for child.
 *   3. Clone kernel context via KernelContextSwitch(KCCopy, child, NULL).
 *   4. In parent: uctxt->regs[0] = child->pid.
 *   5. In child:  uctxt->regs[0] = 0.
 *   6. Add child to ready queue.
 */
void KernelFork(UserContext *uctxt);

/*
 * Exec - replace current process image with a new program.
 *
 * Pseudocode:
 *   1. Get filename from uctxt->regs[0] (validate pointer).
 *   2. Get argvec  from uctxt->regs[1] (validate pointer).
 *   3. Free current Region 1 pages.
 *   4. Call LoadProgram(filename, argvec, current_process).
 *   5. If LoadProgram fails: return ERROR.
 *   6. Does not return to old program on success.
 */
void KernelExec(UserContext *uctxt);

/*
 * Exit - terminate the calling process.
 *
 * Pseudocode:
 *   1. status = uctxt->regs[0].
 *   2. If current_process is init (pid == 1): Halt().
 *   3. Reparent children (parent = NULL or init).
 *   4. If parent is alive and waiting: wake parent.
 *   5. Else: become zombie (save status).
 *   6. Free resources (all but PCB shell if zombie).
 *   7. Schedule and switch to next process.
 *   (Never returns.)
 */
void KernelExit(UserContext *uctxt);

/*
 * Wait - collect exit status of a child.
 *
 * Pseudocode:
 *   1. If no children: return ERROR.
 *   2. If zombie child exists:
 *        collect its status.
 *        free its PCB.
 *        return child pid.
 *   3. Else block until a child exits, then collect.
 */
void KernelWait(UserContext *uctxt);

/*
 * GetPid - return the pid of the calling process.
 *
 * Pseudocode:
 *   uctxt->regs[0] = current_process->pid.
 */
void KernelGetPid(UserContext *uctxt);

/*
 * Brk - set the user heap break.
 *
 * Pseudocode:
 *   1. addr = uctxt->regs[0].
 *   2. Validate addr is above data and below stack.
 *   3. Grow or shrink user heap pages accordingly.
 *   4. uctxt->regs[0] = 0 on success, ERROR on failure.
 */
void KernelBrk(UserContext *uctxt);

/*
 * Delay - block process for N clock ticks.
 *
 * Pseudocode:
 *   1. ticks = uctxt->regs[0].
 *   2. If ticks < 0: return ERROR.
 *   3. If ticks == 0: return 0 immediately.
 *   4. Set current_process->delay_ticks = ticks.
 *   5. Move process to delay queue.
 *   6. Context switch to next process.
 *   7. When woken: return 0.
 */
void KernelDelay(UserContext *uctxt);

/*
 * TtyRead - read a line from a terminal.
 *
 * Pseudocode:
 *   1. tty_id = uctxt->regs[0].
 *   2. buf    = uctxt->regs[1] (validate pointer).
 *   3. len    = uctxt->regs[2].
 *   4. If line already buffered for this terminal:
 *        copy min(len, line_len) bytes to user buf.
 *        return bytes copied.
 *   5. Else block process until TRAP_TTY_RECEIVE fires.
 */
void KernelTtyRead(UserContext *uctxt);

/*
 * TtyWrite - write bytes to a terminal.
 *
 * Pseudocode:
 *   1. tty_id = uctxt->regs[0].
 *   2. buf    = uctxt->regs[1] (validate pointer).
 *   3. len    = uctxt->regs[2].
 *   4. Copy data to kernel buffer (Region 0).
 *   5. Block caller.
 *   6. Call TtyTransmit when terminal is free.
 *   7. When TRAP_TTY_TRANSMIT fires: unblock caller.
 */
void KernelTtyWrite(UserContext *uctxt);

#endif /* SYSCALLS_H */

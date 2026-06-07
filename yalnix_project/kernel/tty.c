#include "tty.h"
#include "process.h"
#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>
#include <ylib.h>

typedef struct tty_state {
    int busy;
    pcb_t *writer;
    process_queue_t write_queue;
} tty_state_t;

static tty_state_t ttys[NUM_TERMINALS];

static int UserBufferReadable(void *buf, int len)
{
    unsigned int start;
    unsigned int end;
    int first_page;
    int last_page;

    if (len < 0 || (len > 0 && buf == NULL)) {
        return 0;
    }

    if (len == 0) {
        return 1;
    }

    start = (unsigned int)buf;
    end = start + len - 1;

    if (end < start || start < VMEM_1_BASE || end >= VMEM_1_LIMIT ||
        current_process == NULL || current_process->region1_pt == NULL) {
        return 0;
    }

    first_page = (start - VMEM_1_BASE) >> PAGESHIFT;
    last_page = (end - VMEM_1_BASE) >> PAGESHIFT;

    for (int vpn = first_page; vpn <= last_page; vpn++) {
        if (!current_process->region1_pt[vpn].valid ||
            !(current_process->region1_pt[vpn].prot & PROT_READ)) {
            return 0;
        }
    }

    return 1;
}

static void StartTransmit(pcb_t *proc)
{
    int remaining = proc->tty_write_len - proc->tty_write_offset;
    int chunk = remaining;

    if (chunk > TERMINAL_MAX_LINE) {
        chunk = TERMINAL_MAX_LINE;
    }

    ttys[proc->tty_id].busy = 1;
    ttys[proc->tty_id].writer = proc;
    TtyTransmit(proc->tty_id, proc->tty_write_buf + proc->tty_write_offset, chunk);
}

static void StartNextWriter(int tty_id)
{
    pcb_t *next = DequeueProcess(&ttys[tty_id].write_queue);

    if (next == NULL) {
        ttys[tty_id].busy = 0;
        ttys[tty_id].writer = NULL;
        return;
    }

    StartTransmit(next);
}

void InitTtySystem(void)
{
    for (int tty_id = 0; tty_id < NUM_TERMINALS; tty_id++) {
        ttys[tty_id].busy = 0;
        ttys[tty_id].writer = NULL;
        InitProcessQueue(&ttys[tty_id].write_queue);
    }
}

int KernelTtyWrite(int tty_id, void *buf, int len, int *blocked)
{
    char *kernel_buf;

    if (tty_id < 0 || tty_id >= NUM_TERMINALS || len < 0 || blocked == NULL) {
        return ERROR;
    }

    if (len == 0) {
        return 0;
    }

    if (!UserBufferReadable(buf, len)) {
        return ERROR;
    }

    kernel_buf = (char *)malloc(len);
    if (kernel_buf == NULL) {
        return ERROR;
    }

    memcpy(kernel_buf, buf, len);

    current_process->tty_write_blocked = 1;
    current_process->tty_id = tty_id;
    current_process->tty_write_buf = kernel_buf;
    current_process->tty_write_len = len;
    current_process->tty_write_offset = 0;
    RemoveProcessFromQueue(&ready_queue, current_process);
    *blocked = 1;

    if (!ttys[tty_id].busy) {
        StartTransmit(current_process);
    } else {
        EnqueueProcess(&ttys[tty_id].write_queue, current_process);
    }

    return SUCCESS;
}

void HandleTtyTransmit(int tty_id)
{
    pcb_t *writer;

    if (tty_id < 0 || tty_id >= NUM_TERMINALS) {
        TracePrintf(0, "HandleTtyTransmit: invalid tty %d\n", tty_id);
        return;
    }

    writer = ttys[tty_id].writer;
    if (writer == NULL) {
        ttys[tty_id].busy = 0;
        StartNextWriter(tty_id);
        return;
    }

    writer->tty_write_offset +=
        (writer->tty_write_len - writer->tty_write_offset > TERMINAL_MAX_LINE)
            ? TERMINAL_MAX_LINE
            : (writer->tty_write_len - writer->tty_write_offset);

    if (writer->tty_write_offset < writer->tty_write_len) {
        StartTransmit(writer);
        return;
    }

    writer->user_context.regs[0] = writer->tty_write_len;
    writer->tty_write_blocked = 0;
    free(writer->tty_write_buf);
    writer->tty_write_buf = NULL;
    writer->tty_write_len = 0;
    writer->tty_write_offset = 0;

    EnqueueProcess(&ready_queue, writer);
    ttys[tty_id].busy = 0;
    ttys[tty_id].writer = NULL;
    StartNextWriter(tty_id);
}

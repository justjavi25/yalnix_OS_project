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
    process_queue_t read_queue;
    struct tty_line *line_head;
    struct tty_line *line_tail;
} tty_state_t;

typedef struct tty_line {
    int len;
    int offset;
    char data[TERMINAL_MAX_LINE];
    struct tty_line *next;
} tty_line_t;

static tty_state_t ttys[NUM_TERMINALS];

static int UserBufferValidFor(pcb_t *proc, void *buf, int len, int prot)
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
        proc == NULL || proc->region1_pt == NULL) {
        return 0;
    }

    first_page = (start - VMEM_1_BASE) >> PAGESHIFT;
    last_page = (end - VMEM_1_BASE) >> PAGESHIFT;

    for (int vpn = first_page; vpn <= last_page; vpn++) {
        if (!proc->region1_pt[vpn].valid ||
            ((proc->region1_pt[vpn].prot & prot) != prot)) {
            return 0;
        }
    }

    return 1;
}

static int UserBufferReadable(void *buf, int len)
{
    return UserBufferValidFor(current_process, buf, len, PROT_READ);
}

static int UserBufferWritable(void *buf, int len)
{
    return UserBufferValidFor(current_process, buf, len, PROT_WRITE);
}

static void EnqueueLine(int tty_id, char *buf, int len)
{
    tty_line_t *line;

    if (tty_id < 0 || tty_id >= NUM_TERMINALS || len < 0) {
        return;
    }

    line = (tty_line_t *)malloc(sizeof(tty_line_t));
    if (line == NULL) {
        TracePrintf(0, "EnqueueLine: malloc failed for tty %d\n", tty_id);
        return;
    }

    line->len = len;
    line->offset = 0;
    line->next = NULL;
    if (len > 0) {
        memcpy(line->data, buf, len);
    }

    if (ttys[tty_id].line_tail == NULL) {
        ttys[tty_id].line_head = line;
        ttys[tty_id].line_tail = line;
    } else {
        ttys[tty_id].line_tail->next = line;
        ttys[tty_id].line_tail = line;
    }
}

static tty_line_t *PeekLine(int tty_id)
{
    if (tty_id < 0 || tty_id >= NUM_TERMINALS) {
        return NULL;
    }

    return ttys[tty_id].line_head;
}

static void DropFrontLine(int tty_id)
{
    tty_line_t *line = ttys[tty_id].line_head;

    if (line == NULL) {
        return;
    }

    ttys[tty_id].line_head = line->next;
    if (ttys[tty_id].line_head == NULL) {
        ttys[tty_id].line_tail = NULL;
    }
    free(line);
}

static void CopyToUser(pcb_t *proc, void *dst, char *src, int len)
{
    unsigned int saved_ptbr1 = 0;

    if (len <= 0) {
        return;
    }

    if (current_process != NULL && current_process->region1_pt != NULL) {
        saved_ptbr1 = (unsigned int)current_process->region1_pt;
    }

    WriteRegister(REG_PTBR1, (unsigned int)proc->region1_pt);
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    memcpy(dst, src, len);

    if (saved_ptbr1 != 0) {
        WriteRegister(REG_PTBR1, saved_ptbr1);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    }
}

static int CompleteReadFromBufferedLine(pcb_t *reader)
{
    tty_line_t *line;
    int tty_id;
    int available;
    int count;

    if (reader == NULL) {
        return ERROR;
    }

    tty_id = reader->tty_read_id;
    line = PeekLine(tty_id);
    if (line == NULL) {
        return ERROR;
    }

    available = line->len - line->offset;
    count = reader->tty_read_len;
    if (count > available) {
        count = available;
    }

    if (count > 0) {
        CopyToUser(reader, reader->tty_read_buf, line->data + line->offset, count);
        line->offset += count;
    }

    if (line->offset >= line->len) {
        DropFrontLine(tty_id);
    }

    reader->user_context.regs[0] = count;
    reader->tty_read_blocked = 0;
    reader->tty_read_buf = NULL;
    reader->tty_read_len = 0;
    EnqueueProcess(&ready_queue, reader);
    return SUCCESS;
}

static void ServiceReaders(int tty_id)
{
    pcb_t *reader;

    while (PeekLine(tty_id) != NULL) {
        reader = DequeueProcess(&ttys[tty_id].read_queue);
        if (reader == NULL) {
            return;
        }
        CompleteReadFromBufferedLine(reader);
    }
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
        InitProcessQueue(&ttys[tty_id].read_queue);
        ttys[tty_id].line_head = NULL;
        ttys[tty_id].line_tail = NULL;
    }
}

int KernelTtyRead(int tty_id, void *buf, int len, int *blocked)
{
    tty_line_t *line;
    pcb_t *reader;

    if (tty_id < 0 || tty_id >= NUM_TERMINALS || len < 0 || blocked == NULL) {
        return ERROR;
    }

    if (len == 0) {
        return 0;
    }

    if (!UserBufferWritable(buf, len)) {
        return ERROR;
    }

    line = PeekLine(tty_id);
    if (line != NULL) {
        int available = line->len - line->offset;
        int count = len;
        if (count > available) {
            count = available;
        }
        if (count > 0) {
            memcpy(buf, line->data + line->offset, count);
            line->offset += count;
        }
        if (line->offset >= line->len) {
            DropFrontLine(tty_id);
        }
        return count;
    }

    reader = current_process;
    reader->tty_read_blocked = 1;
    reader->tty_read_id = tty_id;
    reader->tty_read_buf = buf;
    reader->tty_read_len = len;
    RemoveProcessFromQueue(&ready_queue, reader);
    EnqueueProcess(&ttys[tty_id].read_queue, reader);
    *blocked = 1;
    return SUCCESS;
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

void HandleTtyReceive(int tty_id)
{
    char buf[TERMINAL_MAX_LINE];
    int len;

    if (tty_id < 0 || tty_id >= NUM_TERMINALS) {
        TracePrintf(0, "HandleTtyReceive: invalid tty %d\n", tty_id);
        return;
    }

    len = TtyReceive(tty_id, buf, TERMINAL_MAX_LINE);
    if (len < 0) {
        TracePrintf(0, "HandleTtyReceive: TtyReceive failed tty %d rc %d\n",
                    tty_id, len);
        return;
    }

    EnqueueLine(tty_id, buf, len);
    ServiceReaders(tty_id);
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

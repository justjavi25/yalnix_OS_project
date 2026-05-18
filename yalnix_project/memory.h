/* memory.h */
#ifndef MEMORY_H
#define MEMORY_H

#include <hardware.h>
#include <yalnix.h>

/*
 * Physical frame tracking.
 *
 * Design pseudocode:
 *
 * We will use a bitmap (or array of ints) to track which
 * physical frames are free vs. used.
 *
 * num_frames = pmem_size / PAGESIZE
 *
 * frame_table[i] = 0 means frame i is FREE.
 * frame_table[i] = 1 means frame i is USED.
 *
 * At boot:
 *   - Mark all frames below kernel_brk_page as USED.
 *   - Mark all frames above as FREE.
 */

/*
 * Initialize the frame tracking table.
 * Must be called before VM is enabled.
 *
 * Pseudocode:
 *   - Allocate frame_table of size (pmem_size / PAGESIZE).
 *   - Mark kernel text, data, heap frames as used.
 *   - Mark remaining frames as free.
 */
void init_frame_table(unsigned int pmem_size);

/*
 * Allocate one free physical frame.
 *
 * Pseudocode:
 *   - Scan frame_table for a free frame.
 *   - Mark it used.
 *   - Return frame number.
 *   - Return ERROR if none available.
 */
int allocate_frame(void);

/*
 * Free a physical frame.
 *
 * Pseudocode:
 *   - Validate frame number.
 *   - Mark frame free in frame_table.
 */
void free_frame(int frame_num);

/*
 * Build the initial Region 0 page table at boot.
 *
 * Pseudocode:
 *   - Allocate page table of MAX_PT_LEN entries.
 *   - Map kernel text pages    -> PROT_READ | PROT_EXEC
 *   - Map kernel data/bss/heap -> PROT_READ | PROT_WRITE
 *   - Map kernel stack pages   -> PROT_READ | PROT_WRITE
 *   - Leave all other entries invalid.
 *   - Write to REG_PTBR0 and REG_PTLR0.
 */
void build_region0_page_table(void);

/*
 * Allocate and return a new empty Region 1 page table.
 *
 * Pseudocode:
 *   - Malloc table of MAX_PT_LEN entries.
 *   - Set all entries invalid (valid bit = 0).
 *   - Return pointer.
 */
struct pte *create_region1_pt(void);

/*
 * Free a Region 1 page table and all its mapped frames.
 *
 * Pseudocode:
 *   - For each valid entry:
 *       free_frame(entry.pfn).
 *   - Free the table itself.
 */
void destroy_region1_pt(struct pte *pt);

#endif /* MEMORY_H */

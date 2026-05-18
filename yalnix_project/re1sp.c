/*
 * re1sp.c - Region 1 (user address space) setup helpers.
 *
 * Pseudocode:
 *
 * Region 1 spans virtual addresses VMEM_1_BASE to VMEM_1_LIMIT.
 * Each process has its own Region 1 page table.
 *
 * Layout from LoadProgram (Checkpoint 3):
 *
 *   PT[MAX_PT_LEN - 1]
 *   ...
 *   PT[MAX_PT_LEN - stack_npg]     <- user stack (grows down)
 *   ...                            <- red zone (unmapped)
 *   PT[data_pg1 + data_npg - 1]   <- user data/heap (grows up)
 *   ...
 *   PT[data_pg1]
 *   ...
 *   PT[text_pg1 + t_npg - 1]      <- user text
 *   ...
 *   PT[text_pg1]
 *
 * For idle (Checkpoint 2):
 *   - Only need one valid stack page at the top of Region 1.
 *   - All other entries invalid.
 *
 * For init and user processes (Checkpoint 3+):
 *   - LoadProgram populates this based on the ELF executable.
 *
 * Switching Region 1 on context switch:
 *   - WriteRegister(REG_PTBR1, next->region1_pt);
 *   - WriteRegister(REG_PTLR1, MAX_PT_LEN);
 *   - WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
 */

#include <hardware.h>
#include <yalnix.h>

void switch_region1_pt(struct pte *new_pt)
{
    /*
     * TODO Checkpoint 2 / 3:
     * Implement Region 1 page table switch.
     * See pseudocode above.
     */

    (void) new_pt;

    TracePrintf(1, "switch_region1_pt: stub\n");
}

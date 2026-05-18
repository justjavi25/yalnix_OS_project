/*
 * re0sp.c - Region 0 (kernel address space) setup.
 *
 * Pseudocode for Checkpoint 2:
 *
 * Region 0 spans virtual addresses 0 to VMEM_1_BASE.
 * It contains:
 *   - Kernel text   (PROT_READ | PROT_EXEC)
 *   - Kernel data   (PROT_READ | PROT_WRITE)
 *   - Kernel heap   (PROT_READ | PROT_WRITE, grows up)
 *   - Kernel stack  (PROT_READ | PROT_WRITE, fixed at top of Region 0)
 *
 * Building Region 0 page table:
 *   1. Allocate array of MAX_PT_LEN pte_t entries.
 *   2. For page i in [first_kernel_text_page, first_kernel_data_page):
 *        pt[i].valid = 1;
 *        pt[i].pfn   = i;  (identity mapping)
 *        pt[i].prot  = PROT_READ | PROT_EXEC;
 *   3. For page i in [first_kernel_data_page, orig_kernel_brk_page):
 *        pt[i].valid = 1;
 *        pt[i].pfn   = i;
 *        pt[i].prot  = PROT_READ | PROT_WRITE;
 *   4. For kernel stack pages (top of Region 0):
 *        pt[i].valid = 1;
 *        pt[i].pfn   = i;
 *        pt[i].prot  = PROT_READ | PROT_WRITE;
 *   5. Write to hardware:
 *        WriteRegister(REG_PTBR0, (unsigned int) pt);
 *        WriteRegister(REG_PTLR0, MAX_PT_LEN);
 *
 * Note:
 *   first_kernel_text_page, first_kernel_data_page, and
 *   orig_kernel_brk_page are provided by the build system
 *   via yalnix.h as extern ints.
 */

#include <hardware.h>
#include <yalnix.h>

void build_region0_pt(void)
{
    /*
     * TODO Checkpoint 2:
     * Implement Region 0 page table setup.
     * See pseudocode above.
     */

    TracePrintf(1, "build_region0_pt: stub\n");
}

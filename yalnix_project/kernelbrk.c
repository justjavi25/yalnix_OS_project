#include <hardware.h>
#include <yalnix.h>

/*
 * SetKernelBrk - Adjust the kernel heap boundary.
 *
 * Called by the kernel malloc library (not by user code).
 *
 * Argument:
 *   addr - the new lowest address NOT used by the kernel heap.
 *
 * Pseudocode:
 *
 * Case 1: Virtual memory NOT yet enabled.
 *   - Track whether the kernel break has grown beyond
 *     orig_kernel_brk_page * PAGESIZE.
 *   - Do not manipulate page tables yet.
 *   - Return 0.
 *
 * Case 2: Virtual memory IS enabled.
 *   - Compute new_brk_page  = addr / PAGESIZE (round up).
 *   - Compute old_brk_page  = current kernel break / PAGESIZE.
 *
 *   If growing (new_brk_page > old_brk_page):
 *     For each new page from old_brk_page to new_brk_page:
 *       - Allocate a free physical frame.
 *       - Map it into Region 0 page table with PROT_READ | PROT_WRITE.
 *     Flush TLB Region 0.
 *
 *   If shrinking (new_brk_page < old_brk_page):
 *     For each page from new_brk_page to old_brk_page:
 *       - Unmap it from Region 0 page table.
 *       - Free its physical frame.
 *     Flush TLB Region 0.
 *
 *   - Update kernel break pointer.
 *   - Return 0 on success, ERROR on failure.
 *
 * Return:
 *   0 on success.
 *   ERROR if out of physical memory or invalid address.
 */
int SetKernelBrk(void *addr)
{
    (void) addr;

    TracePrintf(1, "SetKernelBrk: stub called\n");

    /*
     * TODO Checkpoint 2:
     * Implement the pseudocode above.
     */

    return 0;
}

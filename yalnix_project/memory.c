#include <hardware.h>
#include <yalnix.h>
#include <ykernel.h>
#include <ylib.h>
#include "memory.h"

//maximum number of physical frames the Yalnix hardware can provide.
#define MAX_PHYSICAL_FRAMES (MAX_PMEM_SIZE / PAGESIZE)
#define FRAME_FREE 0
#define FRAME_USED 1
//keep track of whether vm has been enabled yet.
int vm_enabled = 0;

static int total_frames = 0;
//track physical frame ownership with one byte per possible frame.
static unsigned char frame_used[MAX_PHYSICAL_FRAMES];
//remember the Region 0 pt
static pte_t *region0_page_table = NULL;
//first page after the currently mapped kernel heap.
static int kernel_brk_page = 0;
//track the first page after the largest pre-VM kernel heap request.
static int requested_kernel_brk_page = 0;

//mark one physical frame as used if it is a real frame.
static void MarkFrameUsed(int pfn)
{
    //ignore invalid physical frame numbers
    if (pfn < 0 || pfn >= total_frames) {
        //Return when frame number is outside physical memory
        return;
    }
    // frame is no longer available.
    frame_used[pfn] = FRAME_USED;
}

// mark one physical frame as free if it is a real frame.
static void MarkFrameFree(int pfn)
{
    // ignore invalid physical frame numbers
    if (pfn < 0 || pfn >= total_frames) {
        return;
    }

    // record that this physical frame can be reused.
    frame_used[pfn] = FRAME_FREE;
}

//return nonzero when the physical frame is valid and free.
static int IsFrameFree(int pfn)
{
    if (pfn < 0 || pfn >= total_frames) {
        return 0;
    }
    //Return true when the bitmap says this frame is free
    return frame_used[pfn] == FRAME_FREE;
}

//convert a virtual address to the corresponding virtual page number.
static int AddrToVpn(void *addr)
{
    return UP_TO_PAGE(addr) >> PAGESHIFT; //Round the address up
}

//clear a page table entry so the virtual page is unmapped
static void ClearPte(pte_t *pte)
{
    //mark the virtual page invalid.
    pte->valid = 0;
    //Remove all access permissions from the virtual page.
    pte->prot = PROT_NONE;
    pte->pfn = 0; // Clear the physical frame number
}

//purpose: Initialize our free-fram tracking given the total physical memory size in bytes
void InitPhysicalMemory(unsigned int pmem_size)
{
    //Convert the physical memory byte count into page frame count
    total_frames = pmem_size / PAGESIZE;
    if (total_frames > MAX_PHYSICAL_FRAMES) {
        //Keep the bitmap from being indexed beyond its size
        total_frames = MAX_PHYSICAL_FRAMES;
    }
    //clear the entire frame bitmap
    memset(frame_used, FRAME_FREE, sizeof(frame_used));
    //start the kernel break at the original break page
    kernel_brk_page = _orig_kernel_brk_page;
    //start requested break there too, until SetKernelBrk asks for more.
    requested_kernel_brk_page = _orig_kernel_brk_page;

    //mark every frame below the original kbreak as already used
    for (int pfn = 0; pfn < _orig_kernel_brk_page && pfn < total_frames; pfn++) {
        MarkFrameUsed(pfn);
    }

    // compute first virtual page used by the fixed kernel stack
    int kstack_first_page = KERNEL_STACK_BASE >> PAGESHIFT;
    // compute the page just after the fixed kernel stack
    int kstack_limit_page = KERNEL_STACK_LIMIT >> PAGESHIFT;

    // Mark the physical frames used by the boot kernel stack as unavailable
    for (int pfn = kstack_first_page; pfn < kstack_limit_page; pfn++) {
        //VPN equals PFN here.
        MarkFrameUsed(pfn);
    }

    //checkpoint debugging
    TracePrintf(1, "InitPhysicalMemory: %d frames available\n", total_frames);
}

//Return one unused physical frame number, used whenever we need a new physical page
int AllocFrame(void)
{
    //Search the whole physical frame bitmap for a free frame
    for (int pfn = 0; pfn < total_frames; pfn++) {
        // Use the first frame that is currently marked free
        if (IsFrameFree(pfn)) {
            // Mark the frame used before returning it to the caller
            MarkFrameUsed(pfn);
            //Return the physical frame number
            return pfn;
        }
    }
    // Report allocation failure for when no physical frame is free
    return ERROR;
}

//Purpose: Mark a physical frame as reusable
void FreeFrame(int pfn)
{
    // Ignore invalid physical frame numbers.
    if (pfn < 0 || pfn >= total_frames) {
        return;
    }
    //mark the frame free
    MarkFrameFree(pfn);
}

// This builds the kernel half of virtual memory (region 0) and creates a page table mapping
// virtual region 0 pages to physical frames.
//
// At boot, before VM is enabled, physical and virtual addresses are effectively the same.
// So virtual page N -> physical frame N
pte_t *BuildRegion0PageTable(void){
    //allocate the Region 0 page table from the kernel heap.
    pte_t *pt = (pte_t *)malloc(sizeof(pte_t) * MAX_PT_LEN);

    if (pt == NULL) {
        // Stop the kernel
        helper_abort("BuildRegion0PageTable: malloc failed");
    }

    //clear every Region 0 mapping before adding the valid kernel mappings
    for (int vpn = 0; vpn < MAX_PT_LEN; vpn++) {
        // Leave unmapped pages invalid by default
        ClearPte(&pt[vpn]);
    }

    //kernel text pages read/execute
    for (int vpn = _first_kernel_text_page; vpn < _first_kernel_data_page; vpn++) {
        //text should be readable and executable and not writable.
        MapPage(pt, vpn, vpn, PROT_READ | PROT_EXEC);
    }

    // kernel data and original heap pages read/write
    for (int vpn = _first_kernel_data_page; vpn < kernel_brk_page; vpn++) {
        // Data and heap pages must be writable by the kernel.
        MapPage(pt, vpn, vpn, PROT_READ | PROT_WRITE);
    }

    //computing the first virtual page used by the fixed kernel stack.
    int kstack_first_page = KERNEL_STACK_BASE >> PAGESHIFT;

    // page just after the fixed kernel stack.
    int kstack_limit_page = KERNEL_STACK_LIMIT >> PAGESHIFT;

    //map the boot kernel stack read/write 
    for (int vpn = kstack_first_page; vpn < kstack_limit_page; vpn++) {
        // Kernel stack pages must be writable
        MapPage(pt, vpn, vpn, PROT_READ | PROT_WRITE);
    }
    //Save the Region 0 page table for SetKernelBrk after VM is enabled
    region0_page_table = pt;

    // Return the finished Region 0 page table to KernelStart
    return pt;
}

// Purpose: allocate and initialize a region 1 page table
// Checkpoint 2: Idle's region 1 only needs a user stack page
pte_t *CreateRegion1PageTable(void){
    // Allocate the Region 1 page table from the kernel heap.
    pte_t *pt = (pte_t *)malloc(sizeof(pte_t) * MAX_PT_LEN);

    if (pt == NULL) {
        return NULL;
    }

    // Clear every Region 1 mapping before the caller maps user pages
    for (int vpn = 0; vpn < MAX_PT_LEN; vpn++) {
        // Invalid entries make accidental Region 1 accesses trap cleanly.
        ClearPte(&pt[vpn]);
    }

    // Return the empty Region 1 page table
    return pt;
}

// fill one page table entry
int MapPage(pte_t *pt, int vpn, int pfn, int prot){
    if (pt == NULL) {
        return ERROR;
    }

    // reject invalid virtual page numbers
    if (vpn < 0 || vpn >= MAX_PT_LEN) {
        // Mapping outside the page table would corrupt memory
        return ERROR;
    }

    // Reject invalid physical frame numbers.
    if (pfn < 0 || pfn >= total_frames) {
        // Mapping to nonexistent physical memory would crash later.
        return ERROR;
    }

    //Mark this virtual page as mapped
    pt[vpn].valid = 1;
    // place page protection bits used by the hardware
    pt[vpn].prot = prot;
    //store the physical frame number backing this virtual page
    pt[vpn].pfn = pfn;

    // Report success
    return SUCCESS;
}

int SetKernelBrk(void *addr)
{
    // Convert the requested break address to the first page after the heap
    int new_page = AddrToVpn(addr);

    // Refuse a break below the original kernel heap boundary.
    if (new_page < _orig_kernel_brk_page) {
        return ERROR;
    }

    //don't allow a break that would run into or past the kernel stack area.
    if (new_page >= (KERNEL_STACK_BASE >> PAGESHIFT)) {
        return ERROR;
    }

    // Before VM is enabled
    if (!vm_enabled) {
        //save the pre-VM break page so KernelStart can map it
        requested_kernel_brk_page = new_page;
        // Return success
        return SUCCESS;
    }

    // Refuse post-VM kernel heap changes if Region 0 was not initialized
    if (region0_page_table == NULL) {
        //Without a Region 0 page table, there is nowhere to record mappings
        return ERROR;
    }

    //grow the kernel heap by mapping every newly requested page
    if (new_page > kernel_brk_page) {
        // Walk each virtual page between the old break and the new break
        for (int vpn = kernel_brk_page; vpn < new_page; vpn++) {
            //allocate one physical frame for this kernel heap page
            int pfn = AllocFrame();
            //stop and fail if physical memory is exhausted.
            if (pfn == ERROR) {
                // Leave already-mapped pages in place; caller sees allocation failure.
                return ERROR;
            }
            //map new heap page read/write in Region 0.
            if (MapPage(region0_page_table, vpn, pfn, PROT_READ | PROT_WRITE) == ERROR) {
                // Give the frame back if the page table update failed
                FreeFrame(pfn);
                // Report the mapping failure
                return ERROR;
            }
        }
    }
    //Shrink the kernel heap by unmapping pages above the new break
    if (new_page < kernel_brk_page) {
        // Walk backward over pages that are no longer part of the heap
        for (int vpn = new_page; vpn < kernel_brk_page; vpn++) {
            //Free the old physical frame only if the page was mapped
            if (region0_page_table[vpn].valid) {
                // Return the backing physical frame to the free pool
                FreeFrame(region0_page_table[vpn].pfn);
            }
            //clear the page-table entry so the page becomes invalid.
            ClearPte(&region0_page_table[vpn]);
        }
    }

    //flush Region 0 translations after changing kernel heap mappings
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    // Record the new heap boundary.
    kernel_brk_page = new_page;
    // requested break boundary is now current page
    requested_kernel_brk_page = new_page;
    //Report success to the kernel heap library
    return SUCCESS;
}

void SyncKernelBrkBeforeVM(pte_t *region0_pt)
{
    //remember the Region 0 page table so SetKernelBrk can use it later.
    region0_page_table = region0_pt;

    //Do nothing if the page table was not created successfully.
    if (region0_pt == NULL) {
        return;
    }

    // Map every kernel heap page requested by pre-VM malloc calls.
    for (int vpn = kernel_brk_page; vpn < requested_kernel_brk_page; vpn++) {
        //Mark the identity-mapped physical frame as used by the kernel heap
        MarkFrameUsed(vpn);
        //add a read/write identity mapping for the kernel heap page.
        MapPage(region0_pt, vpn, vpn, PROT_READ | PROT_WRITE);
    }
    // The mapped kernel break now catches up to the requested pre-VM break
    kernel_brk_page = requested_kernel_brk_page;
}

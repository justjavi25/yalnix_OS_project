#ifndef MEMORY_H
#define MEMORY_H

#include <hardware.h>

//tracks whether virtual memory has been enabled yet.
extern int vm_enabled;

//initializes physical frame bookkeeping from the hardware memory size.
void InitPhysicalMemory(unsigned int pmem_size);

//allocates one free physical frame and returns its pfn.
int AllocFrame(void);

//marks a physical frame as reusable.
void FreeFrame(int pfn);

//builds the initial Region 0 page table for the kernel.
pte_t *BuildRegion0PageTable(void);

//creates an empty Region 1 page table.
pte_t *CreateRegion1PageTable(void);

//maps one virtual page number to one physical frame number.
int MapPage(pte_t *pt, int vpn, int pfn, int prot);

//maps any kernel heap growth that happened before VM was enabled.
void SyncKernelBrkBeforeVM(pte_t *region0_pt);

#endif

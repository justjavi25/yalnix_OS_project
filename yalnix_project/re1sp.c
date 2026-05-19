/*
 * re1sp.c - Region 1 (user address space) setup helpers.
 *
 * Pseudocode:
 *
 * Region 1 spans virtual addresses VMEM_1_BASE to VMEM_1_LIMIT.
 * Each process has its own Region 1 page table.
 */

#include <hardware.h>
#include <yalnix.h>

void switch_region1_pt(struct pte *new_pt)
{
   //given a command to create a new pagetable we must  
   //take a chunk of memory and divide it according to the OS specs of one region 1 context

}

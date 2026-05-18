/* memory.c */
#include "memory.h"

/*
 * TODO Checkpoint 2:
 * Implement all functions declared in memory.h.
 * See pseudocode in each function comment above.
 */

void init_frame_table(unsigned int pmem_size)
{
    (void) pmem_size;
    TracePrintf(1, "init_frame_table: stub\n");
}

int allocate_frame(void)
{
    TracePrintf(1, "allocate_frame: stub\n");
    return 0;
}

void free_frame(int frame_num)
{
    (void) frame_num;
    TracePrintf(1, "free_frame: stub\n");
}

void build_region0_page_table(void)
{
    TracePrintf(1, "build_region0_page_table: stub\n");
}

struct pte *create_region1_pt(void)
{
    TracePrintf(1, "create_region1_pt: stub\n");
    return NULL;
}

void destroy_region1_pt(struct pte *pt)
{
    (void) pt;
    TracePrintf(1, "destroy_region1_pt: stub\n");
}

/* memory.c */
#include "memory.h"

void init_frame_table(unsigned int pmem_size)
{
  //helper function used to initialize a frame table
  // this is used to track what frames are free and which are used up 
  // used in boot and it should be 
  //  bit vector is made to keep track of these
}

int allocate_frame(void)
{
    //first we check if the frame is able to be allocated in bit vector
    //should call a memory trap?
    //if memory trap returned ok we return non error and change bit vector to reflect taht
    //else error
}

void free_frame(int frame_num)
{
  //we check if frame is freeable to beign with
  //if it is memory_trap
  //if memory trap return ok we change bit vector to reflect this

}

void build_region0_page_table(void)
{
  //setup region 0
}

struct pte *create_region1_pt(void)
{
    
}

void destroy_region1_pt(struct pte *pt)
{
    (void) pt;
    TracePrintf(1, "destroy_region1_pt: stub\n");
}

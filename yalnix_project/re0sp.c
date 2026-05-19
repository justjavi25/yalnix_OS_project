

/*
 * re0sp.c - Region 0 (kernel address space) setup.
 *
 */



#include <hardware.h>
#include <yalnix.h>

void build_region0_pt(void)
{
  //create region 0
  //instantiated as an array up to max number of pages/2
  //we divide the regions of the array into the heap and stack of region 0 according to the spec
}

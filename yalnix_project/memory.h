/* memory.h */
#ifndef MEMORY_H
#define MEMORY_H

#include <hardware.h>
#include <yalnix.h>


void init_frame_table(unsigned int pmem_size);


int allocate_frame(void);


void free_frame(int frame_num);


void build_region0_page_table(void);


struct pte *create_region1_pt(void);

void destroy_region1_pt(struct pte *pt);



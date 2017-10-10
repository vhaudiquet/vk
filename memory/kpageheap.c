#include "system.h"
#include "mem.h"
#include "error/error.h"

#define KPHEAP_BLOCK_SIZE 4096
#define KPHEAP_BLOCK_FREE 0
#define KPHEAP_BLOCK_USED 1

#define KPHEAP_VIRT_BASE 0xC0400000

u8 kpheap_blocks[1024] = {0};
u32 kpheap_page_table[1024] __attribute__((aligned(4096)));

void install_page_heap()
{
    kprintf("[MEM] Installing page heap...");

    u32 KPHEAP_PHYS_BASE = reserve_specific(0x400000, 0x400000, 0xA);
    u32 pd_index = KPHEAP_VIRT_BASE >> 22;
    unsigned int i;
    for(i = 0; i<1024; i++)
    {
        kpheap_page_table[i] = (i*0x1000 + KPHEAP_PHYS_BASE) | 3;
    }

    kernel_page_directory[pd_index] = (((u32) kpheap_page_table) - KERNEL_VIRTUAL_BASE) | 3;

    vga_text_okmsg();
}

u32* pt_alloc()
{
    unsigned int i;
    for(i=0;i<1024;i++)
    {
        if(kpheap_blocks[i] == KPHEAP_BLOCK_FREE)
	    {
		    kpheap_blocks[i] = KPHEAP_BLOCK_USED;
		    return ((u32*) (KPHEAP_VIRT_BASE+KPHEAP_BLOCK_SIZE*i));
	    }
    }
    fatal_kernel_error("Page heap is full. How did you do this ?", "PT_ALLOC");
    return 0;
}

void pt_free(u32* pt)
{
    u32 index = (((u32) pt) - KPHEAP_VIRT_BASE)/KPHEAP_BLOCK_SIZE;
    kpheap_blocks[index] = KPHEAP_BLOCK_FREE;
}

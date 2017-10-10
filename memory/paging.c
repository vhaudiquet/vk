#include "../system.h"
#include "error/error.h"
#include "mem.h"
#include "cpu/cpu.h"

/*
* This file provides function to map physical memory at virtual adresses
*/

//PAGE DIRECTORY : Must be 4 KiB aligned (0x1000)
u32 kernel_page_directory[1024] __attribute__((aligned(4096))) = {0};
u32 kernel_page_table[1024] __attribute__((aligned(4096)));

u32* current_page_directory = kernel_page_directory;

static void map_page(u32 phys_addr, u32 virt_addr, u32* page_directory);
static void map_page_table(u32 phys_addr, u32 virt_addr, u32* page_directory);

void finish_paging()
{
    kprintf("[MEM] Finishing paging...");

    if(cpu_pse)
        asm("mov %cr4, %eax \n \
            or $0x10, %eax \n \
            mov %eax, %cr4 \n");

    vga_text_okmsg();
}

void pd_switch(u32* pd)
{
    if(current_page_directory != pd)
    {
        asm("mov %0, %%cr3"::"r"((u32) pd-KERNEL_VIRTUAL_BASE));
        current_page_directory = pd;
    }
    //kprintf("%lpd switched.\n", 1);
}

u32* get_kernel_pd_clone()
{
    u32* tr = pt_alloc();
    u32 i = 0;
    for(i = 0; i < 1024; i++)
    {
        tr[i] = kernel_page_directory[i];
    }
    return tr;
}

static void map_page(u32 phys_addr, u32 virt_addr, u32* page_directory)
{
    if(phys_addr % 4096) fatal_kernel_error("Trying to map a non-aligned physical address", "MAP_PAGE");
    if(virt_addr % 4096) fatal_kernel_error("Trying to map physical to non-aligned virtual address", "MAP_PAGE");

    bool kernel = false;
    if(page_directory == kernel_page_directory) kernel = true;

    u32 pd_index = virt_addr >> 22;
    u32 pt_index = virt_addr >> 12 & 0x03FF;

    //kprintf("mapping 0x%X to 0x%X (pd_i = %d, pt_i = %d)\n", phys_addr, virt_addr, pd_index, pt_index);

    u32* page_table = (u32*) page_directory[pd_index];
    if(!page_table)
    {
        page_table = (u32*) (((u32) pt_alloc()) - KERNEL_VIRTUAL_BASE);
        memset(((u32*) (((u32)page_table)+KERNEL_VIRTUAL_BASE)), 0, 4096);
        if(kernel) page_directory[pd_index] = ((u32) page_table) | 3;
        else page_directory[pd_index] = ((u32) page_table) | 7;
    }
    else
    {
        //checking that page table is used as a page table and not as a 4MiB page
        if((((u32)page_table) << 24 >> 31) == 1) fatal_kernel_error("Trying to map physical to an already mapped page table", "MAP_PAGE");
        page_table = (u32*) (((u32)page_table) >> 12 << 12);
    }

    u32* page = (u32*) (((u32) page_table) + pt_index*4 + KERNEL_VIRTUAL_BASE);
    //if(*page) kprintf("page=%x (virt=0x%X)\n", *page, virt_addr);
    if(*page) fatal_kernel_error("Trying to map physical to an already mapped virtual address", "MAP_PAGE");

    if(kernel) *page = (phys_addr) | 3;
    else *page = (phys_addr) | 7;

    //flush, update or do something with the cache
}

static void map_page_table(u32 phys_addr, u32 virt_addr, u32* page_directory)
{
    if(phys_addr % 0x400000) fatal_kernel_error("Trying to map a non-aligned physical address", "MAP_PAGE_TABLE");
    if(virt_addr % 0x400000) fatal_kernel_error("Trying to map physical to non-aligned virtual address", "MAP_PAGE_TABLE");

    bool kernel = false;
    if(page_directory == kernel_page_directory) kernel = true;

    u32 pd_index = virt_addr >> 22;
    //u32 pt_index = virt_addr >> 12 & 0x03FF;
    //if(pt_index != 0) fatal_kernel_error("Virtual address is not representing a page table", "MAP_PAGE_TABLE");
    //fixed with align to 0x400000

    u32* page_table = (u32*) page_directory[pd_index];
    if(page_table) fatal_kernel_error("Trying to map a page table to an already mapped page table", "MAP_PAGE_TABLE");

    if(cpu_pse)
    {
        if(kernel) page_directory[pd_index] = phys_addr | 131;
        else page_directory[pd_index] = phys_addr | 135;
    }
    else
    {
        page_table = (u32*) (((u32) pt_alloc()) - KERNEL_VIRTUAL_BASE);
        if(kernel) page_directory[pd_index] = ((u32) page_table) | 3;
        else page_directory[pd_index] = ((u32) page_table) | 7;

        unsigned int i;
        for(i = 0;i<1024;i++)
        {
            page_table[i] = (phys_addr) | (kernel ? 3 : 7);
            phys_addr+=0x1000;
        }
    }

    //flush, update, or do something with the cache

}

//if we need to access some defined point of physical memory (like the ACPI table)
//void* map_physical(u32 phys_addr, u32 size);
//vois unmap_physical(void* pointer, u32 size);
/*void map_physical(p_block_t* tm, u32 virt_addr)
{
    u32 ca = tm->base_addr;
    aligndown(ca, 0x1000);
    u32 cs = tm->size;
    while(cs > 0)
    {
        if(cs > 0x400000 && ca % 0x400000 == 0 && virt_addr % 0x400000 == 0)
        {
            map_page_table(ca, virt_addr, true);
            ca+=0x400000;
            virt_addr+=0x400000;
            cs-=0x400000;
        }
        else
        {
            map_page(ca, virt_addr, true);
            ca+=0x1000;
            virt_addr+=0x1000;
            if(cs >= 0x1000) cs-=0x1000;
            else cs = 0;
        }
    }
}
*/

//if we need some memory (to do task loading as example, or setuping a stack, or ...)
void map_memory(u32 size, u32 virt_addr, u32* page_directory)
{
    u32 bvaddr = virt_addr;
    aligndown(virt_addr, 4096);
    size += (bvaddr-virt_addr);
    alignup(size, 4096);

    u32 add = 0;

    u32 phys_addr = reserve_block(size, page_directory == kernel_page_directory ? PHYS_KERNELF_BLOCK_TYPE : PHYS_USER_BLOCK_TYPE);

    if((!(virt_addr % 0x400000)) && size > 0x400000)
    {
        while(size > 0x400000)
        {
            map_page_table(phys_addr+add, virt_addr+add, page_directory);
            size -= 0x400000;
            add += 0x400000;
        }
    }
    
    while(size)
    {
        map_page(phys_addr+add, virt_addr+add, page_directory);
        size -= 4096;
        add += 4096;
    }
}

u32 get_physical(u32 virt_addr, u32* page_directory)
{
    u32 pd_index = virt_addr >> 22;
    u32 pt_index = virt_addr >> 12 & 0x03FF;
    u32* page_table = (u32*) page_directory[pd_index];
    if(!page_table) return 0;
    if((((u32)page_table) << 24 >> 31) == 1) return (((u32) page_table) >> 12 << 12);
    page_table = (u32*) (((u32)page_table) >> 12 << 12);
    u32* page = (u32*) (((u32) page_table) + pt_index*4 + KERNEL_VIRTUAL_BASE);
    if(*page) return (((u32)page) >> 12 << 12);
    else return 0;
}

bool is_mapped(u32 virt_addr, u32* page_directory)
{
    u32 pd_index = virt_addr >> 22;
    u32 pt_index = virt_addr >> 12 & 0x03FF;
    u32* page_table = (u32*) page_directory[pd_index];
    if(!page_table) return false;
    if((((u32)page_table) << 24 >> 31) == 1) return true;
    page_table = (u32*) (((u32)page_table) >> 12 << 12);
    u32* page = (u32*) (((u32) page_table) + pt_index*4 + KERNEL_VIRTUAL_BASE);
    if(*page) return true;
    else return false;
}
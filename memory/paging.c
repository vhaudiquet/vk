/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../system.h"
#include "error/error.h"
#include "mem.h"
#include "cpu/cpu.h"

/*
* This file provides function to map physical memory at virtual adresses
*/

#define PD_BIT_4KB_PAGE 0x80
#define PD_ADDRESS_MASK 0xFFFFF000

//PAGE DIRECTORY : Must be 4 KiB aligned (0x1000)
u32 kernel_page_directory[1024] __attribute__((aligned(4096))) = {0};
u32 kernel_page_table[1024] __attribute__((aligned(4096)));

u32* current_page_directory = kernel_page_directory;

static void map_page(u32 phys_addr, u32 virt_addr, u32* page_directory);
static void map_page_table(u32 phys_addr, u32 virt_addr, u32* page_directory);

void finish_paging()
{
    if(cpu_pse)
        asm("mov %cr4, %eax \n \
            or $0x10, %eax \n \
            mov %eax, %cr4 \n");
}

void pd_switch(u32* pd)
{
    if(current_page_directory != pd)
    {
        asm("mov %0, %%cr3"::"r"((u32) pd-KERNEL_VIRTUAL_BASE));
        current_page_directory = pd;
    }
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

u32* copy_adress_space(u32* page_directory)
{
    u32* cpd = current_page_directory;

    u32* tr = get_kernel_pd_clone();
    u32 i = 0;
    for(i = 0; i < 768; i++)
    {
        if(!page_directory[i]) continue;
        u32 pt_addr = page_directory[i] & PD_ADDRESS_MASK;
        if(page_directory[i] & PD_BIT_4KB_PAGE)
        {
            map_memory(0x400000, i << 22, tr);
            
            void* kbuffer = kmalloc(0x400000);
            
            pd_switch(page_directory);
            memcpy(kbuffer, (void*) (i << 22), 0x400000);
            pd_switch(tr);
            memcpy((void*) (i << 22), kbuffer, 0x400000);
            pd_switch(cpd);
        }
        else
        {
            u32* pt = (u32*) pt_addr;
            u32 j;
            for(j = 0;j < 1024;j++)
            {
                if(!pt[j]) continue;
                
                map_memory(4096, (i << 22)+(j << 22), tr);
                
                void* kbuffer = kmalloc(4096);

                pd_switch(page_directory);
                memcpy(kbuffer, (void*) ((i << 22)+(j << 22)), 4096);
                pd_switch(tr);
                memcpy((void*) ((i << 22)+(j << 22)), kbuffer, 4096);
                pd_switch(cpd);
            }
        }
    }

    pd_switch(cpd);
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

static void unmap_page(u32 virt_addr, u32* page_directory)
{
    if(virt_addr % 4096) fatal_kernel_error("Trying to unmap a non-aligned virtual address", "UNMAP_PAGE");

    u32 pd_index = virt_addr >> 22;
    u32 pt_index = virt_addr >> 12 & 0x03FF;
    
    u32* page_table = (u32*) page_directory[pd_index];
    if(!page_table)
        return;
    else
    {
        //checking that page table is used as a page table and not as a 4MiB page
        if((((u32)page_table) << 24 >> 31) == 1) fatal_kernel_error("Trying to unmap page, but page table is mapped", "UNMAP_PAGE");
        page_table = (u32*) (((u32)page_table) >> 12 << 12);
    }

    u32* page = (u32*) (((u32) page_table) + pt_index*4 + KERNEL_VIRTUAL_BASE);
    if(!(*page)) fatal_kernel_error("Trying to unmap a non-mapped virtual address", "UNMAP_PAGE");

    *page = 0;

    //flush, update or do something with the cache
}

static void unmap_page_table(u32 virt_addr, u32* page_directory)
{
    if(virt_addr % 0x400000) fatal_kernel_error("Trying to unmap a non-aligned virtual address", "UNMAP_PAGE_TABLE");

    u32 pd_index = virt_addr >> 22;

    u32* page_table = (u32*) page_directory[pd_index];
    if(!page_table) fatal_kernel_error("Trying to unmap an unmapped page table", "UNMAP_PAGE_TABLE");

    if(cpu_pse)
    {
        page_directory[pd_index] = 0;
    }
    else
    {
        pt_free(page_table + KERNEL_VIRTUAL_BASE);
        page_directory[pd_index] = 0;
    }

    //flush, update, or do something with the cache

}

//if we need to access some defined point of physical memory (like the ACPI table)
void map_flexible(u32 size, u32 physical, u32 virt_addr, u32* page_directory)
{
    u32 bvaddr = virt_addr;
    aligndown(virt_addr, 4096);
    size += (bvaddr-virt_addr);
    alignup(size, 4096);

    u32 add = 0;

    if((!(virt_addr % 0x400000)) && size > 0x400000)
    {
        while(size > 0x400000)
        {
            map_page_table(physical+add, virt_addr+add, page_directory);
            size -= 0x400000;
            add += 0x400000;
        }
    }
    
    while(size)
    {
        map_page(physical+add, virt_addr+add, page_directory);
        size -= 4096;
        add += 4096;
    }
}

void unmap_flexible(u32 size, u32 virt_addr, u32* page_directory)
{
    u32 bvaddr = virt_addr;
    aligndown(virt_addr, 4096);
    size += (bvaddr-virt_addr);
    alignup(size, 4096);

    u32 add = 0;

    if((!(virt_addr % 0x400000)) && size > 0x400000)
    {
        while(size > 0x400000)
        {
            unmap_page_table(virt_addr+add, page_directory);
            size -= 0x400000;
            add += 0x400000;
        }
    }
    
    while(size)
    {
        unmap_page(virt_addr+add, page_directory);
        size -= 4096;
        add += 4096;
    }
}

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
    if((((u32)page_table) << 24 >> 31) == 1) {return (((u32) *page_table) >> 12 << 12)+(virt_addr%0x400000);}
    else
    {
        page_table = (u32*) (((u32)page_table) >> 12 << 12);
        u32* page = (u32*) (((u32) page_table) + pt_index*4 + KERNEL_VIRTUAL_BASE);
        if(*page) return (((u32)*page) >> 12 << 12) + (virt_addr%4096);
        else return 0;
    }
}

bool is_mapped(u32 virt_addr, u32* page_directory)
{
    u32 pd_index = virt_addr >> 22;
    u32 pt_index = virt_addr >> 12 & 0x03FF;
    u32* page_table = (u32*) page_directory[pd_index];
    if(!page_table) return false;
    if(((u32)page_table) & PD_BIT_4KB_PAGE) return true;
    page_table = (u32*) (((u32)page_table) & PD_ADDRESS_MASK);
    u32* page = (u32*) (((u32) page_table) + pt_index*4 + KERNEL_VIRTUAL_BASE);
    if(*page) return true;
    else return false;
}

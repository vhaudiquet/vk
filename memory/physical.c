#include "system.h"
#include "mem.h"
#include "error/error.h"

/*
* This file has the goal to trace the physical memory and to gets avaible blocks of it
*/

p_block_t* first_block = 0;
u64 detected_memory = 0;
u32 detected_memory_below32;

void physmem_get(multiboot_info_t* mbt)
{
    kprintf("[MEM] Getting physical memory map...");

    //Parse memory map from GRUB
    memory_map_t* mmap = (memory_map_t*) (mbt->mmap_addr+KERNEL_VIRTUAL_BASE);
    p_block_t* prev = 0;
    p_block_t* current_block = first_block = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(p_block_t), "Physical memory block struct");
    #else
    kmalloc(sizeof(p_block_t));
    #endif
    
    unsigned int i = 0;
    while(i < mbt->mmap_length)
    {
        if(first_block == 0) first_block = current_block;
        detected_memory += mmap->length;
        if(mmap->base_addr < U32_MAX)
        {
            current_block->base_addr = (u32) mmap->base_addr;
            if(mmap->length < U32_MAX)
                current_block->size = (u32) mmap->length;
            else
                current_block->size = U32_MAX-1;
            detected_memory_below32 += current_block->size;
            current_block->type = mmap->type;
            current_block->prev = prev;
            i+= (sizeof(memory_map_t));
            if(i < mbt->mmap_length)
            {
                current_block->next = 
                #ifdef MEMLEAK_DBG
                kmalloc(sizeof(p_block_t), "Physical memory block");
                #else
                kmalloc(sizeof(p_block_t));
                #endif
                prev = current_block;
                current_block = current_block->next;
                mmap = (memory_map_t*) ((u32) mmap + mmap->size + sizeof(mmap->size));
            }
        }
        else
        {
            i+= (sizeof(memory_map_t));
            if(i < mbt->mmap_length)
                mmap = (memory_map_t*) ((u32) mmap + mmap->size + sizeof(mmap->size));
        }
    }
    //Mark the kernel page as used (except the first 1 mib that are mapped but free/used by hardware)
    reserve_specific(0x100000, 0x300000, PHYS_KERNEL_BLOCK_TYPE);
    //Mark the kernel heap page as used
    reserve_specific(KHEAP_PHYS_START, KHEAP_BASE_SIZE, PHYS_KERNEL_BLOCK_TYPE);

    vga_text_okmsg();
}

u32 get_free_mem()
{
    u32 tr = 0;
    p_block_t* curr = first_block;
    while(curr)
    {
        if(curr->type == 1 && curr->base_addr > 0x100000) tr+= curr->size;
        curr = curr->next;
    }
    return tr;
}

u32 reserve_block(u32 size, u8 type)
{
    p_block_t* curr = first_block;
    while(curr)
    {
        if(curr->base_addr >= 0x100000 && curr->type == PHYS_FREE_BLOCK_TYPE)
        {
            if(curr->size < size) {curr = curr->next; continue;}
            p_block_t* newblock = 
            #ifdef MEMLEAK_DBG
            kmalloc(sizeof(p_block_t), "physical memory new block (reserve_block)");
            #else
            kmalloc(sizeof(p_block_t));
            #endif
            p_block_t* next = curr->next;
            newblock->base_addr = curr->base_addr+size;
            newblock->size = curr->size-size;
            newblock->type = 1;
            newblock->next = next;
            newblock->prev = curr;
            next->prev = newblock;
            curr->size = size;
            curr->next = newblock;
            curr->type = type;
            return curr->base_addr;
        }
        curr = curr->next;
    }
    fatal_kernel_error("Trying to reserve more physical memory than available", "RESERVE_BLOCK");
    return 0;
}

//CARE : UNSAFE
u32 reserve_specific(u32 addr, u32 size, u8 type)
{
    p_block_t* curr = first_block;
    while(curr)
    {
        if(curr->base_addr == addr && curr->type == PHYS_FREE_BLOCK_TYPE)
        {
            if(curr->type != PHYS_FREE_BLOCK_TYPE) break;
            if(curr->size < size) {break;}
            p_block_t* newblock = 
            #ifdef MEMLEAK_DBG
            kmalloc(sizeof(p_block_t), "physical memory new block (reserve_specific)");
            #else
            kmalloc(sizeof(p_block_t));
            #endif
            p_block_t* next = curr->next;
            newblock->base_addr = curr->base_addr+size;
            newblock->size = curr->size-size;
            newblock->type = PHYS_FREE_BLOCK_TYPE;
            newblock->next = next;
            newblock->prev = curr;
            next->prev = newblock;
            curr->size = size;
            curr->next = newblock;
            curr->type = type;
            return curr->base_addr;
        }
        else if(curr->base_addr < addr && curr->base_addr+curr->size > addr)
        {
            if(curr->type != PHYS_FREE_BLOCK_TYPE) break;
            if(curr->size < size) {break;}
            p_block_t* beforeblock = 
            #ifdef MEMLEAK_DBG
            kmalloc(sizeof(p_block_t), "physical memory new block (reserve_specific)");
            #else
            kmalloc(sizeof(p_block_t));
            #endif
            beforeblock->base_addr = curr->base_addr;
            beforeblock->size = addr - size;
            beforeblock->next = curr;
            beforeblock->prev = curr->prev;
            beforeblock->type = PHYS_FREE_BLOCK_TYPE; 
            curr->prev->next = beforeblock;

            curr->prev = beforeblock;
            curr->base_addr = addr;
            curr->size -= beforeblock->size;

            p_block_t* afterblock = 
            #ifdef MEMLEAK_DBG
            kmalloc(sizeof(p_block_t), "physical memory new block (reserve_specific)");
            #else
            kmalloc(sizeof(p_block_t));
            #endif
            p_block_t* next = curr->next;
            afterblock->base_addr = curr->base_addr+size;
            afterblock->size = curr->size-size;
            afterblock->type = PHYS_FREE_BLOCK_TYPE;
            afterblock->next = next;
            afterblock->prev = curr;

            curr->next = afterblock;
            curr->size = size;
            curr->type = type;
            return curr->base_addr;
        }
        curr = curr->next;
    }
    fatal_kernel_error("Trying to reserve specific failed", "RESERVE_SPECIFIC");
    return 0;
}

void free_block(u32 base_addr)
{
    p_block_t* curr = first_block;
    while(curr)
    {
        if(curr->base_addr == base_addr)
        {
            if(curr->type != PHYS_KERNELF_BLOCK_TYPE && curr->type != PHYS_USER_BLOCK_TYPE)
                fatal_kernel_error("Trying to free a non-freeable block", "FREE_BLOCK");
            curr->type = PHYS_FREE_BLOCK_TYPE;
            //block merging before and after
            p_block_t* m = curr->prev;
            while(m && m->type == PHYS_FREE_BLOCK_TYPE)
            {
                m->size += curr->size;
                m->next = curr->next;
                m->next->prev = m;
                kfree(curr);
                curr = m;
                m = m->prev;
            }
            m = curr->next;
            while(m && m->type == PHYS_FREE_BLOCK_TYPE)
            {
                curr->size += m->size;
                curr->next = m->next;
                curr->next->prev = curr;
                kfree(m);
                m = curr->next;
            }
            return;
        }
        curr = curr->next;
    }
    fatal_kernel_error("Trying to free an unknown block", "FREE_BLOCK");
}

p_block_t* get_block(u32 some_addr)
{
    p_block_t* curr = first_block;
    while(curr)
    {
        if(curr->base_addr < some_addr && curr->base_addr+curr->size > some_addr)
            return curr;
        curr = curr->next;
    }
    fatal_kernel_error("This address is not present in memory", "GET_BLOCK");
    return 0;
}
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
#include "tasking/task.h"
#include "mem.h"

/*
* This is the kernel heap ; it provides kmalloc(), kfree() and krealloc().
*/

#define UNKNOWN_BLOCK_ERRMSG "Unknown block in kernel heap"
#define HEAP_FULL_ERRMSG "The kernel heap is full. How did you do this ?"

//Kernel heap above the kernel
u32 KHEAP_PHYS_START = 0x800000;
u32 KHEAP_BASE_START = 0xC0800000;//(u32) &_kernel_end;
u32 KHEAP_BASE_END = 0xC0800000 + KHEAP_BASE_SIZE;; //(u32) &_kernel_end + KHEAP_BASE_SIZE;
u32 kheap_page_table[1024] __attribute__((aligned(4096)));

static void merge_free_blocks();
static void kheap_expand();

void kheap_install()
{
    //mapping memory manually, as we have heap yet
    u32 pd_index = KHEAP_BASE_START >> 22;
    unsigned int i;
    for(i = 0; i<1024; i++)
    {
        kheap_page_table[i] = (i*0x1000 + KHEAP_PHYS_START) | 3;
    }

    kernel_page_directory[pd_index] = (((u32) kheap_page_table) - KERNEL_VIRTUAL_BASE) | 3;

    memset((void*) KHEAP_BASE_START, 0, KHEAP_BASE_SIZE);
    block_header_t* base_block = (block_header_t*) KHEAP_BASE_START;
    base_block->magic = BLOCK_HEADER_MAGIC;
    base_block->size = KHEAP_BASE_END - (((u32) base_block) + sizeof(block_header_t));
    base_block->status = 0;
}

#ifdef MEMLEAK_DBG
void* kmalloc(u32 size, char* comment)
#else
void* kmalloc(u32 size)
#endif
{
    //Always align size at 4 bytes, so that base address on the heap (with align_skip=0) are 4-bytes aligned
    alignup(size, 4);

    u32 i;

    i = KHEAP_BASE_START;
    while(i < KHEAP_BASE_END)
    {
        block_header_t* currentBlock = (block_header_t*) i;

        //log
        //kprintf("[ALLOC] [MALLOC] Block %X (size = %d) (status = %s)\n", i, currentBlock->size, (currentBlock->status ? "RESERVED" : "FREE"))        //kprintf("Comment : %s\n", currentBlock->comment);

        //Check if the current block is valid
        if(currentBlock->magic != BLOCK_HEADER_MAGIC)
        {
            #ifdef MEMLEAK_DBG
            kprintf("Error on block at 0x%X\n", currentBlock);
            #endif
            fatal_kernel_error(UNKNOWN_BLOCK_ERRMSG, "Memory allocation");
        }

        //Check if the current block is free and large enough
        if(!currentBlock->status && currentBlock->size >= size)
        {
            unsigned int oldSize = currentBlock->size;
            if(oldSize - size > sizeof(block_header_t))
            {
                currentBlock->size = size;
                //Split the block if it is big
                block_header_t* newblock = (block_header_t*) (i+sizeof(block_header_t)+currentBlock->size);
                newblock->magic = BLOCK_HEADER_MAGIC;
                newblock->size = oldSize-size-sizeof(block_header_t);
                newblock->status = 0;
                #ifdef MEMLEAK_DBG
                newblock->comment = 0;
                #endif
                //kprintf("Setting up new block at %X (size = %d) (cbS = %d)", newblock, newblock->size, currentBlock->size);
            }
            //Mark the block as reserved
            currentBlock->status = 1;

            #ifdef MEMLEAK_DBG
            currentBlock->comment = comment;
            #endif

            //Return the block
            //kprintf("[ALLOC] [MALLOC] Returned block %X (size %d)\n", ((u32)currentBlock), currentBlock->size);
            return ((void*) ((u32)currentBlock)+sizeof(block_header_t));
        }
        //The current block did not match, skipping to next block
        i += (currentBlock->size+sizeof(block_header_t));
    }
    //Heap is full : expand
    kheap_expand();
    #ifdef MEMLEAK_DBG
    return kmalloc(size, comment);
    #else
    return kmalloc(size);
    #endif
    //fatal_kernel_error(HEAP_FULL_ERRMSG, "Memory allocation");
    //return ((void*) 0);
}

void kfree(void* pointer)
{
    block_header_t* blockHeader = (block_header_t*) (pointer - sizeof(block_header_t));
    if(blockHeader->magic != BLOCK_HEADER_MAGIC) 
        fatal_kernel_error(UNKNOWN_BLOCK_ERRMSG, "Pointer freeing");
    blockHeader->status = 0;
    #ifdef MEMLEAK_DBG
    blockHeader->comment = 0;
    #endif

    //kprintf("[ALLOC] [FREE] Block %X is now free\n", blockHeader);

    merge_free_blocks();
}

u32 kheap_get_size(void* ptr)
{
    block_header_t* blockHeader = (block_header_t*) (ptr - sizeof(block_header_t));
    if(blockHeader->magic != BLOCK_HEADER_MAGIC) 
        fatal_kernel_error(UNKNOWN_BLOCK_ERRMSG, "Pointer size");
    return blockHeader->size;
}

void* krealloc(void* pointer, u32 newsize)
{
    block_header_t* blockHeader = (block_header_t*) (pointer - sizeof(block_header_t));
    if(blockHeader->size > newsize) fatal_kernel_error("Reallocating less space ?!", "KREALLOC");
    
    #ifdef MEMLEAK_DBG
    void* np = kmalloc(newsize, blockHeader->comment);
    #else
    void* np = kmalloc(newsize);
    #endif

    memcpy(np, pointer, blockHeader->size);
    kfree(pointer);
    return np;
}

static void merge_free_blocks()
{
    block_header_t* current_block = (block_header_t*) KHEAP_BASE_START;
    //Examine all the blocks on the heap
    while(((u32) current_block) < KHEAP_BASE_END)
    {
        //kprintf("[ALLOC] [MERGE] Current block : %X (size = %d) (status=%s)\n", current_block, current_block->size, current_block->status ? "RESERVED" : "FREE");
        //Check if the current block is valid
        if(current_block->magic != BLOCK_HEADER_MAGIC)
            fatal_kernel_error(UNKNOWN_BLOCK_ERRMSG, "Block merging (first block lookup)");
        //Check if the current block is free ; if it is not, go to the next one
        if(current_block->status) 
        {
            current_block = (block_header_t*) (((u32) current_block) + ((current_block->size)+sizeof(block_header_t))); 
            continue;
        }

        //At this point we know that the current block is free
        //Check if the block next to the current block is free
        block_header_t* next_block = (block_header_t*) ((((u32) current_block) + current_block->size+sizeof(block_header_t)));
        if(((u32) next_block) >= KHEAP_BASE_END) return;
        while(!next_block->status)
        {
            //if the block next to the current block is the end of the heap, we are done
            if(((u32) next_block) >= KHEAP_BASE_END) return;
            //check if the block next to the current block is valid
            if(next_block->magic != BLOCK_HEADER_MAGIC) 
                fatal_kernel_error(UNKNOWN_BLOCK_ERRMSG, "Block merging (merging loop)");
            //kprintf("[ALLOC] [MERGE] Next block : %X (size = %d) (status=%s)\n", next_block, next_block->size, next_block->status ? "RESERVED" : "FREE");
            //If the block next to the current block is free, merge them, and the block next becomes the next_block
            current_block->size += (next_block->size+sizeof(block_header_t));
            //kprintf("[ALLOC] [MERGE] Blocks %X and %X merged\n", current_block, next_block);
            next_block = (block_header_t*) (((u32) next_block) + (next_block->size+sizeof(block_header_t)));
            if(((u32) next_block) >= KHEAP_BASE_END) return;
        }
        //We're done, go to the next block
        current_block = (block_header_t*) (((u32) current_block) + ((current_block->size)+sizeof(block_header_t))); 
    }
}

static void kheap_expand()
{
    if(KHEAP_BASE_END >= FREE_KVM_START) fatal_kernel_error("Kernel heap full ! How ?", "KHEAP_EXPAND");
    
    //we need to EXPAND heap in ALL CURRENT PAGES DIRS (all processes page dirs)
    //todo : check if we can handle that in PF ? (if page fault but kernel page mapped, map ?)
    //anyway we will need a spinlock on that too (lock kernelpagedir or something like that)
    #ifdef PAGING_DEBUG
    kprintf("%lKHEAP_EXPAND: mapping 0x%X (size 0x%X)...\n", 3, KHEAP_BASE_END, 0x400000);
    #endif
    
    map_memory(0x400000, KHEAP_BASE_END, kernel_page_directory);
    u32 i = 0;
    for(;i<processes_size;i++)
    {
        process_t* process = processes[i];
        map_memory(0x400000, KHEAP_BASE_END, process->page_directory);
    }

    block_header_t* base_block = (block_header_t*) KHEAP_BASE_END;
    base_block->magic = BLOCK_HEADER_MAGIC;
    base_block->size = 0x400000 - sizeof(block_header_t);
    base_block->status = 0;
    KHEAP_BASE_END += 0x400000;
    merge_free_blocks();
}
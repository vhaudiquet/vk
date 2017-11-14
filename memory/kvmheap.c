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

#include "system.h"
#include "mem.h"
#include "error/error.h"

typedef struct VM_BLOCK
{
    u32 vaddr;
    u32 size;
    struct VM_BLOCK* next;
    struct VM_BLOCK* prev;
} vm_block_t;

vm_block_t* vm_first_block = 0;

void kvmheap_install()
{
    vm_first_block = kmalloc(sizeof(vm_block_t));
    vm_first_block->vaddr = FREE_KVM_START;
    vm_first_block->size = 0xFFFFFFFF - FREE_KVM_START;
    vm_first_block->next = 0;
    vm_first_block->prev = 0;
}

u32 kvm_reserve_block(u32 size)
{
    alignup(size, 4096);
    vm_block_t* curr = vm_first_block;
    while(curr)
    {
        if(curr->size < size) {curr = curr->next; continue;}
        vm_block_t* newblock = 
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(vm_block_t), "virtual memory new block (kvm_reserve_block)");
        #else
        kmalloc(sizeof(vm_block_t));
        #endif
        vm_block_t* next = curr->next;
        newblock->vaddr = curr->vaddr+size;
        newblock->size = curr->size-size;
        newblock->next = next;
        newblock->prev = curr;
        if(next) next->prev = newblock;
        curr->size = size;
        curr->next = newblock;
        return curr->vaddr;
    }
    fatal_kernel_error("Trying to reserve more virtual memory than available", "KVM_RESERVE_BLOCK");
    return 0;
}

void kvm_free_block(u32 base_addr)
{
    vm_block_t* curr = vm_first_block;
    while(curr)
    {
        if(curr->vaddr == base_addr)
        {
            //block merging before and after
            vm_block_t* m = curr->prev;
            while(m)
            {
                m->size += curr->size;
                m->next = curr->next;
                if(m->next) m->next->prev = m;
                kfree(curr);
                curr = m;
                m = m->prev;
            }
            m = curr->next;
            while(m)
            {
                curr->size += m->size;
                curr->next = m->next;
                if(curr->next) curr->next->prev = curr;
                kfree(m);
                m = curr->next;
            }
            return;
        }
        curr = curr->next;
    }
    fatal_kernel_error("Trying to free an unknown block", "KVM_FREE_BLOCK");
}

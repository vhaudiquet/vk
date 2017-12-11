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
#include "task.h"
#include "filesystem/fs.h"
#include "memory/mem.h"
#include "cpu/cpu.h"

/*
* Implementation of PROCESS, used by the scheduler
*/

process_t* kernel_process = 0;
process_t* idle_process = 0;

process_t* create_process(file_descriptor_t* executable)
{
    //check if the file is really an ELF executable
    if(!elf_check(executable)) return 0;

    u32* page_directory = get_kernel_pd_clone();

    list_entry_t* data_loc = kmalloc(sizeof(list_entry_t));
    u32 data_size = 0;
    void* code_offset = (void*) elf_load(executable, page_directory, data_loc, &data_size);

    if((!code_offset) | (((u32)code_offset) > 0xC0000000)) {pt_free(page_directory); return 0;}

    //TODO: check if this area isnt already mapped by elf code/data ; and find a better area to permit to the stack to grow later
    void* stack_offset = (void*) 0xC0000000-8194;
    
    map_memory(8192, (u32) stack_offset-8192, page_directory);
    pd_switch(page_directory);
    memset((void*)(stack_offset-8192), 0, 8192);
    pd_switch(kernel_page_directory);

    process_t* tr = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(process_t), "process struct");
    #else
    kmalloc(sizeof(process_t));
    #endif

    tr->data_loc = data_loc;
    tr->data_size = data_size;

    tr->base_stack = (u32) stack_offset-8192;

    tr->gregs.eax = 0;
    tr->gregs.ebx = 0;
    tr->gregs.ecx = 0;
    tr->gregs.edx = 0;
    tr->gregs.esi = 0;
    tr->gregs.edi = 0;
    tr->ebp = 0;

    tr->sregs.ds = tr->sregs.es = tr->sregs.fs = tr->sregs.gs = tr->sregs.ss = 0x23;
    tr->sregs.cs = 0x1B;

    tr->eip = (u32) code_offset;
    tr->esp = (u32) stack_offset;

    tr->page_directory = page_directory;

    //process kernel stack
    void* kstack = 
    #ifdef MEMLEAK_DBG
    kmalloc(8192, "process kernel stack");
    #else
    kmalloc(8192);
    #endif
    tr->kesp = ((u32) kstack) + 8191;

    tr->base_kstack = (u32) kstack;

    return tr;
}

extern void idle_loop();
asm(".global idle_loop\n \
idle_loop:\n \
hlt \n \
jmp idle_loop\n");

process_t* init_idle_process()
{
    idle_process = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(process_t), "idle process");
    #else
    kmalloc(sizeof(process_t));
    #endif
    idle_process->gregs.eax = idle_process->gregs.ebx = idle_process->gregs.ecx = idle_process->gregs.edx = 0;
    idle_process->gregs.edi = idle_process->gregs.esi = idle_process->ebp = 0;
    idle_process->sregs.ds = idle_process->sregs.es = idle_process->sregs.fs = idle_process->sregs.gs = idle_process->sregs.ss = 0x10;
    idle_process->sregs.cs = 0x08;
    idle_process->eip = (u32) idle_loop; //IDLE LOOP
    idle_process->esp = idle_process->kesp = 
    #ifdef MEMLEAK_DBG
    ((u32) kmalloc(256, "idle process kernel stack"))+255;
    #else
    ((u32) kmalloc(256))+255;
    #endif
    idle_process->base_stack = idle_process->base_kstack = idle_process->kesp - 255;
    idle_process->page_directory = kernel_page_directory;
    return idle_process;
}

process_t* init_kernel_process()
{
    kernel_process = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(process_t), "kernel process");
    #else
    kmalloc(sizeof(process_t));
    #endif
    kernel_process->page_directory = kernel_page_directory;

    current_process = kernel_process;

    return kernel_process;
}

void exit_process(process_t* process)
{
    //mark physical memory reserved for process stack as free
    u32 stack_phys = get_physical(process->base_stack, process->page_directory);
    free_block(stack_phys);

    //TODO : mark all data/code blocks reserved for the process as free
    u32 i = 0;
    list_entry_t* dloc = process->data_loc;
    for(i=0;i<process->data_size;i++)
    {
        u32 phys = get_physical(((u32*)dloc->element)[0], process->page_directory);
        free_block(phys);
    }
    list_free(dloc, process->data_size);

    //free process's kernel stack
    kfree((void*) process->base_kstack);

    //free process's page directory
    pt_free(process->page_directory);

    //remove process from schedulers
    scheduler_remove_process(process);
}
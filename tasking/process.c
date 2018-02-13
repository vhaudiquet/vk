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

#define PROCESSES_ARRAY_SIZE 20
process_t** processes = 0;
u32 processes_size = 0;

process_t* kernel_process = 0;
process_t* idle_process = 0;

void process_init()
{
    processes_size = PROCESSES_ARRAY_SIZE;
    processes = kmalloc(processes_size);
}

process_t* create_process(fd_t* executable, int argc, char** argv, tty_t* tty)
{
    //check if the file is really an ELF executable
    if(elf_check(executable) != ERROR_NONE) return 0;

    u32* page_directory = get_kernel_pd_clone();

    list_entry_t* data_loc = kmalloc(sizeof(list_entry_t));
    u32 data_size = 0;
    void* code_offset = (void*) elf_load(executable, page_directory, data_loc, &data_size);

    if((!code_offset) | (((u32)code_offset) > 0xC0000000)) {pt_free(page_directory); return 0;}

    //TODO: check if this area isnt already mapped by elf code/data
    void* stack_offset = (void*) 0xC0000000;
    
    map_memory(8192, (u32) stack_offset-8192, page_directory);
    pd_switch(page_directory);
    memset((void*)(stack_offset-8192), 0, 8192);
    u32 base_stack = (u32) stack_offset-8192;

    /* ARGUMENTS PASSING */
    int i;
    char** uparam = (char**) kmalloc(sizeof(char*) * ((u32) argc));
    
    //copy strings
    for (i=0; i<argc; i++) 
    {
        stack_offset -= (strlen(argv[i]) + 1);
        strcpy((char*) stack_offset, argv[i]);
        uparam[i] = (char*) stack_offset;
    }

    //copy adresses
    for (i=argc-1; i>=0; i--)
    {
        stack_offset -= sizeof(char*);
        *((char**) stack_offset) = uparam[i]; 
    }

    //copy argv
    stack_offset -= sizeof(char*);
    *((char**) stack_offset) = (char*) (stack_offset + 4); 

    //copy argc
    stack_offset -= sizeof(char*);
    *((int*) stack_offset) = argc; 

    stack_offset -= sizeof(char*);
    /* ARGUMENTS PASSED */

    pd_switch(current_process->page_directory);

    process_t* tr = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(process_t), "process struct");
    #else
    kmalloc(sizeof(process_t));
    #endif

    tr->data_loc = data_loc;
    tr->data_size = data_size;

    tr->base_stack = base_stack;

    //set default registers to 0
    tr->gregs.eax = 0;
    tr->gregs.ebx = 0;
    tr->gregs.ecx = 0;
    tr->gregs.edx = 0;
    tr->gregs.esi = 0;
    tr->gregs.edi = 0;
    tr->ebp = 0;

    //set default segment registers
    tr->sregs.ds = tr->sregs.es = tr->sregs.fs = tr->sregs.gs = tr->sregs.ss = 0x23;
    tr->sregs.cs = 0x1B;

    tr->eip = (u32) code_offset;
    tr->esp = (u32) stack_offset;

    //set process page directory (the one we just allocated)
    tr->page_directory = page_directory;

    //set process tty, depending on argument
    tr->tty = tty;

    //init process file array
    tr->files_size = 5;
    tr->files = kmalloc(tr->files_size*sizeof(fd_t));

    //init stdout, stdin, stderr
    tr->files[0] = tty->pointer;
    tr->files[1] = tty->pointer;
    tr->files[2] = tty->pointer;
    tr->files_count = 3;

    //set process heap
    //for now we decide that the last mem segment will be the start of the heap (cause it's easier and i'm lazy)
    list_entry_t* ptr = data_loc;
    u32 dsize = data_size-1;
    while(dsize)
    {
        ptr = ptr->next;
        dsize--;
    }
    tr->heap_addr = ((u32*)ptr->element)[0]+((u32*)ptr->element)[1];
    tr->heap_size = 0;

    //process kernel stack
    void* kstack = 
    #ifdef MEMLEAK_DBG
    kmalloc(8192, "process kernel stack");
    #else
    kmalloc(8192);
    #endif
    tr->kesp = ((u32) kstack) + 8191;

    tr->base_kstack = (u32) kstack;

    //register process in process list
    u32 j = 0;
    for(;j<processes_size;j++)
    {
        if(!processes[j]) {processes[j] = tr; tr->pid = j;}
    }

    if(!tr->pid)
    {
        processes_size*=2;
        processes = krealloc(processes, processes_size);
        processes[j+1] = tr; tr->pid = (j+1);
    }

    return tr;
}

void exit_process(process_t* process)
{
    //mark physical memory reserved for process stack as free
    u32 stack_phys = get_physical(process->base_stack, process->page_directory);
    free_block(stack_phys);

    //mark all data/code blocks reserved for the process as free
    u32 i = 0;
    list_entry_t* dloc = process->data_loc;
    list_entry_t* ptr = dloc;
    for(i=0;i<process->data_size;i++)
    {
        u32 phys = get_physical(((u32*)ptr->element)[0], process->page_directory);
        free_block(phys);
        ptr = ptr->next;
    }
    list_free(dloc, process->data_size);

    //free process's kernel stack
    kfree((void*) process->base_kstack);

    //free process's page directory
    pt_free(process->page_directory);

    //remove process from schedulers
    scheduler_remove_process(process);

    //remove process from array
    processes[process->pid] = 0;

    //free process struct
    kfree(process);
}

u32 sbrk(process_t* process, u32 incr)
{
    u32 new_last_addr = process->heap_addr+process->heap_size+incr;
    if(!is_mapped(new_last_addr, process->page_directory))
    {
        //TODO : Change that, it can actually cause a kernel panic (if the miss of mapped memory is after)
        map_memory(incr, process->heap_addr+process->heap_size, process->page_directory);        
    }
    process->heap_size += incr;
    return process->heap_addr+process->heap_size;
}

process_t* fork(process_t* process)
{
    //copy from original process
    process_t* tr = kmalloc(sizeof(process_t));
    memcpy(tr, process, sizeof(process_t));

    //get own adress space
    u32* page_directory = copy_adress_space(process->page_directory);
    tr->page_directory = page_directory;

    //get own copies of file_descriptors
    tr->files = kmalloc(process->files_size*sizeof(fd_t));
    u32 i = 0;
    for(;i<process->files_size;i++)
    {
        fd_t* tocopy = process->files[i];
        fd_t* toadd = kmalloc(sizeof(fd_t));
        memcpy(toadd, tocopy, sizeof(fd_t));
        tr->files[i] = toadd;
    }

    //get own pid / register in the process list
    u32 j = 0;
    for(;j<processes_size;j++)
    {
        if(!processes[j]) {processes[j] = tr; tr->pid = j;}
    }

    if(!tr->pid)
    {
        processes_size*=2;
        processes = krealloc(processes, processes_size);
        processes[j+1] = tr; tr->pid = (j+1);
    }

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

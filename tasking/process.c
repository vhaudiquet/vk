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
    memset(processes, 0, processes_size);
    init_signals();
}

process_t* create_process(fd_t* executable, int argc, char** argv, tty_t* tty)
{
    //check if the file is really an ELF executable
    if(elf_check(executable) != ERROR_NONE) return 0;

    u32* page_directory = get_kernel_pd_clone();

    list_entry_t* data_loc = kmalloc(sizeof(list_entry_t));
    u32 data_size = 0;
    void* code_offset = (void*) elf_load(executable, page_directory, data_loc, &data_size);

    if((!code_offset) | (((u32)code_offset) > 0xC0000000)) {pt_free(page_directory); kfree(data_loc); return 0;}

    //TODO: check if this area isnt already mapped by elf code/data
    void* stack_offset = (void*) 0xC0000000-0x4;
    
    map_memory(8192, (u32) stack_offset-8192, page_directory);
    u32 base_stack = (u32) stack_offset-8192;

    /* ARGUMENTS PASSING */
    pd_switch(page_directory);
    
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

    pd_switch(current_process->page_directory);
    /* ARGUMENTS PASSED */

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
    
    tr->flags = 0; asm("pushf; pop %%eax":"=a"(tr->flags):);

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

    //init stdin, stdout, stderr
    fd_t* std = kmalloc(sizeof(fd_t)); std->offset = 0; std->file = tty->pointer;
    tr->files[0] = std; //stdin
    tr->files[1] = std; //stdout
    tr->files[2] = std; //stderr
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
    tr->kesp = ((u32) kstack) + 8192;

    tr->base_kstack = (u32) kstack;

    tr->status = PROCESS_STATUS_INIT;

    //register process in process list
    tr->pid = PROCESS_INVALID_PID;
    int j = 1; // pid 0 is reserved
    for(;j<(int) processes_size;j++)
    {
        if(!processes[j]) {processes[j] = tr; tr->pid = j; break;}
    }

    if(tr->pid == PROCESS_INVALID_PID)
    {
        processes_size*=2;
        processes = krealloc(processes, processes_size);
        processes[j+1] = tr; tr->pid = (j+1);
    }

    //register as children of current process
    if(current_process && current_process != kernel_process)
    {
        list_entry_t** child = &current_process->children;
        while(*(child)) if((*child)->next) child = &(*child)->next;
        (*child) = kmalloc(sizeof(list_entry_t));
        (*child)->element = current_process;
        (*child)->next = 0;
    }
    tr->parent = current_process;

    //register default signals handler
    memset(tr->signal_handlers, 0, NSIG*sizeof(void*));
    //set sighandler to 0
    memset(&tr->sighandler, 0, sizeof(sighandler_t));
    tr->sighandler.sregs.ds = tr->sighandler.sregs.es = tr->sighandler.sregs.fs = tr->sighandler.sregs.gs = tr->sighandler.sregs.ss = 0x23;
    tr->sighandler.sregs.cs = 0x1B;

    return tr;
}

void exit_process(process_t* process)
{
    //remove process from schedulers
    scheduler_remove_process(process);

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

    //free process children list
    list_entry_t* cptr = process->children;
    while(cptr)
    {
        process_t* cp = cptr->element;   
        cp->parent = 0;

        list_entry_t* tf = cptr;
        cptr = cptr->next;
        kfree(tf);
    }

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
        map_memory_if_not_mapped(incr, process->heap_addr+process->heap_size, process->page_directory);        
    }
    process->heap_size += incr;
    return process->heap_addr+process->heap_size;
}

process_t* fork(process_t* process)
{
    //copy from original process
    process_t* tr = kmalloc(sizeof(process_t));
    memcpy(tr, process, sizeof(process_t));

    //set status to init
    tr->status = PROCESS_STATUS_INIT;

    //get own adress space
    u32* page_directory = copy_adress_space(process->page_directory);
    tr->page_directory = page_directory;

    //get own kernel stack
    void* kstack = kmalloc(8192);
    tr->kesp = ((u32) kstack) + 8192;
    tr->base_kstack = (u32) kstack;
    memcpy(kstack, (void*) process->base_kstack, 8192);

    //set registers to match current status (kernel space)
    tr->sregs.cs = 0x08;
    tr->sregs.ss = 0x10;
    //note: we will restore ESP later, because we cant know it there

    //get own copies of file_descriptors
    tr->files = kmalloc(process->files_size*sizeof(fd_t));
    u32 i = 0;
    for(;i<process->files_size;i++)
    {
        fd_t* tocopy = process->files[i];
        if(!tocopy) continue;
        
        fd_t* toadd = kmalloc(sizeof(fd_t));
        memcpy(toadd, tocopy, sizeof(fd_t));
        tr->files[i] = toadd;
    }

    //get own pid / register in the process list
    tr->pid = PROCESS_INVALID_PID;

    int j = 1; // pid 0 is reserved
    for(;j<(int) processes_size;j++)
    {
        if(!processes[j]) {processes[j] = tr; tr->pid = j; break;}
    }

    if(tr->pid == PROCESS_INVALID_PID)
    {
        processes_size*=2;
        processes = krealloc(processes, processes_size);
        processes[j+1] = tr; tr->pid = (j+1);
    }

    //register as children of current process
    if(current_process && current_process != kernel_process)
    {
        list_entry_t** child = &current_process->children;
        while(*(child)) if((*child)->next) child = &(*child)->next;
        (*child) = kmalloc(sizeof(list_entry_t));
        (*child)->element = current_process;
        (*child)->next = 0;
    }
    tr->parent = current_process;

    //clear pending signals
    memset(&tr->sighandler, 0, sizeof(sighandler_t));

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
    idle_process->status = PROCESS_STATUS_INIT;
    idle_process->pid = PROCESS_IDLE_PID;
    idle_process->flags = 0; asm("pushf; pop %%eax":"=a"(idle_process->flags):);
    idle_process->gregs.eax = idle_process->gregs.ebx = idle_process->gregs.ecx = idle_process->gregs.edx = 0;
    idle_process->gregs.edi = idle_process->gregs.esi = idle_process->ebp = 0;
    idle_process->sregs.ds = idle_process->sregs.es = idle_process->sregs.fs = idle_process->sregs.gs = idle_process->sregs.ss = 0x10;
    idle_process->sregs.cs = 0x08;
    idle_process->eip = (u32) idle_loop; //IDLE LOOP
    idle_process->esp = idle_process->kesp = 
    #ifdef MEMLEAK_DBG
    ((u32) kmalloc(1024, "idle process kernel stack"))+1024;
    #else
    ((u32) kmalloc(1024))+1024;
    #endif
    idle_process->base_stack = idle_process->base_kstack = idle_process->kesp - 1024;
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
    kernel_process->pid = PROCESS_KERNEL_PID;
    kernel_process->page_directory = kernel_page_directory;
    kernel_process->status = PROCESS_STATUS_RUNNING;

    current_process = kernel_process;

    return kernel_process;
}

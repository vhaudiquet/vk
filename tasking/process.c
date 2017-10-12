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

    void* code_offset = (void*) elf_load(executable, page_directory);

    if((!code_offset) | (((u32)code_offset) > 0xC0000000)) {pt_free(page_directory); return 0;}

    void* stack_offset;
    //if((int) (code_offset-0x10000) > 0)
    //    stack_offset = (void*) code_offset-1;
    //else
        stack_offset = (void*) 0xC0000000-8194;
    
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
    ((u32) kmalloc(1024, "idle process kernel stack"))+1023;
    #else
    ((u32) kmalloc(1024))+1023;
    #endif
    idle_process->base_stack = idle_process->base_kstack = idle_process->kesp - 1023;
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

    //current_process = kernel_process;
    return kernel_process;
}

void exit_process(process_t* process)
{
    u32 stack_phys = get_physical(process->base_stack, process->page_directory);
    free_block(stack_phys);
    kfree((void*) process->base_kstack);
    pt_free(process->page_directory);
    scheduler_remove_process(process);
}
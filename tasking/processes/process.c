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
#include "tasking/task.h"
#include "filesystem/fs.h"
#include "memory/mem.h"
#include "cpu/cpu.h"

#define PROCESS_STACK_SIZE_DEFAULT 8192
#define PROCESS_KSTACK_SIZE_DEFAULT 8192
#define PROCESS_DEFAULT_THREADS_SIZE 3

#define PROCESSES_ARRAY_SIZE 20
process_t** processes = 0;
u32 processes_size = 0;

process_t* kernel_process = 0;
process_t* idle_process = 0;

static process_t* init_process();

/* init the process layer (called by kmain()) */
void process_init()
{
    kprintf("Initializing process layer...");
    
    processes_size = PROCESSES_ARRAY_SIZE;
    processes = kmalloc(processes_size*sizeof(process_t*));
    memset(processes, 0, processes_size*sizeof(process_t*));

    signals_init();
    groups_init();

    scheduler_init(); //Init scheduler
    init_kernel_process(); //Add kernel process as current_process (kernel init is not done yet)
    init_idle_process(); //Add idle_process to the queue, so that if there is no process the kernel don't crash

    vga_text_okmsg();
}

/* load an elf executable to 'process' memory */
error_t load_executable(process_t* process, fd_t* executable, int argc, char** argv)
{
    //check if the file is really an ELF executable
    error_t elfc = elf_check(executable);
    if(elfc != ERROR_NONE) return elfc;

    list_entry_t* data_loc = kmalloc(sizeof(list_entry_t));
    u32 data_size = 0;
    void* code_offset = (void*) elf_load(executable, process->page_directory, data_loc, &data_size);

    process->data_loc = data_loc;
    process->data_size = data_size;

    if((!code_offset) | (((u32)code_offset) > 0xC0000000)) 
    {kfree(data_loc); return UNKNOWN_ERROR;}

    //TODO: check if this area isnt already mapped by elf code/data
    void* stack_offset = (void*) 0xC0000000;
    
    #ifdef PAGING_DEBUG
    kprintf("%lLOAD_EXECUTABLE : mapping 0x%X (size 0x%X)...\n", 3, stack_offset-PROCESS_STACK_SIZE_DEFAULT, PROCESS_STACK_SIZE_DEFAULT);
    #endif

    map_memory(PROCESS_STACK_SIZE_DEFAULT, (u32) stack_offset-PROCESS_STACK_SIZE_DEFAULT, process->page_directory);
    u32 base_stack = (u32) stack_offset-PROCESS_STACK_SIZE_DEFAULT;
    process->threads[0].base_stack = base_stack;

    /* ARGUMENTS PASSING */
    pd_switch(process->page_directory);
    
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
    for (i=argc; i>=0; i--)
    {
        stack_offset -= sizeof(char*);
        if(i != argc) *((char**) stack_offset) = uparam[i]; 
        else *((char**) stack_offset) = 0;
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

    //set process heap
    //for now we decide that the last mem segment will be the start of the heap (cause it's easier and i'm lazy)
    list_entry_t* ptr = data_loc;
    u32 dsize = data_size-1;
    while(dsize)
    {
        ptr = ptr->next;
        dsize--;
    }
    process->heap_addr = ((u32*)ptr->element)[0]+((u32*)ptr->element)[1];
    process->heap_size = 0;

    //force general register reset
    process->threads[0].gregs.eax = process->threads[0].gregs.ebx = process->threads[0].gregs.ecx = process->threads[0].gregs.edx = 0;
    process->threads[0].gregs.edi = process->threads[0].gregs.esi = process->threads[0].ebp = 0;
    process->threads[0].sregs.ds = process->threads[0].sregs.es = process->threads[0].sregs.fs = process->threads[0].sregs.gs = process->threads[0].sregs.ss = 0x23;
    process->threads[0].sregs.cs = 0x1B;

    //executable stack and code
    process->threads[0].esp = (u32) stack_offset;
    process->threads[0].eip = (u32) code_offset;

    return ERROR_NONE;
}

/* exit a process and do all the actions (send SIGCHLD, transform zombie, ...) */
void exit_process(process_t* process, u32 exitcode)
{
    //kprintf("%lEXIT(pid %d, code %u)\n", 3, process->pid, exitcode);
    if(process->pid == 1) fatal_kernel_error("Init exited.", "EXIT_PROCESS");

    //TODO : remove non-handled signals from siglist

    free_process_memory(process);

    //free process file descriptors
    u32 i = 0;
    for(i=0;i<process->files_size;i++)
    {
        if(process->files[i]) close_file(process->files[i]);
    }

    //swap process pd to kernel pd (in case of scheduling)
    process->page_directory = kernel_page_directory;

    //free process page directory
    pt_free(process->page_directory);

    //free process children list
    list_entry_t* cptr = process->children;
    while(cptr)
    {
        process_t* cp = cptr->element;
        cp->parent = processes[1]; //all children processes get attached to INIT

        list_entry_t* tf = cptr;
        cptr = cptr->next;
        kfree(tf);
    }

    list_entry_t* gptr = process->group->processes;
    if(gptr->element == process)
    {
        process->group->processes = gptr->next;
        kfree(gptr);
        //TODO : send SIGHUP/SIGCONT if process stopped...
    }
    else
    {
        list_entry_t* gbef = 0;
        while(gptr)
        {
            if(gptr->element == process)
            {
                gbef->next = gptr->next;
                kfree(gptr);
            }
            gbef = gptr;
            gptr = gptr->next;
        }
    }

    /* we put retcode in EAX and the process is zombie */
    process->status = PROCESS_STATUS_ZOMBIE;
    process->threads[process->active_thread].gregs.eax = exitcode;
    
    if(process->parent)
    {
        //send SIGCHLD to parent
        send_signal(process->parent->pid, SIGCHLD);

        //wake up parent if wait()
        if(process->parent->status == PROCESS_STATUS_ASLEEP_CHILD)
            scheduler_add_process(process->parent);
    }

    //free process kernel stack
    kfree((void*) process->threads[process->active_thread].base_kstack);

    //remove process from schedulers
    scheduler_remove_process(process);
}

/* free all memory used by a process (for exec() or exit()) */
void free_process_memory(process_t* process)
{
    u32 threads = process->threads_count;
    process->threads_count = 1;

    u32 i = 0;
    for(i=0; i<threads; i++)
    {
        //mark physical memory reserved for process stack as free
        if(process->threads[i].base_stack)
        {
            u32 stack_phys = get_physical(process->threads[i].base_stack, process->page_directory);

            #ifdef PAGING_DEBUG
            kprintf("%lFREE_PROCESS_MEM(%d): unmapping 0x%X (size 0x%X)\n", 3, process->pid, process->threads[i].base_stack, PROCESS_STACK_SIZE_DEFAULT);
            #endif

            unmap_flexible(PROCESS_STACK_SIZE_DEFAULT, process->threads[i].base_stack, process->page_directory);
            free_block(stack_phys);
            process->threads[i].base_stack = 0;
        }
        if(i != process->active_thread)
        {
            kfree((void*) process->threads[i].base_kstack);
        }
    }

    //mark all data/code blocks reserved for the process as free
    if(process->data_loc)
    {
        u32 i = 0;
        list_entry_t* dloc = process->data_loc;
        list_entry_t* ptr = dloc;
        for(i=0;i<process->data_size;i++)
        {
            u32 phys = get_physical(((u32*)ptr->element)[0], process->page_directory);

            #ifdef PAGING_DEBUG
            kprintf("%lFREE_PROCESS_MEM: unmapping 0x%X (size 0x%X)\n", 3, ((u32*)ptr->element)[0], ((u32*)ptr->element)[1]);
            #endif

            unmap_flexible(((u32*)ptr->element)[1], ((u32*)ptr->element)[0], process->page_directory);
            aligndown(phys, 4096);
            free_block(phys);
            ptr = ptr->next;
        }
        list_free(dloc, process->data_size);
        process->data_loc = 0;
    }

    //free process heap
    #ifdef PAGING_DEBUG
    kprintf("%lFREE_PROCESS_MEM: unmapping if 0x%X (size 0x%X).\n", 3, process->heap_addr, process->heap_size);
    #endif
    if(process->heap_size) unmap_memory_if_mapped(process->heap_size, process->heap_addr, process->page_directory);
}

/* expands process allocated memory (heap) */
u32 sbrk(process_t* process, u32 incr)
{
    u32 new_last_addr = process->heap_addr+process->heap_size+incr;
    if(!is_mapped(new_last_addr, process->page_directory))
    {
        #ifdef PAGING_DEBUG
        kprintf("%lSBRK: mappping if not 0x%X (size 0x%X)...\n", 3, process->heap_addr+process->heap_size, incr);
        #endif
        
        map_memory_if_not_mapped(incr, process->heap_addr+process->heap_size, process->page_directory);        
    }
    process->heap_size += incr;
    return process->heap_addr+process->heap_size;
}

/* create a new process by copying the current one */
process_t* fork(process_t* old_process, u32 old_esp)
{
    process_t* tr = init_process();

    u32 base_kstack = tr->threads[0].base_kstack;
    //copy active thread from old process
    memcpy(&tr->threads[0], &old_process->threads[old_process->active_thread], sizeof(thread_t));
    tr->threads[0].base_kstack = base_kstack;

    //get own copy of data_loc
    tr->flags = old_process->flags;
    tr->data_loc = kmalloc(sizeof(list_entry_t));
    list_copy(tr->data_loc, old_process->data_loc, old_process->data_size, sizeof(u32)*3);
    tr->data_size = old_process->data_size;
    tr->heap_addr = old_process->heap_addr;
    tr->heap_size = old_process->heap_size;
    tr->tty = old_process->tty;

    //copy signal handlers
    memcpy(tr->signal_handlers, old_process->signal_handlers, NSIG*sizeof(void*));
    //copy current dir
    memcpy(tr->current_dir, old_process->current_dir, 100);

    //get own adress space
    u32* page_directory = copy_adress_space(old_process->page_directory);
    tr->page_directory = page_directory;

    //copy kernel stack
    memcpy((void*) base_kstack, (void*) old_process->threads[old_process->active_thread].base_kstack, PROCESS_KSTACK_SIZE_DEFAULT);

    //set registers to match current status (kernel space)
    tr->threads[0].sregs.cs = 0x08;
    tr->threads[0].sregs.ss = 0x10;
    tr->threads[0].eip = (uintptr_t) fork_ret;
    tr->threads[0].esp = old_esp - old_process->threads[old_process->active_thread].base_kstack + base_kstack;
    tr->threads[0].kesp = base_kstack + PROCESS_KSTACK_SIZE_DEFAULT;

    //get own copies of file_descriptors
    tr->files_size = old_process->files_size;
    tr->files_count = old_process->files_count;
    tr->files = kmalloc(old_process->files_size*sizeof(fd_t));
    u32 i = 0;
    for(;i<old_process->files_size;i++)
    {
        fd_t* tocopy = old_process->files[i];
        if(!tocopy) continue;
        
        fd_t* toadd = kmalloc(sizeof(fd_t));
        memcpy(toadd, tocopy, sizeof(fd_t));
        tr->files[i] = toadd;
    }

    scheduler_add_process(tr);

    return tr;
}

/* init a basic process_t, registering it in process list, and setting base signals */
static process_t* init_process()
{
    process_t* tr = kmalloc(sizeof(process_t));
    tr->status = PROCESS_STATUS_INIT;

    //process main thread
    tr->threads = kmalloc(sizeof(thread_t)*PROCESS_DEFAULT_THREADS_SIZE);
    tr->threads_size = PROCESS_DEFAULT_THREADS_SIZE;
    memset(tr->threads, 0, sizeof(thread_t)*PROCESS_DEFAULT_THREADS_SIZE);
    tr->threads_count = 1;
    tr->active_thread = 0;

    //process kernel stack
    void* kstack = kmalloc(PROCESS_KSTACK_SIZE_DEFAULT);
    tr->threads[0].kesp = ((u32) kstack) + PROCESS_KSTACK_SIZE_DEFAULT;
    tr->threads[0].base_kstack = (u32) kstack;

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
        processes = krealloc(processes, processes_size*sizeof(process_t*));
        processes[j+1] = tr; tr->pid = (j+1);
    }

    //register process in a group and session
    if((current_process) && (current_process != kernel_process)) 
    {
        tr->group = current_process->group; 
        //add to the group list
        list_entry_t** ptr = &tr->group->processes;
        while(*ptr) ptr = &((*ptr)->next);
        (*ptr) = kmalloc(sizeof(list_entry_t));
        (*ptr)->next = 0;
        (*ptr)->element = tr;
        tr->session = current_process->session;
    }

    //register as children of current process
    tr->children = 0;
    if(current_process && current_process != kernel_process)
    {
        list_entry_t** child = &current_process->children;
        while(*(child)) child = &(*child)->next;
        (*child) = kmalloc(sizeof(list_entry_t));
        (*child)->element = tr;
        (*child)->next = 0;
        tr->parent = current_process;
    }
    else tr->parent = 0;

    return tr;
}

/* INIT, KERNEL and IDLE processes */

error_t spawn_init_process()
{
    //open init file
    fd_t* init_file = open_file("/sys/init", OPEN_MODE_R);
    if(!init_file) return ERROR_FILE_NOT_FOUND;

    process_t* tr = init_process();

    //allocate page directory
    u32* page_directory = get_kernel_pd_clone();
    tr->page_directory = page_directory;

    //load ELF executable
    error_t elf = load_executable(tr, init_file, 0, 0);

    //close init file
    close_file(init_file);

    if(elf != ERROR_NONE) return elf;

    //create default session and group
    psession_t* session = kmalloc(sizeof(psession_t));
    session->controlling_tty = 0;
    session->groups = kmalloc(sizeof(list_entry_t));
    session->groups->next = 0;
    pgroup_t* group = kmalloc(sizeof(pgroup_t));
    group->gid = 1;
    group->processes = kmalloc(sizeof(list_entry_t));
    group->processes->next = 0;
    group->processes->element = tr;
    group->session = session;
    memcpy(groups, group, sizeof(pgroup_t));
    groups_number++;
    session->groups->element = group;
    tr->group = group;
    tr->session = session;

    //set default registers to 0
    tr->threads[0].gregs.eax = 0;
    tr->threads[0].gregs.ebx = 0;
    tr->threads[0].gregs.ecx = 0;
    tr->threads[0].gregs.edx = 0;
    tr->threads[0].gregs.esi = 0;
    tr->threads[0].gregs.edi = 0;
    tr->threads[0].ebp = 0;
    
    //set flags (just copy from current flags)
    tr->flags = 0; asm("pushf; pop %%eax":"=a"(tr->flags):);

    //set default segment registers
    tr->threads[0].sregs.ds = tr->threads[0].sregs.es = tr->threads[0].sregs.fs = tr->threads[0].sregs.gs = tr->threads[0].sregs.ss = 0x23;
    tr->threads[0].sregs.cs = 0x1B;

    //init process file array
    tr->files_size = 5;
    tr->files = kmalloc(tr->files_size*sizeof(fd_t));
    memset(tr->files, 0, sizeof(fd_t)*tr->files_size);

    //set tty
    tr->tty = tty1;
    session->controlling_tty = tty1;
    tty1->session = session;
    tty1->foreground_processes = group;

    //init stdin, stdout, stderr
    fd_t* std = kmalloc(sizeof(fd_t)); std->offset = 0; std->file = tty1->pointer;
    tr->files[0] = std; //stdin
    tr->files[1] = std; //stdout
    tr->files[2] = std; //stderr
    tr->files_count = 3;

    //register default signals handler
    memset(tr->signal_handlers, 0, NSIG*sizeof(void*));

    //set current dir
    strcpy(tr->current_dir, "/home");
    *(tr->current_dir+5) = 0;

    //add process to scheduler
    scheduler_add_process(tr);

    return ERROR_NONE;
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
    idle_process->active_thread = 0;
    idle_process->threads_count = 1;
    idle_process->threads_size = 1;
    idle_process->threads = kmalloc(sizeof(thread_t));
    idle_process->flags = 0; asm("pushf; pop %%eax":"=a"(idle_process->flags):);
    idle_process->threads[0].gregs.eax = idle_process->threads[0].gregs.ebx = idle_process->threads[0].gregs.ecx = idle_process->threads[0].gregs.edx = 0;
    idle_process->threads[0].gregs.edi = idle_process->threads[0].gregs.esi = idle_process->threads[0].ebp = 0;
    idle_process->threads[0].sregs.ds = idle_process->threads[0].sregs.es = idle_process->threads[0].sregs.fs = idle_process->threads[0].sregs.gs = idle_process->threads[0].sregs.ss = 0x10;
    idle_process->threads[0].sregs.cs = 0x08;
    idle_process->threads[0].eip = (u32) idle_loop; //IDLE LOOP
    idle_process->threads[0].esp = idle_process->threads[0].kesp = 
    #ifdef MEMLEAK_DBG
    ((u32) kmalloc(1024, "idle process kernel stack"))+1024;
    #else
    ((u32) kmalloc(1024))+1024;
    #endif
    idle_process->threads[0].base_stack = idle_process->threads[0].base_kstack = idle_process->threads[0].kesp - 1024;
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
    kernel_process->threads = kmalloc(sizeof(thread_t));
    kernel_process->threads_size = 1;
    kernel_process->threads_count = 1;
    kernel_process->active_thread = 0;
    kernel_process->pid = PROCESS_KERNEL_PID;
    kernel_process->page_directory = kernel_page_directory;
    kernel_process->status = PROCESS_STATUS_RUNNING;

    current_process = kernel_process;

    return kernel_process;
}

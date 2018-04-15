/*  
    This file is part of VK.
    Copyright (C) 2018 Valentin Haudiquet

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

#include "tasking/task.h"

thread_t* init_thread(process_t* process)
{
    thread_t* thread = kmalloc(sizeof(thread_t));
    memset(thread, 0, sizeof(thread_t));

    void* kstack = kmalloc(PROCESS_KSTACK_SIZE_DEFAULT);
    thread->kesp = ((u32) kstack) + PROCESS_KSTACK_SIZE_DEFAULT;
    thread->base_kstack = (u32) kstack;
    
    thread->status = THREAD_STATUS_RUNNING;

    if(process->active_thread) queue_add(process->running_threads, thread);
    else process->active_thread = thread;

    return thread;
}

void free_thread_memory(process_t* process, thread_t* thread)
{
    if(thread->base_stack)
    {
        u32 stack_phys = get_physical(thread->base_stack, process->page_directory);
        
        #ifdef PAGING_DEBUG
        kprintf("%lFREE_THREAD_MEM(pid %d): unmapping 0x%X (size 0x%X)\n", 3, process->pid, thread->base_stack, PROCESS_STACK_SIZE_DEFAULT);
        #endif

        unmap_flexible(PROCESS_STACK_SIZE_DEFAULT, thread->base_stack, process->page_directory);
        free_block(stack_phys);
        thread->base_stack = 0;
    }

    /* we can't free kernel stack if we are on active thread of current process */
    if((process != current_process) | (thread != process->active_thread))
    {
        kfree((void*) thread->base_kstack);
    }
}

void scheduler_remove_thread(process_t* process, thread_t* thread)
{
    if(process->status != PROCESS_STATUS_RUNNING) return;

    /* adding the thread to remove on waiting_threads list */
    list_entry_t** ptr = &process->waiting_threads;
    while(*ptr) ptr = &((*ptr)->next);
    (*ptr) = kmalloc(sizeof(list_entry_t));
    (*ptr)->next = 0;
    (*ptr)->element = thread;

    /* if currentprocess activethread, save context */
    if((process == current_process) && (thread == process->active_thread))
    {
        thread_t* next = queue_take(process->running_threads);

        __asm__ __volatile__("mov %%ebx, %0":"=m"(current_process->active_thread->gregs.ebx));
        __asm__ __volatile__("mov %%edi, %0":"=m"(current_process->active_thread->gregs.edi));
        __asm__ __volatile__("mov %%esi, %0":"=m"(current_process->active_thread->gregs.esi));
        __asm__ __volatile__("mov %%ebp, %0":"=m"(current_process->active_thread->ebp));
        //save segment registers
        __asm__ __volatile__ ("mov %%ds, %0 ; mov %%es, %1 ; mov %%fs, %2 ; mov %%gs, %3":"=m"(current_process->active_thread->sregs.ds), "=m"(current_process->active_thread->sregs.es), "=m"(current_process->active_thread->sregs.fs), "=m"(current_process->active_thread->sregs.gs));
        
        //cs/ss values are obvious cause we are in kernel context
        current_process->active_thread->sregs.ss = 0x10;
        current_process->active_thread->sregs.cs = 0x08;

        //set eip to the end of this void
        current_process->active_thread->eip = (u32) &&srt_end;
        __asm__ __volatile__("mov %%esp, %%eax":"=a"(current_process->active_thread->esp)::); //save esp at last moment
    
        /* if no more threads, remove process */
        //TODO : CRITICAL, don't schedule from that
        process->active_thread = next;
        if(!next) 
        {
            process->status = PROCESS_STATUS_ASLEEP_THREADS;
            scheduler_remove_process(process);
        }
        /* else schedule to that thread */
        else
        {
            __asm__ __volatile__("jmp schedule_switch"::"a"(process->active_thread), "d"(process));
        }

        srt_end: return;
    }
    else queue_remove(process->running_threads, thread);
}

void scheduler_add_thread(process_t* process, thread_t* thread)
{
    if(thread->status == THREAD_STATUS_RUNNING) return;

    /* remove thread from waiting list */
    list_entry_t* ptr = process->waiting_threads;
    list_entry_t* bef = 0;
    while(ptr)
    {
        if(ptr->element == thread)
        {
            if(bef) bef->next = ptr->next;
            else process->waiting_threads = 0;
            kfree(ptr);
        }
        bef = ptr;
        ptr = ptr->next;
    }

    /* if thread was the only thread of the process, waking up */
    if((!process->active_thread) && (process->status == PROCESS_STATUS_ASLEEP_THREADS))
    {
        thread->status = THREAD_STATUS_RUNNING;
        process->active_thread = thread;
        scheduler_add_process(process);
    }
    else queue_add(process->running_threads, thread);
}

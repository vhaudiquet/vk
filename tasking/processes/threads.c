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

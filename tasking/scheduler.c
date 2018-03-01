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

#include "task.h"
#include "memory/mem.h"
#include "cpu/cpu.h"
#include "sync/sync.h"

bool scheduler_started = false;
process_t* current_process = 0;
queue_t* p_ready_queue = 0;
list_entry_t* irq_list[21] = {0};
dlist_entry_t* wait_list = 0;
//mutex_t wait_mutex = 0;

/*
* Initializes the data structures needed by the scheduler
*/
void scheduler_init()
{
    p_ready_queue = queue_init(10);
    //wait_mutex = mutex_alloc(); *wait_mutex = 0;
}

/*
* Start the scheduler, by activating interrupts
*/
void scheduler_start()
{
    kprintf("Starting scheduler...");
    scheduler_started = true;
    asm("sti");
    vga_text_okmsg();
}

/*
* Add a process to the scheduler
*/
void scheduler_add_process(process_t* process)
{
    if((process != idle_process) && (process->status == PROCESS_STATUS_RUNNING)) return;
    process->status = PROCESS_STATUS_RUNNING;
    queue_add(p_ready_queue, process);
}

/*
* Remove an active process from the scheduler
*/
void scheduler_remove_process(process_t* process)
{
    process->status = PROCESS_STATUS_ASLEEP;
    if(current_process == process)
    {
        //this method was called by this process to pause himself
        //once he returns active, the context must be the end of this void
        //so we can just save the current context with eip = end of scheduler_remove_process
        //and we are good

        //following convention, functions can trash eax/ebx/ecx/edx, so we dont care about theses
        current_process->gregs.eax = current_process->gregs.ebx = current_process->gregs.ecx = current_process->gregs.edx = 0;
        //we need to save esi/edi, and to keep something (stack frame) on ebp (but we dont really care)
        __asm__ __volatile__("mov %%edi, %0":"=m"(current_process->gregs.edi));
        __asm__ __volatile__("mov %%esi, %0":"=m"(current_process->gregs.esi));
        __asm__ __volatile__("mov %%ebp, %0":"=m"(current_process->ebp));
        //save segment registers
        __asm__ __volatile__ ("mov %%ds, %0 ; mov %%es, %1 ; mov %%fs, %2 ; mov %%gs, %3":"=m"(current_process->sregs.ds), "=m"(current_process->sregs.es), "=m"(current_process->sregs.fs), "=m"(current_process->sregs.gs));
        
        //cs/ss values are obvious cause we are in kernel context
        current_process->sregs.ss = 0x10;
        current_process->sregs.cs = 0x08;

        //set eip to the end of this void
        current_process->eip = (u32) &&rmv;

        //add idle if there are no processes on queu so that the kernel switch to it (except of crashing)
        if(p_ready_queue->rear < p_ready_queue->front)
        {
            scheduler_add_process(idle_process);
        }
        
        __asm__ __volatile__("mov %%esp, %%eax":"=a"(current_process->esp)::); //save esp at last moment

        /* we directly jump to the part of schedule function that switch processes */
        __asm__ __volatile__("pushl p_ready_queue \n \
        call  queue_take \n \
        add $0x4, %esp \n \
        jmp schedule_switch");

        rmv: return;
    }
    else queue_remove(p_ready_queue, process);
}

/*
* Put a process to sleep, either for an ammount of time or to wait an IRQ
*/
void scheduler_wait_process(process_t* process, u8 sleep_reason, u16 sleep_data, u16 wait_time)
{
    //if(mutex_lock(wait_mutex) != ERROR_NONE) fatal_kernel_error("Could not lock wait mutex", "SCHEDULER_WAIT_PROCESS");

    /* if we need to sleep a certain ammount of time */
    dlist_entry_t* wait_entry = 0;
    if(wait_time && ((sleep_reason == SLEEP_WAIT_IRQ) | (sleep_reason == SLEEP_TIME)))
    {
        dlist_entry_t* ptr = wait_list;
        dlist_entry_t* last = 0;
        u32 cumulated_time = wait_time;

        /* we have to calculate the right place of this process in the list by calculating cumulated time */
        while(ptr && cumulated_time)
        {
            //getting the time of this list element
            u32 element_time = ((u32*)ptr->element)[1];
            //if the cumulated time of this list element is less than our process, we break : the entry needs to be right before that
            if(cumulated_time < element_time) break;
            //recalculate cumulated time of our process
            cumulated_time -= element_time; 
            //iterate through the list
            last = ptr;
            ptr = ptr->next;
        }

        //found a place in the list, allocate entry and put it
        wait_entry = kmalloc(sizeof(dlist_entry_t));
        if(!last) {wait_list = wait_entry; ptr = 0;}
        else {last->next = wait_entry;}
        wait_entry->next = ptr;
        wait_entry->prev = last;

        //allocate space for time/process and put it into the entry
        uintptr_t* element = kmalloc(sizeof(uintptr_t)*2);
        element[0] = (uintptr_t) process;
        element[1] = wait_time;
        wait_entry->element = element;
    }

    /* if we need to wait for an irq */
    if((sleep_data <= 20) && (sleep_reason == SLEEP_WAIT_IRQ))
    {
        //pointer to the list
        list_entry_t** ptr = &irq_list[sleep_data];
        //while there are elements in the list, we iterate through
        while(*ptr) ptr = &((*ptr)->next);
        //we allocate space and set entry data (our process)
        (*ptr) = kmalloc(sizeof(list_entry_t));
        (*ptr)->next = 0;
        u32* element = kmalloc(sizeof(u32)*2);
        element[0] = (uintptr_t) process;
        element[1] = (uintptr_t) wait_entry;
        (*ptr)->element = element;
    }

    //mutex_unlock(wait_mutex);
    scheduler_remove_process(process);
}

/*
* Update the wait_list of processes, to decrease elapsed time (called by schedule())
*/
void scheduler_sleep_update()
{
    //check if there are processes on the list
    if(!wait_list) return;
    //if(mutex_lock(wait_mutex) != ERROR_NONE) return;

    dlist_entry_t* ptr = wait_list;
    u32* element = ptr->element;
    //remove 55 ms to the top element (schedule is executed ~ every 55 ms)
    if(element[1] > 55) element[1] -= 55;
    //one element as reach 0, put it back in queue
    else
    {
        wait_list = 0;
        do
        {
            scheduler_add_process((process_t*) element[0]);

            dlist_entry_t* to_free = ptr;
            ptr = ptr->next;
            wait_list = ptr;
            kfree(to_free);

            if(!ptr) break;

            element = ptr->element;
        }
        while(!element[1]); //while the elements have a value of 0, we add them on queue
    }

    //mutex_unlock(wait_mutex);
}

/*
* Wake up every process that needed to be on irq x (called by every irq)
* TODO : better algo (use double ptrs)
*/
void scheduler_irq_wakeup(u32 irq)
{
    if(!irq_list[irq]) return;

    /* we need to remove/free every element of the list and add every process to the scheduler */
    u32* element = irq_list[irq]->element;

    if(element[1])
    {
        dlist_entry_t* wlist_entry = (dlist_entry_t*) element[1];
        if(wlist_entry == wait_list) wait_list = wlist_entry->next;
        if(wlist_entry->prev) wlist_entry->prev->next = wlist_entry->next;
        if(wlist_entry->next) wlist_entry->next->prev = wlist_entry->prev;
        kfree(wlist_entry->element); kfree(wlist_entry);
    }
    scheduler_add_process((process_t*) element[0]);
    list_entry_t* ptr = irq_list[irq]->next;
    kfree(irq_list[irq]->element); kfree(irq_list[irq]); irq_list[irq] = 0;
    while(ptr)
    {
        element = ptr->element;
        if(element[1])
        {
            dlist_entry_t* wlist_entry = (dlist_entry_t*) element[1];
            if(wlist_entry == wait_list) wait_list = wlist_entry->next;
            else if(wlist_entry->prev) wlist_entry->prev->next = wlist_entry->next;
            if(wlist_entry->next) wlist_entry->next->prev = wlist_entry->prev;
            kfree(wlist_entry->element); kfree(wlist_entry);
        }
        scheduler_add_process((process_t*) element[0]);
        list_entry_t* to_free = ptr;
        ptr = ptr->next;
        kfree(to_free->element);
        kfree(to_free);
    }
}

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
list_entry_t* wait_list = 0;
//mutex_t wait_mutex = 0;

/*
* Initializes the data structures needed by the scheduler
*/
void scheduler_init()
{
    p_ready_queue = queue_init(10);
    //kmalloc(4);
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
        current_process->gregs.eax = current_process->gregs.ebx = current_process->gregs.ecx = current_process->gregs.edx = 0;
        current_process->gregs.edi = current_process->gregs.esi = current_process->ebp = 0;
        current_process->sregs.ds = current_process->sregs.es = current_process->sregs.fs = current_process->sregs.gs = current_process->sregs.ss = 0x10;
        current_process->sregs.cs = 0x08;
        current_process->eip = (u32) &&rmv;

        if(p_ready_queue->rear < p_ready_queue->front)
        {
            scheduler_add_process(idle_process);
        }
        
        __asm__ __volatile__("mov %%esp, %%eax":"=a"(current_process->esp)::); //save esp at last moment
        current_process = 0; //remove current process so that it doesnt get re-saved and put back on queue
        
        asm("int $32"); //call clock int to schedule
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
    if(wait_time && ((sleep_reason == SLEEP_WAIT_IRQ) | (sleep_reason == SLEEP_TIME)))
    {
        list_entry_t* ptr = wait_list;
        list_entry_t* last = 0;
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
        list_entry_t* entry = kmalloc(sizeof(list_entry_t));
        if(!last) {wait_list = entry; ptr = 0;}
        else {last->next = entry;}
        entry->next = ptr;

        //allocate space for time/process and put it into the entry
        uintptr_t* element = kmalloc(sizeof(uintptr_t)*2);
        element[0] = (uintptr_t) process;
        element[1] = wait_time;
        entry->element = element;
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
        (*ptr)->element = process;
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

    list_entry_t* ptr = wait_list;
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

            list_entry_t* to_free = ptr;
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
*/
void scheduler_irq_wakeup(u32 irq)
{
    if(!irq_list[irq]) return;

    /* we need to remove/free every element of the list and add every process to the scheduler */
    scheduler_add_process((process_t*) irq_list[irq]->element);
    list_entry_t* ptr = irq_list[irq]->next;
    kfree(irq_list[irq]); irq_list[irq] = 0;
    while(ptr)
    {
        scheduler_add_process((process_t*) ptr->element);
        list_entry_t* to_free = ptr;
        ptr = ptr->next;
        kfree(to_free);
    }
}

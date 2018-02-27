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

/*
* First SCHEDULER implementation ; schedule() is called on each clock interrupt (PIT is set by the BIOS to 1 interrupt / 54.9254 ms)
*/

typedef struct ASLEEP_PROCESS_D
{
    process_t* process;
    u16 sleep_data;
    u16 sleep_data_2;
    u8 sleep_reason;
} asleep_data_t;

bool scheduler_started = false;
process_t* current_process = 0;
list_entry_t* p_wait_list = 0; u32 p_wl_size = 0;
queue_t* p_ready_queue = 0;

void scheduler_init()
{
    p_ready_queue = queue_init(10);
}

void scheduler_start()
{
    kprintf("Starting scheduler...");
    scheduler_started = true;
    asm("sti");
    vga_text_okmsg();
}

void scheduler_add_process(process_t* process)
{
    queue_add(p_ready_queue, process);
}

void scheduler_remove_process(process_t* process)
{
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
        asm("mov %%esp, %%eax":"=a"(current_process->esp));
        current_process->eip = (u32) &&rmv; //(scheduler_remove_process+0xe8);//(c0108133);

        current_process = 0;
        if(p_ready_queue->rear < p_ready_queue->front)
        {
            scheduler_add_process(idle_process);
            //scheduler_wake_process(idle_process);
        }
        asm("int $32"); //call clock int to schedule
        //schedule(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0, 0, 0);
        rmv: return;
    }
    else queue_remove(p_ready_queue, process);
}

void scheduler_wait_process(process_t* process, u8 sleep_reason, u16 sleep_data, u16 sleep_data_2)
{
    list_entry_t* list_pointer = p_wait_list;
    list_entry_t* list_before = 0;
    u32 i = 0;
    for(i = 0; i < p_wl_size; i++)
    {
        list_before = list_pointer;
        list_pointer = list_pointer->next;
    }
    if(i == 0) p_wait_list = list_pointer = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(list_entry_t), "scheduler: p_wait_list listentry");
    #else
    kmalloc(sizeof(list_entry_t));
    #endif
    else list_pointer =
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(list_entry_t), "scheduler: p_wait_list listentry");
    #else
    kmalloc(sizeof(list_entry_t));
    #endif
    if(list_before) list_before->next = list_pointer;
    asleep_data_t* pdata = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(asleep_data_t), "scheduler: asleep_process_data");
    #else
    kmalloc(sizeof(asleep_data_t));
    #endif
    pdata->process = process;
    pdata->sleep_reason = sleep_reason;
    pdata->sleep_data = sleep_data;
    pdata->sleep_data_2 = sleep_data_2;
    list_pointer->element = pdata;
    //kprintf("sleeping process 0x%X ; reason %u (data %u)\n",pdata->process, pdata->sleep_reason, pdata->sleep_data);
    p_wl_size++;

    scheduler_remove_process(process);
}

void scheduler_wake_process(process_t* process)
{
    list_entry_t* list_pointer = p_wait_list;
    list_entry_t* list_before = 0;
    for(u32 i = 0; i < p_wl_size; i++)
    {
        asleep_data_t* pdata = list_pointer->element;
        if(pdata->process == process)
        {
            if(list_before) list_before->next = list_pointer->next;
            else p_wait_list = list_pointer->next;
            kfree(list_pointer);
            scheduler_add_process(pdata->process);
            kfree(pdata);
            p_wl_size--;
            return;
        }
        list_before = list_pointer;
        list_pointer = list_pointer->next;
    }
    //not found, failed
    kprintf("%lwake: %lwake failed, not found\n", 3, 2);
}

void scheduler_irq_wakeup(u32 irq)
{
    list_entry_t* list_pointer = p_wait_list;
    list_entry_t* list_before = 0;
    for(u32 i = 0; i < p_wl_size; i++)
    {
        asleep_data_t* pdata = list_pointer->element;
        //kprintf("process 0x%X ; reason = %u ; data = %u (irq = %u)\n", pdata->process, pdata->sleep_reason, pdata->sleep_data, irq);
        if(pdata->sleep_reason == SLEEP_WAIT_IRQ && pdata->sleep_data == irq)
        {
            if(list_before) list_before->next = list_pointer->next;
            kfree(list_pointer);
            scheduler_add_process(pdata->process);
            kfree(pdata);
            p_wl_size--;
        }
        list_before = list_pointer;
        list_pointer = list_pointer->next;
        //kprintf("list_b = 0x%X ; list_point = 0x%X\n", list_before, list_pointer);
    }
    //kprintf("out.\n");
}
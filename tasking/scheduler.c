/*  
    This file is part of VK.

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
* First SCHEDULER implementation ; schedule() is called on each clock interrupt
*/

typedef struct ASLEEP_PROCESS_D
{
    process_t* process;
    u8 sleep_reason;
    u8 sleep_data;
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

process_t* toswitch = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void schedule(u32 gs, u32 fs, u32 es, u32 ds, u32 edi, u32 esi, u32 ebp, u32 esp, u32 ebx, u32 edx, u32 ecx, u32 eax, u32 eip, u32 cs, u32 flags, u32 esp_2, u32 ss)
{
    //if there is no process to schedule, we return
    toswitch = queue_take(p_ready_queue);
    if(!toswitch) return;

    //asm("mov %%ss, %%eax":"=a"(ss));
    //kprintf("schedule : ring %u (cs = 0x%X) (ss = 0x%X)\n", cs & 0x3, cs, ss);

    //if we are currently running idle process, lets throw it away
    /*if(current_process == idle_process)
    {
        list_entry_t* list_pointer = p_wait_list;
        list_entry_t* list_before = 0;
        u32 i = 0;
        for(i = 0; i < p_wl_size; i++)
        {
            list_before = list_pointer;
            //list_pointer = list_pointer->next;
        }
        if(!p_wl_size) p_wait_list = list_pointer =
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(list_entry_t), "scheduler: p_wait_list list entry");
        #else
        kmalloc(sizeof(list_entry_t));
        #endif
        else list_pointer =
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(list_entry_t), "scheduler: p_wait_list list entry");
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
        pdata->process = idle_process;
        pdata->sleep_reason = SLEEP_PAUSED;
        pdata->sleep_data = 0;
        list_pointer->element = pdata;
        p_wl_size++;
    }*/
    //else save the context of current process and put in back in queue
    else if(current_process)
    {
        //save context of current process
        current_process->cs  = cs;
        current_process->eip = eip;
        current_process->gregs.eax = eax;
        current_process->gregs.ecx = ecx;
        current_process->gregs.edx = edx;
        current_process->gregs.ebx = ebx;
        current_process->ebp = ebp;
        current_process->gregs.esi = esi;
        current_process->gregs.edi = edi;

        //if system call, theses are not pushed on the stack
        if (current_process->cs != 0x08) 
        {
            current_process->esp = esp_2;
            //current_process->sregs.ss = ss;
        }
        else 
        {
            current_process->esp = esp+0xC;
            //kesp saving : why ? why not ? both seems to work, i'm so confused (maybe saving it and adding 0xC create stack corruption)
            //current_process->kesp = esp+0xC;
            //kprintf("%lSaving p esp : 0x%X\n", 1, esp);
        }

        //current_process->flags = flags;

        //queue it
        queue_add(p_ready_queue, current_process);
    }

    //set current_process = toswitch process
    current_process = toswitch;

    /*restore context of toswitch process and return*/
    //set TSS value
    TSS.esp0 = current_process->kesp;

    //page directory switch
    pd_switch(current_process->page_directory);

    //cleaning PIC interrupt
    outb(0x20, 0x20);

    //DEBUG
    //kprintf("%lgoing to eip 0x%X cs 0x%X esp 0x%X %s\n", 3, current_process->eip, current_process->cs, current_process->esp, current_process == idle_process ? "(idle)" : "");
    //kprintf("%leax 0x%X ebx 0x%X ecx 0x%X edx 0x%X edi 0x%X esi 0x%X ebp 0x%X\n", 3, current_process->gregs.eax, current_process->gregs.ebx, current_process->gregs.ecx, current_process->gregs.edx, current_process->gregs.edi, current_process->gregs.esi, current_process->ebp);

    //restore segments register
    asm("mov %0, %%ds \n \
    mov %0, %%es \n \
	mov %0, %%fs \n \
    mov %0, %%gs \n"::"r"((current_process->cs == 0x08 ? 0x10 : 0x23)));

    //if we return to a system call, we directly restore the stack
    if(current_process->cs == 0x08) 
    {
        __asm__ __volatile__ ("mov %0, %%esp"::"g"(current_process->esp):"%esp");
    }
    else
    //if we return to normal execution, we let iret do the stack switch
    {
        __asm__ __volatile__("pushl %0\n \
             pushl %1\n"::"g"(0x23), "g"(current_process->esp));
    }

    //pushing flags, cs and eip (to be popped out by iret)
    __asm__ __volatile__("pushfl\n \
         popl %%eax        \n \
         orl $0x200, %%eax \n \
         and $0xffffbfff, %%eax \n \
         push %%eax        \n \
         pushl %0 \n \
         pushl %1 \n"::"g"(current_process->cs), "g"(current_process->eip):"%eax");

    //restore general registers (esi, edi, ebp, eax, ebx, ecx, edx)
    __asm__ __volatile__ ("mov %0, %%esi ; mov %1, %%edi ; mov %2, %%ebp"::"g"(current_process->gregs.esi), "g"(current_process->gregs.edi), "g"(current_process->ebp));
    __asm__ __volatile__ ("iret"::"a"(current_process->gregs.eax), "b"(current_process->gregs.ebx), "c"(current_process->gregs.ecx), "d"(current_process->gregs.edx));
    //black magic : the end of function is dead code
    //that cause a corruption of 0xC bytes on the stack, that is handled in current_process saving
}
#pragma GCC diagnostic pop

void scheduler_add_process(process_t* process)
{
    if(!current_process)
    {
        current_process = process;
    }
    else queue_add(p_ready_queue, process);
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
        //current_process->sregs.ds = current_process->sregs.es = current_process->sregs.fs = current_process->sregs.gs = current_process->sregs.ss = 0x10;
        current_process->cs = 0x08;
        asm("mov %%esp, %%eax":"=a"(current_process->esp));
        current_process->eip = (u32) (scheduler_remove_process+0xc5);//(0xc0107bb5);

        current_process = 0;
        if(p_ready_queue->rear < p_ready_queue->front)
        {
            //scheduler_add_process(idle_process);
            scheduler_wake_process(idle_process);
        }
        //asm("int $32"); //call clock int to schedule
        schedule(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0, 0, 0);
    }
    else queue_remove(p_ready_queue, process);
}

void scheduler_wait_process(process_t* process, u8 sleep_reason, u8 sleep_data)
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
        }
        list_before = list_pointer;
        list_pointer = list_pointer->next;
    }
    //not found, failed
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
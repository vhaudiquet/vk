#include "task.h"
#include "memory/mem.h"
#include "cpu/cpu.h"

typedef struct ASLEEP_PROCESS_D
{
    process_t* process;
    u8 sleep_reason;
    u8 sleep_data;
} asleep_data_t;

process_t* current_process = 0;
list_entry_t* p_wait_list = 0; u32 p_wl_size = 0;
queue_t* p_ready_queue = 0;

void scheduler_init()
{
    p_ready_queue = queue_init(10);
}

//u32 time = 0;
void schedule(u32 gs, u32 fs, u32 es, u32 ds, u32 edi, u32 esi, u32 ebp, u32 esp, u32 ebx, u32 edx, u32 ecx, u32 eax, u32 eip, u32 cs, u32 flags, u32 esp_2, u32 ss)
{
    //time++;
    //if(time % 25) return;

    //if there is no process to schedule, we return
    process_t* toswitch = queue_take(p_ready_queue);
    if(!toswitch) return;

    //asm("mov %%ss, %%eax":"=a"(ss));
    //kprintf("schedule : ring %u (cs = 0x%X) (ss = 0x%X)\n", cs & 0x3, cs, ss);
    //if we are currently running idle process, lets throw it away
    if(current_process == idle_process)
    {
        //kprintf("throwing kernel process away...\n");
        list_entry_t* list_pointer = p_wait_list;
        list_entry_t* list_before = 0;
        u32 i = 0;
        for(i = 0; i < p_wl_size; i++)
        {
            list_before = list_pointer;
            list_pointer = list_pointer->next;
        }
        if(i == 0) p_wait_list = list_pointer = kmalloc(sizeof(list_entry_t));
        else list_pointer = kmalloc(sizeof(list_entry_t));
        if(list_before) list_before->next = list_pointer;
        asleep_data_t* pdata = kmalloc(sizeof(asleep_data_t));
        pdata->process = current_process;
        pdata->sleep_reason = SLEEP_PAUSED;
        pdata->sleep_data = 0;
        list_pointer->element = pdata;
        p_wl_size++;
    }
    //else save the context of current process and put in back in queue
    else if(current_process)
    {
        //save context of current process
        current_process->sregs.cs  = cs;
        current_process->eip = eip;
        current_process->gregs.eax = eax;
        current_process->gregs.ecx = ecx;
        current_process->gregs.edx = edx;
        current_process->gregs.ebx = ebx;
        current_process->ebp = ebp;
        current_process->gregs.esi = esi;
        current_process->gregs.edi = edi;
        current_process->sregs.ds = ds;
        current_process->sregs.es = es;
        current_process->sregs.fs = fs;
        current_process->sregs.gs = gs;

        //if system call, theses are not pushed on the stack
        if (current_process->sregs.cs != 0x08) 
        {
            current_process->esp = esp_2;
            current_process->sregs.ss = ss;
        }
        else 
        {
            current_process->esp = esp;//stack_ptr[9] + 12;
            current_process->sregs.ss = TSS.ss0;
        }

        //current_process->flags = flags;

        //queue it
        queue_add(p_ready_queue, current_process);
    }

    //set current_process = toswitch process
    current_process = toswitch;

    /*restore context of toswitch process and return*/
    //set TSS values
    TSS.esp0 = current_process->kesp;
    TSS.ss0 = current_process->kss;

    //page directory switch
    pd_switch(current_process->page_directory);

    //cleaning PIC interrupt
    outb(0x20, 0x20);

    //DEBUG
    kprintf("going to eip 0x%X cs 0x%X esp 0x%X ss 0x%X\n", current_process->eip, current_process->sregs.cs, current_process->esp, current_process->sregs.ss);
    //kprintf("eax 0x%X ebx 0x%X ecx 0x%X edx 0x%X edi 0x%X esi 0x%X ebp 0x%X\n", current_process->gregs.eax, current_process->gregs.ebx, current_process->gregs.ecx, current_process->gregs.edx, current_process->gregs.edi, current_process->gregs.esi, current_process->ebp);
    
    //restore segments register
    asm("mov %0, %%ds \n \
    mov %1, %%es \n \
	mov %2, %%fs \n \
    mov %3, %%gs \n"::"r"(current_process->sregs.ds), "r"(current_process->sregs.es), "r"(current_process->sregs.fs), "r"(current_process->sregs.gs));

    //if we return to normal execution, we let iret do the stack switch
    if(current_process->sregs.cs != 0x08)
    {
        asm("pushl %0\n \
             pushl %1\n"::"r"(current_process->sregs.ss), "r"(current_process->esp));
    }
    //if we return to a system call, we directly restore the stack
    else asm("mov %0, %%esp"::"r"(current_process->esp));
    
    //pushing flags, cs and eip (to be popped out by iret)
    asm("pushfl\n \
         popl %%eax        \n \
         orl $0x200, %%eax \n \
         and $0xffffbfff, %%eax \n \
         push %%eax        \n \
         pushl %0\n \
         pushl %1 \n"::"r"(current_process->sregs.cs), "r"(current_process->eip):"%eax");

    //restore general registers (esi, edi, ebp, eax, ebx, ecx, edx)
    asm("mov %0, %%esi ; mov %1, %%edi ; mov %2, %%ebp"::"r"(current_process->gregs.esi), "r"(current_process->gregs.edi), "r"(current_process->ebp));
    asm("nop"::"a"(current_process->gregs.eax), "b"(current_process->gregs.ebx), "c"(current_process->gregs.ecx), "d"(current_process->gregs.edx));

    //black magic
    asm("iret");
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
        current_process->eip = (u32) (scheduler_remove_process+0xbd);//(0xc0107bb5);

        current_process = 0;
        if(p_ready_queue->rear < p_ready_queue->front)
        {
            scheduler_wake_process(idle_process);
        }
        asm("int $32"); //call clock int to schedule
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
    if(i == 0) p_wait_list = list_pointer = kmalloc(sizeof(list_entry_t));
    else list_pointer = kmalloc(sizeof(list_entry_t));
    if(list_before) list_before->next = list_pointer;
    asleep_data_t* pdata = kmalloc(sizeof(asleep_data_t));
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
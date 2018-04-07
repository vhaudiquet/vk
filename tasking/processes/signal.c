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
#include "sync/sync.h"

static void handle_signal(process_t* process, int sig);

/* default signal actions ; 1=EXIT, 2=IGNORE, 3=CONTINUE, 4=STOP */
static int default_action[] = {1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1, 4, 1, 4, 4, 4, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1};

list_entry_t* signal_list = 0;
mutex_t signal_mutex = 0;

void signals_init()
{
    signal_mutex = mutex_alloc(); *signal_mutex = 0;
}

/*
* This method is called by the scheduler every schedule
* It looks at the signal queue and handle every signal in it
*/
void handle_signals()
{
    if(!signal_list) return;

    if(mutex_lock(signal_mutex) != ERROR_NONE) return;

    list_entry_t* ptr = signal_list;
    while(ptr)
    {
        u32* element = ptr->element;
        handle_signal((process_t*) element[0], (int) element[1]);
        list_entry_t* tofree = ptr;
        ptr = ptr->next;
        kfree(tofree->element);
        kfree(tofree);
    }
    signal_list = 0;

    mutex_unlock(signal_mutex);
}

asm(".global sighandler_end \n \
sighandler_end: \n \
mov $21, %eax \n \
int $0x80 \n \
sighandler_end_end: \n \
.global sighandler_end_end");
extern void sighandler_end();
extern void sighandler_end_end();

/*
* Handle a signal
*/
static void handle_signal(process_t* process, int sig)
{
    void* handler = process->signal_handlers[sig];
    
    /* SIG_DFL */
    if(!handler)
    {
        if(default_action[sig] == 1)
        {
            exit_process(process, EXIT_CONDITION_SIGNAL | ((u8) sig));
        }
        else if(default_action[sig] == 2) return;
        else if(default_action[sig] == 3)
        {
            if(process->status == PROCESS_STATUS_ASLEEP_SIGNAL)
            {
                scheduler_add_process(process);
            }
        }
        else if(default_action[sig] == 4)
        {
            process->status = PROCESS_STATUS_ASLEEP_SIGNAL;
            scheduler_remove_process(process);
        }
    }
    /* SIG_IGN */
    else if(((uintptr_t) handler) == 1) return;
    /* custom signal handling function */
    else
    {
        process->sighandler.eip = (uintptr_t) process->signal_handlers[sig];
        process->sighandler.base_kstack = (uintptr_t) kmalloc(4096);
        process->sighandler.kesp = process->sighandler.base_kstack+4096;
        process->sighandler.esp = process->esp - 0x10;
        
        pd_switch(process->page_directory);
        u32 size = (u32) ((uintptr_t)sighandler_end_end-(uintptr_t)sighandler_end);
        memcpy((u32*) process->sighandler.esp-size, sighandler_end, size);
        *((u32*) process->sighandler.esp - size - 0x4) = process->sighandler.esp - size;
        *((int32_t*) process->sighandler.esp - size - 0x8) = sig;
        pd_switch(current_process->page_directory);

        process->sighandler.esp -= (size + 0x8);
    }
}

/*
* Send a signal to a process
* This method only registers the signal in the list, it will be handled later
*/
void send_signal(int pid, int sig)
{
    process_t* process = processes[pid];

    if((sig <= 0) | (sig >= NSIG)) return;

    /* adding the signal to the list */
    //TODO : wait for mutex
    if(mutex_lock(signal_mutex) != ERROR_NONE) fatal_kernel_error("Could not lock signal mutex", "SEND_SIGNAL");
    list_entry_t** listptr = &signal_list;
    while(*listptr) listptr = &(*listptr)->next;
    (*listptr) = kmalloc(sizeof(list_entry_t));
    u32* element = kmalloc(sizeof(u32)*2);
    element[0] = (uintptr_t) process;
    element[1] = (u32) sig;
    (*listptr)->element = element;
    (*listptr)->next = 0;
    mutex_unlock(signal_mutex);
}

/*
* Send a signal to a process group
* This method only registers the signal in the list, it will be handled later
*/
void send_signal_to_group(int gid, int sig)
{
    pgroup_t* group = get_group(gid);
    
    if((sig <= 0) | (sig >= NSIG)) return;

    list_entry_t* ptr = group->processes;
    while(ptr)
    {
        process_t* prc = ptr->element;
        send_signal(prc->pid, sig);
        ptr = ptr->next;
    }
}
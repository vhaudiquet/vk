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
//SIGCONT is set to IGNORE because CONTINUE action will always be executed
static int default_action[] = {2, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 4, 1, 4, 4, 4, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1};

list_entry_t* signal_list = 0;
mutex_t* signal_mutex = 0;

void signals_init()
{
    signal_mutex = kmalloc(sizeof(mutex_t));
    memset(signal_mutex, 0, sizeof(mutex_t));
}

/*
* This method is called by the scheduler every schedule
* It looks at the signal queue and handle every signal in it
*/
void handle_signals()
{
    if(!signal_list) return;

    list_entry_t* ptr = signal_list;
    while(ptr)
    {
        u32* element = ptr->element;
        //kprintf("handle_signal... ");
        handle_signal((process_t*) element[0], (int) element[1]);
        //kprintf(" ..handled.\n");

        if(mutex_lock(signal_mutex) != ERROR_NONE) mutex_wait(signal_mutex);
        list_entry_t* tofree = ptr;
        ptr = ptr->next;
        kfree(tofree->element);
        kfree(tofree);
        signal_list = ptr;
        mutex_unlock(signal_mutex);
    }
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
    if(process->status == PROCESS_STATUS_ZOMBIE) return;
    void* handler = process->signal_handlers[sig];
    
    if(sig == SIGCONT) if(process->status == PROCESS_STATUS_ASLEEP_SIGNAL) scheduler_add_process(process);

    /* SIG_DFL */
    if(!handler)
    {
        if(default_action[sig] == 1)
        {
            if(process->pid != 1) exit_process(process, EXIT_CONDITION_SIGNAL | ((u8) sig));
        }
        else if(default_action[sig] == 2) return;
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
       
    }
}

/*
* Send a signal to a process
* This method only registers the signal in the list, it will be handled later
*/
void send_signal(int pid, int sig)
{
    process_t* process = processes[pid];

    if(process->status == PROCESS_STATUS_ZOMBIE) return;
    if((sig <= 0) | (sig >= NSIG)) return;

    /* adding the signal to the list */
    while(mutex_lock(signal_mutex) != ERROR_NONE) mutex_wait(signal_mutex);
    list_entry_t** listptr = &signal_list;
    while(*listptr) listptr = &((*listptr)->next);
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
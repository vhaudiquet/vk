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

#include "sync.h"
#include "tasking/task.h"

void mutex_wait(mutex_t* mutex)
{
    //adding to the waiting list
    list_entry_t** ptr = &mutex->waiting;
    while(*(ptr)) ptr = &(*ptr)->next;
    (*ptr) = kmalloc(sizeof(list_entry_t));
    (*ptr)->element = current_process;
    (*ptr)->next = 0;

    current_process->status = PROCESS_STATUS_ASLEEP_MUTEX;
    scheduler_remove_process(current_process);
}

void mutex_unlock_wakeup(mutex_t* mutex)
{
    //waking up first waiting list element and removing it from list
    if(!mutex->waiting) return;
    
    process_t* tw = mutex->waiting->element;
    list_entry_t* ptr = mutex->waiting;
    mutex->waiting = mutex->waiting->next;
    kfree(ptr);

    scheduler_add_process(tw);
}

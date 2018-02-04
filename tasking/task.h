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

#ifndef TASK_HEAD
#define TASK_HEAD

#include "system.h"
#include "filesystem/fs.h"
#include "io/io.h"

//ELF loading
bool elf_check(fd_t* file);
void* elf_load(fd_t* file, u32* page_directory, list_entry_t* data_loc, u32* data_size);

//Process
typedef struct PROCESS
{
    //registers (backed up every schedule)
    g_regs_t gregs;
    s_regs_t sregs;
    u32 eip;
    u32 esp;
    u32 ebp;
    //page directory of the process
    u32* page_directory;
    //process kernel stack current esp
    u32 kesp;
    //process base stack and kernel stack (to free them on exit_process)
    u32 base_stack;
    u32 base_kstack;
    //location of all elf data segments in memory (to free them on exit_process)
    list_entry_t* data_loc;
    u32 data_size;
    //tty of the process
    tty_t* tty;
    //files opened by the process
    fd_t** files;
    u32 files_size;
    u32 files_count;
} process_t;

process_t* create_process(fd_t* executable, int argc, char** argv, tty_t* tty);
void exit_process(process_t* process);
extern process_t* kernel_process;
extern process_t* idle_process;
process_t* init_idle_process();
process_t* init_kernel_process();

//SCHEDULER
extern bool scheduler_started;
extern process_t* current_process;
void scheduler_init();
void scheduler_start();
void schedule();

//add/remove from queue
void scheduler_add_process(process_t* process);
void scheduler_remove_process(process_t* process);

//sleep/awake
#define SLEEP_WAIT_IRQ 1
#define SLEEP_PAUSED 2
#define SLEEP_TIME 3
#define SLEEP_WAIT_MUTEX 4

void scheduler_wait_process(process_t* process, u8 sleep_reason, u16 sleep_data, u16 sleep_data_2);
void scheduler_wake_process(process_t* process);
void scheduler_irq_wakeup(u32 irq);

#endif
#ifndef TASK_HEAD
#define TASK_HEAD

#include "system.h"
#include "filesystem/fs.h"

//User mode
void jump_usermode(void* code_offset, void* stack_offset);

//ELF loading
bool elf_check(file_descriptor_t* file);
void* elf_load(file_descriptor_t* file, u32* page_directory);

//Process
typedef struct PROCESS
{
    g_regs_t gregs;
    s_regs_t sregs;
    u32 eip;
    u32 esp;
    u32 ebp;
    u32* page_directory;
    u32 kesp;
    u32 base_stack;
    u32 base_kstack;
} process_t;

process_t* create_process(file_descriptor_t* executable);
extern process_t* kernel_process;
extern process_t* idle_process;
process_t* init_idle_process();
process_t* init_kernel_process();

//Threads
typedef struct THREAD
{
    g_regs_t gregs;
    u32 eip;
    u32 esp;
    u32 ebp;
    u32 kesp;
    u32 base_stack;
    u32 base_kstack;
    process_t* process;
} thread_t;

//SCHEDULER
extern process_t* current_process;
void scheduler_init();
void schedule();

//add/remove from queue
void scheduler_add_process(process_t* process);
void scheduler_remove_process(process_t* process);

//sleep/awake
#define SLEEP_WAIT_IRQ 1
#define SLEEP_PAUSED 2

void scheduler_wait_process(process_t* process, u8 sleep_reason, u8 sleep_data);
void scheduler_wake_process(process_t* process);
void scheduler_irq_wakeup(u32 irq);

#endif
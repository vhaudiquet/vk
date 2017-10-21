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

#ifndef CPU_HEAD
#define CPU_HEAD
#include "../system.h"

//CPU Informations
extern char CPU_VENDOR[13]; //CPU Vendor : GenuineIntel for intel (as example)
extern u32 CPU_MAX_CPUID; //MAX standard CPUID instruction supported by processor
extern u32 cpu_f_edx, cpu_f_ecx; //Registers content after CPUID with EAX=1 (CPU_FEATURES)
extern u32 cpu_ef_ebx, cpu_ef_ecx; //Registers content after CPUID with EAX=7,ECX=0 (CPU_EXTENDED_FEATURES)
extern bool cpu_e_support; //Does CPU support EXTENDED CPUID ?
extern u32 cpu_max_ecpuid; //MAX extended CPUID instruction supported by processor

extern bool cpu_pse;

void cpu_detect(void);

//GDT
// Task State Segment (TSS)
typedef struct TSS_ENTRY
{
    u32 prev_tss;   // Unused, not using hardware multitasking
    u32 esp0;       // KERNEL STACK
    u32 ss0;
    u32 esp1;       // BELOW HERE : Unused, cause we're not using hardware multitasking
    u32 ss1;
    u32 esp2;
    u32 ss2;
    u32 cr3;
    u32 eip;
    u32 eflags;
    u32 eax;
    u32 ecx;
    u32 edx;
    u32 ebx;
    u32 esp;
    u32 ebp;
    u32 esi;
    u32 edi;
    u32 es;         // The value to load into ES when we change to kernel mode.
    u32 cs;         // The value to load into CS when we change to kernel mode.
    u32 ss;         // The value to load into SS when we change to kernel mode.
    u32 ds;         // The value to load into DS when we change to kernel mode.
    u32 fs;         // The value to load into FS when we change to kernel mode.
    u32 gs;         // The value to load into GS when we change to kernel mode.
    u32 ldt;        // Unused...
    u16 trap;
    u16 iomap_base;
} __attribute__((packed)) tss_entry_t;

void gdt_install(void* stack_pointer);
extern tss_entry_t TSS;

//Interrupts
void idt_install(void); //IDT
void init_idt_desc(int index, u16 select, u32 offset, u16 type);

#endif
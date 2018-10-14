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

#include "system.h"
#include "cpu.h"

typedef struct
{
    u16 lim0_15;
    u16 base0_15;
    u8 base16_23;
    u8 acces;
    u8 lim16_19 : 4;
    u8 other : 4;
    u8 base24_31;
} __attribute__ ((packed)) gdt_desc_t;

typedef struct
{
	u16 limit;
	u32* base;
} __attribute__((packed)) gdt_pointer_t;

#define GDT_SIZE 6 //NULL, KERNEL_CODE, KERNEL_DATA, USER_CODE, USER_DATA, TSS
gdt_desc_t GDT_ENTRIES[GDT_SIZE];
gdt_pointer_t GDT_POINTER;
tss_entry_t TSS = {0};

static void init_gdt_desc(u32 index, u32 base, u32 limite, u8 acces, u8 other)
{
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wconversion"
	GDT_ENTRIES[index].lim0_15 = (limite & 0xffff);
	GDT_ENTRIES[index].base0_15 = (base & 0xffff);
	GDT_ENTRIES[index].base16_23 = (base & 0xff0000) >> 16;
	GDT_ENTRIES[index].acces = acces;
	GDT_ENTRIES[index].lim16_19 = (limite & 0xf0000) >> 16;
	GDT_ENTRIES[index].other = (other & 0xf);
	GDT_ENTRIES[index].base24_31 = (base & 0xff000000) >> 24;
    #pragma GCC diagnostic pop
	return;
}

static void tss_write(u32 index, u16 ss0, u32 esp0)
{
    // Firstly, let's compute the base and limit of our entry into the GDT.
    u32 base = (u32)&TSS;
    u32 limit = sizeof(TSS);

    // Now, add our TSS descriptor's address to the GDT.
    init_gdt_desc(index, base, limit, 0xE9, 0x00);

    TSS.ss0  = ss0;  // Set the kernel stack segment.
    TSS.esp0 = esp0; // Set the kernel stack pointer.

    TSS.cs = 0x08; //0b
    TSS.ss = TSS.ds = TSS.es = TSS.fs = TSS.gs = 0x10; //13
}

void gdt_install(void* stack_pointer)
{
    //Setting up GDT limit and base address
    GDT_POINTER.limit = GDT_SIZE * sizeof(gdt_desc_t);
	GDT_POINTER.base = (u32*) &GDT_ENTRIES;

	/* Initializing descriptors */
   	init_gdt_desc(0, 0, 0, 0, 0);                // NULL SEGMENT (0x0)
	init_gdt_desc(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // KERNEL CODE SEGMENT (0x08)
	init_gdt_desc(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // KERNEL DATA SEGMENT (0x10)
	init_gdt_desc(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // USER CODE SEGMENT (0x18)
	init_gdt_desc(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // USER DATA SEGMENT (0x20)

    tss_write(5, 0x10, (u32) stack_pointer); // TSS : Stack segment = 0x10 (kdata)

	// loading register GDTR
	asm("   lgdt (GDT_POINTER) \n");

	// init segments
	asm("   movw $0x10, %ax	\n \
            movw %ax, %ds	\n \
            movw %ax, %es	\n \
            movw %ax, %fs	\n \
            movw %ax, %gs	\n \
            ljmp $0x08, $next	\n \
        next:		\n");

    //flush TSS
    asm("   mov $0x2B, %ax \n \
            ltr %ax \n  ");
}

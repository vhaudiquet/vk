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

#include "../system.h"
#include "cpu.h"
#include "error/error.h"

typedef struct idt_desc
{
	u16 offset0_15;
	u16 select;
	u16 type;
	u16 offset16_31;
} __attribute__ ((packed)) idt_desc_t;

typedef struct idt_pointer
{
	u16 limit;
	u32* base;
} __attribute__ ((packed)) idt_pointer_t;

#define IDT_SIZE 0xFF
idt_desc_t IDT_ENTRIES[IDT_SIZE];
idt_pointer_t IDT_POINTER;

void init_idt_desc(int index, u16 select, u32 offset, u16 type)
{
	#pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wconversion"
	IDT_ENTRIES[index].offset0_15 = (offset & 0xffff);
	IDT_ENTRIES[index].select = select;
	IDT_ENTRIES[index].type = type;
	IDT_ENTRIES[index].offset16_31 = (offset & 0xffff0000) >> 16;
	#pragma GCC diagnostic pop
}

void unhandled_interrupt()
{
	//TEMP
	fatal_kernel_error("Unhandled interrupt !", "Interruption catch");
}
#include "idt.h"
void idt_install(void)
{
	kprintf("[CPU] Installing IDT...");
	//Initializing ALL interrupts to UNHANDLED
	int i;
	for (i = 0; i < IDT_SIZE; i++)
		init_idt_desc(i, 0x08, (u32) unhandled_interrupt, 0x8E00);

	//Initializing EXCEPTIONS
	init_idt_desc(0, 0x08, (u32) _isr0, 0x8E00);
	init_idt_desc(1, 0x08, (u32) _isr1, 0x8E00);
	init_idt_desc(2, 0x08, (u32) _isr2, 0x8E00);
	init_idt_desc(3, 0x08, (u32) _isr3, 0x8E00);
	init_idt_desc(4, 0x08, (u32) _isr4, 0x8E00);
	init_idt_desc(5, 0x08, (u32) _isr5, 0x8E00);
	init_idt_desc(6, 0x08, (u32) _isr6, 0x8E00);
	init_idt_desc(7, 0x08, (u32) _isr7, 0x8E00);
	init_idt_desc(8, 0x08, (u32) _isr8, 0x8E00);
	init_idt_desc(9, 0x08, (u32) _isr9, 0x8E00);
	init_idt_desc(10, 0x08, (u32) _isr10, 0x8E00);
	init_idt_desc(11, 0x08, (u32) _isr11, 0x8E00);
	init_idt_desc(12, 0x08, (u32) _isr12, 0x8E00);
	init_idt_desc(13, 0x08, (u32) _isr13, 0x8E00);
	init_idt_desc(14, 0x08, (u32) _isr14, 0x8E00);
	init_idt_desc(15, 0x08, (u32) _isr15, 0x8E00);
	init_idt_desc(16, 0x08, (u32) _isr16, 0x8E00);
	init_idt_desc(17, 0x08, (u32) _isr17, 0x8E00);
	init_idt_desc(18, 0x08, (u32) _isr18, 0x8E00);
	init_idt_desc(19, 0x08, (u32) _isr19, 0x8E00);
	init_idt_desc(20, 0x08, (u32) _isr20, 0x8E00);
	init_idt_desc(21, 0x08, (u32) _isr21, 0x8E00);
	init_idt_desc(22, 0x08, (u32) _isr22, 0x8E00);
	init_idt_desc(23, 0x08, (u32) _isr23, 0x8E00);
	init_idt_desc(24, 0x08, (u32) _isr24, 0x8E00);
	init_idt_desc(25, 0x08, (u32) _isr25, 0x8E00);
	init_idt_desc(26, 0x08, (u32) _isr26, 0x8E00);
	init_idt_desc(27, 0x08, (u32) _isr27, 0x8E00);
	init_idt_desc(28, 0x08, (u32) _isr28, 0x8E00);
	init_idt_desc(29, 0x08, (u32) _isr29, 0x8E00);
	init_idt_desc(30, 0x08, (u32) _isr30, 0x8E00);
	init_idt_desc(31, 0x08, (u32) _isr31, 0x8E00);

	//Initializing IRQs
	init_idt_desc(32, 0x08, (u32) CLOCK_IRQ, 0x8E00); //clock
	init_idt_desc(33, 0x08, (u32) _irq1, 0x8E00); //keyboard
	init_idt_desc(34, 0x08, (u32) _irq2, 0x8E00); //UNHANDLED ones
	init_idt_desc(35, 0x08, (u32) _irq3, 0x8E00);
	init_idt_desc(36, 0x08, (u32) _irq4, 0x8E00);
	init_idt_desc(37, 0x08, (u32) _irq5, 0x8E00);
	init_idt_desc(38, 0x08, (u32) _irq6, 0x8E00);
	init_idt_desc(39, 0x08, (u32) _irq7, 0x8E00);
	init_idt_desc(40, 0x08, (u32) _irq8, 0x8E00);
	init_idt_desc(41, 0x08, (u32) _irq9, 0x8E00);
	init_idt_desc(42, 0x08, (u32) _irq10, 0x8E00);
	init_idt_desc(43, 0x08, (u32) _irq11, 0x8E00);
	init_idt_desc(44, 0x08, (u32) _irq12, 0x8E00);
	init_idt_desc(45, 0x08, (u32) _irq13, 0x8E00);
	init_idt_desc(46, 0x08, (u32) _irq14, 0x8E00); //primary ata
	init_idt_desc(47, 0x08, (u32) _irq15, 0x8E00); //secondary ata
	init_idt_desc(48, 0x08, (u32) _irq16, 0x8E00); //unhandled
	init_idt_desc(49, 0x08, (u32) _irq17, 0x8E00);
	init_idt_desc(50, 0x08, (u32) _irq18, 0x8E00);
	init_idt_desc(51, 0x08, (u32) _irq19, 0x8E00);
	init_idt_desc(52, 0x08, (u32) _irq20, 0x8E00);
	
	//Initializing system calls
	init_idt_desc(0x80, 0x08, (u32) SYSCALL_H, 0xEF00);

    //IDTR Struct
	IDT_POINTER.limit = IDT_SIZE * 8;
	IDT_POINTER.base = (u32*) &IDT_ENTRIES;

	//Loading register IDTR
	asm("lidt (IDT_POINTER)\n");

	vga_text_okmsg();
}

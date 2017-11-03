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

#include "system.h"
#include "cpu.h"
#include "error/error.h"
#include "tasking/task.h"

struct regs_int
{
	u32 gs, fs, es, ds;
	u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
	u32 int_no, err_code;
	u32 eip, cs, eflags, useresp, ss;
};

static const char* const exceptionMessages[] =
{
    "Division By Zero",        "Debug",                         "Non Maskable Interrupt",    "Breakpoint",
    "Into Detected Overflow",  "Out of Bounds",                 "Invalid Opcode",            "No Coprocessor",
    "Double Fault",            "Coprocessor Segment Overrun",   "Bad TSS",                   "Segment Not Present",
    "Stack Fault",             "General Protection Fault",      "Page Fault",                "Unknown Interrupt",
    "Coprocessor Fault",       "Alignment Check",               "Machine Check",             "SIMD Exception",
    "Reserved",                "Reserved",                      "Reserved",                  "Reserved",
    "Reserved",                "Reserved",                      "Reserved",                  "Reserved",
    "Reserved",                "Reserved",                      "Reserved",                  "Reserved"
};

static void handle_user_fault(struct regs_int* r);
static void handle_page_fault(struct regs_int* r);

void fault_handler(struct regs_int * r)
{
    switch(r->int_no)
	{
		case 0: handle_user_fault(r); break;
		case 5: handle_user_fault(r); break;
		case 6: handle_user_fault(r); break;
		case 11: handle_user_fault(r); break;
		case 12: handle_user_fault(r); break;
		case 13: handle_user_fault(r); break;
		case 14: handle_page_fault(r); break;
		case 16: handle_user_fault(r); break;
		default: _fatal_kernel_error("Unhandled exception", exceptionMessages[r->int_no], "Unknown", 0); break;
	}
}

static void handle_user_fault(struct regs_int* r)
{
	kprintf("%lEIP = 0x%X\n", 3, r->eip);
	kprintf("%lERROR_CODE = 0x%X\n", 3, r->err_code);
	kprintf("%lss = 0x%X ; esp = 0x%X ; cs = 0x%X\n", 3, r->ss, r->esp, r->cs);
	kprintf("%lgs = 0x%X ; fs = 0x%X ; es = 0x%X ; ds = 0x%X\n", 3, r->gs, r->fs, r->es, r->ds);
	kprintf("%l%s\n", 2, exceptionMessages[r->int_no]);
	//notify user (on exiting)
	//exit current task, or hlt if currenttask == kernelTask
	_fatal_kernel_error("Kernel exception", "Kernel exception catched", "Unknown", 0);
}

static void handle_page_fault(struct regs_int* r)
{
	kprintf("%lEIP = 0x%X (cs = 0x%X)\n", 3, r->eip, r->cs);
	kprintf("%lESP = 0x%X (ss = 0x%X)\n", 3, r->esp, r->ss);
	u32 f_addr; asm("movl %%cr2, %0":"=r"(f_addr):);
	kprintf("Page fault at address : 0x%X\n", f_addr);
	kprintf("Error code : %d\n", r->err_code);
	_fatal_kernel_error("Page fault", "Unknown", "Unknown", 0);
}

void keyboard_interrupt();
void irq_handler(u32 irq_number)
{
	switch(irq_number)
	{
		case 1: {keyboard_interrupt(); break;} //Keyboard interrupt
		case 14: break;//{kprintf("%lPrimary ATA interrupt\n", 3); break;}
		case 15: {kprintf("%lSecondary ATA interrupt\n", 3); break;}
		default : {kprintf("%lUNHANDLED IRQ %u\n", 2, irq_number); break;}
	}
	if(irq_number) scheduler_irq_wakeup(irq_number);
}

#include "devices/keyboard.h"
void keyboard_interrupt()
{
	u8 keycode = inb(0x60);
	if(keycode == 42) kbd_maj = !kbd_maj;
	else if(keycode == 170) kbd_maj = !kbd_maj;
	else if(keycode == 58) kbd_maj = !kbd_maj;
	if(kbd_requested)
	{
		kbd_keycode = keycode;
		kbd_requested = false;
	}
}

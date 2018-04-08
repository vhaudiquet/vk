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
#include "error/error.h"
#include "tasking/task.h"

struct regs_int
{
	u32 gs, fs, es, ds;
	u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
	u32 int_no, err_code;
	u32 eip, cs, eflags, useresp, userss;
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
	if(r->int_no == 8) _fatal_kernel_error("DOUBLE FAULT", "DOUBLE FAULT", "Unknown", 0);
	
	kprintf("%lFAULT in process 0x%X / %d\n", 2, current_process, current_process->pid);
	kprintf("%lEIP = 0x%X\n", 3, r->eip);
    switch(r->int_no)
	{
		case 0: handle_user_fault(r); break;
		case 1:
		{
			u32 dr0 = 0; asm("movl %%dr0, %%eax":"=a"(dr0));
			u32 dr1 = 0; asm("movl %%dr1, %%eax":"=a"(dr1));
			u32 dr2 = 0; asm("movl %%dr2, %%eax":"=a"(dr2));
			u32 dr3 = 0; asm("movl %%dr3, %%eax":"=a"(dr3));
			kprintf("breakpoints : 0x%X 0x%X 0x%X 0x%X\n", dr0, dr1, dr2, dr3);
			u32 dr6 = 0; asm("movl %%dr6, %%eax":"=a"(dr6));
			u32 dr7 = 0; asm("movl %%dr7, %%eax":"=a"(dr7));
			kprintf("dr6 = 0x%X ; dr7 = 0x%X\n", dr6, dr7);
			kprintf("tss trap = 0x%X\n", TSS.trap);
			_fatal_kernel_error("Debug", "Unkown", "Unknown", 0);
			break;
		}
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
	kprintf("%lERROR_CODE = 0x%X\n", 3, r->err_code);
	kprintf("%lesp = 0x%X ; cs = 0x%X\n", 3, r->esp, r->cs);
	kprintf("%lgs = 0x%X ; fs = 0x%X ; es = 0x%X ; ds = 0x%X\n", 3, r->gs, r->fs, r->es, r->ds);
	kprintf("%l%s\n", 2, exceptionMessages[r->int_no]);
	//notify user (on exiting)
	//exit current task, or hlt if currenttask == kernelTask
	_fatal_kernel_error("Kernel exception", "Kernel exception catched", "Unknown", 0);
}

static void handle_page_fault(struct regs_int* r)
{
	kprintf("%lEIP = 0x%X (cs = 0x%X)\n", 3, r->eip, r->cs);
	if(r->cs == 0x08) kprintf("%lKERNEL_ESP = 0x%X\n", 3, r->esp);
	else if(r->cs == 0x1B) kprintf("%lUSER_ESP = 0x%X\n", 3, r->useresp);
	else kprintf("%lIncorrect CS !\n", 1);
	kprintf("%lEAX: 0x%X, EBX: 0x%X, ECX: 0x%X, EDX: 0x%X\nEBP: 0x%X, EDI: 0x%X, ESI: 0x%X\n", 3, r->eax, r->ebx, r->ecx, r->edx, r->ebp, r->edi, r->esi);
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
		//case 14: break;//{kprintf("%lPrimary ATA interrupt\n", 3); break;}
		//case 15: {kprintf("%lSecondary ATA interrupt\n", 3); break;}
		default : break;//{kprintf("%lUNHANDLED IRQ %u\n", 2, irq_number); break;}
	}
	if(irq_number) scheduler_irq_wakeup(irq_number);
}

#include "devices/keyboard.h"
#include "io/io.h"
void keyboard_interrupt()
{
	u8 keycode = inb(0x60);

	//if ttys are'nt initilized, we return
	if(!tty1 | !tty2 | !tty3) return;
	
	//handling shift and majlock
	if(keycode == 42) kbd_maj = !kbd_maj;
	else if(keycode == 170) kbd_maj = !kbd_maj;
	else if(keycode == 58) kbd_maj = !kbd_maj;
	
	//handling ctrl
	if(keycode == 0x1D) kbd_ctrl = true;
	else if(keycode == 0x9D) kbd_ctrl = false;

	//handling alt
	if(keycode == 0x38) kbd_alt = true;
	else if(keycode == 0xB8) kbd_alt = false;

	//handling tty switch
	if(kbd_ctrl && kbd_alt && (keycode == 0x3B)) {tty_switch(tty1); return;}
	if(kbd_ctrl && kbd_alt && (keycode == 0x3C)) {tty_switch(tty2); return;}
	if(kbd_ctrl && kbd_alt && (keycode == 0x3D)) {tty_switch(tty3); return;}

	u8 ch = getchar(keycode);
	if(ch) tty_input(current_tty, ch);
}

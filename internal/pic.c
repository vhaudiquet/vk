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

//Portable Interrupt Controller utilities
#include "system.h"
#include "internal.h"

//Remaps pic interrupts (0-15) to 32-47
void pic_install()
{
	kprintf("[PIC] Installing PIC...");
	//send INIT command to PIC1 and PIC2
	outb(0x20, 0x11);
	outb(0xA0, 0x11);

	outb(0x21, 0x20); //PIC1 interrupts starts at 0x20 (= 32)
	outb(0xA1, 0x28); //PIC2 interrupts starts at 0x28 (=40)

	outb(0x21, 0x04); //Set PIC1 as MASTER
	outb(0xA1, 0x02); //Set PIC2 as SLAVE

	//set PICS in x86 mode
	outb(0x21, 0x01);
	outb(0xA1, 0x01);

	vga_text_okmsg();
}

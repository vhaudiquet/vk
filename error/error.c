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

void vga_text_nl();

void _fatal_kernel_error(const char* error_message, const char* error_context, char* err_file, u32 err_line)
{
    //Switch back to VGA TEXT Video Mode : set_video_mode(g_80x25_text);
    asm("cli");
    vga_text_nl();
    kprintf("%v[FATAL ERROR] : %s\n", 0b00001100, error_message);
    kprintf("%v[CONTEXT] : %s\n", 0b00001100, error_context);
    kprintf("%v[LOCATION] : %s:%d\n", 0b00001100, err_file, err_line);
    kprintf("%l-- SYSTEM HALTED --\n", 2);
    asm("hlt");
}

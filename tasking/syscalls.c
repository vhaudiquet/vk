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
#include "task.h"
#include "video/video.h"
#include "memory/mem.h"
#include "devices/keyboard.h"

/*
* For now, i have less than 10 syscalls, and they are all in this file
* Then i may need to expand the file or to rewrite it
* For now it contains the system calls, that i use only to debug the kernel
* (the idea is to build a stable and powerfull kernel, and then creates a way for userland to call each kernel service)
*/

static void vga_text_syscall(u16 ss, u32 ebx, u32 edx);
static void kbd_syscall(u16 ss, u32 ebx);

bool ptr_validate(u32 ptr, u32* page_directory)
{
    if(ptr >= 0xC0000000) return false;
    if(!is_mapped(ptr, page_directory)) return false;
    return true;
}

void syscall_global(u32 syscall_number, u32 ebx, u32 edx)
{
    u32 snbr = (u32) syscall_number >> 16;
    u16 ss = syscall_number & 0xFFFF;//<< 16 >> 16;
    switch(snbr)
    {
        case 0:
            vga_text_syscall(ss, ebx, edx);
            break;
        case 1:
            kbd_syscall(ss, ebx);
            break;
        default:
            break;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void vga_text_syscall(u16 ss, u32 ebx, u32 edx)
{
    switch(ss)
    {
        case 1:
        {
            u8 ch = ebx & 0xFF;//<< 24 >> 24;
            //u8 color = edx & 0xFF;//<< 24 >> 24;
            tty_write(&ch, 1, current_process->tty);
            break;
        }
        case 2:
        {
            if(ptr_validate(ebx, current_process->page_directory))
            {
                u8* str = (u8*) ebx;
                //u8 color = edx & 0xFF;//<< 24 >> 24;
                tty_write(str, strlen((char*) str), current_process->tty);
            }
            break;
        }
        case 3:
            vga_text_cls();
            break;
        case 4:
            vga_text_enable_cursor();
            break;
        case 5:
            vga_text_disable_cursor();
    }
}
#pragma GCC diagnostic pop

static void kbd_syscall(u16 ss, u32 ebx)
{
    switch(ss)
    {
        case 1:
        {
            if(ptr_validate(ebx, current_process->page_directory))
            {
                u8* ptr = (u8*) ebx;
                *ptr = 0; //*ptr = kbd_getkeycode();
            }
            break;
        }
        case 2:
        {
            if(ptr_validate(ebx, current_process->page_directory))
            {
                u8* ptr = (u8*) ebx;
                *ptr = tty_getch(current_process->tty);
            }
            break;
        }
        default:
            break;
    }
}

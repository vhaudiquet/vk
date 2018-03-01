/*  
    This file is part of VK.
    Copyright (C) 2018 Valentin Haudiquet

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

#include "io.h"
#include "video/video.h"
#include "tasking/task.h"
#include "error/error.h"
#include "filesystem/devfs.h"

#define TTY_DEFAULT_BUFFER_SIZE 1024

tty_t* tty1 = 0;
tty_t* tty2 = 0;
tty_t* tty3 = 0;

tty_t* current_tty = 0;

void ttys_init()
{
    kprintf("Initializing ttys...");

    tty1 = tty_init("tty1");
    if(!tty1) {vga_text_failmsg(); fatal_kernel_error("Failed to initialize TTY 1", "TTYS_INIT");}

    tty2 = tty_init("tty2");
    if(!tty2) {vga_text_failmsg(); fatal_kernel_error("Failed to initialize TTY 2", "TTYS_INIT");}

    tty3 = tty_init("tty3");
    if(!tty3) {vga_text_failmsg(); fatal_kernel_error("Failed to initialize TTY 3", "TTYS_INIT");}

    current_tty = tty1;

    vga_text_okmsg();
}

tty_t* tty_init(char* name)
{
    tty_t* tr = kmalloc(sizeof(tty_t));
    tr->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    tr->count = 0; 
    tr->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    tr->keyboard_stream = iostream_alloc();
    tty_write((u8*) "VK 0.0-indev (", 14, tr);
    tty_write((u8*) name, strlen(name), tr);
    tty_write((u8*) ")\n", 2, tr);
    fsnode_t* trf = devfs_register_device(devfs->root_dir, name, tr, DEVFS_TYPE_TTY, 0);
    if(!trf) {kfree(tr->buffer); iostream_free(tr->keyboard_stream); kfree(tr); return 0;}
    tr->pointer = trf;
    return tr;
}

/*
* This function writes to a virtual terminal
*/
u8 tty_write(u8* buffer, u32 count, tty_t* tty)
{
    if(tty->count+count > tty->buffer_size)
    {
        tty->buffer_size*=2;
        tty->buffer = krealloc(tty->buffer, tty->buffer_size);
    }

    memcpy(tty->buffer+tty->count, buffer, count);
    tty->count += count;

    if(tty == current_tty) 
    {
        u32 t = 0;
        while(count)
        {
            vga_text_putc(*(buffer+t), 0b00000111);
            count--;
            t++;
        }
    }

    return 0;
}

/*
* This function reads from a virtual terminal
* CARE : it actually reads the keyboard stream associated to this virtual terminal
* CARE : this function can block (wait keyboard IRQ)
*/
u8 tty_getch(tty_t* tty)
{
    if(!tty->keyboard_stream->count)
    {
        scheduler_wait_process(current_process, SLEEP_WAIT_IRQ, 1, 0);
        return tty_getch(tty);
    }

    u8 c = iostream_getch(tty->keyboard_stream);
    tty_write(&c, 1, tty);
    return c;
}

void tty_switch(tty_t* tty)
{
    if(tty != current_tty)
    {
        current_tty = tty;
        vga_text_tty_switch(current_tty);
    }
}

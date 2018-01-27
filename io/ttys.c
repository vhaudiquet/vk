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

#define TTY_DEFAULT_BUFFER_SIZE 1024

tty_t* tty1 = 0;
tty_t* tty2 = 0;
tty_t* tty3 = 0;

tty_t* current_tty = 0;

void tty_init()
{
    kprintf("Initializing ttys...");

    tty1 = kmalloc(sizeof(tty_t)); tty1->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    strcpy((char*) tty1->buffer, "VK 0.0-indev (tty1)\n");
    tty1->count = 0; tty1->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    devfs_register_device("tty1", tty1, DEVICE_TYPE_TTY, 0);

    tty2 = kmalloc(sizeof(tty_t)); tty2->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    strcpy((char*) tty2->buffer, "VK 0.0-indev (tty2)\n");
    tty2->count = 0; tty2->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    devfs_register_device("tty2", tty2, DEVICE_TYPE_TTY, 0);

    tty3 = kmalloc(sizeof(tty_t)); tty3->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    strcpy((char*) tty3->buffer, "VK 0.0-indev (tty3)\n");
    tty3->count = 0; tty3->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    devfs_register_device("tty3", tty3, DEVICE_TYPE_TTY, 0);

    current_tty = tty1;

    vga_text_okmsg();
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

    if(tty == current_tty) vga_text_puts(buffer, 0b00000111);

    return 0;
}

/*
* This function reads from a virtual terminal
* CARE : it actually reads the keyboard stream associated to this virtual terminal
* CARE : this function can block and consume CPU time (maybe improve to wait IRQ later)
*/
u8 tty_getch(tty_t* tty)
{
    return iostream_getch(tty->keyboard_stream);
}

void tty_switch(tty_t* tty)
{
    if(tty != current_tty)
    {
        current_tty = tty;
        vga_text_tty_switch(current_tty);
    }
}

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

static u8 tty_getch(tty_t* tty);

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
    
    tr->termio.c_iflag = ICRNL | BRKINT;
    tr->termio.c_oflag = ONLCR | OPOST;
    tr->termio.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
    tr->termio.c_cflag = CREAD;
    tr->termio.c_cc[VEOF] = 4;
    tr->termio.c_cc[VEOL] = 0;
	tr->termio.c_cc[VERASE] = '\b';
	tr->termio.c_cc[VINTR] = 3;
	tr->termio.c_cc[VKILL] = 21;
	tr->termio.c_cc[VMIN] =  1;
	tr->termio.c_cc[VQUIT] = 28;
	tr->termio.c_cc[VSTART] = 17;
	tr->termio.c_cc[VSTOP] = 19;
	tr->termio.c_cc[VSUSP] = 26;
	tr->termio.c_cc[VTIME] = 0;
    
    return tr;
}

/*
* This function writes to a virtual terminal
*/
error_t tty_write(u8* buffer, u32 count, tty_t* tty)
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

    return ERROR_NONE;
}

/*
* This function reads from a virtual terminal, following POSIX conventions
*/
error_t tty_read(u8* buffer, u32 count, tty_t* tty)
{
    if(tty->termio.c_lflag & ICANON)
    {
        u32 off = 0;
        while(count)
        {
            u8 ch = tty_getch(tty);
            *(buffer+off) = ch;
            if(ch == '\n') return ERROR_NONE;
            off++;
            count--;
        }
    }
    else
    {
        if(tty->termio.c_cc[VMIN] == 0 && tty->termio.c_cc[VTIME] == 0)
        {
            if(tty->keyboard_stream->count)
            {
                u32 countmin = count > tty->keyboard_stream->count ? tty->keyboard_stream->count : count;
                memcpy(buffer, tty->keyboard_stream->buffer, countmin);
                tty->keyboard_stream->count -= countmin;
                memcpy(tty->keyboard_stream->buffer+countmin, tty->keyboard_stream->buffer, tty->keyboard_stream->count);
            }
        }
        else if(tty->termio.c_cc[VMIN] > 0 && tty->termio.c_cc[VTIME] == 0)
        {
            u32 countmin = tty->termio.c_cc[VMIN] > count ? count : tty->termio.c_cc[VMIN];
            u32 off = 0;
            while(countmin)
            {
                *(buffer+off) = tty_getch(tty);
                off++;
                countmin--;
            }
        }
        else if(tty->termio.c_cc[VMIN] == 0 && tty->termio.c_cc[VTIME] > 0)
        {
            return UNKNOWN_ERROR;
        }
        else if(tty->termio.c_cc[VMIN] > 0 && tty->termio.c_cc[VTIME] > 0)
        {
            return UNKNOWN_ERROR;
        }
    }

    return ERROR_NONE;
}

void tty_switch(tty_t* tty)
{
    if(tty != current_tty)
    {
        current_tty = tty;
        vga_text_tty_switch(current_tty);
    }
}

/*
* This function reads from the keyboard stream associed to a virtual terminal
* CARE : this function can block (wait keyboard IRQ)
*/
static u8 tty_getch(tty_t* tty)
{
    if(!tty->keyboard_stream->count)
    {
        scheduler_wait_process(current_process, SLEEP_WAIT_IRQ, 1, 0);
        return tty_getch(tty);
    }

    u8 c = iostream_getch(tty->keyboard_stream);

    if(tty->termio.c_iflag & ISTRIP) c &= 127; //remove 8th bit

    if(tty->termio.c_iflag & INLCR) if(c == '\n') c = '\r';
    if(tty->termio.c_iflag & IGNCR) if(c == '\r') return tty_getch(tty);
    if(tty->termio.c_iflag & ICRNL) if(c == '\r') c = '\n';

    if(c == tty->termio.c_cc[VERASE])
    {
        if((tty->termio.c_lflag & ICANON) && (tty->termio.c_lflag & ECHOE) && (tty->count))
        {
            u8 terase = *(tty->buffer+tty->count-1);
            if((terase != tty->termio.c_cc[VEOF]) && (terase != tty->termio.c_cc[VEOL]) && (terase != '\n'))
            {
                tty->count--;
                *(tty->buffer+tty->count) = 0;
                vga_text_putc('\b', 0);
            }
        }
        return tty_getch(tty);
    }

    if(tty->termio.c_lflag & ECHO) tty_write(&c, 1, tty);
    else if(c == '\n' && (tty->termio.c_lflag & ICANON & ECHONL)) tty_write(&c, 1, tty);

    return c;
}

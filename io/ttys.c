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
#include "ioctl.h"

#define TTY_DEFAULT_BUFFER_SIZE 1024

tty_t* tty1 = 0;
tty_t* tty2 = 0;
tty_t* tty3 = 0;

tty_t* current_tty = 0;

/*
* This function is called by the kernel to init std ttys : tty1, tty2, tty3
*/
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

/*
* This function inits a new tty structure
*/
tty_t* tty_init(char* name)
{
    tty_t* tr = kmalloc(sizeof(tty_t));
    tr->buffer = kmalloc(TTY_DEFAULT_BUFFER_SIZE);
    tr->count = 0; 
    tr->buffer_size = TTY_DEFAULT_BUFFER_SIZE;
    tr->keyboard_stream = iostream_alloc();
    tr->keyboard_stream->attributes |= IOSTREAM_ATTR_ONE_BYTE;
    tr->canon_buffer = kmalloc(256);
    tr->canon_buffer_count = 0;
    tr->canon_buffer_size = 256;
    tr->foreground_processes = 0;
    
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

    tr->char_width = 80;
    tr->char_height = 25;
    tr->cursor_pos = 0;

    return tr;
}

/*
* This function writes to a virtual terminal
*/
error_t tty_write(u8* buffer, u32 count, tty_t* tty)
{
    //handle terminal writing permission errors
    if(current_process->group != tty->foreground_processes)
    {
        /* if TOSTOP is set and process is not ignoring/blocking SIGTTOU */
        if((tty->termio.c_lflag & TOSTOP) && (current_process->signal_handlers[SIGTTOU] != ((void*) 1)))
        {
            send_signal(current_process->pid, SIGTTOU);
            return ERROR_IO;
        }
    }

    //handle ANSI escape sequences
    if(strcfirst("\x1B[", (char*) buffer) == 2)
    {
        uint32_t i = 0;
        //note: assuming n and m will not be negative
        u32 n = (u32) atoiindex(buffer+2, &i); if(!n) n = 1; //parse n or set default value if absent
        u8 control = buffer[2+i]; //reset control char
        u32 m = 1;
        if(control == ';') 
        {
            m = (u32) atoiindex(buffer+2+i, &i); if(!m) m = 1; //parse m if needed and set default value if absent
            control = buffer[2+i]; //reset control char
        }

        //kprintf("%lPARSED SEQUENCE = CSI %u;%u %c\n", 3, n, m, control);
        if(control == 'H')
        {
            tty->cursor_pos = tty->char_width*n + m;
            vga_text_set_cursor((u8) (n-1), (u8) (m-1));
        }
        else if(control == 'J')
        {
            if(n==2) vga_text_cls();
        }
        
        count = count-3-i;
        if(count) return tty_write(buffer+3+i, count, tty);
        else return ERROR_NONE;
    }

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
    if(current_process->group != tty->foreground_processes)
    {
        send_signal(current_process->pid, SIGTTIN);
        return ERROR_IO;
    }

    if(tty->termio.c_lflag & ICANON)
    {
        while(count)
        {
            u8 ch = iostream_getch(tty->keyboard_stream);
            
            if(tty->canon_buffer_count >= tty->canon_buffer_size) 
            {
                tty->canon_buffer_size*=2;
                tty->canon_buffer = krealloc(tty->canon_buffer, tty->canon_buffer_size);
            }

            *(tty->canon_buffer+tty->canon_buffer_count) = ch;
            tty->canon_buffer_count++;
            
            count--;

            if(ch == '\n') break;
        }
        
        memcpy(buffer, tty->canon_buffer, tty->canon_buffer_count);
        memset(buffer+tty->canon_buffer_count, 0, count-tty->canon_buffer_count);

        tty->canon_buffer_count = 0;
        *tty->canon_buffer = 0;
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
                *(buffer+off) = iostream_getch(tty->keyboard_stream);
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

/*
* This function switches current view to tty
*/
void tty_switch(tty_t* tty)
{
    if(tty != current_tty)
    {
        current_tty = tty;
        vga_text_tty_switch(current_tty);
    }
}

/*
* This function inputs text to a tty (ex: from keyboard to tty, called on kbd irq for 'current_tty')
*/
void tty_input(tty_t* tty, u8 c)
{
    if(tty->termio.c_iflag & ISTRIP) c &= 127; //remove 8th bit

    if(tty->termio.c_iflag & INLCR) if(c == '\n') c = '\r';
    if(tty->termio.c_iflag & IGNCR) if(c == '\r') return;
    if(tty->termio.c_iflag & ICRNL) if(c == '\r') c = '\n';

    if(c == tty->termio.c_cc[VERASE])
    {
        //kprintf("kc:%u", tty->canon_buffer_count);
        if((tty->termio.c_lflag & ICANON) && (tty->termio.c_lflag & ECHOE) && (tty->canon_buffer_count))
        {
            u8 terase = *(tty->canon_buffer+tty->canon_buffer_count-1);
            //kprintf("terase = %u ", terase);
            if((terase != tty->termio.c_cc[VEOF]) && (terase != tty->termio.c_cc[VEOL]) && (terase != '\n'))
            {
                tty->canon_buffer_count--;
                *(tty->canon_buffer+tty->canon_buffer_count) = 0;
                
                tty->count--;
                *(tty->buffer+tty->count) = 0;

                if(tty->termio.c_lflag & ECHO) vga_text_putc('\b', 0);
            }
        }
        return;
    }
    
    /* handle signals */
    if((tty->termio.c_lflag & ISIG) && (tty->foreground_processes))
    {
        if(c == tty->termio.c_cc[VINTR])
        {
            send_signal_to_group(tty->foreground_processes->gid, SIGINT);
            if(tty->termio.c_lflag & ECHO) tty_write((u8*) "^C\n", 3, tty);
            return;
        }
        else if(c == tty->termio.c_cc[VKILL])
        {
            send_signal_to_group(tty->foreground_processes->gid, SIGKILL);
            if(tty->termio.c_lflag & ECHO) tty_write((u8*) "^U\n", 3, tty);
            return;
        }
        else if(c == tty->termio.c_cc[VSTART])
        {
            send_signal_to_group(tty->foreground_processes->gid, SIGCONT);
            if(tty->termio.c_lflag & ECHO) tty_write((u8*) "^Q\n", 3, tty);
            return;
        }
        else if(c == tty->termio.c_cc[VSTOP])
        {
            send_signal_to_group(tty->foreground_processes->gid, SIGSTOP);
            if(tty->termio.c_lflag & ECHO) tty_write((u8*) "^S\n", 3, tty);
            return;
        }
        else if(c == tty->termio.c_cc[VSUSP])
        {
            send_signal_to_group(tty->foreground_processes->gid, SIGTSTP);
            if(tty->termio.c_lflag & ECHO) tty_write((u8*) "^Z\n", 3, tty);
            return;
        }
    }
    if(c == tty->termio.c_cc[VEOF]) return;

    if(tty->termio.c_lflag & ECHO) tty_write(&c, 1, tty);
    else if(c == '\n' && (tty->termio.c_lflag & ICANON & ECHONL)) tty_write(&c, 1, tty);

    iostream_write(&c, 1, tty->keyboard_stream);
}

/*
* This function can be used to set/read some parameters of the tty
*/
error_t tty_ioctl(tty_t* tty, u32 func, u32 arg)
{
    //kprintf("%lTTY_IOCTL(0x%X, %u, 0x%X)\n", 3, tty, func, arg);
    switch(func)
    {
        case I_TTY_SETPGRP:
        {   
            if(tty != current_process->tty) return ERROR_NO_TTY;
            if(tty->session != current_process->session) return ERROR_IS_ANOTHER_SESSION;
            pgroup_t* group = get_group((int) arg);
            if((!group) || (group->session != current_process->session) || (!group->processes) || (!group->processes->element))
                return ERROR_PERMISSION;

            tty->foreground_processes = group;
            return ERROR_NONE;
        }
        case I_TTY_GETPGRP:
        {
            if(tty != current_process->tty) return ERROR_NO_TTY;

            int* i = (int*) arg;
            
            if(tty->foreground_processes)
                *i = tty->foreground_processes->gid;
            else
                *i = (S32_MAX-1);

            return ERROR_NONE;
        }
    }
    
    return UNKNOWN_ERROR;
}

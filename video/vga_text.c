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

#include "video.h"

#define VIDEO_TYPE_NONE 0x0
#define VIDEO_TYPE_COLOR 0x20
#define VIDEO_TYPE_MONOCHROME 0x30

//CONSTS
u8 VIDEO_TYPE;
u8* VGA_MEM_START;
u8 VGA_COLUMNS = 80;
u8 VGA_LINES = 25;
u8* VGA_MEM_END;

u8 TEXT_CURSOR_X = 0;
u8 TEXT_CURSOR_Y = 0;

/* basic 80x25 text mode */
u8 g_80x25_text[] =
{
/* MISC */
	0x67,
/* SEQ */
	0x03, 0x00, 0x03, 0x00, 0x02,
/* CRTC */
	0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
	0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
	0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
	0xFF,
/* GC */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00,
	0xFF,
/* AC */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
	0x0C, 0x00, 0x0F, 0x08, 0x00
};

bool vga_setup(void)
{
    //Set video_mode to VGA_TEXT 80*25
    set_video_mode(VIDEO_MODE_VGA_TEXT);

    //Detect video type using BDA
    VIDEO_TYPE = (*((u16*) 0xC0000410) & 0x30);

    //Detect starting video ram depending on video type
    if(VIDEO_TYPE == VIDEO_TYPE_NONE) return false;
    if(VIDEO_TYPE == VIDEO_TYPE_MONOCHROME) VGA_MEM_START = (u8*) 0xC00B0000;
    if(VIDEO_TYPE == VIDEO_TYPE_COLOR) VGA_MEM_START = (u8*) 0xC00B8000;
    VGA_MEM_END = VGA_MEM_START + 0xFA0;

    //Cleaning video ram and initializing pointer
    vga_text_cls();

    vga_text_enable_cursor();

    kprintf("%lVGA display setup done\n", 1);
    return true;
}

void vga_text_cls()
{
    u32 i = 0;
    while(i < 0xFA0)
    {
        u8* vg = (u8*) VGA_MEM_START+i;
        *vg = 0x0;
        *(vg+1) = 0xF;
        i+=2;
    }
}

void vga_text_reset()
{
    vga_text_cls();
    TEXT_CURSOR_X = 0;
    TEXT_CURSOR_Y = 0;
}

//static
 void vga_text_scroll_up()
{
    if(get_video_mode() != VIDEO_MODE_VGA_TEXT) return;
    if(VIDEO_TYPE == VIDEO_TYPE_NONE) return;

    u8* VRAM;
    u8* tmp;
    for (VRAM = VGA_MEM_START; (VRAM+1) < VGA_MEM_END; VRAM += 2)
    {
        //assign temp position to start of next line
        tmp = (u8*) (VRAM + VGA_COLUMNS*2);

        //if temp position is in VRAM, put it back in previous line
        if (tmp < VGA_MEM_END)
        {
            VRAM[0] = tmp[0];
            VRAM[1] = tmp[1];
        }
        //else this means that this line is the last, so we empty it
        else
        {
            VRAM[0] = 0;
            VRAM[1] = 0x07;
        }
    }

    if(TEXT_CURSOR_Y > 0)
        TEXT_CURSOR_Y = (u8) (TEXT_CURSOR_Y - 1);
}

static void vga_text_update_cursor()
{
    unsigned short position = (u16) ((TEXT_CURSOR_Y*80) + TEXT_CURSOR_X);
    // cursor LOW port to vga INDEX register
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(position&0xFF));
    // cursor HIGH port to vga INDEX register
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char )((position>>8)&0xFF));
}

void vga_text_disable_cursor()
{
    outb(0x3D4, 0x0A); // LOW cursor shape port to vga INDEX register
    outb(0x3D5, 0x3f); //bits 6-7 must be 0 , if bit 5 set the cursor is disable  , bits 0-4 controll the cursor shape .
}

void vga_text_enable_cursor()
{
    outb(0x3D4, 0x0A); // LOW cursor shape port to vga INDEX register
    outb(0x3D5, 12); //bits 6-7 must be 0 , if bit 5 set the cursor is disable  , bits 0-4 controll the cursor shape .
}

void vga_text_putc(unsigned char c, u8 color)
{
    if(get_video_mode() != VIDEO_MODE_VGA_TEXT) return;
    if(VIDEO_TYPE == VIDEO_TYPE_NONE) return;

    if(c == '\n'){TEXT_CURSOR_X = 0; TEXT_CURSOR_Y++;}
    else if(c == '\t')
    {
        u8 next_cursor = (u8) (TEXT_CURSOR_X + 8 - (TEXT_CURSOR_X % 8));
        if(next_cursor > VGA_COLUMNS - 1) {TEXT_CURSOR_Y++;TEXT_CURSOR_X = 0;}
        else {TEXT_CURSOR_X = next_cursor;}
    }
    else if(c == '\r'){TEXT_CURSOR_X = 0;}
    else if(c == '\b')
    {
        if(TEXT_CURSOR_X > 0)
        {
            TEXT_CURSOR_X--;
            u8* CURRENT = (u8*) (VGA_MEM_START + 2 * TEXT_CURSOR_X + 2*VGA_COLUMNS * TEXT_CURSOR_Y);
            *CURRENT = 0;
        }
        else
        {
            if(TEXT_CURSOR_Y > 0)
            {
                TEXT_CURSOR_Y--;
                TEXT_CURSOR_X = VGA_COLUMNS;
            }
        }
    }
    else
    {
        //get current video mem address and put char in it, + color if video mode = color
        u8* CURRENT = (u8*) (VGA_MEM_START + 2 * TEXT_CURSOR_X + 2*VGA_COLUMNS * TEXT_CURSOR_Y);
        *CURRENT = c;
        if(VIDEO_TYPE == VIDEO_TYPE_COLOR) *(CURRENT+1) = color;
        //update X
        TEXT_CURSOR_X++;
    }

    //update text cursor X if line end reached
    if(TEXT_CURSOR_X >= VGA_COLUMNS)
    {
        TEXT_CURSOR_X = 0;
        TEXT_CURSOR_Y++;
    }

    //update text cursor Y if screen end reached
    if(TEXT_CURSOR_Y >= (VGA_LINES)) vga_text_scroll_up(); //this actually causes the last line not to be used
    
    //update vga cursor
    vga_text_update_cursor();
}

void vga_text_puts(unsigned char* str, u8 color)
{
    if(get_video_mode() != VIDEO_MODE_VGA_TEXT) return;
    if(VIDEO_TYPE == VIDEO_TYPE_NONE) return;
    while(*str != 0)
    {
        vga_text_putc(*str, color);
        str++;
    }
}

void vga_text_okmsg()
{
    TEXT_CURSOR_X = 50;
    vga_text_puts((u8*)"OK\n", 0b00001010);
}

void vga_text_failmsg()
{
    TEXT_CURSOR_X = 50;
    vga_text_puts((u8*)"FAILED\n", 0b00001100);
}

void vga_text_skipmsg()
{
    TEXT_CURSOR_X = 50;
    vga_text_puts((u8*)"SKIPPED\n", 0b00001111);
}

void vga_text_spemsg(char* msg, u8 color)
{
    TEXT_CURSOR_X = 50;
    vga_text_puts((u8*)msg, color);
}

void vga_text_nl()
{
    if(TEXT_CURSOR_X != 0)
    {
        TEXT_CURSOR_Y++;
        TEXT_CURSOR_X = 0;
    }
}

void vga_text_tty_switch(tty_t* tty)
{
    vga_text_reset();
    vga_text_puts(tty->buffer, 0b00000111);
}

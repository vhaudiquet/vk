#include "video.h"

static void vga_write_regs(u8* regs);

//The bootloader set that up for us
u8 VIDEO_MODE = VIDEO_MODE_VGA_TEXT;

void set_video_mode(u8 video_mode)
{
    if(VIDEO_MODE == video_mode) return;
    if(video_mode == VIDEO_MODE_VGA_TEXT)
    {
        vga_write_regs(g_80x25_text);
        VIDEO_MODE = video_mode;
    }
}

u8 get_video_mode()
{
    return VIDEO_MODE;
}

static void vga_write_regs(u8* regs) 
{
	size_t i;

	/* write MISCELLANEOUS reg */
	outb(VGA_MISC_WRITE, *regs);
	regs++;
	/* write SEQUENCER regs */
	for(i = 0; i < VGA_NUM_SEQ_REGS; i++)
	{
		outb(VGA_SEQ_INDEX, i);
		outb(VGA_SEQ_DATA, *regs);
		regs++;
	}
	/* unlock CRTC registers */
	outb(VGA_CRTC_INDEX, 0x03);
	outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
	outb(VGA_CRTC_INDEX, 0x11);
	outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);
	/* make sure they remain unlocked */
	regs[0x03] |= 0x80;
	regs[0x11] &= (u8) ~0x80;
	/* write CRTC regs */
	for(i = 0; i < VGA_NUM_CRTC_REGS; i++)
	{
		outb(VGA_CRTC_INDEX, i);
		outb(VGA_CRTC_DATA, *regs);
		regs++;
	}
	/* write GRAPHICS CONTROLLER regs */
	for(i = 0; i < VGA_NUM_GC_REGS; i++)
	{
		outb(VGA_GC_INDEX, i);
		outb(VGA_GC_DATA, *regs);
		regs++;
	}
	/* write ATTRIBUTE CONTROLLER regs */
	for(i = 0; i < VGA_NUM_AC_REGS; i++)
	{
		(void)inb(VGA_INSTAT_READ);
		outb(VGA_AC_INDEX, i);
		outb(VGA_AC_WRITE, *regs);
		regs++;
	}
	/* lock 16-color palette and unblank display */
	(void)inb(VGA_INSTAT_READ);
	outb(VGA_AC_INDEX, 0x20);
}
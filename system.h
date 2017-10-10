#ifndef SYS_HEAD
#define SYS_HEAD

#include "util/util.h"

//Memory constants
#define KERNEL_VIRTUAL_BASE 0xC0000000
extern u32 _kernel_start;
extern u32 _kernel_end;

//Write byte on a port (8 bits ports)
#define outb(port,value) \
        asm volatile ("outb %%al, %%dx" :: "d" (port), "a" (value));

//Write word on a port (16 bits ports)
#define outw(port,value) \
        asm volatile ("outw %%ax, %%dx" :: "d" (port), "a" (value));

//Write an int on a port (32 bits ports)
#define outl(port,value) \
        asm volatile ("outl %%eax, %%dx" :: "d" (port), "a" (value));


//Write byte on a port and pause
/*#define outbp(port,value) \
        asm volatile ("outb %%al, %%dx; jmp 1f; 1:" :: "d" (port), "a" (value));
*/

//Read byte from a port
#define inb(port) ({    \
        unsigned char _v;       \
        asm volatile ("inb %%dx, %%al" : "=a" (_v) : "d" (port)); \
        _v;     \
})

//Read word from a port
#define inw(port) ({    \
        unsigned short _v;       \
        asm volatile ("inw %%dx, %%ax" : "=a" (_v) : "d" (port)); \
        _v;     \
})

//Read int from a port
#define inl(port) ({    \
        unsigned int _v;       \
        asm volatile ("inl %%dx, %%eax" : "=a" (_v) : "d" (port)); \
        _v;     \
})


//utils
void kprintf(const char* args, ...);
void vga_text_okmsg();
void vga_text_skipmsg();
void vga_text_failmsg();
void vga_text_spemsg(char* msg, u8 color);

//args
#define KERNEL_MODE_LIVE 1
#define KERNEL_MODE_INSTALLED 2
extern bool alive;
extern char aroot_dir[5];
extern u8 aboot_hint_present;
extern bool asilent;

typedef struct g_regs
{
	unsigned int edi, esi, ebx, edx, ecx, eax;
} g_regs_t;

typedef struct s_regs
{
        unsigned int ds, es, fs, gs, ss, cs;
} s_regs_t;

#endif
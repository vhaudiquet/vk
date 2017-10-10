#include "system.h"
#include "task.h"
#include "video/video.h"
#include "memory/mem.h"
#include "devices/keyboard.h"

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
    u16 ss = syscall_number << 16 >> 16;
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

static void vga_text_syscall(u16 ss, u32 ebx, u32 edx)
{
    switch(ss)
    {
        case 1:
        {
            u8 ch = ebx << 24 >> 24;
            u8 color = edx << 24 >> 24;
            vga_text_putc(ch, color);
            break;
        }
        case 2:
        {
            if(ptr_validate(ebx, current_process->page_directory))
            {
                u8* str = (u8*) ebx;
                u8 color = edx << 24 >> 24;
                vga_text_puts(str, color);
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

static void kbd_syscall(u16 ss, u32 ebx)
{
    switch(ss)
    {
        case 1:
        {
            if(ptr_validate(ebx, current_process->page_directory))
            {
                u8* ptr = (u8*) ebx;
                *ptr = kbd_getkeycode();
            }
            break;
        }
        case 2:
        {
            if(ptr_validate(ebx, current_process->page_directory))
            {
                u8* ptr = (u8*) ebx;
                *ptr = kbd_getkeychar();
            }
            break;
        }
        default:
            break;
    }
}

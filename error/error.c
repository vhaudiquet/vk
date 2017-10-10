#include "../system.h"

void vga_text_nl();

void _fatal_kernel_error(const char* error_message, const char* error_context, char* err_file, u32 err_line)
{
    //Switch back to VGA TEXT Video Mode
    asm("cli");
    vga_text_nl();
    kprintf("%v[FATAL ERROR] : %s\n", 0b00001100, error_message);
    kprintf("%v[CONTEXT] : %s\n", 0b00001100, error_context);
    kprintf("%v[LOCATION] : %s:%d\n", 0b00001100, err_file, err_line);
    kprintf("%l-- SYSTEM HALTED --\n", 2);
    asm("hlt");
}

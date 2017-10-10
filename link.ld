ENTRY(start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    . = 0xC0100000;
    _kernel_start = .;

    .text : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
        *(.rodata*)
    }

    .data : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data)
    }

    .bss : AT(ADDR(.bss) - 0xC0000000)
    {
        *(COMMON)
        *(.bss)
    }

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)    
        *(.eh_frame) /* if c++ */
        *(.debug_info)
        *(.debug_abbrev)
        *(.debug_loc)
        *(.debug_aranges)
        *(.debug_ranges)
        *(.debug_line)
        *(.debug_str)
    }
}
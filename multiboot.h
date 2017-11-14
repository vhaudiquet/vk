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

#ifndef MULTIBOOT_HEAD
#define MULTIBOOT_HEAD
#include "system.h"

/* The section header table for ELF. */
typedef struct elf_section_header_table
{
    unsigned long num;
    unsigned long size;
    unsigned long addr;
    unsigned long shndx;
} __attribute__ ((packed)) elf_section_header_table_t;

typedef struct
{
    unsigned long flags;
    unsigned long mem_lower;
    unsigned long mem_upper;
    unsigned long boot_device;
    unsigned long cmdline;
    unsigned long mods_count;
    unsigned long mods_addr;
    elf_section_header_table_t elf_sec;
    unsigned long mmap_length;
    unsigned long mmap_addr;
} __attribute__ ((packed)) multiboot_info_t;

/* The memory map. Be careful that the offset 0 is base_addr_low
but no size. */ //this means that size is at offset -4 (anyway idk i dont understand that works and we are fine)
typedef struct memory_map
{
    u32 size;
    u64 base_addr;
    u64 length;
    u32 type;
} __attribute__ ((packed)) memory_map_t;

#endif

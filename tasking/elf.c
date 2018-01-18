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

#include "task.h"
#include "memory/mem.h"

/* 
* This file provides ways to read an ELF executable and copy it in memory
*/

typedef struct elf_header
{
    u8 magic[4];
    u8 bits;
    u8 endianness;
    u8 version_0;
    u8 unused[9];
    u16 type;
    u16 instruction_set;
    u32 version_1;
    u32 program_entry;
    u32 program_header_table;
    u32 section_header_table;
    u32 flags;
    u16 header_size;
    u16 ph_entry_size;
    u16 ph_entry_nbr;
    u16 sh_entry_size;
    u16 sh_entry_nbr;
    u16 sh_index;
} elf_header_t;

typedef struct elf_program_header
{
    u32 segment_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 undefined;
    u32 p_filesz;
    u32 p_memsz;
    u32 flags;
    u32 align;
} elf_program_header_t;

bool elf_check(fd_t* file)
{
    //ignoring offset
    u64 old_offset = file->offset;
    file->offset = 0;

    //read header to check it
    elf_header_t eh;
    read_file(file, &eh, sizeof(elf_header_t));

    //restoring offset
    file->offset = old_offset;

    //check magic
    if((eh.magic[0] != 0x7F) | (eh.magic[1] != 'E') | (eh.magic[2] != 'L') | (eh.magic[3] != 'F')) return false;

    //check 32bits
    if(eh.bits != 1) return false;

    //check executable
    if(eh.type != 2) return false;

    //check instruction set
    if((eh.instruction_set != 0) & (eh.instruction_set != 3)) return false;

    return true;
}

void* elf_load(fd_t* file, u32* page_directory, list_entry_t* data_loc, u32* data_size)
{
    u8* buffer = 
    #ifdef MEMLEAK_DBG
    kmalloc((u32) file->file->length, "ELF loading buffer");
    #else
    kmalloc((u32) file->file->length);
    #endif

    //ignoring offset
    u64 old_offset = file->offset;
    file->offset = 0;

    read_file(file, buffer, flength(file));

    //restoring offset
    file->offset = old_offset;

    elf_header_t* header = (elf_header_t*) buffer;

    elf_program_header_t* prg_h = (elf_program_header_t*) (buffer + header->program_header_table);
    for(u32 i = 0; i < header->ph_entry_nbr; i++)
    {
        if((u32) prg_h+i > (u32) buffer+flength(file)) return 0;
        if(prg_h[i].p_memsz == 0) continue;

        map_memory(prg_h[i].p_memsz, prg_h[i].p_vaddr, page_directory);
        
        //marking allocated memory on the list
        data_loc->element = kmalloc(sizeof(u32)*2);
        ((u32*)data_loc->element)[0] = prg_h[i].p_vaddr;
        ((u32*)data_loc->element)[1] = prg_h[i].p_memsz;
        data_loc->next = kmalloc(sizeof(list_entry_t));
        data_loc = data_loc->next;
        (*data_size)++;

        pd_switch(page_directory);
        memcpy((void*)prg_h[i].p_vaddr, buffer + prg_h[i].p_offset, prg_h[i].p_filesz);
        memset((void*)prg_h[i].p_vaddr + prg_h[i].p_filesz, 0, prg_h[i].p_memsz - prg_h[i].p_filesz);
        pd_switch(kernel_page_directory);
    }
    //freeing last list entry, unused
    kfree(data_loc);

    void* tr = (void*) header->program_entry;
    kfree(buffer);
    return tr;
}
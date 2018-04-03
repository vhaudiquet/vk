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

#include "system.h"
#include "util/util.h"
#include "cpu/cpu.h"
#include "video/video.h"
#include "memory/mem.h"
#include "internal/internal.h"
#include "storage/storage.h"
#include "error/error.h"
#include "filesystem/fs.h"
#include "filesystem/devfs.h"
#include "tasking/task.h"
#include "time/time.h"
#include "io/io.h"
void args_parse(char* cmdline);

void kmain(multiboot_info_t* mbt, void* stack_pointer)
{
    //Current status : 32 bits, protected mode, interrupts off, paging on (0-4MiB mapped to 0xC0000000) 

    //getting command line and parsing arguments
    if((mbt->flags & 0b100))
        args_parse((char*) mbt->cmdline+KERNEL_VIRTUAL_BASE);

    //init
    vga_setup(); //Setup VGA : set video_mode to 80x25 TEXT, get display type, set and clean VRAM, disable cursor
    
    kprintf("Setting up CPU...");
    gdt_install(stack_pointer); //Install GDT : Null segment, Kernel code, Kernel data, User code, User data, TSS
    idt_install(); //TODO : ISRs ! (actually the handle isn't that bad but could be reaaally better)
    cpu_detect(); //TODO : Special handle INVALID_OPCODE
    vga_text_okmsg();

    kprintf("Setting up memory...");
    kheap_install(); //Install kernel heap (allows kmalloc(), kfree(), krealloc())
    physmem_get(mbt); //Setup physical memory map
    install_page_heap(); //Install page heap (allows pt_alloc(), pt_free()) (page tables / directories must be 4096 aligned)
    finish_paging(); //Enables page size extension
    kvmheap_install(); //Install Virtual Memory heap
    vga_text_okmsg();

    pic_install(); //Install PIC : remaps IRQ

    pci_install(); //Setup PCI devices
    install_block_devices(); //Setup block devices (ATA, ATAPI,...)

    process_init(); //Init process array
    scheduler_start();

    //DEBUG : printing kernel stack bottom / top ; code start/end
    //kprintf("%lKernel stack : 0x%X - 0x%X\n", 3, stack_pointer-8192, stack_pointer);
    //kprintf("%lKernel code : 0x%X - 0x%X\n", 3, &_kernel_start, &_kernel_end);

    kprintf("Getting kernel execution context...");
    //kprintf("%lARGS : m=%u, r=%s\n", 3, aboot_hint_present, aroot_dir);

    //getting live / root dir infos (for now we are ignoring args, we'll update that later)
    u8 root_drive = 0;
    u8 part_index = 1;
    u8 mode = 0; //aboot_hint_present
    if(!mode)
    {
        if(block_device_count == 0) {vga_text_failmsg(); fatal_kernel_error("No block device detected", "KERNEL_CONTEXT_GUESSING");}
        //the kernel will try to guess what mode is needed (relatively to boot_device)
        if(mbt->flags & 0b10)
        {
            if((mbt->boot_device >> 24 == 0x0) | (mbt->boot_device >> 24 == 0x1)) mode = 0;
            else if((mbt->boot_device >> 24 == 0x80) | (mbt->boot_device >> 24 == 0x81)) 
            {
                mode = KERNEL_MODE_INSTALLED;
                while(block_devices[root_drive]->device_class != HARD_DISK_DRIVE && root_drive < block_device_count)
                {
                    root_drive++;
                    //TODO : check if this loop ended well or not ; if not : fatal error
                }
            }
            else if(mbt->boot_device >> 24 == 0xE0) 
            {
                mode = KERNEL_MODE_LIVE;
                while(block_devices[root_drive]->device_class != CD_DRIVE && root_drive < block_device_count)
                {
                    root_drive++;
                    //TODO : check if this loop ended well or not ; if not : fatal error
                }
            }
            else if(mbt->boot_device >> 24 == 0xA0) 
            {
                mode = KERNEL_MODE_LIVE;
                while(block_devices[root_drive]->device_class != USB_DRIVE && root_drive < block_device_count)
                {
                    root_drive++;
                    //TODO : check if this loop ended well or not ; if not : fatal error
                }
            }
            else mode = 0;
        }

        if(!mode) {vga_text_failmsg(); fatal_kernel_error("Could not guess kernel context...", "KERNEL_CONTEXT_GUESSING");}
    }

    //printing kernel context
    char* context = (mode == KERNEL_MODE_LIVE ? "LIVE\n" : mode == KERNEL_MODE_INSTALLED ? "INSTALLED\n" : "FAILED\n");
    u8 color = (mode == KERNEL_MODE_LIVE ? 0b00001111 : mode == KERNEL_MODE_INSTALLED ? 0b00001111 : 0b00001100);
    vga_text_spemsg(context, color);

    //mounting root directory
    if(mode == KERNEL_MODE_LIVE)
    {
        //mounting live CD on /
        kprintf("Mounting root directory...");
        block_device_t* dev = block_devices[root_drive];
        if(!dev) fatal_kernel_error("Could not find root drive !", "LIVE_KERNEL_LOADING");
        if(!mount_volume("/", dev, (u8) 0))
        {
            vga_text_failmsg();
            fatal_kernel_error("Could not mount root directory.", "LIVE_KERNEL_LOADING");
        }
        vga_text_okmsg();
    }
    else if(mode == KERNEL_MODE_INSTALLED)
    {
        //mounting hard disk on /
        kprintf("Mounting root directory...");

        block_device_t* dev = block_devices[root_drive];
        if(!dev) fatal_kernel_error("Could not find root drive !", "INSTALLED_KERNEL_LOADING");
        if(!mount_volume("/", dev, (u8) (part_index)))
        {
            vga_text_failmsg();
            fatal_kernel_error("Could not mount root directory.", "INSTALLED_KERNEL_LOADING");
        }
        vga_text_okmsg();
    }

    //initializing/mounting devfs
    devfs_init();

    //initializing ttys
    ttys_init();

    //switching video output to tty1
    vga_text_tty_switch(current_tty);

    //create init process
    error_t init = spawn_init_process();
    if(init != ERROR_NONE)
    {
        kprintf("%lCould not run init process : %d\n", 2, init);
        fatal_kernel_error("Could not run init", "INIT_RUN");
    }

    scheduler_remove_process(kernel_process);

    //loop infinetely
    //(actually this is dead code, because kernel process is removed, and the scheduler switch to init process)
    while(1) asm("hlt");
}

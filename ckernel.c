#include "system.h"
#include "util/util.h"
#include "cpu/cpu.h"
#include "video/video.h"
#include "memory/mem.h"
#include "internal/internal.h"
#include "storage/storage.h"
#include "error/error.h"
#include "filesystem/fs.h"
#include "tasking/task.h"
void args_parse(char* cmdline);

//TODO : ATA PIO driver -> check for solutions on write_28, write_48, or delete them (as we dont need write on PIO if we have it on DMA)

void kmain(multiboot_info_t* mbt, void* stack_pointer)
{
    //Current status : 32 bits, protected mode, interrupts off, paging on (0-4MiB mapped to 0xC0000000) 

    //getting command line and parsing arguments
    if((mbt->flags & 0b100))
        args_parse((char*) mbt->cmdline+KERNEL_VIRTUAL_BASE);

    //init
    vga_setup(); //Setup VGA : set video_mode to 80x25 TEXT, get display type, set and clean VRAM, disable cursor
    gdt_install(stack_pointer); //Install GDT : Null segment, Kernel code, Kernel data, User code, User data, TSS
    idt_install(); //TODO : ISRs ! (actually the handle isn't that bad but could be reaaally better)
    cpu_detect(); //TODO : Special handle INVALID_OPCODE
    kheap_install(); //Install kernel heap (allows kmalloc(), kfree(), krealloc())
    physmem_get(mbt); //Setup physical memory map
    install_page_heap(); //Install page heap (allows pt_alloc(), pt_free()) (page tables / directories must be 4096 aligned)
    finish_paging(); //Enables page size extension

    pic_install(); //Install PIC : remaps IRQ

    pci_install(); //Setup PCI devices
    install_block_devices(); //Setup block devices (ATA, ATAPI,...)

    scheduler_init(); //Init scheduler
    scheduler_add_process(init_idle_process()); //Add idle_process to the loop, so that if there is no process the kernel don't crash
    init_kernel_process(); //Add kernel process as current_process (kernel init is not done yet)

    //kprintf("%lKernel stack : 0x%X - 0x%X\n", 3, stack_pointer-8192, stack_pointer);
    kprintf("Getting kernel execution context...");
    //kprintf("%lARGS : m=%u, r=%s\n", 3, aboot_hint_present, aroot_dir);
    asm("sti");

    //getting live / root dir infos
    u8 mode = aboot_hint_present;
    if(!mode)
    {
        //the kernel will try to guess what mode is needed (relatively to boot_device)
        if(mbt->flags & 0b10)
        {
            if((mbt->boot_device >> 24 == 0x0) | (mbt->boot_device >> 24 == 0x1)) mode = 0;
            else if((mbt->boot_device >> 24 == 0x80) | (mbt->boot_device >> 24 == 0x81)) 
            {
                mode = KERNEL_MODE_INSTALLED;
                if(hd_devices[0])
                    strncpy(aroot_dir, "hda1", 4);
                else if(sd_devices[0])
                    strncpy(aroot_dir, "sda1", 4);
                else fatal_kernel_error("Could not guess root device...\n", "KERNEL_CONTEXT_GUESSING");
            }
            else if(mbt->boot_device >> 24 == 0xE0) {mode = KERNEL_MODE_LIVE;strncpy(aroot_dir, "cd1", 3);}
            else if(mbt->boot_device >> 24 == 0xA0) {mode = KERNEL_MODE_LIVE;strncpy(aroot_dir, "usb1", 4);}
            else mode = 0;
        }

        if(!mode) {vga_text_failmsg(); fatal_kernel_error("Could not guess kernel context...", "KERNEL_CONTEXT_GUESSING");}
    }

    //printing kernel context
    char* context = (mode == KERNEL_MODE_LIVE ? "LIVE\n" : mode == KERNEL_MODE_INSTALLED ? "INSTALLED\n" : "FAILED\n");
    u8 color = (mode == KERNEL_MODE_LIVE ? 0b00001111 : mode == KERNEL_MODE_INSTALLED ? 0b00001111 : 0b00001100);
    vga_text_spemsg(context, color);
    
    //mounting / either to RAMFS (live) or DISK (installed)
    if(mode == KERNEL_MODE_LIVE)
    {
        //mount root dir in ramfs
        ramfs_t* rfs = ramfs_init(0x100000);
        mount("/", FS_TYPE_RAMFS, rfs);
        kprintf("%lramfs mounted '/'\n", 3);
        kprintf("live dir : %s\n", aroot_dir);
        //need to mount /sys
        fatal_kernel_error("mode currently not supported", "LIVE_KERNEL_LOADING");
    }
    else if(mode == KERNEL_MODE_INSTALLED)
    {
        //mount root dir with root_dir partition
        u8 drive_index = (u8) (aroot_dir[2] - 97);
        u8 part_index = (u8) (aroot_dir[3] - 49);
        char h = *aroot_dir;

        kprintf("Mounting root dir to drive %cd%c%u...", h, drive_index+'a', part_index+1);

        //kprintf("%lroot: drive %d, partition %d (type:%c)\n", 3, drive_index, part_index, h);

        hdd_device_t* dev = h == 'h' ? hd_devices[drive_index] : sd_devices[drive_index];
        if(!dev) fatal_kernel_error("Could not find root drive !", "INSTALLED_KERNEL_LOADING");
        if(!mount_volume("/", dev, part_index))
        {
            vga_text_failmsg();
            fatal_kernel_error("Could not mount root dir.", "INSTALLED_KERNEL_LOADING");
        }
        vga_text_okmsg();
    }
    
    //running /sys/init
    file_descriptor_t* init_file = open_file("/sys/init");//open_file("/boot/grub/fonts/unicode.pf2");
    if(!init_file) fatal_kernel_error("Could not open init file.", "INIT_RUN");
    char* init_buffer =
    #ifdef MEMLEAK_DBG
    kmalloc((u32) init_file->length+1, "init file buffer");
    #else
    kmalloc((u32) init_file->length+1);
    #endif
    memset(init_buffer, 0, (size_t) init_file->length+1);
    read_file(init_file, init_buffer, init_file->length);
    init_buffer[init_file->length-1] = 0;

    kprintf("[INIT] Opening %s...", init_buffer);
    file_descriptor_t* elf_task = open_file(init_buffer);
    if(!elf_task) {vga_text_failmsg(); fatal_kernel_error("Could not open init process file", "INIT_RUN");}
    else vga_text_okmsg();

    process_t* init_process = create_process(elf_task);
    if(!init_process) fatal_kernel_error("Could not create init process", "INIT_RUN");

    fd_free(elf_task);

    kfree(init_buffer);
    fd_free(init_file);

    kprintf("[INIT] Init task loaded in memory at 0x%X\n", init_process->eip);

    scheduler_add_process(init_process);

    //file_descriptor_t* dbg_task_file = open_file("/sys/dbg.elf");
    //process_t* dbg_process = create_process(dbg_task_file);
    //scheduler_add_process(dbg_process);

    //enabling interrupts
    asm("sti");

    scheduler_remove_process(kernel_process);
    //loop infinetely
    //(actually this is dead code, because as soon as the interrupts gets enabled, the scheduler switch to init process)
    while(1) asm("hlt");
}
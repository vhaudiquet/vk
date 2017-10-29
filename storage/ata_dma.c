/*  
    This file is part of VK.

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

#include "storage.h"
#include "memory/mem.h"
#include "tasking/task.h"
#include "internal/internal.h"
#include "error/error.h"

#define BYTES_PER_SECTOR 512

typedef struct PRD
{
    u32 data_pointer;
    u16 byte_count;
    u16 reserved;
} __attribute__((packed)) prd_t;

//PRDT : u32 aligned, contiguous in physical, cannot cross 64k boundary,  1 per ATA bus
u8 ata_dma_read_28(u32 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
    if(sector & 0xF0000000) return DISK_FAIL_OUT;

    kprintf("ATA_DMA_READ_28: sector 0x%X ; count %u B\n", sector, count);

    pci_write_device(drive->controller, 0x04, pci_read_device(drive->controller, 0x04)|7);

    /*prepare a PRDT (Physical Region Descriptor Table)*/
    u32 virtual = 0xE0800000;
    u32 prdt_phys = reserve_block(sizeof(prd_t)+4, PHYS_KERNELF_BLOCK_TYPE);
    u32 prdt_phys_aligned = prdt_phys; alignup(prdt_phys_aligned, sizeof(u32));
    map_flexible(sizeof(prd_t), prdt_phys_aligned, virtual, kernel_page_directory);
    prd_t* prd = (prd_t*) virtual;
    prd->data_pointer = get_physical((u32) data, current_process->page_directory); 
    //TEMP: this can't work (the adress maybe isnt contiguous, or cross 64kib boundary, ...)
    //the solution is to map enough memory to do the transfer, then memcpy it to the destination and unmap memory/unmark it
    //kprintf("%lphysical pointer : 0x%X\n", 2, prd->data_pointer);
    prd->byte_count = (u16) count;
    prd->reserved = 0x8000;
    unmap_flexible(sizeof(prd_t), virtual, kernel_page_directory);

    /*Send the physical PRDT addr to Bus Master PRDT Register*/
    u32 bar4 = pci_read_device(drive->controller, BAR4);

    //temp
    if(!(bar4 & 1)) fatal_kernel_error("BAR4 is not I/O ports but memory mapped !", "ATA_DMA_READ_28");

    bar4 = (bar4 & 0xFFFFFFFC) + (drive->master ? 0 : 8);
    
    //TEMP : Reset start/stop bit
    //outb(bar4, inb(bar4) & ~1);

    //kprintf("%lbar4 = 0x%X\n", 3, bar4);
    outl(bar4+4, prdt_phys_aligned);

    /*set read bit in the Bus Master Command Register*/
    outb(bar4, inb(bar4) | 8); // Set read bit

    /*Clear err/interrupt bits in Bus Master Status Register*/
    outb(bar4 + 2, inb(bar4 + 2) & (0x04 | 0x02)); //Clear interrupt and error flags

    /*Select drive*/
	outb(DEVICE_PORT(drive), (drive->master ? 0xE0 : 0xF0) | ((sector & 0x0F000000) >> 24));

    /*Send LBA and sector count*/
    //calculate sector count
	u32 scount = count / BYTES_PER_SECTOR;
    if(count % BYTES_PER_SECTOR) scount++;
	//calculate sector offset
    while(offset>=BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}
    if(scount > 255) return DISK_FAIL_INTERNAL;
    
    //clean previous error msgs
    outb(ERROR_PORT(drive), 0);

    outb(SECTOR_COUNT_PORT(drive), scount);

    outb(LBA_LOW_PORT(drive), (sector & 0x000000FF));
	outb(LBA_MID_PORT(drive), (sector & 0x0000FF00) >> 8);
	outb(LBA_HI_PORT(drive), (sector & 0x00FF0000) >> 16);

    /*send DMA transfer command*/
    outb(COMMAND_PORT(drive), 0xC8); //28bits DMA read : 0xC8

    kprintf("%lWaiting for irq %u...\n", 3, drive->irq);

    /*Set the Start/Stop bit in Bus Master Command Register*/
    outb(bar4, inb(bar4) | 1); // Set start/stop bit

    /*Wait for interrupt*/
    scheduler_wait_process(kernel_process, SLEEP_WAIT_IRQ, 14);

    kprintf("%lInterrupt received !\n", 3);
    
    /*Reset Start/Stop bit*/
    outb(bar4, inb(bar4) & ~1); // Clear start/stop bit

    free_block(prdt_phys);

    /*Read controller and drive status to see if transfer went well*/
    u8 end_status = inb(bar4+2);
    u8 end_disk_status = inb(COMMAND_PORT(drive));

    kprintf("end controller status : 0x%X ; disk : 0x%X\n", end_status, end_disk_status);
    
    return DISK_SUCCESS; //TODO: check if transfer went well
}

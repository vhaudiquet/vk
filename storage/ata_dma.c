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

    //kprintf("ATA_DMA_READ: sector 0x%X ; count %u B\n", sector, count);

    /*prepare a PRDT (Physical Region Descriptor Table)*/
    u32 virtual = 0xE0800000;
    u32 prdt_phys = reserve_block(sizeof(prd_t)+4, PHYS_KERNELF_BLOCK_TYPE);
    u32 prdt_phys_aligned = prdt_phys; alignup(prdt_phys_aligned, sizeof(u32));
    map_flexible(sizeof(prd_t), prdt_phys_aligned, virtual, kernel_page_directory);
    prd_t* prd = (prd_t*) virtual;
    prd->data_pointer = get_physical((u32) data, current_process->page_directory); //TEMP: this can't work (the adress maybe isnt contiguous, or cross 64kib boundary, ...)
    //the solution is to map enough memory to do the transfer, than memcpy it to the destination and unmap memory/unmark it
    kprintf("%lphysical pointer : 0x%X\n", 2, prd->data_pointer);
    prd->byte_count = (u16) count;
    prd->reserved = 0x8000;

    /*Send the physical PRDT addr to Bus Master PRDT Register*/
    u32 bar4 = pci_read_device(drive->controller, BAR4);

    //TEMP : Setting a bit into PCI Command register (saw that in some documentation/wiki/osdevforum)
    //u32 cs = pci_read_device(drive->controller, 0x04);
    //kprintf("cmd/status = 0x%X\n", cs);
    //pci_write_device(drive->controller, 0x04, cs|2);
    //kprintf("newcmd/status = 0x%X\n", pci_read_device(drive->controller, 0x04));

    //temp
    if(!(bar4 & 1)) fatal_kernel_error("BAR4 is not I/O ports but memory mapped !", "ATA_DMA_READ_28");

    bar4 = (bar4 & 0xFFFFFFFC) + (drive->master ? 0 : 8);
    
    //TEMP : Reset start/stop bit
    outb(bar4, inb(bar4) & ~1);

    kprintf("%lbar4 = 0x%X\n", 3, bar4);
    outl(bar4+4, prdt_phys_aligned);

    /*set read bit in the Bus Master Command Register*/
    //u8 command = inb(bar4);
    //outb(bar4, command & 0b11110111);
    outb(bar4, inb(bar4) | 8); // Set read bit

    /*Clear err/interrupt bits in Bus Master Status Register*/
    //u8 status = inb((bar4)+2);
    //outb(bar4+2, status | 0b00000110);
    outb(bar4 + 2, inb(bar4 + 2) & ~(0x04 | 0x02)); //Clear interrupt and error flags

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

    u8 statuss = inb(COMMAND_PORT(drive));
	u32 times = 0;
	while(((statuss & 0x80) == 0x80) && ((statuss & 0x01) != 0x01) && ((statuss & 0x20) != 0x20) && times < 0xFFFFF)
		{statuss = inb(COMMAND_PORT(drive)); times++;}

	//TEMP
	if(statuss == 0x20) {kprintf("%lDRIVE FAULT !\n", 2); return DISK_FAIL_BUSY;}
	if(times == 0xFFFFF) {kprintf("%lCOULD NOT REACH DEVICE (TIMED OUT)\n", 2); return DISK_FAIL_BUSY;}

    /*send DMA transfer command*/
    outb(COMMAND_PORT(drive), 0xC8); //28bits DMA read : 0xC8

    /*Set the Start/Stop bit in Bus Master Command Register*/
    //outb(bar4, command | 1);
    outb(bar4, inb(bar4) | 1); // Set start/stop bit

    /*Wait for interrupt*/
    kprintf("%lWaiting for irq %u...\n", 3, drive->irq);
    scheduler_wait_process(kernel_process, SLEEP_WAIT_IRQ, drive->irq);

    /*Reset Start/Stop bit*/
    //outb(bar4, command & 0b11111110);
    outb(bar4, inb(bar4) & ~0x1); // Clear start/stop bit

    /*Read controller and drive status to see if transfer went well*/
    return DISK_SUCCESS; //TODO: check if transfer went well
}
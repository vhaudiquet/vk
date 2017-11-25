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

#include "../storage.h"
#include "memory/mem.h"
#include "tasking/task.h"
#include "internal/internal.h"
#include "error/error.h"

typedef struct PRD
{
    u32 data_pointer;
    u16 byte_count;
    u16 reserved;
} __attribute__((packed)) prd_t;

u8 ata_dma_read_flexible(u64 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
    if(!count) return DISK_SUCCESS;
    
    u32 bps = ((drive->flags & ATA_FLAG_ATAPI) ? ATAPI_SECTOR_SIZE : BYTES_PER_SECTOR);

    /* Calculate sector count */
    //calculate sector count
    u32 scount = count / bps;
    if(count % bps) scount++;
    
    //calculate sector offset
    while(offset>=bps) {offset-=bps; sector++; scount--;}
    if(scount > 127) return DISK_FAIL_INTERNAL;
    if((scount > 31) && (drive->flags & ATA_FLAG_ATAPI)) return DISK_FAIL_INTERNAL;

    /*prepare a PRDT (Physical Region Descriptor Table)*/
    //PRDT : u32 aligned, contiguous in physical, cannot cross 64k boundary,  1 per ATA bus
    u32 virtual = kvm_reserve_block(sizeof(prd_t)+scount*bps);
    u32 prdt_phys = reserve_block(sizeof(prd_t)+4+scount*bps, PHYS_KERNELF_BLOCK_TYPE);
    u32 prdt_phys_aligned = prdt_phys; alignup(prdt_phys_aligned, sizeof(u32));
    map_flexible(sizeof(prd_t)+scount*bps, prdt_phys_aligned, virtual, kernel_page_directory);
    prd_t* prd = (prd_t*) virtual;
    prd->data_pointer = prdt_phys_aligned+sizeof(prd_t);
    prd->byte_count = (u16) (scount*bps);//count;
    prd->reserved = 0x8000;

    /*Send the physical PRDT addr to Bus Master PRDT Register*/
    //stop
    outb(drive->bar4, 0x0);
    //prdt
    outl(drive->bar4+4, prdt_phys_aligned);

    /*set read bit in the Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) | 8); // Set read bit

    /*Clear err/interrupt bits in Bus Master Status Register*/
    outb(drive->bar4 + 2, inb(drive->bar4 + 2) | 0x04 | 0x02); //Clear interrupt and error flags   

    if(drive->flags & ATA_FLAG_ATAPI)
    {
        if(sector & 0xF0000000) return DISK_FAIL_OUT;
        if(atapi_cmd_dma_read_28((u32) sector, drive) != DISK_SUCCESS) return DISK_FAIL_BUSY;
    }
    else if(sector > U32_MAX)
    {
        if(!(drive->flags & ATA_FLAG_LBA48)) return DISK_FAIL_OUT;
        if(sector & 0xFFFF000000000000) return DISK_FAIL_OUT;
        ata_cmd_48(sector, scount, ATA_CMD_DMA_READ_48, drive);
    }
    else
    {
        if(sector & 0xF0000000) return DISK_FAIL_OUT;
        ata_cmd_28((u32) sector, (u32) scount, ATA_CMD_DMA_READ_28, drive);        
    }

    /*Set the Start/Stop bit in Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) | 1); // Set start/stop bit
    
    /*Wait for interrupt*/
    scheduler_wait_process(kernel_process, SLEEP_WAIT_IRQ, drive->irq);
    
    //kprintf("%lInterrupt received !\n", 3);
        
    /*Reset Start/Stop bit*/
    outb(drive->bar4, inb(drive->bar4) & ~1); // Clear start/stop bit
    
    /*Read controller and drive status to see if transfer went well*/
    u8 end_status = inb(drive->bar4+2);
    u8 end_disk_status = inb(COMMAND_PORT(drive));

    //kprintf("end controller status : 0x%X ; disk : 0x%X\n", end_status, end_disk_status);
    
    memcpy(data, (void*)(virtual+sizeof(prd_t)+offset), count);
    free_block(prdt_phys);
    unmap_flexible(sizeof(prd_t)+scount*512, virtual, kernel_page_directory);
    kvm_free_block(virtual);

    if(!(end_status & 5)) return DISK_FAIL_INTERNAL;
    if(end_status & 2) return DISK_FAIL_INTERNAL;
    if(end_disk_status & 1) return DISK_FAIL_INTERNAL;

    return DISK_SUCCESS;
}

u8 ata_dma_write_flexible(u64 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
    if(!count) return DISK_SUCCESS;
    
    //calculate sector count
    u32 scount = (u32)(count / BYTES_PER_SECTOR);
    if(count % BYTES_PER_SECTOR) scount++;

    //calculate sector offset
    while(offset>BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}

    /* cache first and last sector if necessary */
    u8* cached_f = 0;
	if(offset | (count%512)) //cache starting sector (before any out cmd)
	{
		cached_f = 
		#ifdef MEMLEAK_DBG
		kmalloc(BYTES_PER_SECTOR, "ATA write first sector caching");
		#else
		kmalloc(BYTES_PER_SECTOR);
		#endif
		ata_dma_read_flexible(sector, 0, cached_f, BYTES_PER_SECTOR, drive);
	}

	//calculate last sector offset
	u64 last_sector = sector+((count/512)*512); //does that actually works ?

	u8* cached_l = 0;
	if((last_sector != sector) && (count % 512)) //cache last sector
	{
		cached_l = 
		#ifdef MEMLEAK_DBG
		kmalloc(BYTES_PER_SECTOR, "ATA write last sector caching");
		#else
		kmalloc(BYTES_PER_SECTOR);
		#endif
		ata_dma_read_flexible(last_sector, 0, cached_l, BYTES_PER_SECTOR, drive);		
    }
    
    /*prepare a PRDT (Physical Region Descriptor Table)*/
    //PRDT : u32 aligned, contiguous in physical, cannot cross 64k boundary,  1 per ATA bus
    u32 virtual = kvm_reserve_block(sizeof(prd_t)+scount*BYTES_PER_SECTOR);
    u32 prdt_phys = reserve_block(sizeof(prd_t)+4+scount*BYTES_PER_SECTOR, PHYS_KERNELF_BLOCK_TYPE);
    u32 prdt_phys_aligned = prdt_phys; alignup(prdt_phys_aligned, sizeof(u32));
    map_flexible(sizeof(prd_t)+scount*BYTES_PER_SECTOR, prdt_phys_aligned, virtual, kernel_page_directory);
    memcpy((void*)(virtual+sizeof(prd_t)+offset), data, count);
    unmap_flexible(sizeof(prd_t)+scount*512, virtual, kernel_page_directory);
    kvm_free_block(virtual);
    prd_t* prd = (prd_t*) virtual;
    prd->data_pointer = prdt_phys_aligned+sizeof(prd_t);
    prd->byte_count = (u16) (scount*BYTES_PER_SECTOR);//count;
    prd->reserved = 0x8000;

    /*Send the physical PRDT addr to Bus Master PRDT Register*/
    //stop
    outb(drive->bar4, 0x0);
    //prdt
    outl(drive->bar4+4, prdt_phys_aligned);

    /*set read bit in the Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) & ~8); // Set read bit

    /*Clear err/interrupt bits in Bus Master Status Register*/
    outb(drive->bar4 + 2, inb(drive->bar4 + 2) | 0x04 | 0x02); //Clear interrupt and error flags

    if(sector > U32_MAX)
    {
        if(!(drive->flags & ATA_FLAG_LBA48)) return DISK_FAIL_OUT;
        if(sector & 0xFFFF000000000000) return DISK_FAIL_OUT;
        ata_cmd_48(sector, scount, ATA_CMD_DMA_WRITE_48, drive);
    }
    else
    {
        if(sector & 0xF0000000) return DISK_FAIL_OUT;
        ata_cmd_28((u32) sector, (u32) scount, ATA_CMD_DMA_WRITE_28, drive);        
    }

    /*Set the Start/Stop bit in Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) | 1); // Set start/stop bit
    
    //kprintf("%lWaiting for interrupt...", 3);

    /*Wait for interrupt*/
    scheduler_wait_process(kernel_process, SLEEP_WAIT_IRQ, drive->irq);
    
    //kprintf("%lInterrupt received !\n", 3);
    
    /*Reset Start/Stop bit*/
    outb(drive->bar4, inb(drive->bar4) & ~1); // Clear start/stop bit
    
    /*Read controller and drive status to see if transfer went well*/
    u8 end_status = inb(drive->bar4+2);
    u8 end_disk_status = inb(COMMAND_PORT(drive));

    //kprintf("end controller status : 0x%X ; disk : 0x%X\n", end_status, end_disk_status);
    
    free_block(prdt_phys);

    if(!(end_status & 5)) return DISK_FAIL_INTERNAL;
    if(end_status & 2) return DISK_FAIL_INTERNAL;
    if(end_disk_status & 1) return DISK_FAIL_INTERNAL;

    return DISK_SUCCESS;
}
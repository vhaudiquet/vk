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

error_t ata_dma_read_flexible(u64 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
    if(!count) return ERROR_NONE;
    if(mutex_lock(drive->mutex) != ERROR_NONE) return ERROR_MUTEX_ALREADY_LOCKED;

    u32 bps = ((drive->flags & ATA_FLAG_ATAPI) ? ATAPI_SECTOR_SIZE : BYTES_PER_SECTOR);

    /* Calculate sector count */
    u32 scount = count / bps;
    if(count % bps) scount++;
    
    if(scount > 127) return ERROR_DISK_INTERNAL;
    if((scount > 31) && (drive->flags & ATA_FLAG_ATAPI)) return ERROR_DISK_INTERNAL;

    //kprintf("%lDMA: sector %x scount %x (offset %x, buffer %x) drive %x\n", 3, ((u32)sector), scount, offset, data, drive);
    /*set read bit in the Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) | 8); // set read bit

    /*Clear err/interrupt bits in Bus Master Status Register*/
    outb(drive->bar4 + 2, inb(drive->bar4 + 2) | 0x04 | 0x02); //Clear interrupt and error flags   

    if(drive->flags & ATA_FLAG_ATAPI)
    {
        if(sector & 0xF0000000) {mutex_unlock(drive->mutex);return ERROR_DISK_OUT;}
        if(atapi_cmd_dma_read_28((u32) sector, drive) != ERROR_NONE) 
        {mutex_unlock(drive->mutex); return ERROR_DISK_BUSY;}
    }
    else if(sector > U32_MAX)
    {
        if(!(drive->flags & ATA_FLAG_LBA48)) {mutex_unlock(drive->mutex); return ERROR_DISK_OUT;}
        if(sector & 0xFFFF000000000000) {mutex_unlock(drive->mutex); return ERROR_DISK_OUT;}
        ata_cmd_48(sector, scount, ATA_CMD_DMA_READ_48, drive);
    }
    else
    {
        if(sector & 0xF0000000) {mutex_unlock(drive->mutex); return ERROR_DISK_OUT;}
        ata_cmd_28((u32) sector, (u32) scount, ATA_CMD_DMA_READ_28, drive);        
    }

    /*Set the Start/Stop bit in Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) | 1); // Set start/stop bit
    
    /*Wait for interrupt*/
    scheduler_wait_process(kernel_process, SLEEP_WAIT_IRQ, drive->irq, 5000);
    
    /*Reset Start/Stop bit*/
    outb(drive->bar4, inb(drive->bar4) & (~1)); // Clear start/stop bit
    
    /*Read controller and drive status to see if transfer went well*/
    u8 end_status = inb(drive->bar4+2);
    u8 end_disk_status = inb(COMMAND_PORT(drive));

    //kprintf("end controller status : 0x%X ; disk : 0x%X\n", end_status, end_disk_status);
    
    //copying data from PRDT
    memcpy(data, (void*)(((u32)drive->prdt_virt)+sizeof(prd_t)+offset), count);

    //unlock drive mutex
    mutex_unlock(drive->mutex);

    //neither the active bit or the interrupt bit are set : error
    if(!(end_status & 5)) 
    {kprintf("%lATA_DMA_READ : status = 0x%X\n", 3, end_status); outb(CONTROL_PORT(drive), 0x4); return ERROR_DISK_INTERNAL;}
    //DMA ERROR bit is set
    if(end_status & 2) 
    {kprintf("%lATA_DMA_READ: ends, status = 0x%X, pci status = 0x%X\n", 3, end_status, pci_read_device(drive->controller, 0x06)); outb(CONTROL_PORT(drive), 0x4); return ERROR_DISK_INTERNAL;}
    //disk status errror bit is set
    if(end_disk_status & 1) {outb(CONTROL_PORT(drive), 0x4); return ERROR_DISK_INTERNAL;}

    return ERROR_NONE;
}

error_t ata_dma_write_flexible(u64 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
    if(!count) return ERROR_NONE;
    
    //calculate sector count
    u32 scount = (u32)(count / BYTES_PER_SECTOR);
    if(count % BYTES_PER_SECTOR) scount++;

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
    
    //copying data in PRDT
    memcpy((void*)(((u32)drive->prdt_virt)+sizeof(prd_t)+offset), data, count);

    /*set read bit in the Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) & ~8); // Set read bit

    /*Clear err/interrupt bits in Bus Master Status Register*/
    outb(drive->bar4 + 2, 0x04 | 0x02); //Clear interrupt and error flags

    if(sector > U32_MAX)
    {
        if(!(drive->flags & ATA_FLAG_LBA48)) return ERROR_DISK_OUT;
        if(sector & 0xFFFF000000000000) return ERROR_DISK_OUT;
        ata_cmd_48(sector, scount, ATA_CMD_DMA_WRITE_48, drive);
    }
    else
    {
        if(sector & 0xF0000000) return ERROR_DISK_OUT;
        ata_cmd_28((u32) sector, (u32) scount, ATA_CMD_DMA_WRITE_28, drive);        
    }

    /*Set the Start/Stop bit in Bus Master Command Register*/
    outb(drive->bar4, inb(drive->bar4) | 1); // Set start/stop bit
    
    //kprintf("%lWaiting for interrupt...", 3);

    /*Wait for interrupt*/
    scheduler_wait_process(kernel_process, SLEEP_WAIT_IRQ, drive->irq, 5000);
    
    //kprintf("%lInterrupt received !\n", 3);
    
    /*Reset Start/Stop bit*/
    outb(drive->bar4, inb(drive->bar4) & ~1); // Clear start/stop bit
    
    /*Read controller and drive status to see if transfer went well*/
    u8 end_status = inb(drive->bar4+2);
    u8 end_disk_status = inb(COMMAND_PORT(drive));

    //kprintf("end controller status : 0x%X ; disk : 0x%X\n", end_status, end_disk_status);

    if(!(end_status & 5)) return ERROR_DISK_INTERNAL;
    if(end_status & 2) return ERROR_DISK_INTERNAL;
    if(end_disk_status & 1) return ERROR_DISK_INTERNAL;

    return ERROR_NONE;
}
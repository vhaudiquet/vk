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

//This file is an ATA PIO driver (used to access drives)
//we are using the "polling the status" technique, and so, avoiding IRQs (would be great to disable them but BUGS (cf osdev))
//SO : this driver cannot be used in real env : the max speed is 16MiB/s and it consumes all CPU time
//but it is great in boot env, to like detect and setup the disks, and get the partition tables, as we have no multitasking yet
//and it doesnt use irqs, and ...
#include "../storage.h"
#include "memory/mem.h"

static void ata_cmd_pio_read_28(u32 sector, u32 scount, ata_device_t* drive)
{
	//kprintf("ATA_PIO_READ_28: sector 0x%X ; count %u sectors\n", sector, scount);

	//select drive and write sector addr (4 upper bits)
	outb(DEVICE_PORT(drive), ((drive->flags & ATA_FLAG_MASTER) ? 0xE0 : 0xF0) | ((sector & 0x0F000000) >> 24));
	ata_io_wait(drive); //wait for drive selection

	//clean previous error msgs
	outb(ERROR_PORT(drive), 0);

	//number of sectors to read
	outb(SECTOR_COUNT_PORT(drive), scount);

	//write sector addr (24 bits left)
	outb(LBA_LOW_PORT(drive), (sector & 0x000000FF));
	outb(LBA_MID_PORT(drive), (sector & 0x0000FF00) >> 8);
	outb(LBA_HI_PORT(drive), (sector & 0x00FF0000) >> 16);

	//command read
	outb(COMMAND_PORT(drive), 0xC4); //0x20: read, 0xC4: read multiple
}

static void ata_cmd_pio_read_48(u64 sector, u32 scount, ata_device_t* drive)
{
	//select drive
	outb(DEVICE_PORT(drive), ((drive->flags & ATA_FLAG_MASTER) ? 0x40 : 0x50));
	ata_io_wait(drive); //wait for drive selection

	//write sector count top bytes
	outb(SECTOR_COUNT_PORT(drive), 0);

	//split address in 8-bits values
	u8* addr = (u8*) ((u64*)&sector);
	//write address high bytes to the 3 ports
	outb(LBA_LOW_PORT(drive), addr[3]);
	outb(LBA_MID_PORT(drive), addr[4]);
	outb(LBA_HI_PORT(drive), addr[5]);
	
	//write sector count low bytes
	outb(SECTOR_COUNT_PORT(drive), scount);
	
	//write address low bytes to the 3 ports
	outb(LBA_LOW_PORT(drive), addr[0]);
	outb(LBA_MID_PORT(drive), addr[1]);
	outb(LBA_HI_PORT(drive), addr[2]);

	//command extended read
	outb(COMMAND_PORT(drive), 0x29); //0x24 = READ_SECTOR_EXT (ext = 48) //0x29 : READ MULTIPLE EXT ?
}

u8 ata_pio_read_flexible(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
	if(!count) return DISK_SUCCESS;

	//calculate sector count
	u32 scount = (u32) (count / BYTES_PER_SECTOR);
	if(count % BYTES_PER_SECTOR) scount++;

	//calculate sector offset
	while(offset>=BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}

	if(scount > 255) return DISK_FAIL_INTERNAL;

	if(sector > U32_MAX)
	{
		if(!(drive->flags & ATA_FLAG_LBA48)) return DISK_FAIL_OUT;
		//verify that sector is really 48-bits integer
		if(sector & 0xFFFF000000000000) return DISK_FAIL_OUT;
		ata_cmd_pio_read_48(sector, scount, drive);
	}
	else
	{
		//verify that sector is really a 28-bits integer
		if(sector & 0xF0000000) return DISK_FAIL_OUT;
		ata_cmd_pio_read_28((u32) sector, (u32) scount, drive);
	}

	//wait for drive to be ready
	u8 status = ata_pio_poll_status(drive);
	if(status & 0x1) return DISK_FAIL_BUSY;

	//actually read the data
	u32 data_read = 0;
	u32 i;
	for(i = 0; i < (count+offset); i+=2)
	{
		if(i+1<offset) inw(DATA_PORT(drive)); 
		else 
		{
			u16 wdata = inw(DATA_PORT(drive));
			if(i>=offset)
				data[i-offset] = (u8) wdata & 0x00FF;
			if(i+1 < count+offset)
				data[i+1-offset] = (u8) (wdata >> 8) & 0x00FF;
		}
		data_read+=2;
		if(!(data_read % (512*2)))
		{
			u8 status = ata_pio_poll_status(drive);
			if(status & 0x1) return DISK_FAIL_BUSY;
		}
	}

	//we need to read from FULL SECTOR (or it will error)
	while((data_read % BYTES_PER_SECTOR) != 0)
	{
		inw(DATA_PORT(drive));
		data_read+=2;
	}

	return DISK_SUCCESS;
}

static u8 ata_pio_write_28(u32 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
	//kprintf("writing sector %d at off %d for %d bytes into buffer at 0x%X\n", sector, offset, count, data);
	//verify that sector is really a 28-bits integer (the fatal error is temp)
	if(sector & 0xF0000000) return DISK_FAIL_OUT;//fatal_kernel_error("Trying to write to a unadressable sector", "WRITE_28");

	//calculate sector count
	u32 scount = count / BYTES_PER_SECTOR;
	if(count % BYTES_PER_SECTOR) scount++;

	//calculate sector offset
	while(offset>BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}
	
	u8* cached_f = 0;
	if(offset | (count%512)) //cache starting sector (before any out cmd)
	{
		cached_f = 
		#ifdef MEMLEAK_DBG
		kmalloc(BYTES_PER_SECTOR, "ATA write28 first sector caching");
		#else
		kmalloc(BYTES_PER_SECTOR);
		#endif
		ata_pio_read_flexible(sector, 0, cached_f, BYTES_PER_SECTOR, drive);
	}

	//calculate last sector offset
	u32 last_sector = sector+((count/512)*512); //does that actually works ?

	u8* cached_l = 0;
	if((last_sector != sector) && (count % 512)) //cache last sector
	{
		cached_l = 
		#ifdef MEMLEAK_DBG
		kmalloc(BYTES_PER_SECTOR, "ATA write28 last sector caching");
		#else
		kmalloc(BYTES_PER_SECTOR);
		#endif
		ata_pio_read_flexible(last_sector, 0, cached_l, BYTES_PER_SECTOR, drive);		
	}

	//select drive and write sector addr (4 upper bits)
	outb(DEVICE_PORT(drive), ((drive->flags & ATA_FLAG_MASTER) ? 0xE0 : 0xF0) | ((sector & 0x0F000000) >> 24));
	ata_io_wait(drive); //wait for drive selection

	//clean previous error msgs
	outb(ERROR_PORT(drive), 0);

	//number of sectors to write (always 1 for now)
	outb(SECTOR_COUNT_PORT(drive), scount);

	//write sector addr (24 bits left)
	outb(LBA_LOW_PORT(drive), (sector & 0x000000FF));
	outb(LBA_MID_PORT(drive), (sector & 0x0000FF00) >> 8);
	outb(LBA_HI_PORT(drive), (sector & 0x00FF0000) >> 16);

	//command write
	outb(COMMAND_PORT(drive), 0x30);

	//actually write the data
	u32 i;
	for(i = 0; i < count+offset; i +=2)
	{
		if(i+1<offset)
		{
			//output cached
			u16 wdata = cached_f[i];
			wdata |= (u16) (((u16) cached_f[i+1]) << 8);
			outw(DATA_PORT(drive), wdata);
		}
		else 
		{
			//output data
			u16 wdata;
			if(i>=offset)
				wdata = data[i-offset];
			else wdata = cached_f[i];

			if(i+1 < count+offset)
				wdata |= (u16) (((u16) data[i+1-offset]) << 8);
				
			outw(DATA_PORT(drive), wdata);
		}
	}
	//we need to write to FULL SECTOR (or it will error)
	for(i = count+offset + ((count+offset) % 2); i < BYTES_PER_SECTOR*scount; i+=2)
	{
		//output cached
		if(last_sector == sector)
		{
			u16 wdata = cached_f[i];
			wdata |= (u16) (((u16) cached_f[i+1]) << 8);
			outw(DATA_PORT(drive), wdata);
		}
		else
		{
			u16 wdata = cached_l[i];
			wdata |= (u16) (((u16) cached_l[i+1]) << 8);
			outw(DATA_PORT(drive), wdata);
		}
	}

	if(offset) kfree(cached_f);
	if((last_sector != sector) && (count % 512)) kfree(cached_l);

	//then flush drive
	outb(COMMAND_PORT(drive), 0xE7);
	
	u8 status = ata_pio_poll_status(drive);
	
	if(status & 0x1) return DISK_FAIL_BUSY;//fatal_kernel_error("Drive error while flushing", "WRITE_28"); //temp debug
	return DISK_SUCCESS;
}

static u8 ata_pio_write_48(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
	//verify that sector is really a 48-bits integer (the fatal error is temp)
	if(sector & 0xFFFF000000000000) return DISK_FAIL_OUT;//fatal_kernel_error("Trying to write to a unadressable sector", "WRITE_48");

	//calculating sector count
	u32 scount = (u32) (count / BYTES_PER_SECTOR);
	if(count % BYTES_PER_SECTOR) scount++;

	//calculate sector offset
	while(offset>BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}

	u8* cached_f = 0;
	if(offset) //cache starting sector (before any out cmd)
	{
		cached_f = 
		#ifdef MEMLEAK_DBG
		kmalloc(BYTES_PER_SECTOR, "ATA write48 first sector caching");
		#else
		kmalloc(BYTES_PER_SECTOR);
		#endif
		ata_pio_read_flexible(sector, 0, cached_f, BYTES_PER_SECTOR, drive);
	}

	//calculate last sector offset
	u64 last_sector = sector+((count/512)*512); //does that actually works ?

	u8* cached_l = 0;
	if((last_sector != sector) && (count % 512)) //cache last sector
	{
		cached_l = 
		#ifdef MEMLEAK_DBG
		kmalloc(BYTES_PER_SECTOR, "ATA write48 last sector caching");
		#else
		kmalloc(BYTES_PER_SECTOR);
		#endif
		ata_pio_read_flexible(last_sector, 0, cached_l, BYTES_PER_SECTOR, drive);		
	}

	//select drive
	outb(DEVICE_PORT(drive), ((drive->flags & ATA_FLAG_MASTER) ? 0x40 : 0x50));
	ata_io_wait(drive); //wait for drive selection

	//write sector count top bytes
	outb(SECTOR_COUNT_PORT(drive), scount); //always 1 sector for now

	//split address in 8-bits values
	u8* addr = (u8*) ((u64*)&sector);
	//write address high bytes to the 3 ports
	outb(LBA_LOW_PORT(drive), addr[3]);
	outb(LBA_MID_PORT(drive), addr[4]);
	outb(LBA_HI_PORT(drive), addr[5]);
	
	//write sector count low bytes
	outb(SECTOR_COUNT_PORT(drive), 1); //always 1 sector for now
	
	//write address low bytes to the 3 ports
	outb(LBA_LOW_PORT(drive), addr[0]);
	outb(LBA_MID_PORT(drive), addr[1]);
	outb(LBA_HI_PORT(drive), addr[2]);

	//command extended write
	outb(COMMAND_PORT(drive), 0x34);

	//actually write the data
	u64 i;
	for(i = 0; i < count+offset; i +=2)
	{
		if(i+1<offset)
		{
			//output cached
			u16 wdata = cached_f[i];
			wdata |= (u16) (((u16) cached_f[i+1]) << 8);
			outw(DATA_PORT(drive), wdata);
		}
		else 
		{
			//output data
			u16 wdata;
			if(i>=offset)
				wdata = data[i-offset];
			else wdata = cached_f[i];

			if(i+1 < count+offset)
				wdata |= (u16) (((u16) data[i+1-offset]) << 8);
				
			outw(DATA_PORT(drive), wdata);
		}
	}

	//we need to write to FULL SECTOR (or it will error)
	for(i = count+offset + ((count+offset) % 2); i < BYTES_PER_SECTOR*scount; i+=2)
	{
		//output cached
		if(last_sector == sector)
		{
			u16 wdata = cached_f[i];
			wdata |= (u16) (((u16) cached_f[i+1]) << 8);
			outw(DATA_PORT(drive), wdata);
		}
		else
		{
			u16 wdata = cached_l[i];
			wdata |= (u16) (((u16) cached_l[i+1]) << 8);
			outw(DATA_PORT(drive), wdata);
		}
	}

	if(offset) kfree(cached_f);
	if((last_sector != sector) && (count % 512)) kfree(cached_l);

	//then flush drive (actually verify that this part works for 48-bits LBA)
	outb(COMMAND_PORT(drive), 0xE7);
	
	u8 status = ata_pio_poll_status(drive);
	
	if(status & 0x1) return DISK_FAIL_BUSY;//fatal_kernel_error("Drive error while flushing", "WRITE_48"); //temp debug
	return DISK_SUCCESS;
}

u8 ata_pio_write(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
		if(sector > U32_MAX && (drive->flags & ATA_FLAG_LBA48))
			return ata_pio_write_48(sector, offset, data, count, drive);
		else
			return ata_pio_write_28((u32) sector, offset, data, (u32) count, drive);
}

u8 ata_pio_poll_status(ata_device_t* drive)
{
	u8 status = inb(COMMAND_PORT(drive));
	u32 times = 0;
	while(((status & 0x80) == 0x80) && ((status & 0x8) != 0x8) && ((status & 0x01) != 0x01) && ((status & 0x20) != 0x20) && times < 0xFFFFF)
		{status = inb(COMMAND_PORT(drive)); times++;}

	//TEMP
	if(status & 0x20) return 1;//{kprintf("%lDRIVE FAULT !\n", 2); return 0x01;}
	if(times == 0xFFFFF) return 1;//{kprintf("%lCOULD NOT REACH DEVICE (TIMED OUT)\n", 2);return 0x01;}
	if(status & 0x1) return 1;//{kprintf("%lDRIVE ERR !\n", 2); return 0x01;}
	return status;
}

void ata_read_partitions(block_device_t* drive)
{
	if(drive->device_type != ATA_DEVICE) return;
	ata_device_t* current = (ata_device_t*) drive->device_struct;

	memset(drive->partitions, 0, sizeof(void*)*4);
	//drive->partitions = {0};

	#ifdef MEMLEAK_DBG
	master_boot_record_t* mbr = kmalloc(sizeof(master_boot_record_t), "ATA Drive MBR");
	#else
	master_boot_record_t* mbr = kmalloc(sizeof(master_boot_record_t));
	#endif
	if(ata_pio_read_flexible(0, 0, ((u8*) mbr), 512, current) != DISK_SUCCESS)
	{kfree(mbr); return;}

	if(mbr->magic_number != 0xAA55) {kfree(mbr); return;}

	u32 i;
	for(i = 0;i<4;i++)
	{
		#ifdef MEMLEAK_DBG
		drive->partitions[i] = kmalloc(sizeof(partition_descriptor_t), "ATA Drive partition");
		#else
		drive->partitions[i] = kmalloc(sizeof(partition_descriptor_t));
		#endif
		if(mbr->partitions[i].system_id)
		{
			drive->partitions[i]->start_lba = mbr->partitions[i].start_lba;
			drive->partitions[i]->length = mbr->partitions[i].length;
			drive->partitions[i]->system_id = mbr->partitions[i].system_id;
			drive->partitions[i]->bootable = mbr->partitions[i].bootable;
			//kprintf("registering part %u, start 0x%X, length 0x%X, bootable %u\n", i, mbr->partitions[i].start_lba, mbr->partitions[i].length, mbr->partitions[i].bootable);
		}
		else
		{
			drive->partitions[i]->system_id = 0;
		}
	}

	kfree(mbr);
}
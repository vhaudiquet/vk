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
#include "storage.h"
#include "error/error.h"
#include "memory/mem.h"
#include "internal/internal.h"
#include "cpu/cpu.h"

#define PRIMARY_ATA 0x1F0
#define SECONDARY_ATA 0x170

#define BYTES_PER_SECTOR 512

static u8 ata_pio_poll_status(ata_device_t* drive);
static block_device_t* ata_identify_drive(u16 base_port, u16 control_port, bool master, u8 interrupt, pci_device_t* controller);
static block_device_t* atapi_identify_drive(u16 base_port, u16 control_port, bool master, u8 interrupt, pci_device_t* controller);
static void ata_read_partitions(block_device_t* drive);
static u8 ata_pio_read_28(u32 sector, u32 offset, u8* data, u32 count, ata_device_t* drive);

extern void _irq14();
extern void _irq15();
void ata_install()
{
	bool std_primary_initialized = false;
	bool std_secondary_initialized = false;

	//finding AHCI controllers that may be in ATA emulation mode and initialize them too
	pci_device_t* curr = pci_first;
	while(curr != pci_last)
	{
		if(curr->class_id == 0x01)
		{
			//bar0: primary ata data port
			u16 port = pci_read_device(curr, BAR0) & 0b1111111111111100;// >> 2 << 2;
			if((port == 0x0) | (port == 0x1)) port = 0x1F0;
			//bar1: primary ata control port
			u16 control_port = pci_read_device(curr, BAR1) & 0b1111111111111100;
			if((control_port == 0x0) | (control_port == 0x1)) control_port = 0x3F6;
			//bar2: secondary ata data port
			u16 port_s = pci_read_device(curr, BAR2) & 0b1111111111111100;
			if((port_s == 0x0) | (port_s == 0x1)) port_s = 0x170;
			//bar3: secondary ata control port
			u16 control_port_s = pci_read_device(curr, BAR3) & 0b1111111111111100;
			if((control_port_s == 0x0) | (control_port_s == 0x1)) control_port_s = 0x376;

			//pci interrupt
			u8 in = ((u8) (((u16)(curr->interrupt << 8)) >> 8)); //wtf ?
			if(!in){in = 14;}

			if(curr->subclass_id == 0x01)
			{
				kprintf("%lFound ATA controller ; port = 0x%X (irq %d)\n", 3, port, in);
				block_device_t* primary_master = ata_identify_drive(port, control_port, true, in, curr);
				if(primary_master) {block_devices[block_device_count] = primary_master; block_device_count++;}
				block_device_t* primary_slave = ata_identify_drive(port, control_port, false, in, curr);
				if(primary_slave) {block_devices[block_device_count] = primary_slave; block_device_count++;}
				block_device_t* secondary_master = ata_identify_drive(port_s, control_port_s, true, (u8) (in+1), curr);
				if(secondary_master) {block_devices[block_device_count] = secondary_master; block_device_count++;}
				block_device_t* secondary_slave = ata_identify_drive(port_s, control_port_s, false, (u8) (in+1), curr);
				if(secondary_slave) {block_devices[block_device_count] = secondary_slave; block_device_count++;}
				//TEMP IDT
				if((primary_master != 0) | (primary_slave != 0)) init_idt_desc(in+32, 0x08, (u32) _irq14,0x8E00);
				if((secondary_master != 0) | (secondary_slave != 0)) init_idt_desc(in+33, 0x08, (u32) _irq15,0x8E00);
				if((port == PRIMARY_ATA) | (port_s == PRIMARY_ATA)) std_primary_initialized = true;
				if((port == SECONDARY_ATA) | (port_s == SECONDARY_ATA)) std_secondary_initialized = true;
			}
			else if(curr->subclass_id == 0x06)
			{
				kprintf("%lFound unsupported AHCI ATA controller\n", 2);
			}
		}
		curr = curr->next;
	}

	//installing standard PRIMARY_ATA and SECONDARY_ATA ports
	if(!std_primary_initialized)
	{
		kprintf("%lATA PRIMARY NOT INITIALIZED ?\n", 2);
		block_device_t* master = ata_identify_drive(PRIMARY_ATA, 0x3F6, true, 14, 0);
		if(master) {block_devices[block_device_count] = master; block_device_count++;}
		block_device_t* slave = ata_identify_drive(PRIMARY_ATA, 0x3F6, false, 14, 0);
		if(slave) {block_devices[block_device_count] = slave; block_device_count++;}
	}

	if(!std_secondary_initialized)
	{
		kprintf("%lATA SECONDARY NOT INITIALIZED ?\n", 2);
		block_device_t* master = ata_identify_drive(SECONDARY_ATA, 0x376, true, 15, 0);
		if(master) {block_devices[block_device_count] = master; block_device_count++;}
		block_device_t* slave = ata_identify_drive(SECONDARY_ATA, 0x376,false, 15, 0);
		if(slave) {block_devices[block_device_count] = slave; block_device_count++;}
	}
}

static block_device_t* ata_identify_drive(u16 base_port, u16 control_port, bool master, u8 irq, pci_device_t* controller)
{
	//prepare data structs
	#ifdef MEMLEAK_DBG
	ata_device_t* current = kmalloc(sizeof(ata_device_t), "ATA Device struct");
	block_device_t* current_top = kmalloc(sizeof(block_device_t), "ATA HDD Device struct");
	#else
	ata_device_t* current = kmalloc(sizeof(ata_device_t));
	block_device_t* current_top = kmalloc(sizeof(block_device_t));
	#endif

	//setting up parameters
	current->master = master;
	current->base_port = base_port;
	current->control_port = control_port;
	current->controller = controller;
	current->irq = irq;

	//select drive
	outb(DEVICE_PORT(current), 0xA0); //always master there, to see if there is a device on the cable or not
	u8 status = inb(COMMAND_PORT(current));
	if(status == 0xFF)
	{
		return 0; //no device found
	}

	//select drive
	outb(DEVICE_PORT(current), master ? 0xA0 : 0xB0); //0xB0 = slave

	//initialize drive
	outb(CONTROL_PORT(current), 0); //clears bits of control register

	//identify
	outb(SECTOR_COUNT_PORT(current), 0);
	outb(LBA_LOW_PORT(current), 0);
	outb(LBA_MID_PORT(current), 0);
	outb(LBA_HI_PORT(current), 0);
	outb(COMMAND_PORT(current), 0xEC); //identify cmd

	status = inb(COMMAND_PORT(current));
	if(status == 0x0)
	{
		return 0; //no device
	}
	
	//wait for drive busy/drive error
	status = ata_pio_poll_status(current);

	if(status & 0x01)
	{
		u8 t0 = inb(LBA_MID_PORT(current));
		u8 t1 = inb(LBA_HI_PORT(current));
		if(t0 == 0x14 && t1 == 0xEB) {return atapi_identify_drive(base_port, control_port, master, irq, controller);}
		else if(t0 == 0x3C && t1 == 0xC3) kprintf("%l(ata id: is a sata device)\n", 3); //TEMP
		else kprintf("%l(ata id: unknown error)\n", 3); //TEMP
		return 0;
	}

	//at this point we know that data is ready
	u16 i; u16 size[2]; u16 size_64[4]; bool lba48 = false; u16 spb = 0;
	for(i = 0;i < BYTES_PER_SECTOR/2; i++)
	{
		u16 data = inw(DATA_PORT(current));
		if(i == 46) spb = data & 0xFF;
		if(i == 83) lba48 = (data >> 9 << 15 ? 1 : 0);
		if(i == 60) size[0] = data; if(i == 61) size[1] = data;
		if(i == 100) size_64[0] = data; if(i == 101) size_64[1] = data; if(i == 102) size_64[2] = data; if(i == 103) size_64[3] = data;
	}
	u32 rsize = *((u32*) size);
	u64 rsize_64 = *((u64*) size_64);

	if(rsize_64 > 0 && lba48)
		current_top->device_size = (u32) (rsize_64*BYTES_PER_SECTOR/1024/1024);
	else
		current_top->device_size = (u32) (rsize*BYTES_PER_SECTOR/1024/1024);

	current->lba48_support = lba48;
	current->sectors_per_block = spb;

	current_top->device_struct = (void*) current;
	current_top->device_type = ATA_DEVICE;
	current_top->device_class = HARD_DISK_DRIVE;

	ata_read_partitions(current_top);

	return current_top;
}

static block_device_t* atapi_identify_drive(u16 base_port, u16 control_port, bool master, u8 irq, pci_device_t* controller)
{
	//prepare data structs
	#ifdef MEMLEAK_DBG
	atapi_device_t* current = kmalloc(sizeof(atapi_device_t), "ATAPI Device struct");
	block_device_t* current_top = kmalloc(sizeof(block_device_t), "ATAPI Block Device struct");
	#else
	atapi_device_t* current = kmalloc(sizeof(atapi_device_t));
	block_device_t* current_top = kmalloc(sizeof(block_device_t));
	#endif

	//fill basic informations
	current->master = master;
	current->base_port = base_port;
	current->control_port = control_port;
	current->controller = controller;
	current->irq = irq;

	u8 status;

	//select drive
	outb(DEVICE_PORT(current), master ? 0xA0 : 0xB0); //0xB0 = slave
	//identify
	outb(SECTOR_COUNT_PORT(current), 0);
	outb(LBA_LOW_PORT(current), 0);
	outb(LBA_MID_PORT(current), 0);
	outb(LBA_HI_PORT(current), 0);
	outb(COMMAND_PORT(current), 0xA1); //identify atapi cmd

	status = inb(COMMAND_PORT(current));
	if(status == 0x0)
	{
		return 0; //no device (strange and should never happen)
	}

	//wait for drive busy/drive error
	status = ata_pio_poll_status((ata_device_t*) current);

	if(status & 0x01)
	{
		fatal_kernel_error("Unknown ATAPI error", "ATAPI_IDENTIFY");//TEMP
		return 0;
	}

	//at this point we know that data is ready
	u16 i; bool lba48 = false; u16 unknown_data = 0;
	for(i = 0;i < BYTES_PER_SECTOR/2; i++)
	{
		u16 data = inw(DATA_PORT(current));
		if(i == 0) unknown_data = data; //TEMP
		if(i == 83) lba48 = (data >> 9 << 15 ? 1 : 0);
	}

	current->lba48_support = lba48;
	current->media_type = unknown_data;

	
	current_top->device_type = ATAPI_DEVICE;
	current_top->device_struct = (void*) current;
	current_top->device_class = CD_DRIVE;

	return current_top;
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
		ata_pio_read_28(sector, 0, cached_f, BYTES_PER_SECTOR, drive);
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
		ata_pio_read_28(last_sector, 0, cached_l, BYTES_PER_SECTOR, drive);		
	}

	//select drive and write sector addr (4 upper bits)
	outb(DEVICE_PORT(drive), (drive->master ? 0xE0 : 0xF0) | ((sector & 0x0F000000) >> 24));

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

static u8 ata_pio_read_28(u32 sector, u32 offset, u8* data, u32 count, ata_device_t* drive)
{
	//verify that sector is really a 28-bits integer (the fatal error is temp)
	if(sector & 0xF0000000) return DISK_FAIL_OUT;//fatal_kernel_error("Trying to read an unadressable sector", "READ_28");

	//calculate sector count
	u32 scount = count / BYTES_PER_SECTOR;
	if(count % BYTES_PER_SECTOR) scount++;

	//calculate sector offset
	while(offset>=BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}

	if(scount > 255) return DISK_FAIL_INTERNAL; //fatal_kernel_error("Trying to read more than 255 sectors", "READ_28");

	//select drive and write sector addr (4 upper bits)
	outb(DEVICE_PORT(drive), (drive->master ? 0xE0 : 0xF0) | ((sector & 0x0F000000) >> 24));

	//clean previous error msgs
	outb(ERROR_PORT(drive), 0);

	//number of sectors to read
	outb(SECTOR_COUNT_PORT(drive), scount);

	//kprintf("ATA_PIO_READ_28 : %u sectors, off %u\n", scount, offset);

	//write sector addr (24 bits left)
	outb(LBA_LOW_PORT(drive), (sector & 0x000000FF));
	outb(LBA_MID_PORT(drive), (sector & 0x0000FF00) >> 8);
	outb(LBA_HI_PORT(drive), (sector & 0x00FF0000) >> 16);

	//command read
	outb(COMMAND_PORT(drive), 0xC4); //0x20: read, 0xC4: read multiple

	//wait for drive to be ready
	u8 status = ata_pio_poll_status(drive);
	if(status & 0x1) return DISK_FAIL_BUSY;//fatal_kernel_error("Drive error while reading", "READ_28"); //temp debug

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
		if(data_read % (512*32))
		{
			u8 status = ata_pio_poll_status(drive);
			if(status & 0x1) return DISK_FAIL_BUSY;//fatal_kernel_error("Drive error while reading", "READ_28"); //temp debug
		}
	}

	//kprintf("%lbytes read : %u", 3, data_read);

	//we need to read from FULL SECTOR (or it will error)
	while((data_read % BYTES_PER_SECTOR) != 0)
	{
		inw(DATA_PORT(drive));
		data_read+=2;
	}

	return DISK_SUCCESS;
}

static u8 ata_pio_read_48(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
	//verify that sector is really 48-bits integer (the fatal error is temp)
	if(sector & 0xFFFF000000000000) return DISK_FAIL_OUT;//fatal_kernel_error("Trying to read an unadressable sector", "READ_48");

	//calculate sector count
	u32 scount = (u32) (count / BYTES_PER_SECTOR);
	if(count % BYTES_PER_SECTOR) scount++;

	//calculate sector offset
	while(offset>=BYTES_PER_SECTOR) {offset-=BYTES_PER_SECTOR; sector++; scount--;}

	if(scount > 255) return DISK_FAIL_INTERNAL;//fatal_kernel_error("Trying to read more than 255 sectors", "READ_28");

	//select drive
	outb(DEVICE_PORT(drive), (drive->master ? 0x40 : 0x50));

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

	//command extended read
	outb(COMMAND_PORT(drive), 0x29); //0x24 = READ_SECTOR_EXT (ext = 48) //0x29 : READ MULTIPLE EXT ?

	//wait for drive to be ready
	u8 status = ata_pio_poll_status(drive);

	if(status & 0x1) return DISK_FAIL_BUSY;//fatal_kernel_error("Drive error while reading", "READ_48"); //temp debug

	//actually read the data
	u32 data_read = 0;
	u64 i;
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
		if(data_read % (512*32))
		{
			u8 status = ata_pio_poll_status(drive);
			if(status & 0x1) return DISK_FAIL_BUSY;//fatal_kernel_error("Drive error while reading", "READ_28"); //temp debug
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
		ata_pio_read_48(sector, 0, cached_f, BYTES_PER_SECTOR, drive);
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
		ata_pio_read_48(last_sector, 0, cached_l, BYTES_PER_SECTOR, drive);		
	}

	//select drive
	outb(DEVICE_PORT(drive), (drive->master ? 0x40 : 0x50));

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

u8 ata_pio_read(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
	if(sector > U32_MAX && drive->lba48_support)
		return ata_pio_read_48(sector, offset, data, count, drive);
	else
		return ata_pio_read_28((u32) sector, offset, data, (u32) count, drive);
}

u8 ata_pio_write(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
		if(sector > U32_MAX && drive->lba48_support)
			return ata_pio_write_48(sector, offset, data, count, drive);
		else
			return ata_pio_write_28((u32) sector, offset, data, (u32) count, drive);
}

static u8 ata_pio_poll_status(ata_device_t* drive)
{
	u8 status = inb(COMMAND_PORT(drive));
	u32 times = 0;
	while(((status & 0x80) == 0x80) && ((status & 0x01) != 0x01) && ((status & 0x20) != 0x20) && times < 0xFFFFF)
		{status = inb(COMMAND_PORT(drive)); times++;}

	//TEMP
	if(status == 0x20) {kprintf("%lDRIVE FAULT !\n", 2); return 0x01;}
	if(times == 0xFFFFF) {kprintf("%lCOULD NOT REACH DEVICE (TIMED OUT)\n", 2);return 0x01;}
	return status;
}

static void ata_read_partitions(block_device_t* drive)
{
	if(drive->device_type != ATA_DEVICE) fatal_kernel_error("How did you try to ATA read partitions of a non-ATA device ?", "ATA_READ_PARTITIONS");
	ata_device_t* current = (ata_device_t*) drive->device_struct;

	memset(drive->partitions, 0, sizeof(void*)*4);
	//drive->partitions = {0};

	#ifdef MEMLEAK_DBG
	master_boot_record_t* mbr = kmalloc(sizeof(master_boot_record_t), "ATA Drive MBR");
	#else
	master_boot_record_t* mbr = kmalloc(sizeof(master_boot_record_t));
	#endif
	ata_pio_read_28(0, 0, ((u8*) mbr), 512, current);

	if(mbr->magic_number != 0xAA55) fatal_kernel_error("Invalid MBR", "ATA_READ_PARTITIONS"); //TEMP, return then

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
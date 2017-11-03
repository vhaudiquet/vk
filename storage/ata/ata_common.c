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

#include "../storage.h"
#include "tasking/task.h"
#include "memory/mem.h"
#include "internal/internal.h"
#include "cpu/cpu.h"

typedef struct ATA_IDENTIFY
{
    u16 unused0[23];
    u8 firmware[8];
    u8 model[40];
    u16 unused1[11];
    u16 capabilities;
    u16 size_rw_multiple;
    u32 sectors_28;
    u16 unused2[28];
    u64 sectors_48;
    u16 unused3[158];
} ata_identify_data_t;

static block_device_t* ata_identify_drive(u16 base_port, u16 control_port, u16 bar4, bool master, u8 irq, pci_device_t* controller);

extern void _irq14(); //TEMP
extern void _irq15(); //TEMP
void ata_install()
{
    pci_device_t* curr = pci_first;
	while(curr != pci_last)
	{
		if(curr->class_id == 0x01)
		{
			//bar0: primary ata data port
			u16 port = pci_read_device(curr, BAR0) & 0xFFFC;// >> 2 << 2;
			if((port == 0x0) | (port == 0x1)) port = 0x1F0;
			//bar1: primary ata control port
			u16 control_port =(u16) ((pci_read_device(curr, BAR1) & 0xFFFC)+2);
			if((control_port == 0x2) | (control_port == 0x3)) control_port = 0x3F6;
			//bar2: secondary ata data port
			u16 port_s = pci_read_device(curr, BAR2) & 0xFFFC;
			if((port_s == 0x0) | (port_s == 0x1)) port_s = 0x170;
			//bar3: secondary ata control port
			u16 control_port_s = (u16) ((pci_read_device(curr, BAR3) & 0xFFFC)+2);
            if((control_port_s == 0x2) | (control_port_s == 0x3)) control_port_s = 0x376;
            //bar4: bus master register
			u16 bar4 = pci_read_device(curr, BAR4) & 0xFFFC;

			//pci interrupt
			u8 in = ((u8) (((u16)(curr->interrupt << 8)) >> 8)); //wtf ?
			if(!in){in = 14;}

			if(curr->subclass_id == 0x01)
			{
				block_device_t* primary_master = ata_identify_drive(port, control_port, bar4, true, in, curr);
				if(primary_master) {block_devices[block_device_count] = primary_master; block_device_count++;}
				block_device_t* primary_slave = ata_identify_drive(port, control_port, bar4, false, in, curr);
				if(primary_slave) {block_devices[block_device_count] = primary_slave; block_device_count++;}
				block_device_t* secondary_master = ata_identify_drive(port_s, control_port_s, (u16)(bar4+0x8), true, (u8) (in+1), curr);
				if(secondary_master) {block_devices[block_device_count] = secondary_master; block_device_count++;}
				block_device_t* secondary_slave = ata_identify_drive(port_s, control_port_s, (u16)(bar4+0x8), false, (u8) (in+1), curr);
				if(secondary_slave) {block_devices[block_device_count] = secondary_slave; block_device_count++;}
				//TEMP IDT
				if((primary_master != 0) | (primary_slave != 0)) {init_idt_desc(in+32, 0x08, (u32) _irq14,0x8E00);}
				if((secondary_master != 0) | (secondary_slave != 0)) {init_idt_desc(in+33, 0x08, (u32) _irq15,0x8E00);}
            }
            //Note : DEBUG message
			//else if(curr->subclass_id == 0x06)
			//{
			//	kprintf("%lFound unsupported AHCI ATA controller\n", 2);
			//}
		}
		curr = curr->next;
	}
}

static block_device_t* ata_identify_drive(u16 base_port, u16 control_port, u16 bar4, bool master, u8 irq, pci_device_t* controller)
{
	//prepare data structs
	#ifdef MEMLEAK_DBG
	ata_device_t* current = kmalloc(sizeof(ata_device_t), "ATA Device struct");
	block_device_t* current_top = kmalloc(sizeof(block_device_t), "ATA Block Device struct");
	#else
	ata_device_t* current = kmalloc(sizeof(ata_device_t));
	block_device_t* current_top = kmalloc(sizeof(block_device_t));
	#endif

	//setting up parameters
	current->flags = (u8) (current->flags | (master ? ATA_FLAG_MASTER : 0));
	current->base_port = base_port;
	current->control_port = control_port;
	current->bar4 = bar4;
	current->controller = controller;
	current->irq = irq;

    //write i/o flag (needed for DMA transfers)
	pci_write_device(current->controller, 0x04, pci_read_device(current->controller, 0x04)|7);

	//select drive
    outb(DEVICE_PORT(current), 0xA0); //always master there, to see if there is a device on the cable or not
	u8 status = inb(COMMAND_PORT(current));
	if(status == 0xFF)
	{
		return 0; //no device found
	}

	//select drive
	outb(DEVICE_PORT(current), master ? 0xA0 : 0xB0); //0xB0 = slave
    ata_io_wait(current); //wait for drive selection

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

    //if error, check if drive is atapi, or return
	if(status & 0x01)
	{
		u8 t0 = inb(LBA_MID_PORT(current));
		u8 t1 = inb(LBA_HI_PORT(current));
        if(t0 == 0x14 && t1 == 0xEB) //if device is an ATAPI device
        {
            current->flags |= ATA_FLAG_ATAPI;
            outb(COMMAND_PORT(current), 0xA1); //identify packet cmd
            status = inb(COMMAND_PORT(current));
            if(status == 0x0)
            {
                return 0; //no device (strange and should never happen)
            }
            //wait for drive busy/drive error
            status = ata_pio_poll_status((ata_device_t*) current);
            if(status & 0x01)
            {
                return 0;
            }
        }
		else return 0;
	}

    //at this point we know that data is ready
    ata_identify_data_t id = {0};
	u16 i;
	for(i = 0;i < BYTES_PER_SECTOR/2; i++)
	{
		*((u16*)(&id+i)) = inw(DATA_PORT(current));
	}

    current->flags = (u8)(current->flags | ((id.capabilities >> 9 << 15) ? ATA_FLAG_LBA48 : 0));

	if((id.sectors_48 > 0) && (current->flags & ATA_FLAG_LBA48))
		current_top->device_size = (u32) (id.sectors_48*BYTES_PER_SECTOR/1024/1024);
	else
		current_top->device_size = (u32) (id.sectors_28*BYTES_PER_SECTOR/1024/1024);

	current->sectors_per_block = id.size_rw_multiple;

	current_top->device_struct = (void*) current;
	current_top->device_type = ATA_DEVICE;
    if(current->flags & ATA_FLAG_ATAPI) current_top->device_class = CD_DRIVE;
    else current_top->device_class = HARD_DISK_DRIVE;

	if(!(current->flags & ATA_FLAG_ATAPI)) ata_read_partitions(current_top);

	return current_top;
}

void ata_io_wait(ata_device_t* drive)
{
	inb(CONTROL_PORT(drive));
    inb(CONTROL_PORT(drive));
    inb(CONTROL_PORT(drive));
    inb(CONTROL_PORT(drive));
}

u8 ata_read_flexible(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive)
{
    if(drive->flags & ATA_FLAG_ATAPI)
    {
        return DISK_FAIL_INTERNAL;
    }
    else
    {
        if(!scheduler_started)
        {
            u8 err = DISK_SUCCESS;
            u32 a = 0; u32 as = 0;
            while(count > 255*512)
            {
                err = ata_pio_read_flexible(sector+as, offset, data+a, 255*512, drive);
                count -= 255*512;
                a += 255*512;
                as += 255;
                offset = 0;
                if(err != DISK_SUCCESS) return err;
            }
            return ata_pio_read_flexible(sector+as, offset, data+a, count, drive);
        }
        else
        {
            u8 err = DISK_SUCCESS;
            u32 a = 0; u32 as = 0;
            while(count > 127*512)
            {
                err = ata_dma_read_flexible((u32) sector+as, offset, data+a, (u32) 127*512, drive);
                count -= 127*512;
                a += 127*512;
                as += 127;
                offset = 0;
                if(err != DISK_SUCCESS) return err;
            }
            return ata_dma_read_flexible((u32) sector+as, offset, data+a, (u32) count, drive);
        }
    }
}

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

error_t atapi_cmd_dma_read_28(u32 sector, u32 scount, ata_device_t* drive)
{
    outb(DEVICE_PORT(drive), ((drive->flags & ATA_FLAG_MASTER) ? 0xA0 : 0xB0)); //select drive
    ata_io_wait(drive); //wait for drive selection

    outb(ERROR_PORT(drive), 1); //set feature DMA

    //send buffer size (0 cause DMA)
    outb(LBA_MID_PORT(drive), 0);
    outb(LBA_HI_PORT(drive), 0);

    outb(COMMAND_PORT(drive), 0xA0); //send PACKET command

    error_t status = ata_pio_poll_status(drive); //poll status
    if(status != ERROR_NONE) return status;

    u8 atapi_read_sectors[] = 
    {
        0xA8, /* ATAPI_READ_SECTORS */
        0, 
        ((sector >> 24) & 0x0F), 
        ((sector >> 16) & 0xFF), 
        ((sector >> 8) & 0xFF), 
        (sector & 0xFF), 
        0, 0, 0, scount, 0, 0
    };

    u8 i = 0;
    for(i = 0; i<6;i++)
    {
        outw(DATA_PORT(drive), ((u16*)&atapi_read_sectors)[i]);
    }

    return ERROR_NONE;
}

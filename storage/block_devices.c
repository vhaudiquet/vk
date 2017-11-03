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

block_device_t** block_devices = 0;
u16 block_device_count = 0;

void install_block_devices()
{
    kprintf("[STO] Installing block devices...");

    block_devices = kmalloc(sizeof(void*)*10);
    ata_install();

    vga_text_okmsg();
}

u8 block_read_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* drive)
{
    if(drive->device_type == ATA_DEVICE)
    {
        return ata_read_flexible(sector, offset, data, count, drive->device_struct);
    }
    return DISK_FAIL_INTERNAL;
}

u8 block_write_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* drive)
{
    if(drive->device_type == ATA_DEVICE)
    {
        return ata_pio_write(sector, offset, data, count, (ata_device_t*) drive->device_struct);
    }
    return DISK_FAIL_INTERNAL;
}
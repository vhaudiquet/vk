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
#include "storage.h"
#include "memory/mem.h"

block_device_t* init_ramdisk(u32 size)
{
    //fill block device struct
    alignup(size, BYTES_PER_SECTOR);
    block_device_t* tr = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(block_device_t), "ramdisk struct");
    #else
    kmalloc(sizeof(block_device_t));
    #endif
    tr->device_size = size/BYTES_PER_SECTOR;
    tr->device_type = RAMDISK_DEVICE;
    tr->device_class = HARD_DISK_DRIVE;
    tr->partition_count = 0;

    //we are using the pointer device_struct to store ramdisk base addr
    tr->device_struct = 
    #ifdef MEMLEAK_DBG
    kmalloc(size, "ramdisk content");
    #else
    kmalloc(size);
    #endif

    return tr;
}

u8 ramdisk_read_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* ramdisk)
{
    if(ramdisk->device_type != RAMDISK_DEVICE) return DISK_FAIL_INTERNAL;
    if(sector*BYTES_PER_SECTOR+offset > ramdisk->device_size*BYTES_PER_SECTOR) return DISK_FAIL_OUT;
    memcpy(data, ramdisk->device_struct+sector*BYTES_PER_SECTOR+offset, (size_t) count);
    return DISK_SUCCESS;
}

u8 ramdisk_write_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* ramdisk)
{
    if(ramdisk->device_type != RAMDISK_DEVICE) return DISK_FAIL_INTERNAL;
    if(sector*BYTES_PER_SECTOR+offset > ramdisk->device_size*BYTES_PER_SECTOR) return DISK_FAIL_OUT;
    memcpy(ramdisk->device_struct+sector*BYTES_PER_SECTOR+offset, data, (size_t) count);
    return DISK_SUCCESS;
}
